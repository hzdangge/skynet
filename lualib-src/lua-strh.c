#include "skynet_malloc.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#define BLOCK_SIZE 128
#define NUMB_SIZE 32

struct block {
	struct block * next;
	char buffer[BLOCK_SIZE];
};

struct wbuffer {
	struct block * head;
	struct block * current;
	int len;
	int ptr;
	int bfirst;
	char sep[1];
};

inline static struct block *
_balloc(void) {
	struct block *b = skynet_malloc(sizeof(struct block));
	b->next = NULL;
	return b;
}

inline static void
_push(struct wbuffer *b, const void *buf, int sz) {
	const char * buffer = buf;
	if (b->ptr == BLOCK_SIZE) {
__again:
		b->current = b->current->next = _balloc();
		b->ptr = 0;
	}
	if (b->ptr <= BLOCK_SIZE - sz) {
		memcpy(b->current->buffer + b->ptr, buffer, sz);
		b->ptr+=sz;
		b->len+=sz;
	} else {
		int copy = BLOCK_SIZE - b->ptr;
		memcpy(b->current->buffer + b->ptr, buffer, copy);
		buffer += copy;
		b->len += copy;
		sz -= copy;
		goto __again;
	}
}

inline static void
_pushex(struct wbuffer *b, const void *buf, int sz) {
	if (b->bfirst)
		b->bfirst = 0;
	else
		_push(b, b->sep, 1);
	_push(b, buf, sz);
}

static void
_init(struct wbuffer *wb , struct block *b, char sep) {
	wb->head = b;
	assert(b->next == NULL);
	wb->len = 0;
	wb->current = wb->head;
	wb->ptr = 0;
	wb->bfirst = 1;
	wb->sep[0] = sep;
}

static void
_free(struct wbuffer *wb) {
	struct block *blk = wb->head;
	blk = blk->next;	// the first block is on stack
	while (blk) {
		struct block * next = blk->next;
		skynet_free(blk);
		blk = next;
	}
	wb->head = NULL;
	wb->current = NULL;
	wb->ptr = 0;
	wb->len = 0;
	wb->bfirst = 0;
	wb->sep[0] = '\0';
}

static lua_Integer
_toint(lua_State *L, const char *pstr, int len, int *success) {
	lua_pushlstring(L, pstr, len);
	lua_Integer val = lua_tointegerx(L, -1, success);
	lua_pop(L, 1);
	return val;
}

static void
_seri(lua_State *L, struct block *b, int len) {
	char *buffer = skynet_malloc(len);
	char *ptr = buffer;
	int sz = len;
	while(len>0) {
		if (len >= BLOCK_SIZE) {
			memcpy(ptr, b->buffer, BLOCK_SIZE);
			ptr += BLOCK_SIZE;
			len -= BLOCK_SIZE;
			b = b->next;
		} else {
			memcpy(ptr, b->buffer, len);
			break;
		}
	}
	lua_pushlstring(L, buffer, sz);
	skynet_free(buffer);
}

static void
_check(lua_State *L, struct wbuffer *b, int boolean, const char *error) {
	if (boolean) {
		if (b) _free(b);
		luaL_error(L, error);
	}
}

static void
_pushtable(lua_State *L, int index, struct wbuffer *b, int depth) {
	if (depth == 1) {
	    int success;
	    lua_pushnil(L);
	    while (lua_next(L, index) != 0) {
	    	lua_Integer key = lua_tointeger(L, -2);
			lua_Integer val = lua_tointegerx(L, -1, &success);

			if (success) {
				char tmp[2*NUMB_SIZE+1];
				int tmpl = 0;
				tmpl = sprintf(tmp, "%lld=%lld", key, val);
				_pushex(b, tmp, tmpl);
			}

	    	lua_pop(L, 1);
	    }
	} else if (depth == 2) {
		int success;
		lua_pushnil(L);
		while (lua_next(L, index) != 0) {
			lua_Integer key1 = lua_tointeger(L, -2);
			if (lua_istable(L, -1)) {
				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					lua_Integer key2 = lua_tointeger(L, -2);
					lua_Integer val = lua_tointegerx(L, -1, &success);

					if (success) {
						char tmp[3*NUMB_SIZE+2];
						int tmpl = 0;
						tmpl = sprintf(tmp, "%lld=%lld=%lld", key1, key2, val);
						_pushex(b, tmp, tmpl);
					}

					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
}

static int
_table_length(lua_State *L, int index, int depth) {
	int len = 0;
	if (depth == 1) {
	    lua_pushnil(L);
	    while (lua_next(L, index) != 0) {
	    	len += 1;
	    	lua_pop(L, 1);
	    }
    } else if (depth == 2) {
    	lua_pushnil(L);
    	while (lua_next(L, index) != 0) {
    		if (lua_istable(L, -1)) {
    			lua_pushnil(L);
    			while (lua_next(L, -2) != 0) {
    				len += 1;
    				lua_pop(L, 1);
    			}
    		}
    		lua_pop(L, 1);
    	}
    }
    return len;
}

static int
_isdouble(const char *start, const char *end) {
	const char *pstr = start;
	for ( ; pstr < end; pstr += 1) {
		if (*pstr == '.') return end-pstr;
	}
	return 0;
}

static int
lvalue(lua_State *L) {
	luaL_argcheck(L, lua_gettop(L) == 3, 1, "lvalue: expected 3 argument");
	// strh.value(str, index, delimiter)
	size_t len = 0;
	const char *pstr = luaL_checklstring(L, 1, &len);
	lua_Integer index = luaL_checkinteger(L, 2);
	const char *sep = luaL_checkstring(L, 3);
    const char *end = pstr + len;

    char tmp[NUMB_SIZE+1];
    int tmpl = 0;
    int i=1;
	for ( ; pstr < end; i++) {
		const char *fix = strchr(pstr, *sep);
    	if (fix == NULL) fix = end;

    	if (i == index) {
    		lua_pushboolean(L, 1);
    		lua_pushlstring(L, pstr, fix-pstr);
    		return 2;
    	}

    	tmpl = fix-pstr;
    	_check(L, NULL, tmpl>NUMB_SIZE, "lvalue: number length large than 32");
    	memcpy(tmp, pstr, tmpl);

    	pstr = fix + 1;
	}

	lua_pushboolean(L, 0);
	lua_pushlstring(L, tmp, tmpl);
	return 2;
}

static int
ltoarray(lua_State *L) {
    luaL_argcheck(L, lua_gettop(L) == 3, 1, "ltoarray: expected 3 argument");

    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    int bNum = lua_toboolean(L, 2);
    const char *sep = luaL_checkstring(L, 3);
    const char *end = pstr + len;

	lua_newtable(L);
	int i=1;
    for ( ; pstr < end; i++) {
    	int success;

    	const char *fix = strchr(pstr, *sep);
    	if (fix == NULL) fix = end;

		lua_pushlstring(L, pstr, fix-pstr);
    	if (bNum) {
    		if (_isdouble(pstr, fix-1)) {
				lua_Number val = lua_tonumberx(L, -1, &success);
				_check(L, NULL, !success, "ltoarray: value expected number");
				lua_pop(L, 1);
				lua_pushnumber(L, val);
			} else {
				lua_Integer val = lua_tointegerx(L, -1, &success);
				_check(L, NULL, !success, "ltoarray: value expected number");
				lua_pop(L, 1);
				lua_pushinteger(L, val);
			}
    	}
    	lua_rawseti(L, -2, i);
    	pstr = fix + 1;
    }

    return 1;
}

static int
ltonummap(lua_State *L) {
	luaL_argcheck(L, lua_gettop(L) == 2, 1, "ltonummap: expected 2 argument");
    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    const char *sep = luaL_checkstring(L, 2);
    const char *end = pstr + len;

    lua_newtable(L);

    for ( ; pstr < end; ) {
    	int success;

    	const char *fix = strchr(pstr, '=');
		_check(L, NULL, fix==NULL, "ltonummap: string formal error");
		lua_pushlstring(L, pstr, fix-pstr);
		lua_Number key = lua_tonumberx(L, -1, &success);
		_check(L, NULL, !success, "ltonummap: key expected number");
		pstr = fix + 1;
		lua_pop(L, 1);

		fix = strchr(pstr, *sep);
    	if (fix == NULL) fix = end;
    	lua_pushlstring(L, pstr, fix-pstr);
    	if (_isdouble(pstr, fix-1)) {
			lua_Number val = lua_tonumberx(L, -1, &success);
			_check(L, NULL, !success, "ltonummap: value expected number");
			lua_pop(L, 1);
			lua_pushnumber(L, val);
		} else {
			lua_Integer val = lua_tointegerx(L, -1, &success);
			_check(L, NULL, !success, "ltonummap: value expected number");
			lua_pop(L, 1);
			lua_pushinteger(L, val);
		}
		pstr = fix + 1;

		lua_rawseti(L, -2, key);
    }

    return 1;
}

static int
lgetv(lua_State *L) {
    luaL_argcheck(L, lua_gettop(L) == 4, 1, "lgetv: expected 4 argument");

    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    lua_Integer k = luaL_checkinteger(L, 2);
    lua_Integer def = luaL_checkinteger(L, 3);
    const char *sep = luaL_checkstring(L, 4);
    const char *end = pstr + len;

    for ( ; pstr < end; ) {
		int success;

    	/* find key */
    	const char *fix = strchr(pstr, '=');
		_check(L, NULL, fix==NULL, "lgetv: string formal error");
		lua_Integer key = _toint(L, pstr, fix-pstr, &success);
		_check(L, NULL, !success, "lgetv: key expected integer");
		pstr = fix + 1;

		/* find val */
		fix = strchr(pstr, *sep);
		if (fix == NULL) fix = end;
		lua_Integer val = _toint(L, pstr, fix-pstr, &success);
		_check(L, NULL, !success, "lgetv: value expect integer");
		pstr = fix + 1;

		if (key == k) {
			def = val;
			goto __tail;
		}
    }

__tail:
    lua_pushinteger(L, def);
	return 1;
}

static int
lgetvdk(lua_State *L) {
    luaL_argcheck(L, lua_gettop(L) == 5, 1, "lgetvdk: expected 5 argument");

    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    lua_Integer k1 = luaL_checkinteger(L, 2);
    lua_Integer k2 = luaL_checkinteger(L, 3);
    lua_Integer def = luaL_checkinteger(L, 4);
    const char *sep = luaL_checkstring(L, 5);
    const char *end = pstr + len;

    for ( ; pstr < end; ) {
		int success;

    	/* find key1 */
    	const char *fix = strchr(pstr, '=');
		_check(L, NULL, fix==NULL, "lgetvdk: key1 formal error");
		lua_Integer key1 = _toint(L, pstr, fix-pstr, &success);
		_check(L, NULL, !success, "lgetvdk: key1 expected integer");
		pstr = fix + 1;

    	/* find key2 */
		fix = strchr(pstr, '=');
		_check(L, NULL, fix==NULL, "lgetvdk: key2 formal error");
		lua_Integer key2 = _toint(L, pstr, fix-pstr, &success);
		_check(L, NULL, !success, "lgetvdk: key2 expected integer");
		pstr = fix + 1;

		/* find val */
		fix = strchr(pstr, *sep);
		if (fix == NULL) fix = end;
		lua_Integer val = _toint(L, pstr, fix-pstr, &success);
		_check(L, NULL, !success, "lgetvdk: value expected integer");
		pstr = fix + 1;

		if (key1 == k1 && key2 == k2) {
			def = val;
			goto __tail;
		}
    }

__tail:
    lua_pushinteger(L, def);
	return 1;
}

static int
lmoddk(lua_State *L) {
    luaL_argcheck(L, lua_gettop(L) == 4, 1, "lmoddk: expected 4 argument");

    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    int tlen = _table_length(L, 2, 2);
    int binc = lua_toboolean(L, 3);
	const char *sep = luaL_checkstring(L, 4);
    const char *begin = pstr;
    const char *end = pstr+len;
	const char *step = pstr;

	struct block temp;
	temp.next = NULL;
	struct wbuffer wb;
	_init(&wb, &temp, *sep);

	for ( ; pstr < end; ) {
		step = pstr;
		int success;

		/* find key1 */
		const char *fix = strchr(pstr, '=');
		_check(L, &wb, fix==NULL, "lmoddk: string formal error");
		lua_Integer key1 = _toint(L, pstr, fix-pstr, &success);
		_check(L, &wb, !success, "lmoddk: key1 expected integer");
		pstr = fix + 1;

		/* find key2 */
		fix = strchr(pstr, '=');
		_check(L, &wb, fix==NULL, "lmoddk: string formal error");
		lua_Integer key2 = _toint(L, pstr, fix-pstr, &success);
		_check(L, &wb, !success, "lmoddk: key2 expected integer");
		pstr = fix + 1;

		/* find val */
		fix = strchr(pstr, *sep);
    	if (fix == NULL) fix = end;
    	lua_Integer val = _toint(L, pstr, fix-pstr, &success);
		_check(L, &wb, !success, "lmoddk: value expected integer");
    	pstr = fix + 1;

    	if (lua_rawgeti(L, 2, key1) != LUA_TNIL &&
    			lua_istable(L, -1) &&
    				lua_rawgeti(L, -1, key2) != LUA_TNIL) {
    		if (step != begin)
    			_pushex(&wb, begin, step-1-begin);
    		begin = pstr;

    		lua_Integer rep = lua_tointegerx(L, -1, &success);
    		lua_pop(L, 1);

    		if (success) {
    			char tmp[3*NUMB_SIZE+2];
	    		int tmpl;
				tmpl = sprintf(tmp, "%lld=%lld=%lld", key1, key2, binc?(val+rep):rep);
				_pushex(&wb, tmp, tmpl);
			}

			lua_pushnil(L);
			lua_rawseti(L, -2, key2);
			tlen--;
    	}
		lua_pop(L, 1);

    	if (tlen == 0 && pstr < end) {
    		_pushex(&wb, pstr, end-pstr);
    		goto __tail;
    	}
	}
    if (pstr != begin)
    	_pushex(&wb, begin, end-begin);
	_pushtable(L, 2, &wb, 2);

__tail:
    _seri(L, &temp, wb.len);
    _free(&wb);
    return 1;
}

static int
lmodify(lua_State *L) {
    luaL_argcheck(L, lua_gettop(L) == 4, 1, "lmodify: expected 4 argument");

    size_t len = 0;
    const char *pstr = luaL_checklstring(L, 1, &len);
    int tlen = _table_length(L, 2, 1);
    int binc = lua_toboolean(L, 3);
	const char *sep = luaL_checkstring(L, 4);
    const char *begin = pstr;
    const char *end = pstr+len;
	const char *step = pstr;

	struct block temp;
	temp.next = NULL;
	struct wbuffer wb;
	_init(&wb, &temp, *sep);

    for ( ; pstr < end; ) {
    	step = pstr;
    	int success;

    	/* find key */
    	const char *fix = strchr(pstr, '=');
    	_check(L, &wb, fix==NULL, "lmodify: string fromal error");
    	lua_Integer key = _toint(L, pstr, fix-pstr, &success);
		_check(L, &wb, !success, "lmodify: key expected integer");
		pstr = fix + 1;

    	/* find val */
		fix = strchr(pstr, *sep);
    	if (fix == NULL) fix = end;
    	lua_Integer val = _toint(L, pstr, fix-pstr, &success);
		_check(L, &wb, !success, "lmodify: value expected integer");
    	pstr = fix + 1;

    	if (lua_rawgeti(L, 2, key) != LUA_TNIL) {
    		if (step != begin)
    			_pushex(&wb, begin, step-1-begin);
    		begin = pstr;

    		lua_Integer rep = lua_tointegerx(L, -1, &success);
    		lua_pop(L, 1);

    		if (success) {
	    		char tmp[2*NUMB_SIZE+1];
	    		int tmpl;
				tmpl = sprintf(tmp, "%lld=%lld", key, binc?(val+rep):rep);
				_pushex(&wb, tmp, tmpl);
			}

			lua_pushnil(L);
			lua_rawseti(L, 2, key);
			tlen--;
    	} else {
	    	lua_pop(L, 1);
    	}

    	if (tlen == 0 && pstr < end) {
    		_pushex(&wb, pstr, end-pstr);
    		goto __tail;
    	}
    }

    if (pstr != begin)
    	_pushex(&wb, begin, end-begin);
	_pushtable(L, 2, &wb, 1);

__tail:
    _seri(L, &temp, wb.len);
    _free(&wb);
    return 1;
}

int
luaopen_strh(lua_State *L) {
	luaL_Reg l[] = {
		{ "modify", lmodify },
		{ "moddk", lmoddk },
		{ "getv", lgetv },
		{ "getvdk", lgetvdk },
		{ "toarray", ltoarray },
		{ "tonummap", ltonummap },
		{ "value", lvalue },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}
