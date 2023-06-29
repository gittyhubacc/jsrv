#ifndef shutdown_h_
#define shutdown_h_

#include <pthread.h>

extern int shutdown_flag;
extern pthread_cond_t shutdown_cond;
extern pthread_mutex_t shutdown_mutex;

void graceful_shutdown();

#endif
