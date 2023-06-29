#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include "networkio.h"
#include "shutdown.h"

int shutdown_flag;
pthread_cond_t shutdown_cond;
pthread_mutex_t shutdown_mutex;

void signal_handler(int s)
{
	graceful_shutdown();
}

int main(int argc, char **argv)
{
	// pre-threaded initialization
	shutdown_flag = 0;

	int res;
	pthread_t accept_read_th;

	pthread_mutex_init(&shutdown_mutex, NULL);
	pthread_cond_init(&shutdown_cond, NULL);

	signal(SIGINT, signal_handler);

	res = pthread_create(&accept_read_th, NULL, accept_recv_loop, NULL);
	if (res < 0) {
		perror("pthread_create()");
		graceful_shutdown();
	}

	pthread_mutex_lock(&shutdown_mutex);
	while (!shutdown_flag) {
		pthread_cond_wait(&shutdown_cond, &shutdown_mutex);
	}
	pthread_mutex_unlock(&shutdown_mutex);

	printf("shutting down...\n");

	pthread_cancel(accept_read_th);

	return EXIT_SUCCESS;
}
