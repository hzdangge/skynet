/*
	This is libray of http://en.wikipedia.org/wiki/XXTEA for lua for very simple encryption.

	compile in gcc with
		gcc --shared -fPIC -O2 -o xxtea.so xxtea.c
	use in lua
		require'xxtea'
		str = 'something'
		encstr = xxtea.encrypt( str, 'abcd1234abcd1234' )
		decstr = xxtea.decrypt( encstr, 'abcd1234abcd1234' )
	where the key is a 128 bit hex string
*/

#include "skynet_malloc.h"

#include <lua.h>
#include <lauxlib.h>
#include <memory.h>

#include <stddef.h> /* for size_t & NULL declarations */

#if defined(_MSC_VER)

typedef  __int32 xxtea_long;

#else

#if defined(__FreeBSD__) && __FreeBSD__ < 5
/* FreeBSD 4 doesn't have stdint.h file */
#include <inttypes.h>
#else
#include <stdint.h>
#endif

typedef uint32_t xxtea_long;

#endif /* end of if defined(_MSC_VER) */

#define XXTEA_MX (z >> 5 ^ y << 2) + (y >> 3 ^ z << 4) ^ (sum ^ y) + (k[p & 3 ^ e] ^ z)
#define XXTEA_DELTA 0x9e3779b9

static void xxtea_long_encrypt(xxtea_long *v, xxtea_long len, xxtea_long *k)
{
    xxtea_long n = len - 1;
    xxtea_long z = v[n], y = v[0], p, q = 6 + 52 / (n + 1), sum = 0, e;
    if (n < 1) {
        return;
    }
    while (0 < q--) {
        sum += XXTEA_DELTA;
        e = sum >> 2 & 3;
        for (p = 0; p < n; p++) {
            y = v[p + 1];
            z = v[p] += XXTEA_MX;
        }
        y = v[0];
        z = v[n] += XXTEA_MX;
    }
}

static void xxtea_long_decrypt(xxtea_long *v, xxtea_long len, xxtea_long *k)
{
    xxtea_long n = len - 1;
    xxtea_long z = v[n], y = v[0], p, q = 6 + 52 / (n + 1), sum = q * XXTEA_DELTA, e;
    if (n < 1) {
        return;
    }
    while (sum != 0) {
        e = sum >> 2 & 3;
        for (p = n; p > 0; p--) {
            z = v[p - 1];
            y = v[p] -= XXTEA_MX;
        }
        z = v[n];
        y = v[0] -= XXTEA_MX;
        sum -= XXTEA_DELTA;
    }
}

static char *fix_key_length(char *key, xxtea_long key_len)
{
    char *tmp = (char *)skynet_malloc(16);
    memcpy(tmp, key, key_len);
    memset(tmp + key_len, '\0', 16 - key_len);
    return tmp;
}

static xxtea_long *xxtea_to_long_array(char *data, xxtea_long len, int include_length, xxtea_long *ret_len) {
    xxtea_long i, n, *result;

    n = len >> 2;
    n = (((len & 3) == 0) ? n : n + 1);
    if (include_length) {
        result = (xxtea_long *)skynet_malloc((n + 1) << 2);
        result[n] = len;
        *ret_len = n + 1;
    } else {
        result = (xxtea_long *)skynet_malloc(n << 2);
        *ret_len = n;
    }
    memset(result, 0, n << 2);
    for (i = 0; i < len; i++) {
        result[i >> 2] |= (xxtea_long)data[i] << ((i & 3) << 3);
    }

    return result;
}

static  char *xxtea_to_byte_array(xxtea_long *data, xxtea_long len, int include_length, xxtea_long *ret_len) {
    xxtea_long i, n, m;
     char *result;

    n = len << 2;
    if (include_length) {
        m = data[len - 1];
        if ((m < n - 7) || (m > n - 4)) return NULL;
        n = m;
    }
    result = (char *)skynet_malloc(n + 1);
    for (i = 0; i < n; i++) {
        result[i] = ( char)((data[i >> 2] >> ((i & 3) << 3)) & 0xff);
    }
    result[n] = '\0';
    *ret_len = n;

    return result;
}

static char *do_xxtea_encrypt( char *data, xxtea_long len,  char *key, xxtea_long *ret_len) {
     char *result;
    xxtea_long *v, *k, v_len, k_len;

    v = xxtea_to_long_array(data, len, 1, &v_len);
    k = xxtea_to_long_array(key, 16, 0, &k_len);
    xxtea_long_encrypt(v, v_len, k);
    result = xxtea_to_byte_array(v, v_len, 0, ret_len);
    skynet_free(v);
    skynet_free(k);

    return result;
}

static char *do_xxtea_decrypt( char *data, xxtea_long len,  char *key, xxtea_long *ret_len) {
     char *result;
    xxtea_long *v, *k, v_len, k_len;

    v = xxtea_to_long_array(data, len, 0, &v_len);
    k = xxtea_to_long_array(key, 16, 0, &k_len);
    xxtea_long_decrypt(v, v_len, k);
    result = xxtea_to_byte_array(v, v_len, 1, ret_len);
    skynet_free(v);
    skynet_free(k);

    return result;
}

 char *xxtea_encrypt( char *data, xxtea_long data_len,  char *key, xxtea_long key_len, xxtea_long *ret_length)
{
     char *result;

    *ret_length = 0;

    if (key_len < 16) {
        char *key2 = fix_key_length(key, key_len);
        result = do_xxtea_encrypt(data, data_len, key2, ret_length);
        skynet_free(key2);
    }
    else
    {
        result = do_xxtea_encrypt(data, data_len, key, ret_length);
    }

    return result;
}

char *xxtea_decrypt( char *data, xxtea_long data_len,  char *key, xxtea_long key_len, xxtea_long *ret_length)
{
     char *result;

    *ret_length = 0;

    if (key_len < 16) {
         char *key2 = fix_key_length(key, key_len);
        result = do_xxtea_decrypt(data, data_len, key2, ret_length);
        skynet_free(key2);
    }
    else
    {
        result = do_xxtea_decrypt(data, data_len, key, ret_length);
    }

    return result;
}

static int decrypt( lua_State *L )
{
	size_t textLength;
	size_t keyLength;
	const  char *text = luaL_checklstring( L, 1, &textLength );
	const  char *key = luaL_checklstring( L, 2, &keyLength );
	xxtea_long ret_length;
	 char * result = xxtea_decrypt(( char *)text, (xxtea_long)textLength, ( char *)key, (xxtea_long)keyLength, &ret_length);
	lua_pushlstring(L, result, ret_length);
	skynet_free(result);

	return 1;
}

static int encrypt( lua_State *L)
{
	size_t textLength;
	size_t keyLength;
	const  char *text = luaL_checklstring( L, 1, &textLength );
        const  char *key = luaL_checklstring( L, 2, &keyLength );
	xxtea_long ret_length;
	 char * result = xxtea_encrypt(( char *)text, (xxtea_long)textLength, ( char *)key, (xxtea_long)keyLength, &ret_length);
	lua_pushlstring(L, result, ret_length);
	skynet_free(result);

	return 1;
}

/* table of operations */
static const struct luaL_Reg xxtea [] = {
	{"encrypt", encrypt},
	{"decrypt", decrypt},
	{NULL, NULL}
};

/* register library */
LUALIB_API int luaopen_xxtea( lua_State *L ){
	luaL_checkversion(L);
  	luaL_newlib(L, xxtea);
  	return 1;
}
