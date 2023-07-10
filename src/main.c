#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include "networkio.h"
#include "worker.h"
#include "shutdown.h"

#define WORKER_CNT 4

static int shutdown_flag;
static pthread_cond_t shutdown_cond;
static pthread_mutex_t shutdown_mutex;

int should_shutdown()
{
	return shutdown_flag;
}

void graceful_shutdown()
{
	pthread_mutex_lock(&shutdown_mutex);
	shutdown_flag = 1;
	pthread_mutex_unlock(&shutdown_mutex);
	pthread_cond_broadcast(&shutdown_cond);
}

void *signal_handler(void *arg)
{
	int res;
	sigset_t sigset;
	siginfo_t siginfo;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	while (!should_shutdown()) {
		res = sigwaitinfo(&sigset, &siginfo);
		if (res < 0) {
			printf("sigwait(): returned %i\n", res);
			break;
		}
		printf("signal_handler(): received signal %d\n", siginfo.si_signo);
		if (siginfo.si_signo == SIGINT) break;
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
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	// initialize threads n at
	pthread_t signal_thread;
	pthread_t recvmsg_thread;
	pthread_t wrk_threads[WORKER_CNT];
	pthread_mutex_init(&shutdown_mutex, NULL);
	pthread_cond_init(&shutdown_cond, NULL);
	queue_init(&workqueue);

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
	for (int i = 0; i < WORKER_CNT; i++) {
		res = pthread_create(&wrk_threads[i], NULL, worker_loop, NULL);
		if (res < 0) {
			perror("pthread_create()");
			goto destroy_sync;
		}
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
	for (int i = 0; i < WORKER_CNT; i++) {
		pthread_cancel(wrk_threads[i]);
		pthread_join(wrk_threads[i], NULL);
	}

destroy_sync:
	queue_destroy(&workqueue);
	pthread_cond_destroy(&shutdown_cond);
	pthread_mutex_destroy(&shutdown_mutex);

	return EXIT_SUCCESS;
}
