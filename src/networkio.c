#include <stdio.h>
#include <stdlib.h>
#include <liburing.h>
#include "networkio.h"
#include "shutdown.h"

#define MAX_CONNECTIONS 16

void *accept_read_loop(void *arg)
{
	printf("something went wrong!\n");
	graceful_shutdown();
	return NULL;
}

