#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <lua.h>
#include "shutdown.h"
#include "worker.h"
#include "game.h"

#define JSMN_HEADER
#include "jsmn.h"
#undef JSMN_HEADER

struct queue workqueue;

#define MAX_TOKENS 256
#define HEADER_SZ 4
static void json_operation(struct work_msg *msg)
{
	// read how many tokens are in the payload
	int tok_cnt = ntohs(((short int *)msg->payload)[1]);
	if (tok_cnt <= 0 || tok_cnt > MAX_TOKENS) {
		fprintf(stderr, "bad tok_cnt: %i\n", tok_cnt);
		free(msg);
		return;
	}

	jsmntok_t *tokens = malloc(sizeof(*tokens) * tok_cnt);
	if (!tokens) {
		perror("json_operation(): malloc()");
		free(msg);
		return;
	}

	jsmn_parser p;
	size_t json_sz = msg->len - HEADER_SZ;
	char *json_str = msg->payload + HEADER_SZ;

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

	struct game_msg *game_msg = malloc(sizeof(*msg));
	if (!game_msg) {
		perror("json_operation(): malloc()");
		free(tokens);
		free(msg);
		return;
	}
	game_msg->raw = msg;
	game_msg->tokens = tokens;
	game_msg->tok_cnt = tok_cnt;
	game_msg->payload = msg->payload + HEADER_SZ;
	queue_push(&gamequeue, game_msg);
}
#undef HEADER_SZ
#undef MAX_TOKENS

#define OPERATION_CNT 1
typedef void (*operation_t)(struct work_msg *msg);
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
	struct work_msg *msg;
	pthread_cleanup_push(worker_cleanup, NULL);
	while (!should_shutdown()) {
		msg = (struct work_msg *)queue_pop(&workqueue);
		opcode = ntohs(*(short int *)msg->payload);
		if (opcode < 0 || opcode >= OPERATION_CNT) {
			fprintf(stderr, "bad opcode: %i\n", opcode);
		} else {
			operations[opcode](msg);
		}
	}
	pthread_cleanup_pop(1);
	return NULL;
}
#undef OPERATION_CNT
