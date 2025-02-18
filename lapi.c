/*
** $Id: lapi.c $
** Vmk API
** See Copyright Notice in vmk.h
*/

#define lapi_c
#define VMK_CORE

#include "lprefix.h"


#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "vmk.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"



const char vmk_ident[] =
  "$VmkVersion: " VMK_COPYRIGHT " $"
  "$VmkAuthors: " VMK_AUTHORS " $";



/*
** Test for a valid index (one that is not the 'nilvalue').
*/
#define isvalid(L, o)	((o) != &G(L)->nilvalue)


/* test for pseudo index */
#define ispseudo(i)		((i) <= VMK_REGISTRYINDEX)

/* test for upvalue */
#define isupvalue(i)		((i) < VMK_REGISTRYINDEX)


/*
** Convert an acceptable index to a pointer to its respective value.
** Non-valid indices return the special nil value 'G(L)->nilvalue'.
*/
static TValue *index2value (vmk_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, idx <= ci->top.p - (ci->func.p + 1), "unacceptable index");
    if (o >= L->top.p) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    return s2v(L->top.p + idx);
  }
  else if (idx == VMK_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = VMK_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(ci->func.p))) {  /* C closure? */
      CClosure *func = clCvalue(s2v(ci->func.p));
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1]
                                      : &G(L)->nilvalue;
    }
    else {  /* light C fn or Vmk fn (through a hook)?) */
      api_check(L, ttislcf(s2v(ci->func.p)), "caller not a C fn");
      return &G(L)->nilvalue;  /* no upvalues */
    }
  }
}



/*
** Convert a valid actual index (not a pseudo-index) to its address.
*/
static StkId index2stack (vmk_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, o < L->top.p, "invalid index");
    return o;
  }
  else {    /* non-positive index */
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return L->top.p + idx;
  }
}


VMK_API int vmk_checkstack (vmk_State *L, int n) {
  int res;
  CallInfo *ci;
  vmk_lock(L);
  ci = L->ci;
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last.p - L->top.p > n)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else  /* need to grow stack */
    res = vmkD_growstack(L, n, 0);
  if (res && ci->top.p < L->top.p + n)
    ci->top.p = L->top.p + n;  /* adjust frame top */
  vmk_unlock(L);
  return res;
}


VMK_API void vmk_xmove (vmk_State *from, vmk_State *to, int n) {
  int i;
  if (from == to) return;
  vmk_lock(to);
  api_checkpop(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top.p - to->top.p >= n, "stack overflow");
  from->top.p -= n;
  for (i = 0; i < n; i++) {
    setobjs2s(to, to->top.p, from->top.p + i);
    to->top.p++;  /* stack already checked by previous 'api_check' */
  }
  vmk_unlock(to);
}


VMK_API vmk_CFunction vmk_atpanic (vmk_State *L, vmk_CFunction panicf) {
  vmk_CFunction old;
  vmk_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  vmk_unlock(L);
  return old;
}


VMK_API vmk_Number vmk_version (vmk_State *L) {
  UNUSED(L);
  return VMK_VERSION_NUM;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
VMK_API int vmk_absindex (vmk_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top.p - L->ci->func.p) + idx;
}


VMK_API int vmk_gettop (vmk_State *L) {
  return cast_int(L->top.p - (L->ci->func.p + 1));
}


VMK_API void vmk_settop (vmk_State *L, int idx) {
  CallInfo *ci;
  StkId func, newtop;
  ptrdiff_t diff;  /* difference for new top */
  vmk_lock(L);
  ci = L->ci;
  func = ci->func.p;
  if (idx >= 0) {
    api_check(L, idx <= ci->top.p - (func + 1), "new top too large");
    diff = ((func + 1) + idx) - L->top.p;
    for (; diff > 0; diff--)
      setnilvalue(s2v(L->top.p++));  /* clear new slots */
  }
  else {
    api_check(L, -(idx+1) <= (L->top.p - (func + 1)), "invalid new top");
    diff = idx + 1;  /* will "subtract" index (as it is negative) */
  }
  newtop = L->top.p + diff;
  if (diff < 0 && L->tbclist.p >= newtop) {
    vmk_assert(ci->callstatus & CIST_TBC);
    newtop = vmkF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->top.p = newtop;  /* correct top only after closing any upvalue */
  vmk_unlock(L);
}


VMK_API void vmk_closeslot (vmk_State *L, int idx) {
  StkId level;
  vmk_lock(L);
  level = index2stack(L, idx);
  api_check(L, (L->ci->callstatus & CIST_TBC) && (L->tbclist.p == level),
     "no variable to close at given level");
  level = vmkF_close(L, level, CLOSEKTOP, 0);
  setnilvalue(s2v(level));
  vmk_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'vmk_rotate')
** Note that we move(copy) only the value inside the stack.
** (We do not move additional fields that may exist.)
*/
static void reverse (vmk_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, s2v(from));
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
VMK_API void vmk_rotate (vmk_State *L, int idx, int n) {
  StkId p, t, m;
  vmk_lock(L);
  t = L->top.p - 1;  /* end of stack segment being rotated */
  p = index2stack(L, idx);  /* start of segment */
  api_check(L, L->tbclist.p < p, "moving a to-be-closed slot");
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  vmk_unlock(L);
}


VMK_API void vmk_copy (vmk_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  vmk_lock(L);
  fr = index2value(L, fromidx);
  to = index2value(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* fn upvalue? */
    vmkC_barrier(L, clCvalue(s2v(L->ci->func.p)), fr);
  /* VMK_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  vmk_unlock(L);
}


VMK_API void vmk_pushvalue (vmk_State *L, int idx) {
  vmk_lock(L);
  setobj2s(L, L->top.p, index2value(L, idx));
  api_incr_top(L);
  vmk_unlock(L);
}



/*
** access functions (stack -> C)
*/


VMK_API int vmk_type (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (isvalid(L, o) ? ttype(o) : VMK_TNONE);
}


VMK_API const char *vmk_typename (vmk_State *L, int t) {
  UNUSED(L);
  api_check(L, VMK_TNONE <= t && t < VMK_NUMTYPES, "invalid type");
  return ttypename(t);
}


VMK_API int vmk_iscfunction (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


VMK_API int vmk_isinteger (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return ttisinteger(o);
}


VMK_API int vmk_isnumber (vmk_State *L, int idx) {
  vmk_Number n;
  const TValue *o = index2value(L, idx);
  return tonumber(o, &n);
}


VMK_API int vmk_isstring (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


VMK_API int vmk_isuserdata (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}


VMK_API int vmk_rawequal (vmk_State *L, int index1, int index2) {
  const TValue *o1 = index2value(L, index1);
  const TValue *o2 = index2value(L, index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? vmkV_rawequalobj(o1, o2) : 0;
}


VMK_API void vmk_arith (vmk_State *L, int op) {
  vmk_lock(L);
  if (op != VMK_OPUNM && op != VMK_OPBNOT)
    api_checkpop(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checkpop(L, 1);
    setobjs2s(L, L->top.p, L->top.p - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  vmkO_arith(L, op, s2v(L->top.p - 2), s2v(L->top.p - 1), L->top.p - 2);
  L->top.p--;  /* pop second operand */
  vmk_unlock(L);
}


VMK_API int vmk_compare (vmk_State *L, int index1, int index2, int op) {
  const TValue *o1;
  const TValue *o2;
  int i = 0;
  vmk_lock(L);  /* may call tag method */
  o1 = index2value(L, index1);
  o2 = index2value(L, index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case VMK_OPEQ: i = vmkV_equalobj(L, o1, o2); break;
      case VMK_OPLT: i = vmkV_lessthan(L, o1, o2); break;
      case VMK_OPLE: i = vmkV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  vmk_unlock(L);
  return i;
}


VMK_API unsigned (vmk_numbertocstring) (vmk_State *L, int idx, char *buff) {
  const TValue *o = index2value(L, idx);
  if (ttisnumber(o)) {
    unsigned len = vmkO_tostringbuff(o, buff);
    buff[len++] = '\0';  /* add final zero */
    return len;
  }
  else
    return 0;
}


VMK_API size_t vmk_stringtonumber (vmk_State *L, const char *s) {
  size_t sz = vmkO_str2num(s, s2v(L->top.p));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


VMK_API vmk_Number vmk_tonumberx (vmk_State *L, int idx, int *pisnum) {
  vmk_Number n = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}


VMK_API vmk_Integer vmk_tointegerx (vmk_State *L, int idx, int *pisnum) {
  vmk_Integer res = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}


VMK_API int vmk_toboolean (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return !l_isfalse(o);
}


VMK_API const char *vmk_tolstring (vmk_State *L, int idx, size_t *len) {
  TValue *o;
  vmk_lock(L);
  o = index2value(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      vmk_unlock(L);
      return NULL;
    }
    vmkO_tostring(L, o);
    vmkC_checkGC(L);
    o = index2value(L, idx);  /* previous call may reallocate the stack */
  }
  vmk_unlock(L);
  if (len != NULL)
    return getlstr(tsvalue(o), *len);
  else
    return getstr(tsvalue(o));
}


VMK_API vmk_Unsigned vmk_rawlen (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case VMK_VSHRSTR: return cast(vmk_Unsigned, tsvalue(o)->shrlen);
    case VMK_VLNGSTR: return cast(vmk_Unsigned, tsvalue(o)->u.lnglen);
    case VMK_VUSERDATA: return cast(vmk_Unsigned, uvalue(o)->len);
    case VMK_VTABLE: return vmkH_getn(hvalue(o));
    default: return 0;
  }
}


VMK_API vmk_CFunction vmk_tocfunction (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C fn */
}


l_sinline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case VMK_TUSERDATA: return getudatamem(uvalue(o));
    case VMK_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


VMK_API void *vmk_touserdata (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return touserdata(o);
}


VMK_API vmk_State *vmk_tothread (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


/*
** Returns a pointer to the internal representation of an object.
** Note that ANSI C does not allow the conversion of a pointer to
** fn to a 'void*', so the conversion here goes through
** a 'size_t'. (As the returned pointer is only informative, this
** conversion should not be a problem.)
*/
VMK_API const void *vmk_topointer (vmk_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case VMK_VLCF: return cast_voidp(cast_sizet(fvalue(o)));
    case VMK_VUSERDATA: case VMK_VLIGHTUSERDATA:
      return touserdata(o);
    default: {
      if (iscollectable(o))
        return gcvalue(o);
      else
        return NULL;
    }
  }
}



/*
** push functions (C -> stack)
*/


VMK_API void vmk_pushnil (vmk_State *L) {
  vmk_lock(L);
  setnilvalue(s2v(L->top.p));
  api_incr_top(L);
  vmk_unlock(L);
}


VMK_API void vmk_pushnumber (vmk_State *L, vmk_Number n) {
  vmk_lock(L);
  setfltvalue(s2v(L->top.p), n);
  api_incr_top(L);
  vmk_unlock(L);
}


VMK_API void vmk_pushinteger (vmk_State *L, vmk_Integer n) {
  vmk_lock(L);
  setivalue(s2v(L->top.p), n);
  api_incr_top(L);
  vmk_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
VMK_API const char *vmk_pushlstring (vmk_State *L, const char *s, size_t len) {
  TString *ts;
  vmk_lock(L);
  ts = (len == 0) ? vmkS_new(L, "") : vmkS_newlstr(L, s, len);
  setsvalue2s(L, L->top.p, ts);
  api_incr_top(L);
  vmkC_checkGC(L);
  vmk_unlock(L);
  return getstr(ts);
}


VMK_API const char *vmk_pushexternalstring (vmk_State *L,
	        const char *s, size_t len, vmk_Alloc falloc, void *ud) {
  TString *ts;
  vmk_lock(L);
  api_check(L, len <= MAX_SIZE, "string too large");
  api_check(L, s[len] == '\0', "string not ending with zero");
  ts = vmkS_newextlstr (L, s, len, falloc, ud);
  setsvalue2s(L, L->top.p, ts);
  api_incr_top(L);
  vmkC_checkGC(L);
  vmk_unlock(L);
  return getstr(ts);
}


VMK_API const char *vmk_pushstring (vmk_State *L, const char *s) {
  vmk_lock(L);
  if (s == NULL)
    setnilvalue(s2v(L->top.p));
  else {
    TString *ts;
    ts = vmkS_new(L, s);
    setsvalue2s(L, L->top.p, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  vmkC_checkGC(L);
  vmk_unlock(L);
  return s;
}


VMK_API const char *vmk_pushvfstring (vmk_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  vmk_lock(L);
  ret = vmkO_pushvfstring(L, fmt, argp);
  vmkC_checkGC(L);
  vmk_unlock(L);
  return ret;
}


VMK_API const char *vmk_pushfstring (vmk_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  vmk_lock(L);
  va_start(argp, fmt);
  ret = vmkO_pushvfstring(L, fmt, argp);
  va_end(argp);
  vmkC_checkGC(L);
  if (ret == NULL)  /* error? */
    vmkD_throw(L, VMK_ERRMEM);
  vmk_unlock(L);
  return ret;
}


VMK_API void vmk_pushcclosure (vmk_State *L, vmk_CFunction fn, int n) {
  vmk_lock(L);
  if (n == 0) {
    setfvalue(s2v(L->top.p), fn);
    api_incr_top(L);
  }
  else {
    int i;
    CClosure *cl;
    api_checkpop(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    cl = vmkF_newCclosure(L, n);
    cl->f = fn;
    for (i = 0; i < n; i++) {
      setobj2n(L, &cl->upvalue[i], s2v(L->top.p - n + i));
      /* does not need barrier because closure is white */
      vmk_assert(iswhite(cl));
    }
    L->top.p -= n;
    setclCvalue(L, s2v(L->top.p), cl);
    api_incr_top(L);
    vmkC_checkGC(L);
  }
  vmk_unlock(L);
}


VMK_API void vmk_pushboolean (vmk_State *L, int b) {
  vmk_lock(L);
  if (b)
    setbtvalue(s2v(L->top.p));
  else
    setbfvalue(s2v(L->top.p));
  api_incr_top(L);
  vmk_unlock(L);
}


VMK_API void vmk_pushlightuserdata (vmk_State *L, void *p) {
  vmk_lock(L);
  setpvalue(s2v(L->top.p), p);
  api_incr_top(L);
  vmk_unlock(L);
}


VMK_API int vmk_pushthread (vmk_State *L) {
  vmk_lock(L);
  setthvalue(L, s2v(L->top.p), L);
  api_incr_top(L);
  vmk_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Vmk -> stack)
*/


static int auxgetstr (vmk_State *L, const TValue *t, const char *k) {
  lu_byte tag;
  TString *str = vmkS_new(L, k);
  vmkV_fastget(t, str, s2v(L->top.p), vmkH_getstr, tag);
  if (!tagisempty(tag))
    api_incr_top(L);
  else {
    setsvalue2s(L, L->top.p, str);
    api_incr_top(L);
    tag = vmkV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, tag);
  }
  vmk_unlock(L);
  return novariant(tag);
}


static void getGlobalTable (vmk_State *L, TValue *gt) {
  Table *registry = hvalue(&G(L)->l_registry);
  lu_byte tag = vmkH_getint(registry, VMK_RIDX_GLOBALS, gt);
  (void)tag;  /* avoid not-used warnings when checks are off */
  api_check(L, novariant(tag) == VMK_TTABLE, "global table must exist");
}


VMK_API int vmk_getglobal (vmk_State *L, const char *name) {
  TValue gt;
  vmk_lock(L);
  getGlobalTable(L, &gt);
  return auxgetstr(L, &gt, name);
}


VMK_API int vmk_gettable (vmk_State *L, int idx) {
  lu_byte tag;
  TValue *t;
  vmk_lock(L);
  api_checkpop(L, 1);
  t = index2value(L, idx);
  vmkV_fastget(t, s2v(L->top.p - 1), s2v(L->top.p - 1), vmkH_get, tag);
  if (tagisempty(tag))
    tag = vmkV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, tag);
  vmk_unlock(L);
  return novariant(tag);
}


VMK_API int vmk_getfield (vmk_State *L, int idx, const char *k) {
  vmk_lock(L);
  return auxgetstr(L, index2value(L, idx), k);
}


VMK_API int vmk_geti (vmk_State *L, int idx, vmk_Integer n) {
  TValue *t;
  lu_byte tag;
  vmk_lock(L);
  t = index2value(L, idx);
  vmkV_fastgeti(t, n, s2v(L->top.p), tag);
  if (tagisempty(tag)) {
    TValue key;
    setivalue(&key, n);
    tag = vmkV_finishget(L, t, &key, L->top.p, tag);
  }
  api_incr_top(L);
  vmk_unlock(L);
  return novariant(tag);
}


static int finishrawget (vmk_State *L, lu_byte tag) {
  if (tagisempty(tag))  /* avoid copying empty items to the stack */
    setnilvalue(s2v(L->top.p));
  api_incr_top(L);
  vmk_unlock(L);
  return novariant(tag);
}


l_sinline Table *gettable (vmk_State *L, int idx) {
  TValue *t = index2value(L, idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}


VMK_API int vmk_rawget (vmk_State *L, int idx) {
  Table *t;
  lu_byte tag;
  vmk_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  tag = vmkH_get(t, s2v(L->top.p - 1), s2v(L->top.p - 1));
  L->top.p--;  /* pop key */
  return finishrawget(L, tag);
}


VMK_API int vmk_rawgeti (vmk_State *L, int idx, vmk_Integer n) {
  Table *t;
  lu_byte tag;
  vmk_lock(L);
  t = gettable(L, idx);
  vmkH_fastgeti(t, n, s2v(L->top.p), tag);
  return finishrawget(L, tag);
}


VMK_API int vmk_rawgetp (vmk_State *L, int idx, const void *p) {
  Table *t;
  TValue k;
  vmk_lock(L);
  t = gettable(L, idx);
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, vmkH_get(t, &k, s2v(L->top.p)));
}


VMK_API void vmk_createtable (vmk_State *L, int narray, int nrec) {
  Table *t;
  vmk_lock(L);
  t = vmkH_new(L);
  sethvalue2s(L, L->top.p, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    vmkH_resize(L, t, cast_uint(narray), cast_uint(nrec));
  vmkC_checkGC(L);
  vmk_unlock(L);
}


VMK_API int vmk_getmetatable (vmk_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  vmk_lock(L);
  obj = index2value(L, objindex);
  switch (ttype(obj)) {
    case VMK_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case VMK_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue2s(L, L->top.p, mt);
    api_incr_top(L);
    res = 1;
  }
  vmk_unlock(L);
  return res;
}


VMK_API int vmk_getiuservalue (vmk_State *L, int idx, int n) {
  TValue *o;
  int t;
  vmk_lock(L);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->nuvalue) {
    setnilvalue(s2v(L->top.p));
    t = VMK_TNONE;
  }
  else {
    setobj2s(L, L->top.p, &uvalue(o)->uv[n - 1].uv);
    t = ttype(s2v(L->top.p));
  }
  api_incr_top(L);
  vmk_unlock(L);
  return t;
}


/*
** set functions (stack -> Vmk)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
static void auxsetstr (vmk_State *L, const TValue *t, const char *k) {
  int hres;
  TString *str = vmkS_new(L, k);
  api_checkpop(L, 1);
  vmkV_fastset(t, str, s2v(L->top.p - 1), hres, vmkH_psetstr);
  if (hres == HOK) {
    vmkV_finishfastset(L, t, s2v(L->top.p - 1));
    L->top.p--;  /* pop value */
  }
  else {
    setsvalue2s(L, L->top.p, str);  /* push 'str' (to make it a TValue) */
    api_incr_top(L);
    vmkV_finishset(L, t, s2v(L->top.p - 1), s2v(L->top.p - 2), hres);
    L->top.p -= 2;  /* pop value and key */
  }
  vmk_unlock(L);  /* locked done by caller */
}


VMK_API void vmk_setglobal (vmk_State *L, const char *name) {
  TValue gt;
  vmk_lock(L);  /* unlock done in 'auxsetstr' */
  getGlobalTable(L, &gt);
  auxsetstr(L, &gt, name);
}


VMK_API void vmk_settable (vmk_State *L, int idx) {
  TValue *t;
  int hres;
  vmk_lock(L);
  api_checkpop(L, 2);
  t = index2value(L, idx);
  vmkV_fastset(t, s2v(L->top.p - 2), s2v(L->top.p - 1), hres, vmkH_pset);
  if (hres == HOK) {
    vmkV_finishfastset(L, t, s2v(L->top.p - 1));
  }
  else
    vmkV_finishset(L, t, s2v(L->top.p - 2), s2v(L->top.p - 1), hres);
  L->top.p -= 2;  /* pop index and value */
  vmk_unlock(L);
}


VMK_API void vmk_setfield (vmk_State *L, int idx, const char *k) {
  vmk_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, index2value(L, idx), k);
}


VMK_API void vmk_seti (vmk_State *L, int idx, vmk_Integer n) {
  TValue *t;
  int hres;
  vmk_lock(L);
  api_checkpop(L, 1);
  t = index2value(L, idx);
  vmkV_fastseti(t, n, s2v(L->top.p - 1), hres);
  if (hres == HOK)
    vmkV_finishfastset(L, t, s2v(L->top.p - 1));
  else {
    TValue temp;
    setivalue(&temp, n);
    vmkV_finishset(L, t, &temp, s2v(L->top.p - 1), hres);
  }
  L->top.p--;  /* pop value */
  vmk_unlock(L);
}


static void aux_rawset (vmk_State *L, int idx, TValue *key, int n) {
  Table *t;
  vmk_lock(L);
  api_checkpop(L, n);
  t = gettable(L, idx);
  vmkH_set(L, t, key, s2v(L->top.p - 1));
  invalidateTMcache(t);
  vmkC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p -= n;
  vmk_unlock(L);
}


VMK_API void vmk_rawset (vmk_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->top.p - 2), 2);
}


VMK_API void vmk_rawsetp (vmk_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}


VMK_API void vmk_rawseti (vmk_State *L, int idx, vmk_Integer n) {
  Table *t;
  vmk_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  vmkH_setint(L, t, n, s2v(L->top.p - 1));
  vmkC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p--;
  vmk_unlock(L);
}


VMK_API int vmk_setmetatable (vmk_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  vmk_lock(L);
  api_checkpop(L, 1);
  obj = index2value(L, objindex);
  if (ttisnil(s2v(L->top.p - 1)))
    mt = NULL;
  else {
    api_check(L, ttistable(s2v(L->top.p - 1)), "table expected");
    mt = hvalue(s2v(L->top.p - 1));
  }
  switch (ttype(obj)) {
    case VMK_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        vmkC_objbarrier(L, gcvalue(obj), mt);
        vmkC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case VMK_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        vmkC_objbarrier(L, uvalue(obj), mt);
        vmkC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttype(obj)] = mt;
      break;
    }
  }
  L->top.p--;
  vmk_unlock(L);
  return 1;
}


VMK_API int vmk_setiuservalue (vmk_State *L, int idx, int n) {
  TValue *o;
  int res;
  vmk_lock(L);
  api_checkpop(L, 1);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->nuvalue)))
    res = 0;  /* 'n' not in [1, uvalue(o)->nuvalue] */
  else {
    setobj(L, &uvalue(o)->uv[n - 1].uv, s2v(L->top.p - 1));
    vmkC_barrierback(L, gcvalue(o), s2v(L->top.p - 1));
    res = 1;
  }
  L->top.p--;
  vmk_unlock(L);
  return res;
}


/*
** 'load' and 'call' functions (run Vmk code)
*/


#define checkresults(L,na,nr) \
     (api_check(L, (nr) == VMK_MULTRET \
               || (L->ci->top.p - L->top.p >= (nr) - (na)), \
	"results from fn overflow current stack size"), \
      api_check(L, VMK_MULTRET <= (nr) && (nr) <= MAXRESULTS,  \
                   "invalid number of results"))


VMK_API void vmk_callk (vmk_State *L, int nargs, int nresults,
                        vmk_KContext ctx, vmk_KFunction k) {
  StkId func;
  vmk_lock(L);
  api_check(L, k == NULL || !isVmk(L->ci),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->status == VMK_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top.p - (nargs+1);
  if (k != NULL && yieldable(L)) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    vmkD_call(L, func, nresults);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    vmkD_callnoyield(L, func, nresults);  /* just do the call */
  adjustresults(L, nresults);
  vmk_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (vmk_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  vmkD_callnoyield(L, c->func, c->nresults);
}



VMK_API int vmk_pcallk (vmk_State *L, int nargs, int nresults, int errfunc,
                        vmk_KContext ctx, vmk_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  vmk_lock(L);
  api_check(L, k == NULL || !isVmk(L->ci),
    "cannot use continuations inside hooks");
  api_checkpop(L, nargs + 1);
  api_check(L, L->status == VMK_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2stack(L, errfunc);
    api_check(L, ttisfunction(s2v(o)), "error handler must be a fn");
    func = savestack(L, o);
  }
  c.func = L->top.p - (nargs+1);  /* fn to be called */
  if (k == NULL || !yieldable(L)) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = vmkD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->u2.funcidx = cast_int(savestack(L, c.func));
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;  /* fn can do error recovery */
    vmkD_call(L, c.func, nresults);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = VMK_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  vmk_unlock(L);
  return status;
}


VMK_API int vmk_load (vmk_State *L, vmk_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  vmk_lock(L);
  if (!chunkname) chunkname = "?";
  vmkZ_init(L, &z, reader, data);
  status = vmkD_protectedparser(L, &z, chunkname, mode);
  if (status == VMK_OK) {  /* no errors? */
    LClosure *f = clLvalue(s2v(L->top.p - 1));  /* get new fn */
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      TValue gt;
      getGlobalTable(L, &gt);
      /* set global table as 1st upvalue of 'f' (may be VMK_ENV) */
      setobj(L, f->upvals[0]->v.p, &gt);
      vmkC_barrier(L, f->upvals[0], &gt);
    }
  }
  vmk_unlock(L);
  return status;
}


/*
** Dump a Vmk fn, calling 'writer' to write its parts. Ensure
** the stack returns with its original size.
*/
VMK_API int vmk_dump (vmk_State *L, vmk_Writer writer, void *data, int strip) {
  int status;
  ptrdiff_t otop = savestack(L, L->top.p);  /* original top */
  TValue *f = s2v(L->top.p - 1);  /* fn to be dumped */
  vmk_lock(L);
  api_checkpop(L, 1);
  api_check(L, isLfunction(f), "Vmk fn expected");
  status = vmkU_dump(L, clLvalue(f)->p, writer, data, strip);
  L->top.p = restorestack(L, otop);  /* restore top */
  vmk_unlock(L);
  return status;
}


VMK_API int vmk_status (vmk_State *L) {
  return L->status;
}


/*
** Garbage-collection fn
*/
VMK_API int vmk_gc (vmk_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  global_State *g = G(L);
  if (g->gcstp & (GCSTPGC | GCSTPCLS))  /* internal stop? */
    return -1;  /* all options are invalid when stopped */
  vmk_lock(L);
  va_start(argp, what);
  switch (what) {
    case VMK_GCSTOP: {
      g->gcstp = GCSTPUSR;  /* stopped by the user */
      break;
    }
    case VMK_GCRESTART: {
      vmkE_setdebt(g, 0);
      g->gcstp = 0;  /* (other bits must be zero here) */
      break;
    }
    case VMK_GCCOLLECT: {
      vmkC_fullgc(L, 0);
      break;
    }
    case VMK_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case VMK_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case VMK_GCSTEP: {
      lu_byte oldstp = g->gcstp;
      l_mem n = cast(l_mem, va_arg(argp, size_t));
      int work = 0;  /* true if GC did some work */
      g->gcstp = 0;  /* allow GC to run (other bits must be zero here) */
      if (n <= 0)
        n = g->GCdebt;  /* force to run one basic step */
      vmkE_setdebt(g, g->GCdebt - n);
      vmkC_condGC(L, (void)0, work = 1);
      if (work && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      g->gcstp = oldstp;  /* restore previous state */
      break;
    }
    case VMK_GCISRUNNING: {
      res = gcrunning(g);
      break;
    }
    case VMK_GCGEN: {
      res = (g->gckind == KGC_INC) ? VMK_GCINC : VMK_GCGEN;
      vmkC_changemode(L, KGC_GENMINOR);
      break;
    }
    case VMK_GCINC: {
      res = (g->gckind == KGC_INC) ? VMK_GCINC : VMK_GCGEN;
      vmkC_changemode(L, KGC_INC);
      break;
    }
    case VMK_GCPARAM: {
      int param = va_arg(argp, int);
      int value = va_arg(argp, int);
      api_check(L, 0 <= param && param < VMK_GCPN, "invalid parameter");
      res = cast_int(vmkO_applyparam(g->gcparams[param], 100));
      if (value >= 0)
        g->gcparams[param] = vmkO_codeparam(cast_uint(value));
      break;
    }
    default: res = -1;  /* invalid option */
  }
  va_end(argp);
  vmk_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


VMK_API int vmk_error (vmk_State *L) {
  TValue *errobj;
  vmk_lock(L);
  errobj = s2v(L->top.p - 1);
  api_checkpop(L, 1);
  /* error object is the memory error message? */
  if (ttisshrstring(errobj) && eqshrstr(tsvalue(errobj), G(L)->memerrmsg))
    vmkM_error(L);  /* raise a memory error */
  else
    vmkG_errormsg(L);  /* raise a regular error */
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}


VMK_API int vmk_next (vmk_State *L, int idx) {
  Table *t;
  int more;
  vmk_lock(L);
  api_checkpop(L, 1);
  t = gettable(L, idx);
  more = vmkH_next(L, t, L->top.p - 1);
  if (more)
    api_incr_top(L);
  else  /* no more elements */
    L->top.p--;  /* pop key */
  vmk_unlock(L);
  return more;
}


VMK_API void vmk_toclose (vmk_State *L, int idx) {
  StkId o;
  vmk_lock(L);
  o = index2stack(L, idx);
  api_check(L, L->tbclist.p < o, "given index below or equal a marked one");
  vmkF_newtbcupval(L, o);  /* create new to-be-closed upvalue */
  L->ci->callstatus |= CIST_TBC;  /* mark that fn has TBC slots */
  vmk_unlock(L);
}


VMK_API void vmk_concat (vmk_State *L, int n) {
  vmk_lock(L);
  api_checknelems(L, n);
  if (n > 0) {
    vmkV_concat(L, n);
    vmkC_checkGC(L);
  }
  else {  /* nothing to concatenate */
    setsvalue2s(L, L->top.p, vmkS_newlstr(L, "", 0));  /* push empty string */
    api_incr_top(L);
  }
  vmk_unlock(L);
}


VMK_API void vmk_len (vmk_State *L, int idx) {
  TValue *t;
  vmk_lock(L);
  t = index2value(L, idx);
  vmkV_objlen(L, L->top.p, t);
  api_incr_top(L);
  vmk_unlock(L);
}


VMK_API vmk_Alloc vmk_getallocf (vmk_State *L, void **ud) {
  vmk_Alloc f;
  vmk_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  vmk_unlock(L);
  return f;
}


VMK_API void vmk_setallocf (vmk_State *L, vmk_Alloc f, void *ud) {
  vmk_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  vmk_unlock(L);
}


void vmk_setwarnf (vmk_State *L, vmk_WarnFunction f, void *ud) {
  vmk_lock(L);
  G(L)->ud_warn = ud;
  G(L)->warnf = f;
  vmk_unlock(L);
}


void vmk_warning (vmk_State *L, const char *msg, int tocont) {
  vmk_lock(L);
  vmkE_warning(L, msg, tocont);
  vmk_unlock(L);
}



VMK_API void *vmk_newuserdatauv (vmk_State *L, size_t size, int nuvalue) {
  Udata *u;
  vmk_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < SHRT_MAX, "invalid value");
  u = vmkS_newudata(L, size, cast(unsigned short, nuvalue));
  setuvalue(L, s2v(L->top.p), u);
  api_incr_top(L);
  vmkC_checkGC(L);
  vmk_unlock(L);
  return getudatamem(u);
}



static const char *aux_upvalue (TValue *fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttypetag(fi)) {
    case VMK_VCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(cast_uint(n) - 1u < cast_uint(f->nupvalues)))
        return NULL;  /* 'n' not in [1, f->nupvalues] */
      *val = &f->upvalue[n-1];
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case VMK_VLCL: {  /* Vmk closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(cast_uint(n) - 1u  < cast_uint(p->sizeupvalues)))
        return NULL;  /* 'n' not in [1, p->sizeupvalues] */
      *val = f->upvals[n-1]->v.p;
      if (owner) *owner = obj2gco(f->upvals[n - 1]);
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


VMK_API const char *vmk_getupvalue (vmk_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  vmk_lock(L);
  name = aux_upvalue(index2value(L, funcindex), n, &val, NULL);
  if (name) {
    setobj2s(L, L->top.p, val);
    api_incr_top(L);
  }
  vmk_unlock(L);
  return name;
}


VMK_API const char *vmk_setupvalue (vmk_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  GCObject *owner = NULL;  /* to avoid warnings */
  TValue *fi;
  vmk_lock(L);
  fi = index2value(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top.p--;
    setobj(L, val, s2v(L->top.p));
    vmkC_barrier(L, owner, val);
  }
  vmk_unlock(L);
  return name;
}


static UpVal **getupvalref (vmk_State *L, int fidx, int n, LClosure **pf) {
  static const UpVal *const nullup = NULL;
  LClosure *f;
  TValue *fi = index2value(L, fidx);
  api_check(L, ttisLclosure(fi), "Vmk fn expected");
  f = clLvalue(fi);
  if (pf) *pf = f;
  if (1 <= n && n <= f->p->sizeupvalues)
    return &f->upvals[n - 1];  /* get its upvalue pointer */
  else
    return (UpVal**)&nullup;
}


VMK_API void *vmk_upvalueid (vmk_State *L, int fidx, int n) {
  TValue *fi = index2value(L, fidx);
  switch (ttypetag(fi)) {
    case VMK_VLCL: {  /* vmk closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case VMK_VCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (1 <= n && n <= f->nupvalues)
        return &f->upvalue[n - 1];
      /* else */
    }  /* FALLTHROUGH */
    case VMK_VLCF:
      return NULL;  /* light C functions have no upvalues */
    default: {
      api_check(L, 0, "fn expected");
      return NULL;
    }
  }
}


VMK_API void vmk_upvaluejoin (vmk_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  api_check(L, *up1 != NULL && *up2 != NULL, "invalid upvalue index");
  *up1 = *up2;
  vmkC_objbarrier(L, f1, *up1);
}


