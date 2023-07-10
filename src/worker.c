#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <lua.h>
#include "shutdown.h"
#include "worker.h"
#include "jsmn.h"

struct queue workqueue;

#define MAX_TOKENS 256
#define HEADER_SZ 4
static void json_operation(struct workdata *msg)
{
	// read how many tokens are in the payload
	int tok_cnt = ntohs(((short int *)msg->payload)[1]);
	if (tok_cnt <= 0 || tok_cnt > MAX_TOKENS) {
		fprintf(stderr, "bad tok_cnt: %i\n", tok_cnt);
		return;
	}

	jsmn_parser p;
	jsmntok_t tokens[tok_cnt];
	char *json_str = msg->payload + HEADER_SZ;
	size_t json_sz = msg->len - HEADER_SZ;

	jsmn_init(&p);
	int res = jsmn_parse(&p, json_str, json_sz, tokens, tok_cnt);
	if (res < 0) switch (res) {
	case JSMN_ERROR_NOMEM:
		fprintf(stderr, "jsmn_parse(): %i: "
				"message had incorrect token count\n", res);
		return;
	case JSMN_ERROR_PART:
		fprintf(stderr, "jsmn_parse(): %i: "
				"message had incomplete json\n", res);
		return;
	case JSMN_ERROR_INVAL:
		fprintf(stderr, "jsmn_parse(): %i: "
				"message had invalid json\n", res);
		return;
	default:
		fprintf(stderr, "jsmn_parse(): %i: "
				"something bad happened\n", res);
		return;
	}

	printf("got %i tokens\n\t%s\n", res, json_str);
}
#undef HEADER_SZ
#undef MAX_TOKENS

#define OPERATION_CNT 1
typedef void (*operation_t)(struct workdata *msg);
static operation_t operations[OPERATION_CNT] = {
	json_operation
};

static void worker_cleanup(void *arg)
{
	pthread_mutex_unlock(&workqueue.mutex);
}

void *worker_loop(void *arg)
{
	// block signals
	sigset_t signal_set;
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	int opcode;
	struct workdata *message;
	pthread_cleanup_push(worker_cleanup, NULL);
	while (!should_shutdown()) {
		message = (struct workdata *)queue_pop(&workqueue);
		opcode = ntohs(*(short int *)message->payload);
		if (opcode < 0 || opcode >= OPERATION_CNT) {
			fprintf(stderr, "bad opcode: %i\n", opcode);
		} else {
			operations[opcode](message);
		}
		free(message);
	}
	pthread_cleanup_pop(1);
	return NULL;
}
#undef OPERATION_CNT
