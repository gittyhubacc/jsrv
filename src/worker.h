#ifndef worker_h_
#define worker_h_

#include <arpa/inet.h>
#include <lua.h>
#include <pthread.h>
#include <signal.h>
#include "queue.h"

// need to keep this in sync with BUFFER_SZ in networkio.c
#define BUFFER_SZ 1024

struct work_msg {
	int len;
	char payload[BUFFER_SZ];
	struct sockaddr_in addr;
};

extern struct queue workqueue;

void workerpool_init();
void workerpool_destroy();

void *worker_loop(void *arg);

#endif
