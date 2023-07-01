#ifndef worker_h_
#define worker_h_

#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

// need to keep this in sync with BUFFER_SZ in networkio.c
#define BUFFER_SZ 1024

struct workdata {
	int len;
	char payload[BUFFER_SZ];
	struct sockaddr_in addr;
};

struct workqueueitem {
	struct workqueueitem *next;
	struct workdata *data;
};

struct workqueue {
	int size;
	struct workqueueitem *head;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

extern struct workqueue workqueue;

void workqueue_init(struct workqueue *queue);
int workqueue_push(struct workqueue *queue, struct workdata *out);
struct workdata *workqueue_pop(struct workqueue *queue);
void workqueue_destroy(struct workqueue *queue);

void *worker_loop(void *arg);


#endif
