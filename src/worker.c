#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "shutdown.h"
#include "worker.h"

struct workqueue workqueue;

void workqueue_init(struct workqueue *queue)
{
	queue->size = 0;
	queue->head = NULL;
	pthread_mutex_init(&queue->mutex, 0);
	pthread_cond_init(&queue->cond, 0);
}

int workqueue_push(struct workqueue *queue, struct workdata *out)
{
	pthread_mutex_lock(&queue->mutex);

	struct workqueueitem *item = malloc(sizeof(*item));
	if (!item) {
		perror("workqueue_push(): malloc()");
		pthread_mutex_unlock(&queue->mutex);
		return -1;
	}

	item->next = NULL;
	item->data = out;

	struct workqueueitem **current = &queue->head;
	while (*current) current = &(*current)->next;
	*current = item;

	queue->size++;

	pthread_mutex_unlock(&queue->mutex);
	pthread_cond_signal(&queue->cond);

	return 0;
}

struct workdata *workqueue_pop(struct workqueue *queue)
{
	pthread_mutex_lock(&queue->mutex);

	while (queue->size < 1) {
		pthread_cond_wait(&queue->cond, &queue->mutex);
	}

	struct workqueueitem *item = queue->head;
	struct workdata *out = item->data;
	queue->head = item->next;
	queue->size--;

	pthread_mutex_unlock(&queue->mutex);

	free(item);

	return out;
}

void workqueue_destroy(struct workqueue *queue)
{
	pthread_mutex_destroy(&queue->mutex);
	pthread_cond_destroy(&queue->cond);
}

static void print_mesg(struct workdata *data)
{
	char name[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &data->addr.sin_addr, name, INET_ADDRSTRLEN);
	data->payload[data->len] = '\0';
	printf("[%s](%d): %s\n", name, ntohs(data->addr.sin_port), data->payload);
}

static void worker_cleanup(void *arg)
{
	pthread_mutex_unlock(&workqueue.mutex);
}

void *worker_loop(void *arg)
{
	// block signals
	sigset_t signal_set;
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	pthread_cleanup_push(worker_cleanup, NULL);
	struct workdata *out;
	while (!should_shutdown()) {
		out = workqueue_pop(&workqueue);
		print_mesg(out);
		free(out);
	}
	pthread_cleanup_pop(1);
	return NULL;
}
