#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "shutdown.h"
#include "game.h"
#include "worker.h"

struct queue gamequeue;

lua_State *lua;

static void free_game_msg(struct game_msg *msg)
{
	free(msg->tokens);
	free(msg->raw);
	free(msg);
}

void game_loop_init()
{
	queue_init(&gamequeue);
	lua = luaL_newstate();
	luaL_openlibs(lua);
	int res = luaL_loadfile(lua, "game.lua") || lua_pcall(lua, 0, 0, 0);
	if (res != LUA_OK) {
		printf("lua not ok: %s\n", lua_tostring(lua, -1));
	}
}

static void push_token(struct game_msg *msg, int *tok_idx);

static void push_object(struct game_msg *msg, int *tok_idx)
{
	jsmntok_t *t = &msg->tokens[(*tok_idx)++];

	lua_newtable(lua);
	for (int i = 0; i < t->size; i++) {
		push_token(msg, tok_idx);
		push_token(msg, tok_idx);
		lua_settable(lua, -3);
	}
}
static void push_array(struct game_msg *msg, int *tok_idx)
{
	jsmntok_t *t = &msg->tokens[(*tok_idx)++];

	lua_newtable(lua);
	for (int i = 0; i < t->size; i++) {
		lua_pushnumber(lua, i + 1);
		push_token(msg, tok_idx);
		lua_settable(lua, -3);
	}
}
static void push_string(struct game_msg *msg, int *tok_idx)
{
	char *payload = msg->payload;
	jsmntok_t *t = &msg->tokens[(*tok_idx)++];
	lua_pushlstring(lua, payload + t->start, t->end - t->start);
}
static void push_primitive(struct game_msg *msg, int *tok_idx)
{
	jsmntok_t *t = &msg->tokens[(*tok_idx)++];
	char *payload = msg->payload;
	int t_sz = t->end - t->start;
	switch (payload[t->start]) {
	case 't':
	case 'T':
		lua_pushboolean(lua, 1);
		break;
	case 'f':
	case 'F':
		lua_pushboolean(lua, 0);
		break;
	case 'n':
	case 'N':
		lua_pushnil(lua);
		break;
	default:
		lua_pushlstring(lua, payload + t->start, t_sz);
		if (!lua_isnumber(lua, -1)) {
			lua_pop(lua, 1);
			lua_pushnil(lua);
		}
		break;
	}
}

static void push_token(struct game_msg *msg, int *tok_idx)
{
	switch (msg->tokens[*tok_idx].type) {
	case JSMN_OBJECT:
		push_object(msg, tok_idx);
		break;
	case JSMN_ARRAY:
		push_array(msg, tok_idx);
		break;
	case JSMN_STRING:
		push_string(msg, tok_idx);
		break;
	case JSMN_PRIMITIVE:
		push_primitive(msg, tok_idx);
		break;
	default:
		// TODO: abort, clear stack and pop another msg
		*tok_idx = msg->tok_cnt;
		break;
	}
}

static void push_message(struct game_msg *msg)
{
	int tok_idx = 0;
	int arguments = 0;
	lua_getglobal(lua, "process_message");
	while (tok_idx < msg->tok_cnt) {
		push_token(msg, &tok_idx);
		arguments++;
	}
	int res = lua_pcall(lua, arguments, 1, 0);
	if (res != LUA_OK) {
		fprintf(stderr, "failed to call process_message: %s\n", 
				lua_tostring(lua, -1));
	}
	lua_pop(lua, 1);
}

void *game_loop(void *arg)
{
	// block signals
	sigset_t signal_set;
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, NULL);

	struct game_msg *msg;
	while (!should_shutdown()) {
		msg = (struct game_msg *)queue_pop(&gamequeue);
		push_message(msg);
		free_game_msg(msg);
	}
	// drain the queue, dont leak memories~
	while (queue_peek(&gamequeue)) {
		free_game_msg((struct game_msg *)queue_pop(&gamequeue));
	}

	return NULL;
}

void game_loop_destroy()
{
	lua_close(lua);
	queue_destroy(&gamequeue);
}
