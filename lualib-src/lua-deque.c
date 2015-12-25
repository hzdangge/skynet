#include "skynet_malloc.h"
#include "spinlock.h"

#include "skynet.h"
#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

struct node
{
	uint32_t elem;
	struct node *next;
};

struct deque {
	struct spinlock lock;
	struct node *head;
	struct node *tail;
	uint32_t size;
};

#define checkdeque(L) \
	(struct deque *)luaL_checkudata(L, 1, "dangge.deque")

int
lpush(lua_State *L) {
	struct deque *q = checkdeque(L);
	SPIN_LOCK(q)
	if (q == NULL) {
		SPIN_UNLOCK(q)
		return 0;
	}
	uint32_t elem = luaL_checkinteger(L, 2) & 0xFFFFFFFF;
	struct node *pd = skynet_malloc(sizeof(*pd));
	pd->elem = elem;
	pd->next = NULL;
	if(q->tail) {
		q->tail->next = pd;
		q->tail = pd;
	} else {
		q->head = q->tail = pd;
	}
	q->size += 1;
	SPIN_UNLOCK(q)
	return 0;
}

uint32_t
_pop(struct deque *q) {
	struct node *pd = q->head;
	if(pd) {
		q->head = pd->next;
		if(q->head == NULL) {
			assert(pd == q->tail);
			q->tail = NULL;
		}
		pd->next = NULL;
		q->size -= 1;
		uint32_t rtn = pd->elem;
		skynet_free(pd);
		return rtn;
	}
	return 0;
}

int
lpop(lua_State *L) {
	struct deque *q = checkdeque(L);
	SPIN_LOCK(q)
	if (q) {
		uint32_t elem = _pop(q);
		if (elem > 0) {
			lua_pushinteger(L, elem);
			SPIN_UNLOCK(q)
			return 1;
		}
	}
	SPIN_UNLOCK(q)
	return 0;
}

int
lgc(lua_State *L) {
	struct deque *q = checkdeque(L);
	SPIN_LOCK(q)
	if (q) {
		for (;_pop(q) != 0;);

	}
	SPIN_UNLOCK(q)
	SPIN_DESTROY(q)
	skynet_free(q);
	return 0;
}

int
lnew(lua_State *L) {
	struct deque *q = skynet_malloc(sizeof(*q));
	memset(q, 0, sizeof(*q));
	lua_pushlightuserdata(L, q);
	luaL_getmetatable(L, "dangge.deque");
	lua_pushvalue(L, -1);
	lua_pushcfunction(L, lgc);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, 1);
	lua_pop(L, 1);
	SPIN_INIT(q)
	return 1;
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
	SPIN_LOCK(q)
	if (q) {
		lua_pushinteger(L, q->size);
	} else lua_pushinteger(L, 0);
	SPIN_UNLOCK(q)
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
