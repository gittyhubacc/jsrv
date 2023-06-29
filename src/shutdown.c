#include "shutdown.h"

void graceful_shutdown()
{
	pthread_mutex_lock(&shutdown_mutex);
	shutdown_flag = 1;
	pthread_mutex_unlock(&shutdown_mutex);
	pthread_cond_signal(&shutdown_cond);
}
