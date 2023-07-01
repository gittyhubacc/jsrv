#ifndef shutdown_h_
#define shutdown_h_

#include <pthread.h>

int should_shutdown();
void graceful_shutdown();

#endif
