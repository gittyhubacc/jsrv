#ifndef queue_h_
#define queue_h_

#include <pthread.h>

struct queueitem {
	struct queueitem *next;
	void *data;
};

struct queue {
	int size;
	struct queueitem *head;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

void queue_init(struct queue *q);
int queue_push(struct queue *q, void *data);
struct data *queue_pop(struct queue *q);
void queue_destroy(struct queue *q);

#endif
