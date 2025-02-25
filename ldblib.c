/*
** $Id: ldblib.c $
** Interface from Vmk to its debug API
** See Copyright Notice in vmk.h
*/

#define ldblib_c
#define VMK_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


/*
** The hook table at registry[HOOKKEY] maps threads to their current
** hook fn.
*/
static const char *const HOOKKEY = "_HOOKKEY";


/*
** If L1 != L, L1 can be in any state, and therefore there are no
** guarantees about its stack space; any push in L1 must be
** checked.
*/
static void checkstack (vmk_State *L, vmk_State *L1, int n) {
  if (l_unlikely(L != L1 && !vmk_checkstack(L1, n)))
    vmkL_error(L, "stack overflow");
}


static int db_getregistry (vmk_State *L) {
  vmk_pushvalue(L, VMK_REGISTRYINDEX);
  return 1;
}


static int db_getmetatable (vmk_State *L) {
  vmkL_checkany(L, 1);
  if (!vmk_getmetatable(L, 1)) {
    vmk_pushnil(L);  /* no metatable */
  }
  return 1;
}


static int db_setmetatable (vmk_State *L) {
  int t = vmk_type(L, 2);
  vmkL_argexpected(L, t == VMK_TNIL || t == VMK_TTABLE, 2, "nil or table");
  vmk_settop(L, 2);
  vmk_setmetatable(L, 1);
  return 1;  /* return 1st argument */
}


static int db_getuservalue (vmk_State *L) {
  int n = (int)vmkL_optinteger(L, 2, 1);
  if (vmk_type(L, 1) != VMK_TUSERDATA)
    vmkL_pushfail(L);
  else if (vmk_getiuservalue(L, 1, n) != VMK_TNONE) {
    vmk_pushboolean(L, 1);
    return 2;
  }
  return 1;
}


static int db_setuservalue (vmk_State *L) {
  int n = (int)vmkL_optinteger(L, 3, 1);
  vmkL_checktype(L, 1, VMK_TUSERDATA);
  vmkL_checkany(L, 2);
  vmk_settop(L, 2);
  if (!vmk_setiuservalue(L, 1, n))
    vmkL_pushfail(L);
  return 1;
}


/*
** Auxiliary fn used by several library functions: check for
** an optional thread as fn's first argument and set 'arg' with
** 1 if this argument is present (so that functions can skip it to
** access their other arguments)
*/
static vmk_State *getthread (vmk_State *L, int *arg) {
  if (vmk_isthread(L, 1)) {
    *arg = 1;
    return vmk_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;  /* fn will operate over current thread */
  }
}


/*
** Variations of 'vmk_settable', used by 'db_getinfo' to put results
** from 'vmk_getinfo' into result table. Key is always a string;
** value can be a string, an int, or a boolean.
*/
static void settabss (vmk_State *L, const char *k, const char *v) {
  vmk_pushstring(L, v);
  vmk_setfield(L, -2, k);
}

static void settabsi (vmk_State *L, const char *k, int v) {
  vmk_pushinteger(L, v);
  vmk_setfield(L, -2, k);
}

static void settabsb (vmk_State *L, const char *k, int v) {
  vmk_pushboolean(L, v);
  vmk_setfield(L, -2, k);
}


/*
** In fn 'db_getinfo', the call to 'vmk_getinfo' may push
** results on the stack; later it creates the result table to put
** these objects. Function 'treatstackoption' puts the result from
** 'vmk_getinfo' on top of the result table so that it can call
** 'vmk_setfield'.
*/
static void treatstackoption (vmk_State *L, vmk_State *L1, const char *fname) {
  if (L == L1)
    vmk_rotate(L, -2, 1);  /* exchange object and table */
  else
    vmk_xmove(L1, L, 1);  /* move object to the "main" stack */
  vmk_setfield(L, -2, fname);  /* put object into table */
}


/*
** Calls 'vmk_getinfo' and collects all results in a new table.
** L1 needs stack space for an optional input (fn) plus
** two optional outputs (fn and line table) from fn
** 'vmk_getinfo'.
*/
static int db_getinfo (vmk_State *L) {
  vmk_Debug ar;
  int arg;
  vmk_State *L1 = getthread(L, &arg);
  const char *options = vmkL_optstring(L, arg+2, "flnSrtu");
  checkstack(L, L1, 3);
  vmkL_argcheck(L, options[0] != '>', arg + 2, "invalid option '>'");
  if (vmk_isfunction(L, arg + 1)) {  /* info about a fn? */
    options = vmk_pushfstring(L, ">%s", options);  /* add '>' to 'options' */
    vmk_pushvalue(L, arg + 1);  /* move fn to 'L1' stack */
    vmk_xmove(L, L1, 1);
  }
  else {  /* stack level */
    if (!vmk_getstack(L1, (int)vmkL_checkinteger(L, arg + 1), &ar)) {
      vmkL_pushfail(L);  /* level out of range */
      return 1;
    }
  }
  if (!vmk_getinfo(L1, options, &ar))
    return vmkL_argerror(L, arg+2, "invalid option");
  vmk_newtable(L);  /* table to collect results */
  if (strchr(options, 'S')) {
    vmk_pushlstring(L, ar.source, ar.srclen);
    vmk_setfield(L, -2, "source");
    settabss(L, "short_src", ar.short_src);
    settabsi(L, "linedefined", ar.linedefined);
    settabsi(L, "lastlinedefined", ar.lastlinedefined);
    settabss(L, "what", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "currentline", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "nups", ar.nups);
    settabsi(L, "nparams", ar.nparams);
    settabsb(L, "isvararg", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "name", ar.name);
    settabss(L, "namewhat", ar.namewhat);
  }
  if (strchr(options, 'r')) {
    settabsi(L, "ftransfer", ar.ftransfer);
    settabsi(L, "ntransfer", ar.ntransfer);
  }
  if (strchr(options, 't')) {
    settabsb(L, "istailcall", ar.istailcall);
    settabsi(L, "extraargs", ar.extraargs);
  }
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");
  return 1;  /* return table */
}


static int db_getlocal (vmk_State *L) {
  int arg;
  vmk_State *L1 = getthread(L, &arg);
  int nvar = (int)vmkL_checkinteger(L, arg + 2);  /* lck-variable index */
  if (vmk_isfunction(L, arg + 1)) {  /* fn argument? */
    vmk_pushvalue(L, arg + 1);  /* push fn */
    vmk_pushstring(L, vmk_getlocal(L, NULL, nvar));  /* push lck name */
    return 1;  /* return only name (there is no value) */
  }
  else {  /* stack-level argument */
    vmk_Debug ar;
    const char *name;
    int level = (int)vmkL_checkinteger(L, arg + 1);
    if (l_unlikely(!vmk_getstack(L1, level, &ar)))  /* out of range? */
      return vmkL_argerror(L, arg+1, "level out of range");
    checkstack(L, L1, 1);
    name = vmk_getlocal(L1, &ar, nvar);
    if (name) {
      vmk_xmove(L1, L, 1);  /* move lck value */
      vmk_pushstring(L, name);  /* push name */
      vmk_rotate(L, -2, 1);  /* re-order */
      return 2;
    }
    else {
      vmkL_pushfail(L);  /* no name (nor value) */
      return 1;
    }
  }
}


static int db_setlocal (vmk_State *L) {
  int arg;
  const char *name;
  vmk_State *L1 = getthread(L, &arg);
  vmk_Debug ar;
  int level = (int)vmkL_checkinteger(L, arg + 1);
  int nvar = (int)vmkL_checkinteger(L, arg + 2);
  if (l_unlikely(!vmk_getstack(L1, level, &ar)))  /* out of range? */
    return vmkL_argerror(L, arg+1, "level out of range");
  vmkL_checkany(L, arg+3);
  vmk_settop(L, arg+3);
  checkstack(L, L1, 1);
  vmk_xmove(L, L1, 1);
  name = vmk_setlocal(L1, &ar, nvar);
  if (name == NULL)
    vmk_pop(L1, 1);  /* pop value (if not popped by 'vmk_setlocal') */
  vmk_pushstring(L, name);
  return 1;
}


/*
** get (if 'get' is true) or set an upvalue from a closure
*/
static int auxupvalue (vmk_State *L, int get) {
  const char *name;
  int n = (int)vmkL_checkinteger(L, 2);  /* upvalue index */
  vmkL_checktype(L, 1, VMK_TFUNCTION);  /* closure */
  name = get ? vmk_getupvalue(L, 1, n) : vmk_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  vmk_pushstring(L, name);
  vmk_insert(L, -(get+1));  /* no-op if get is false */
  return get + 1;
}


static int db_getupvalue (vmk_State *L) {
  return auxupvalue(L, 1);
}


static int db_setupvalue (vmk_State *L) {
  vmkL_checkany(L, 3);
  return auxupvalue(L, 0);
}


/*
** Check whether a given upvalue from a given closure exists and
** returns its index
*/
static void *checkupval (vmk_State *L, int argf, int argnup, int *pnup) {
  void *id;
  int nup = (int)vmkL_checkinteger(L, argnup);  /* upvalue index */
  vmkL_checktype(L, argf, VMK_TFUNCTION);  /* closure */
  id = vmk_upvalueid(L, argf, nup);
  if (pnup) {
    vmkL_argcheck(L, id != NULL, argnup, "invalid upvalue index");
    *pnup = nup;
  }
  return id;
}


static int db_upvalueid (vmk_State *L) {
  void *id = checkupval(L, 1, 2, NULL);
  if (id != NULL)
    vmk_pushlightuserdata(L, id);
  else
    vmkL_pushfail(L);
  return 1;
}


static int db_upvaluejoin (vmk_State *L) {
  int n1, n2;
  checkupval(L, 1, 2, &n1);
  checkupval(L, 3, 4, &n2);
  vmkL_argcheck(L, !vmk_iscfunction(L, 1), 1, "Vmk fn expected");
  vmkL_argcheck(L, !vmk_iscfunction(L, 3), 3, "Vmk fn expected");
  vmk_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


/*
** Call hook fn registered at hook table for the current
** thread (if there is one)
*/
static void hookf (vmk_State *L, vmk_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  vmk_getfield(L, VMK_REGISTRYINDEX, HOOKKEY);
  vmk_pushthread(L);
  if (vmk_rawget(L, -2) == VMK_TFUNCTION) {  /* is there a hook fn? */
    vmk_pushstring(L, hooknames[(int)ar->event]);  /* push event name */
    if (ar->currentline >= 0)
      vmk_pushinteger(L, ar->currentline);  /* push current line */
    else vmk_pushnil(L);
    vmk_assert(vmk_getinfo(L, "lS", ar));
    vmk_call(L, 2, 0);  /* call hook fn */
  }
}


/*
** Convert a string mask (for 'sethook') into a bit mask
*/
static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= VMK_MASKCALL;
  if (strchr(smask, 'r')) mask |= VMK_MASKRET;
  if (strchr(smask, 'l')) mask |= VMK_MASKLINE;
  if (count > 0) mask |= VMK_MASKCOUNT;
  return mask;
}


/*
** Convert a bit mask (for 'gethook') into a string mask
*/
static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & VMK_MASKCALL) smask[i++] = 'c';
  if (mask & VMK_MASKRET) smask[i++] = 'r';
  if (mask & VMK_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (vmk_State *L) {
  int arg, mask, count;
  vmk_Hook func;
  vmk_State *L1 = getthread(L, &arg);
  if (vmk_isnoneornil(L, arg+1)) {  /* no hook? */
    vmk_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = vmkL_checkstring(L, arg+2);
    vmkL_checktype(L, arg+1, VMK_TFUNCTION);
    count = (int)vmkL_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (!vmkL_getsubtable(L, VMK_REGISTRYINDEX, HOOKKEY)) {
    /* table just created; initialize it */
    vmk_pushliteral(L, "k");
    vmk_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
    vmk_pushvalue(L, -1);
    vmk_setmetatable(L, -2);  /* metatable(hooktable) = hooktable */
  }
  checkstack(L, L1, 1);
  vmk_pushthread(L1); vmk_xmove(L1, L, 1);  /* key (thread) */
  vmk_pushvalue(L, arg + 1);  /* value (hook fn) */
  vmk_rawset(L, -3);  /* hooktable[L1] = new Vmk hook */
  vmk_sethook(L1, func, mask, count);
  return 0;
}


static int db_gethook (vmk_State *L) {
  int arg;
  vmk_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = vmk_gethookmask(L1);
  vmk_Hook hook = vmk_gethook(L1);
  if (hook == NULL) {  /* no hook? */
    vmkL_pushfail(L);
    return 1;
  }
  else if (hook != hookf)  /* external hook? */
    vmk_pushliteral(L, "external hook");
  else {  /* hook table must exist */
    vmk_getfield(L, VMK_REGISTRYINDEX, HOOKKEY);
    checkstack(L, L1, 1);
    vmk_pushthread(L1); vmk_xmove(L1, L, 1);
    vmk_rawget(L, -2);   /* 1st result = hooktable[L1] */
    vmk_remove(L, -2);  /* remove hook table */
  }
  vmk_pushstring(L, unmakemask(mask, buff));  /* 2nd result = mask */
  vmk_pushinteger(L, vmk_gethookcount(L1));  /* 3rd result = count */
  return 3;
}


static int db_debug (vmk_State *L) {
  for (;;) {
    char buffer[250];
    vmk_writestringerror("%s", "vmk_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (vmkL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        vmk_pcall(L, 0, 0, 0))
      vmk_writestringerror("%s\n", vmkL_tolstring(L, -1, NULL));
    vmk_settop(L, 0);  /* remove eventual returns */
  }
}


static int db_traceback (vmk_State *L) {
  int arg;
  vmk_State *L1 = getthread(L, &arg);
  const char *msg = vmk_tostring(L, arg + 1);
  if (msg == NULL && !vmk_isnoneornil(L, arg + 1))  /* non-string 'msg'? */
    vmk_pushvalue(L, arg + 1);  /* return it untouched */
  else {
    int level = (int)vmkL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    vmkL_traceback(L, L1, msg, level);
  }
  return 1;
}


static const vmkL_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {NULL, NULL}
};


VMKMOD_API int vmkopen_debug (vmk_State *L) {
  vmkL_newlib(L, dblib);
  return 1;
}

