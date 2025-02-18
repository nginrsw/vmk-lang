/*
** $Id: lutf8lib.c $
** Standard library for UTF-8 manipulation
** See Copyright Notice in vmk.h
*/

#define lutf8lib_c
#define VMK_LIB

#include "lprefix.h"


#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


#define MAXUNICODE	0x10FFFFu

#define MAXUTF		0x7FFFFFFFu


#define MSGInvalid	"invalid UTF-8 code"


#define iscont(c)	(((c) & 0xC0) == 0x80)
#define iscontp(p)	iscont(*(p))


/* from strlib */
/* translate a relative string position: negative means back from end */
static vmk_Integer u_posrelat (vmk_Integer pos, size_t len) {
  if (pos >= 0) return pos;
  else if (0u - (size_t)pos > len) return 0;
  else return (vmk_Integer)len + pos + 1;
}


/*
** Decode one UTF-8 sequence, returning NULL if byte sequence is
** invalid.  The array 'limits' stores the minimum value for each
** sequence length, to check for overlong representations. Its first
** entry forces an error for non-ascii bytes with no continuation
** bytes (count == 0).
*/
static const char *utf8_decode (const char *s, l_uint32 *val, int strict) {
  static const l_uint32 limits[] =
        {~(l_uint32)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
  unsigned int c = (unsigned char)s[0];
  l_uint32 res = 0;  /* final result */
  if (c < 0x80)  /* ascii? */
    res = c;
  else {
    int count = 0;  /* to count number of continuation bytes */
    for (; c & 0x40; c <<= 1) {  /* while it needs continuation bytes... */
      unsigned int cc = (unsigned char)s[++count];  /* read next byte */
      if (!iscont(cc))  /* not a continuation byte? */
        return NULL;  /* invalid byte sequence */
      res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
    }
    res |= ((l_uint32)(c & 0x7F) << (count * 5));  /* add first byte */
    if (count > 5 || res > MAXUTF || res < limits[count])
      return NULL;  /* invalid byte sequence */
    s += count;  /* skip continuation bytes read */
  }
  if (strict) {
    /* check for invalid code points; too large or surrogates */
    if (res > MAXUNICODE || (0xD800u <= res && res <= 0xDFFFu))
      return NULL;
  }
  if (val) *val = res;
  return s + 1;  /* +1 to include first byte */
}


/*
** utf8len(s [, i [, j [, lax]]]) --> number of characters that
** start in the range [i,j], or nil + current position if 's' is not
** well formed in that interval
*/
static int utflen (vmk_State *L) {
  vmk_Integer n = 0;  /* counter for the number of characters */
  size_t len;  /* string length in bytes */
  const char *s = vmkL_checklstring(L, 1, &len);
  vmk_Integer posi = u_posrelat(vmkL_optinteger(L, 2, 1), len);
  vmk_Integer posj = u_posrelat(vmkL_optinteger(L, 3, -1), len);
  int lax = vmk_toboolean(L, 4);
  vmkL_argcheck(L, 1 <= posi && --posi <= (vmk_Integer)len, 2,
                   "initial position out of bounds");
  vmkL_argcheck(L, --posj < (vmk_Integer)len, 3,
                   "final position out of bounds");
  while (posi <= posj) {
    const char *s1 = utf8_decode(s + posi, NULL, !lax);
    if (s1 == NULL) {  /* conversion error? */
      vmkL_pushfail(L);  /* return fail ... */
      vmk_pushinteger(L, posi + 1);  /* ... and current position */
      return 2;
    }
    posi = ct_diff2S(s1 - s);
    n++;
  }
  vmk_pushinteger(L, n);
  return 1;
}


/*
** codepoint(s, [i, [j [, lax]]]) -> returns codepoints for all
** characters that start in the range [i,j]
*/
static int codepoint (vmk_State *L) {
  size_t len;
  const char *s = vmkL_checklstring(L, 1, &len);
  vmk_Integer posi = u_posrelat(vmkL_optinteger(L, 2, 1), len);
  vmk_Integer pose = u_posrelat(vmkL_optinteger(L, 3, posi), len);
  int lax = vmk_toboolean(L, 4);
  int n;
  const char *se;
  vmkL_argcheck(L, posi >= 1, 2, "out of bounds");
  vmkL_argcheck(L, pose <= (vmk_Integer)len, 3, "out of bounds");
  if (posi > pose) return 0;  /* empty interval; return no values */
  if (pose - posi >= INT_MAX)  /* (vmk_Integer -> int) overflow? */
    return vmkL_error(L, "string slice too long");
  n = (int)(pose -  posi) + 1;  /* upper bound for number of returns */
  vmkL_checkstack(L, n, "string slice too long");
  n = 0;  /* count the number of returns */
  se = s + pose;  /* string end */
  for (s += posi - 1; s < se;) {
    l_uint32 code;
    s = utf8_decode(s, &code, !lax);
    if (s == NULL)
      return vmkL_error(L, MSGInvalid);
    vmk_pushinteger(L, l_castU2S(code));
    n++;
  }
  return n;
}


static void pushutfchar (vmk_State *L, int arg) {
  vmk_Unsigned code = (vmk_Unsigned)vmkL_checkinteger(L, arg);
  vmkL_argcheck(L, code <= MAXUTF, arg, "value out of range");
  vmk_pushfstring(L, "%U", (long)code);
}


/*
** utfchar(n1, n2, ...)  -> char(n1)..char(n2)...
*/
static int utfchar (vmk_State *L) {
  int n = vmk_gettop(L);  /* number of arguments */
  if (n == 1)  /* optimize common case of single char */
    pushutfchar(L, 1);
  else {
    int i;
    vmkL_Buffer b;
    vmkL_buffinit(L, &b);
    for (i = 1; i <= n; i++) {
      pushutfchar(L, i);
      vmkL_addvalue(&b);
    }
    vmkL_pushresult(&b);
  }
  return 1;
}


/*
** offset(s, n, [i])  -> indices where n-th character counting from
**   position 'i' starts and ends; 0 means character at 'i'.
*/
static int byteoffset (vmk_State *L) {
  size_t len;
  const char *s = vmkL_checklstring(L, 1, &len);
  vmk_Integer n  = vmkL_checkinteger(L, 2);
  vmk_Integer posi = (n >= 0) ? 1 : cast_st2S(len) + 1;
  posi = u_posrelat(vmkL_optinteger(L, 3, posi), len);
  vmkL_argcheck(L, 1 <= posi && --posi <= (vmk_Integer)len, 3,
                   "position out of bounds");
  if (n == 0) {
    /* find beginning of current byte sequence */
    while (posi > 0 && iscontp(s + posi)) posi--;
  }
  else {
    if (iscontp(s + posi))
      return vmkL_error(L, "initial position is a continuation byte");
    if (n < 0) {
      while (n < 0 && posi > 0) {  /* move back */
        do {  /* find beginning of previous character */
          posi--;
        } while (posi > 0 && iscontp(s + posi));
        n++;
      }
    }
    else {
      n--;  /* do not move for 1st character */
      while (n > 0 && posi < (vmk_Integer)len) {
        do {  /* find beginning of next character */
          posi++;
        } while (iscontp(s + posi));  /* (cannot pass final '\0') */
        n--;
      }
    }
  }
  if (n != 0) {  /* did not find given character? */
    vmkL_pushfail(L);
    return 1;
  }
  vmk_pushinteger(L, posi + 1);  /* initial position */
  if ((s[posi] & 0x80) != 0) {  /* multi-byte character? */
    do {
      posi++;
    } while (iscontp(s + posi + 1));  /* skip to final byte */
  }
  /* else one-byte character: final position is the initial one */
  vmk_pushinteger(L, posi + 1);  /* 'posi' now is the final position */
  return 2;
}


static int iter_aux (vmk_State *L, int strict) {
  size_t len;
  const char *s = vmkL_checklstring(L, 1, &len);
  vmk_Unsigned n = (vmk_Unsigned)vmk_tointeger(L, 2);
  if (n < len) {
    while (iscontp(s + n)) n++;  /* go to next character */
  }
  if (n >= len)  /* (also handles original 'n' being negative) */
    return 0;  /* no more codepoints */
  else {
    l_uint32 code;
    const char *next = utf8_decode(s + n, &code, strict);
    if (next == NULL || iscontp(next))
      return vmkL_error(L, MSGInvalid);
    vmk_pushinteger(L, l_castU2S(n + 1));
    vmk_pushinteger(L, l_castU2S(code));
    return 2;
  }
}


static int iter_auxstrict (vmk_State *L) {
  return iter_aux(L, 1);
}

static int iter_auxlax (vmk_State *L) {
  return iter_aux(L, 0);
}


static int iter_codes (vmk_State *L) {
  int lax = vmk_toboolean(L, 2);
  const char *s = vmkL_checkstring(L, 1);
  vmkL_argcheck(L, !iscontp(s), 1, MSGInvalid);
  vmk_pushcfunction(L, lax ? iter_auxlax : iter_auxstrict);
  vmk_pushvalue(L, 1);
  vmk_pushinteger(L, 0);
  return 3;
}


/* pattern to match a single UTF-8 character */
#define UTF8PATT	"[\0-\x7F\xC2-\xFD][\x80-\xBF]*"


static const vmkL_Reg funcs[] = {
  {"offset", byteoffset},
  {"codepoint", codepoint},
  {"char", utfchar},
  {"len", utflen},
  {"codes", iter_codes},
  /* placeholders */
  {"charpattern", NULL},
  {NULL, NULL}
};


VMKMOD_API int vmkopen_utf8 (vmk_State *L) {
  vmkL_newlib(L, funcs);
  vmk_pushlstring(L, UTF8PATT, sizeof(UTF8PATT)/sizeof(char) - 1);
  vmk_setfield(L, -2, "charpattern");
  return 1;
}

