/*
** $Id: lbaselib.c $
** Basic library
** See Copyright Notice in vmk.h
*/

#define lbaselib_c
#define VMK_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


static int vmkB_print (vmk_State *L) {
  int n = vmk_gettop(L);  /* number of arguments */
  int i;
  for (i = 1; i <= n; i++) {  /* for each argument */
    size_t l;
    const char *s = vmkL_tolstring(L, i, &l);  /* convert it to string */
    if (i > 1)  /* not the first element? */
      vmk_writestring("\t", 1);  /* add a tab before it */
    vmk_writestring(s, l);  /* print it */
    vmk_pop(L, 1);  /* pop result */
  }
  vmk_writeline();
  return 0;
}


/*
** Creates a warning with all given arguments.
** Check first for errors; otherwise an error may interrupt
** the composition of a warning, leaving it unfinished.
*/
static int vmkB_warn (vmk_State *L) {
  int n = vmk_gettop(L);  /* number of arguments */
  int i;
  vmkL_checkstring(L, 1);  /* at least one argument */
  for (i = 2; i <= n; i++)
    vmkL_checkstring(L, i);  /* make sure all arguments are strings */
  for (i = 1; i < n; i++)  /* compose warning */
    vmk_warning(L, vmk_tostring(L, i), 1);
  vmk_warning(L, vmk_tostring(L, n), 0);  /* close warning */
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, unsigned base, vmk_Integer *pn) {
  vmk_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle sign */
  else if (*s == '+') s++;
  if (!isalnum(cast_uchar(*s)))  /* no digit? */
    return NULL;
  do {
    unsigned digit = cast_uint(isdigit(cast_uchar(*s))
                               ? *s - '0'
                               : (toupper(cast_uchar(*s)) - 'A') + 10);
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum(cast_uchar(*s)));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (vmk_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int vmkB_tonumber (vmk_State *L) {
  if (vmk_isnoneornil(L, 2)) {  /* standard conversion? */
    if (vmk_type(L, 1) == VMK_TNUMBER) {  /* already a number? */
      vmk_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = vmk_tolstring(L, 1, &l);
      if (s != NULL && vmk_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
      vmkL_checkany(L, 1);  /* (but there must be some parameter) */
    }
  }
  else {
    size_t l;
    const char *s;
    vmk_Integer n = 0;  /* to avoid warnings */
    vmk_Integer base = vmkL_checkinteger(L, 2);
    vmkL_checktype(L, 1, VMK_TSTRING);  /* no numbers as strings */
    s = vmk_tolstring(L, 1, &l);
    vmkL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, cast_uint(base), &n) == s + l) {
      vmk_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  vmkL_pushfail(L);  /* not a number */
  return 1;
}


static int vmkB_error (vmk_State *L) {
  int level = (int)vmkL_optinteger(L, 2, 1);
  vmk_settop(L, 1);
  if (vmk_type(L, 1) == VMK_TSTRING && level > 0) {
    vmkL_where(L, level);   /* add extra information */
    vmk_pushvalue(L, 1);
    vmk_concat(L, 2);
  }
  return vmk_error(L);
}


static int vmkB_getmetatable (vmk_State *L) {
  vmkL_checkany(L, 1);
  if (!vmk_getmetatable(L, 1)) {
    vmk_pushnil(L);
    return 1;  /* no metatable */
  }
  vmkL_getmetafield(L, 1, "__metatable");
  return 1;  /* returns either __metatable field (if present) or metatable */
}


static int vmkB_setmetatable (vmk_State *L) {
  int t = vmk_type(L, 2);
  vmkL_checktype(L, 1, VMK_TTABLE);
  vmkL_argexpected(L, t == VMK_TNIL || t == VMK_TTABLE, 2, "nil or table");
  if (l_unlikely(vmkL_getmetafield(L, 1, "__metatable") != VMK_TNIL))
    return vmkL_error(L, "cannot change a protected metatable");
  vmk_settop(L, 2);
  vmk_setmetatable(L, 1);
  return 1;
}


static int vmkB_rawequal (vmk_State *L) {
  vmkL_checkany(L, 1);
  vmkL_checkany(L, 2);
  vmk_pushboolean(L, vmk_rawequal(L, 1, 2));
  return 1;
}


static int vmkB_rawlen (vmk_State *L) {
  int t = vmk_type(L, 1);
  vmkL_argexpected(L, t == VMK_TTABLE || t == VMK_TSTRING, 1,
                      "table or string");
  vmk_pushinteger(L, l_castU2S(vmk_rawlen(L, 1)));
  return 1;
}


static int vmkB_rawget (vmk_State *L) {
  vmkL_checktype(L, 1, VMK_TTABLE);
  vmkL_checkany(L, 2);
  vmk_settop(L, 2);
  vmk_rawget(L, 1);
  return 1;
}

static int vmkB_rawset (vmk_State *L) {
  vmkL_checktype(L, 1, VMK_TTABLE);
  vmkL_checkany(L, 2);
  vmkL_checkany(L, 3);
  vmk_settop(L, 3);
  vmk_rawset(L, 1);
  return 1;
}


static int pushmode (vmk_State *L, int oldmode) {
  if (oldmode == -1)
    vmkL_pushfail(L);  /* invalid call to 'vmk_gc' */
  else
    vmk_pushstring(L, (oldmode == VMK_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}


/*
** check whether call to 'vmk_gc' was valid (not inside a finalizer)
*/
#define checkvalres(res) { if (res == -1) break; }

static int vmkB_collectgarbage (vmk_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "isrunning", "generational", "incremental",
    "param", NULL};
  static const char optsnum[] = {VMK_GCSTOP, VMK_GCRESTART, VMK_GCCOLLECT,
    VMK_GCCOUNT, VMK_GCSTEP, VMK_GCISRUNNING, VMK_GCGEN, VMK_GCINC,
    VMK_GCPARAM};
  int o = optsnum[vmkL_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case VMK_GCCOUNT: {
      int k = vmk_gc(L, o);
      int b = vmk_gc(L, VMK_GCCOUNTB);
      checkvalres(k);
      vmk_pushnumber(L, (vmk_Number)k + ((vmk_Number)b/1024));
      return 1;
    }
    case VMK_GCSTEP: {
      vmk_Integer n = vmkL_optinteger(L, 2, 0);
      int res = vmk_gc(L, o, cast_sizet(n));
      checkvalres(res);
      vmk_pushboolean(L, res);
      return 1;
    }
    case VMK_GCISRUNNING: {
      int res = vmk_gc(L, o);
      checkvalres(res);
      vmk_pushboolean(L, res);
      return 1;
    }
    case VMK_GCGEN: {
      return pushmode(L, vmk_gc(L, o));
    }
    case VMK_GCINC: {
      return pushmode(L, vmk_gc(L, o));
    }
    case VMK_GCPARAM: {
      static const char *const params[] = {
        "minormul", "majorminor", "minormajor",
        "pause", "stepmul", "stepsize", NULL};
      static const char pnum[] = {
        VMK_GCPMINORMUL, VMK_GCPMAJORMINOR, VMK_GCPMINORMAJOR,
        VMK_GCPPAUSE, VMK_GCPSTEPMUL, VMK_GCPSTEPSIZE};
      int p = pnum[vmkL_checkoption(L, 2, NULL, params)];
      vmk_Integer value = vmkL_optinteger(L, 3, -1);
      vmk_pushinteger(L, vmk_gc(L, o, p, (int)value));
      return 1;
    }
    default: {
      int res = vmk_gc(L, o);
      checkvalres(res);
      vmk_pushinteger(L, res);
      return 1;
    }
  }
  vmkL_pushfail(L);  /* invalid call (inside a finalizer) */
  return 1;
}


static int vmkB_type (vmk_State *L) {
  int t = vmk_type(L, 1);
  vmkL_argcheck(L, t != VMK_TNONE, 1, "value expected");
  vmk_pushstring(L, vmk_typename(L, t));
  return 1;
}


static int vmkB_next (vmk_State *L) {
  vmkL_checktype(L, 1, VMK_TTABLE);
  vmk_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (vmk_next(L, 1))
    return 2;
  else {
    vmk_pushnil(L);
    return 1;
  }
}


static int pairscont (vmk_State *L, int status, vmk_KContext k) {
  (void)L; (void)status; (void)k;  /* unused */
  return 3;
}

static int vmkB_pairs (vmk_State *L) {
  vmkL_checkany(L, 1);
  if (vmkL_getmetafield(L, 1, "__pairs") == VMK_TNIL) {  /* no metamethod? */
    vmk_pushcfunction(L, vmkB_next);  /* will return generator, */
    vmk_pushvalue(L, 1);  /* state, */
    vmk_pushnil(L);  /* and initial value */
  }
  else {
    vmk_pushvalue(L, 1);  /* argument 'self' to metamethod */
    vmk_callk(L, 1, 3, 0, pairscont);  /* get 3 values from metamethod */
  }
  return 3;
}


/*
** Traversal fn for 'ipairs'
*/
static int ipairsaux (vmk_State *L) {
  vmk_Integer i = vmkL_checkinteger(L, 2);
  i = vmkL_intop(+, i, 1);
  vmk_pushinteger(L, i);
  return (vmk_geti(L, 1, i) == VMK_TNIL) ? 1 : 2;
}


/*
** 'ipairs' fn. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int vmkB_ipairs (vmk_State *L) {
  vmkL_checkany(L, 1);
  vmk_pushcfunction(L, ipairsaux);  /* iteration fn */
  vmk_pushvalue(L, 1);  /* state */
  vmk_pushinteger(L, 0);  /* initial value */
  return 3;
}


static int load_aux (vmk_State *L, int status, int envidx) {
  if (l_likely(status == VMK_OK)) {
    if (envidx != 0) {  /* 'env' parameter? */
      vmk_pushvalue(L, envidx);  /* environment for loaded fn */
      if (!vmk_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        vmk_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    vmkL_pushfail(L);
    vmk_insert(L, -2);  /* put before error message */
    return 2;  /* return fail plus error message */
  }
}


static const char *getMode (vmk_State *L, int idx) {
  const char *mode = vmkL_optstring(L, idx, "bt");
  if (strchr(mode, 'B') != NULL)  /* Vmk code cannot use fixed buffers */
    vmkL_argerror(L, idx, "invalid mode");
  return mode;
}


static int vmkB_loadfile (vmk_State *L) {
  const char *fname = vmkL_optstring(L, 1, NULL);
  const char *mode = getMode(L, 2);
  int env = (!vmk_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = vmkL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read fn
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' fn: 'vmk_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (vmk_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  vmkL_checkstack(L, 2, "too many nested functions");
  vmk_pushvalue(L, 1);  /* get fn */
  vmk_call(L, 0, 1);  /* call it */
  if (vmk_isnil(L, -1)) {
    vmk_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (l_unlikely(!vmk_isstring(L, -1)))
    vmkL_error(L, "reader fn must return a string");
  vmk_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return vmk_tolstring(L, RESERVEDSLOT, size);
}


static int vmkB_load (vmk_State *L) {
  int status;
  size_t l;
  const char *s = vmk_tolstring(L, 1, &l);
  const char *mode = getMode(L, 3);
  int env = (!vmk_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = vmkL_optstring(L, 2, s);
    status = vmkL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader fn */
    const char *chunkname = vmkL_optstring(L, 2, "=(load)");
    vmkL_checktype(L, 1, VMK_TFUNCTION);
    vmk_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = vmk_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (vmk_State *L, int d1, vmk_KContext d2) {
  (void)d1;  (void)d2;  /* only to match 'vmk_Kfunction' prototype */
  return vmk_gettop(L) - 1;
}


static int vmkB_dofile (vmk_State *L) {
  const char *fname = vmkL_optstring(L, 1, NULL);
  vmk_settop(L, 1);
  if (l_unlikely(vmkL_loadfile(L, fname) != VMK_OK))
    return vmk_error(L);
  vmk_callk(L, 0, VMK_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}


static int vmkB_assert (vmk_State *L) {
  if (l_likely(vmk_toboolean(L, 1)))  /* condition is true? */
    return vmk_gettop(L);  /* return all arguments */
  else {  /* error */
    vmkL_checkany(L, 1);  /* there must be a condition */
    vmk_remove(L, 1);  /* remove it */
    vmk_pushliteral(L, "assertion failed!");  /* default message */
    vmk_settop(L, 1);  /* leave only message (default if no other one) */
    return vmkB_error(L);  /* call 'error' */
  }
}


static int vmkB_select (vmk_State *L) {
  int n = vmk_gettop(L);
  if (vmk_type(L, 1) == VMK_TSTRING && *vmk_tostring(L, 1) == '#') {
    vmk_pushinteger(L, n-1);
    return 1;
  }
  else {
    vmk_Integer i = vmkL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    vmkL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation fn for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (vmk_State *L, int status, vmk_KContext extra) {
  if (l_unlikely(status != VMK_OK && status != VMK_YIELD)) {  /* error? */
    vmk_pushboolean(L, 0);  /* first result (false) */
    vmk_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return vmk_gettop(L) - (int)extra;  /* return all results */
}


static int vmkB_pcall (vmk_State *L) {
  int status;
  vmkL_checkany(L, 1);
  vmk_pushboolean(L, 1);  /* first result if no errors */
  vmk_insert(L, 1);  /* put it in place */
  status = vmk_pcallk(L, vmk_gettop(L) - 2, VMK_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'vmk_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the fn passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int vmkB_xpcall (vmk_State *L) {
  int status;
  int n = vmk_gettop(L);
  vmkL_checktype(L, 2, VMK_TFUNCTION);  /* check error fn */
  vmk_pushboolean(L, 1);  /* first result */
  vmk_pushvalue(L, 1);  /* fn */
  vmk_rotate(L, 3, 2);  /* move them below fn's arguments */
  status = vmk_pcallk(L, n - 2, VMK_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}


static int vmkB_tostring (vmk_State *L) {
  vmkL_checkany(L, 1);
  vmkL_tolstring(L, 1, NULL);
  return 1;
}


static const vmkL_Reg base_funcs[] = {
  {"assert", vmkB_assert},
  {"collectgarbage", vmkB_collectgarbage},
  {"dofile", vmkB_dofile},
  {"error", vmkB_error},
  {"getmetatable", vmkB_getmetatable},
  {"ipairs", vmkB_ipairs},
  {"loadfile", vmkB_loadfile},
  {"load", vmkB_load},
  {"next", vmkB_next},
  {"pairs", vmkB_pairs},
  {"pcall", vmkB_pcall},
  {"print", vmkB_print},
  {"warn", vmkB_warn},
  {"rawequal", vmkB_rawequal},
  {"rawlen", vmkB_rawlen},
  {"rawget", vmkB_rawget},
  {"rawset", vmkB_rawset},
  {"select", vmkB_select},
  {"setmetatable", vmkB_setmetatable},
  {"tonumber", vmkB_tonumber},
  {"tostring", vmkB_tostring},
  {"type", vmkB_type},
  {"xpcall", vmkB_xpcall},
  /* placeholders */
  {VMK_GNAME, NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};


VMKMOD_API int vmkopen_base (vmk_State *L) {
  /* open lib into global table */
  vmk_pushglobaltable(L);
  vmkL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  vmk_pushvalue(L, -1);
  vmk_setfield(L, -2, VMK_GNAME);
  /* set global _VERSION */
  vmk_pushliteral(L, VMK_VERSION);
  vmk_setfield(L, -2, "_VERSION");
  return 1;
}

