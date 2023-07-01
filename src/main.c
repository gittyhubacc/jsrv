#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include "networkio.h"
#include "shutdown.h"

int shutdown_flag;
pthread_cond_t shutdown_cond;
pthread_mutex_t shutdown_mutex;

void graceful_shutdown()
{
	pthread_mutex_lock(&shutdown_mutex);
	shutdown_flag = 1;
	pthread_mutex_unlock(&shutdown_mutex);
	pthread_cond_signal(&shutdown_cond);
}

void *signal_handler(void *arg)
{
	int sig, res;
	sigset_t signal_set;
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	while (1) {
		res = sigwait(&signal_set, &sig);
		if (res < 0) {
			printf("sigwait(): returned %i\n", res);
			break;
		}
		printf("signal_handler(): received signal %d\n", sig);
		if (sig == SIGINT) break;
	}

	graceful_shutdown();
	return NULL;
}

int main(int argc, char **argv)
{
	// pre-threaded initialization
	shutdown_flag = 0;

	// block sigint on the main thread (for some reason? IBM told me to)
	sigset_t signal_set;
	sigemptyset(&signal_set);
	sigaddset(&signal_set, SIGINT);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	// initialize threads n at
	pthread_t signal_thread;
	pthread_t recvmsg_thread;
	pthread_mutex_init(&shutdown_mutex, NULL);
	pthread_cond_init(&shutdown_cond, NULL);

	int res = pthread_create(&signal_thread, NULL, signal_handler, NULL);
	if (res < 0) {
		perror("pthread_create()");
		goto destroy_sync;
	}
	res = pthread_create(&recvmsg_thread, NULL, recvmsg_loop, NULL);
	if (res < 0) {
		perror("pthread_create()");
		goto destroy_sync;
	}

	pthread_mutex_lock(&shutdown_mutex);
	while (!shutdown_flag) {
		pthread_cond_wait(&shutdown_cond, &shutdown_mutex);
	}
	pthread_mutex_unlock(&shutdown_mutex);

	printf("shutting down...\n");

destroy_threads:
	pthread_cancel(signal_thread);
	pthread_join(signal_thread, NULL);
	pthread_join(recvmsg_thread, NULL);

destroy_sync:
	pthread_cond_destroy(&shutdown_cond);
	pthread_mutex_destroy(&shutdown_mutex);

	return EXIT_SUCCESS;
}
