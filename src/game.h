#ifndef game_h_
#define game_h_

#define JSMN_HEADER
#include "jsmn.h"
#undef JSMN_HEADER

#include "queue.h"

struct game_msg {
	char *payload;
	struct work_msg *raw;
	jsmntok_t *tokens;
	int tok_cnt;
};

extern struct queue gamequeue;

void game_loop_init();
void *game_loop(void *arg);
void game_loop_destroy();


#endif
