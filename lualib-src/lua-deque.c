#include "skynet_malloc.h"
#include "spinlock.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

struct deque {
	struct spinlock lock;
	uint32_t *buffer;
	int head;
	int tail;
	int length;
	int size;
};

#define checkdeque(L) \
	(struct deque *)luaL_checkudata(L, 1, "dangge.deque")

int
lpush(lua_State *L) {
	struct deque *q = checkdeque(L);
	if (q == NULL) return 0;
	uint32_t elem = luaL_checkinteger(L, 2) & 0xFFFFFFFF;

	SPIN_LOCK(q)
	if (q->size >= q->length) {
		SPIN_UNLOCK(q)
		lua_pushboolean(L, 0);
		return 1;
	}
	q->buffer[q->tail] = elem;
	q->tail += 1;
	if (q->tail >= q->length)
		q->tail = 0;
	q->size += 1;
	SPIN_UNLOCK(q)
	lua_pushboolean(L, 1);
	return 1;
}

int
lpop(lua_State *L) {
	struct deque *q = checkdeque(L);
	if (q == NULL) return 0;
	SPIN_LOCK(q)
	if (q->size <= 0) {
		SPIN_UNLOCK(q)
		return 0;
	}
	uint32_t elem = q->buffer[q->head];
	q->head += 1;
	if (q->head >= q->length)
		q->head = 0;
	q->size -= 1;
	SPIN_UNLOCK(q)
	lua_pushinteger(L, elem);
	return 1;
}

int
lgc(lua_State *L) {
	struct deque *q = checkdeque(L);
	if (q == NULL) return 0;
	SPIN_LOCK(q)
	skynet_free(q->buffer);
	SPIN_UNLOCK(q)
	SPIN_DESTROY(q)
	return 0;
}

int
lnew(lua_State *L) {
	luaL_argcheck(L, lua_gettop(L) == 1, 1, "lnew: expected 1 argument");
	lua_Integer len = luaL_checkinteger(L, 1);
	if (len < 1) luaL_error(L, "deque length can't less than 1");
	struct deque *q = lua_newuserdata(L, sizeof(*q));
	memset(q, 0, sizeof(*q));
	q->buffer = skynet_malloc(len*sizeof(uint32_t));
	q->length = len;
	lua_pushlightuserdata(L, q);
	luaL_getmetatable(L, "dangge.deque");
	lua_pushvalue(L, -1);
	lua_pushcfunction(L, lgc);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, 2);
	lua_pop(L, 1);
	SPIN_INIT(q)
	return 2;
}

int
lclone(lua_State *L) {
	void *q = lua_touserdata(L, 1);
	if (q == NULL) return 0;
	luaL_getmetatable(L, "dangge.deque");
	lua_setmetatable(L, -2);
	return 1;
}

int
lsize(lua_State *L) {
	struct deque *q = checkdeque(L);
	if (q) {
		SPIN_LOCK(q)
		lua_pushinteger(L, q->size);
		SPIN_UNLOCK(q)
	} else lua_pushinteger(L, 0);
	return 1;
}

int
luaopen_deque(lua_State *L) {
	luaL_Reg m[] = {
		{ "pop", lpop },
		{ "push", lpush },
		{ "size", lsize },
		{ NULL, NULL },
	};
	luaL_newmetatable(L, "dangge.deque");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, m, 0);

	luaL_Reg l[] = {
		{ "new", lnew },
		{ "clone", lclone },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}
