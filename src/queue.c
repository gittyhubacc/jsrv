#include <stdlib.h>
#include <stdio.h>
#include "queue.h"

void queue_init(struct queue *queue)
{
	queue->size = 0;
	queue->head = NULL;
	pthread_mutex_init(&queue->mutex, 0);
	pthread_cond_init(&queue->cond, 0);
}

int queue_push(struct queue *queue, void *data)
{
	// don't push NULL
	if (data == NULL) {
		return -1;
	}

	pthread_mutex_lock(&queue->mutex);

	struct queueitem *item = malloc(sizeof(*item));
	if (!item) {
		perror("queue_push(): malloc()");
		pthread_mutex_unlock(&queue->mutex);
		return -2;
	}

	item->next = NULL;
	item->data = data;

	struct queueitem **current = &queue->head;
	while (*current) current = &(*current)->next;
	*current = item;

	queue->size++;

	pthread_mutex_unlock(&queue->mutex);
	pthread_cond_signal(&queue->cond);

	return 0;
}

struct data *queue_pop(struct queue *queue)
{
	pthread_mutex_lock(&queue->mutex);

	while (queue->size < 1) {
		pthread_cond_wait(&queue->cond, &queue->mutex);
	}

	struct queueitem *item = queue->head;
	void *data = item->data;
	queue->head = item->next;
	queue->size--;

	pthread_mutex_unlock(&queue->mutex);
	pthread_cond_signal(&queue->cond);

	free(item);

	return data;
}

void queue_destroy(struct queue *queue)
{
	pthread_mutex_destroy(&queue->mutex);
	pthread_cond_destroy(&queue->cond);
}
