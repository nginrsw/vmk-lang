/*
** $Id: lauxlib.c $
** Auxiliary functions for building Vmk libraries
** See Copyright Notice in vmk.h
*/

#define lauxlib_c
#define VMK_LIB

#include "lprefix.h"


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
** This file uses only the official API of Vmk.
** Any fn declared here could be written as an application fn.
*/

#include "vmk.h"

#include "lauxlib.h"
#include "llimits.h"


/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	10	/* size of the first part of the stack */
#define LEVELS2	11	/* size of the second part of the stack */



/*
** Search for 'objidx' in table at index -1. ('objidx' must be an
** absolute index.) Return 1 + string at top if it found a good name.
*/
static int findfield (vmk_State *L, int objidx, int level) {
  if (level == 0 || !vmk_istable(L, -1))
    return 0;  /* not found */
  vmk_pushnil(L);  /* start 'next' loop */
  while (vmk_next(L, -2)) {  /* for each pair in table */
    if (vmk_type(L, -2) == VMK_TSTRING) {  /* ignore non-string keys */
      if (vmk_rawequal(L, objidx, -1)) {  /* found object? */
        vmk_pop(L, 1);  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        /* stack: lib_name, lib_table, field_name (top) */
        vmk_pushliteral(L, ".");  /* place '.' between the two names */
        vmk_replace(L, -3);  /* (in the slot occupied by table) */
        vmk_concat(L, 3);  /* lib_name.field_name */
        return 1;
      }
    }
    vmk_pop(L, 1);  /* remove value */
  }
  return 0;  /* not found */
}


/*
** Search for a name for a fn in all loaded modules
*/
static int pushglobalfuncname (vmk_State *L, vmk_Debug *ar) {
  int top = vmk_gettop(L);
  vmk_getinfo(L, "f", ar);  /* push fn */
  vmk_getfield(L, VMK_REGISTRYINDEX, VMK_LOADED_TABLE);
  vmkL_checkstack(L, 6, "not enough stack");  /* slots for 'findfield' */
  if (findfield(L, top + 1, 2)) {
    const char *name = vmk_tostring(L, -1);
    if (strncmp(name, VMK_GNAME ".", 3) == 0) {  /* name start with '_G.'? */
      vmk_pushstring(L, name + 3);  /* push name without prefix */
      vmk_remove(L, -2);  /* remove original name */
    }
    vmk_copy(L, -1, top + 1);  /* copy name to proper place */
    vmk_settop(L, top + 1);  /* remove table "loaded" and name copy */
    return 1;
  }
  else {
    vmk_settop(L, top);  /* remove fn and global table */
    return 0;
  }
}


static void pushfuncname (vmk_State *L, vmk_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {  /* try first a global name */
    vmk_pushfstring(L, "fn '%s'", vmk_tostring(L, -1));
    vmk_remove(L, -2);  /* remove name */
  }
  else if (*ar->namewhat != '\0')  /* is there a name from code? */
    vmk_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  /* use it */
  else if (*ar->what == 'm')  /* main? */
      vmk_pushliteral(L, "main chunk");
  else if (*ar->what != 'C')  /* for Vmk functions, use <file:line> */
    vmk_pushfstring(L, "fn <%s:%d>", ar->short_src, ar->linedefined);
  else  /* nothing left... */
    vmk_pushliteral(L, "?");
}


static int lastlevel (vmk_State *L) {
  vmk_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (vmk_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (vmk_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


VMKLIB_API void vmkL_traceback (vmk_State *L, vmk_State *L1,
                                const char *msg, int level) {
  vmkL_Buffer b;
  vmk_Debug ar;
  int last = lastlevel(L1);
  int limit2show = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  vmkL_buffinit(L, &b);
  if (msg) {
    vmkL_addstring(&b, msg);
    vmkL_addchar(&b, '\n');
  }
  vmkL_addstring(&b, "stack traceback:");
  while (vmk_getstack(L1, level++, &ar)) {
    if (limit2show-- == 0) {  /* too many levels? */
      int n = last - level - LEVELS2 + 1;  /* number of levels to skip */
      vmk_pushfstring(L, "\n\t...\t(skipping %d levels)", n);
      vmkL_addvalue(&b);  /* add warning about skip */
      level += n;  /* and skip to last levels */
    }
    else {
      vmk_getinfo(L1, "Slnt", &ar);
      if (ar.currentline <= 0)
        vmk_pushfstring(L, "\n\t%s: in ", ar.short_src);
      else
        vmk_pushfstring(L, "\n\t%s:%d: in ", ar.short_src, ar.currentline);
      vmkL_addvalue(&b);
      pushfuncname(L, &ar);
      vmkL_addvalue(&b);
      if (ar.istailcall)
        vmkL_addstring(&b, "\n\t(...tail calls...)");
    }
  }
  vmkL_pushresult(&b);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

VMKLIB_API int vmkL_argerror (vmk_State *L, int arg, const char *extramsg) {
  vmk_Debug ar;
  const char *argword;
  if (!vmk_getstack(L, 0, &ar))  /* no stack frame? */
    return vmkL_error(L, "bad argument #%d (%s)", arg, extramsg);
  vmk_getinfo(L, "nt", &ar);
  if (arg <= ar.extraargs)  /* error in an extra argument? */
    argword =  "extra argument";
  else {
    arg -= ar.extraargs;  /* do not count extra arguments */
    if (strcmp(ar.namewhat, "method") == 0) {  /* colon syntax? */
      arg--;  /* do not count (extra) self argument */
      if (arg == 0)  /* error in self argument? */
        return vmkL_error(L, "calling '%s' on bad self (%s)",
                               ar.name, extramsg);
      /* else go through; error in a regular argument */
    }
    argword = "argument";
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? vmk_tostring(L, -1) : "?";
  return vmkL_error(L, "bad %s #%d to '%s' (%s)",
                       argword, arg, ar.name, extramsg);
}


VMKLIB_API int vmkL_typeerror (vmk_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  /* name for the type of the actual argument */
  if (vmkL_getmetafield(L, arg, "__name") == VMK_TSTRING)
    typearg = vmk_tostring(L, -1);  /* use the given type name */
  else if (vmk_type(L, arg) == VMK_TLIGHTUSERDATA)
    typearg = "light userdata";  /* special name for messages */
  else
    typearg = vmkL_typename(L, arg);  /* standard name */
  msg = vmk_pushfstring(L, "%s expected, got %s", tname, typearg);
  return vmkL_argerror(L, arg, msg);
}


static void tag_error (vmk_State *L, int arg, int tag) {
  vmkL_typeerror(L, arg, vmk_typename(L, tag));
}


/*
** The use of 'vmk_pushfstring' ensures this fn does not
** need reserved stack space when called.
*/
VMKLIB_API void vmkL_where (vmk_State *L, int level) {
  vmk_Debug ar;
  if (vmk_getstack(L, level, &ar)) {  /* check fn at level */
    vmk_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      vmk_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  vmk_pushfstring(L, "");  /* else, no information available... */
}


/*
** Again, the use of 'vmk_pushvfstring' ensures this fn does
** not need reserved stack space when called. (At worst, it generates
** a memory error instead of the given message.)
*/
VMKLIB_API int vmkL_error (vmk_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  vmkL_where(L, 1);
  vmk_pushvfstring(L, fmt, argp);
  va_end(argp);
  vmk_concat(L, 2);
  return vmk_error(L);
}


VMKLIB_API int vmkL_fileresult (vmk_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to Vmk API may change this value */
  if (stat) {
    vmk_pushboolean(L, 1);
    return 1;
  }
  else {
    const char *msg;
    vmkL_pushfail(L);
    msg = (en != 0) ? strerror(en) : "(no extra info)";
    if (fname)
      vmk_pushfstring(L, "%s: %s", fname, msg);
    else
      vmk_pushstring(L, msg);
    vmk_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)	/* { */

#if defined(VMK_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */


VMKLIB_API int vmkL_execresult (vmk_State *L, int stat) {
  if (stat != 0 && errno != 0)  /* error with an 'errno'? */
    return vmkL_fileresult(L, 0, NULL);
  else {
    const char *what = "exit";  /* type of termination */
    l_inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      vmk_pushboolean(L, 1);
    else
      vmkL_pushfail(L);
    vmk_pushstring(L, what);
    vmk_pushinteger(L, stat);
    return 3;  /* return true/fail,what,code */
  }
}

/* }====================================================== */



/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

VMKLIB_API int vmkL_newmetatable (vmk_State *L, const char *tname) {
  if (vmkL_getmetatable(L, tname) != VMK_TNIL)  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  vmk_pop(L, 1);
  vmk_createtable(L, 0, 2);  /* create metatable */
  vmk_pushstring(L, tname);
  vmk_setfield(L, -2, "__name");  /* metatable.__name = tname */
  vmk_pushvalue(L, -1);
  vmk_setfield(L, VMK_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}


VMKLIB_API void vmkL_setmetatable (vmk_State *L, const char *tname) {
  vmkL_getmetatable(L, tname);
  vmk_setmetatable(L, -2);
}


VMKLIB_API void *vmkL_testudata (vmk_State *L, int ud, const char *tname) {
  void *p = vmk_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (vmk_getmetatable(L, ud)) {  /* does it have a metatable? */
      vmkL_getmetatable(L, tname);  /* get correct metatable */
      if (!vmk_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      vmk_pop(L, 2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}


VMKLIB_API void *vmkL_checkudata (vmk_State *L, int ud, const char *tname) {
  void *p = vmkL_testudata(L, ud, tname);
  vmkL_argexpected(L, p != NULL, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

VMKLIB_API int vmkL_checkoption (vmk_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? vmkL_optstring(L, arg, def) :
                             vmkL_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return vmkL_argerror(L, arg,
                       vmk_pushfstring(L, "invalid option '%s'", name));
}


/*
** Ensures the stack has at least 'space' extra slots, raising an error
** if it cannot fulfill the request. (The error handling needs a few
** extra slots to format the error message. In case of an error without
** this extra space, Vmk will generate the same 'stack overflow' error,
** but without 'msg'.)
*/
VMKLIB_API void vmkL_checkstack (vmk_State *L, int space, const char *msg) {
  if (l_unlikely(!vmk_checkstack(L, space))) {
    if (msg)
      vmkL_error(L, "stack overflow (%s)", msg);
    else
      vmkL_error(L, "stack overflow");
  }
}


VMKLIB_API void vmkL_checktype (vmk_State *L, int arg, int t) {
  if (l_unlikely(vmk_type(L, arg) != t))
    tag_error(L, arg, t);
}


VMKLIB_API void vmkL_checkany (vmk_State *L, int arg) {
  if (l_unlikely(vmk_type(L, arg) == VMK_TNONE))
    vmkL_argerror(L, arg, "value expected");
}


VMKLIB_API const char *vmkL_checklstring (vmk_State *L, int arg, size_t *len) {
  const char *s = vmk_tolstring(L, arg, len);
  if (l_unlikely(!s)) tag_error(L, arg, VMK_TSTRING);
  return s;
}


VMKLIB_API const char *vmkL_optlstring (vmk_State *L, int arg,
                                        const char *def, size_t *len) {
  if (vmk_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return vmkL_checklstring(L, arg, len);
}


VMKLIB_API vmk_Number vmkL_checknumber (vmk_State *L, int arg) {
  int isnum;
  vmk_Number d = vmk_tonumberx(L, arg, &isnum);
  if (l_unlikely(!isnum))
    tag_error(L, arg, VMK_TNUMBER);
  return d;
}


VMKLIB_API vmk_Number vmkL_optnumber (vmk_State *L, int arg, vmk_Number def) {
  return vmkL_opt(L, vmkL_checknumber, arg, def);
}


static void interror (vmk_State *L, int arg) {
  if (vmk_isnumber(L, arg))
    vmkL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, VMK_TNUMBER);
}


VMKLIB_API vmk_Integer vmkL_checkinteger (vmk_State *L, int arg) {
  int isnum;
  vmk_Integer d = vmk_tointegerx(L, arg, &isnum);
  if (l_unlikely(!isnum)) {
    interror(L, arg);
  }
  return d;
}


VMKLIB_API vmk_Integer vmkL_optinteger (vmk_State *L, int arg,
                                                      vmk_Integer def) {
  return vmkL_opt(L, vmkL_checkinteger, arg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/* userdata to box arbitrary data */
typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;


/* Resize the buffer used by a box. Optimize for the common case of
** resizing to the old size. (For instance, __gc will resize the box
** to 0 even after it was closed. 'pushresult' may also resize it to a
** final size that is equal to the one set when the buffer was created.)
*/
static void *resizebox (vmk_State *L, int idx, size_t newsize) {
  UBox *box = (UBox *)vmk_touserdata(L, idx);
  if (box->bsize == newsize)  /* not changing size? */
    return box->box;  /* keep the buffer */
  else {
    void *ud;
    vmk_Alloc allocf = vmk_getallocf(L, &ud);
    void *temp = allocf(ud, box->box, box->bsize, newsize);
    if (l_unlikely(temp == NULL && newsize > 0)) {  /* allocation error? */
      vmk_pushliteral(L, "not enough memory");
      vmk_error(L);  /* raise a memory error */
    }
    box->box = temp;
    box->bsize = newsize;
    return temp;
  }
}


static int boxgc (vmk_State *L) {
  resizebox(L, 1, 0);
  return 0;
}


static const vmkL_Reg boxmt[] = {  /* box metamethods */
  {"__gc", boxgc},
  {"__close", boxgc},
  {NULL, NULL}
};


static void newbox (vmk_State *L) {
  UBox *box = (UBox *)vmk_newuserdatauv(L, sizeof(UBox), 0);
  box->box = NULL;
  box->bsize = 0;
  if (vmkL_newmetatable(L, "_UBOX*"))  /* creating metatable? */
    vmkL_setfuncs(L, boxmt, 0);  /* set its metamethods */
  vmk_setmetatable(L, -2);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->init.b)


/*
** Whenever buffer is accessed, slot 'idx' must either be a box (which
** cannot be NULL) or it is a placeholder for the buffer.
*/
#define checkbufferlevel(B,idx)  \
  vmk_assert(buffonstack(B) ? vmk_touserdata(B->L, idx) != NULL  \
                            : vmk_touserdata(B->L, idx) == (void*)B)


/*
** Compute new size for buffer 'B', enough to accommodate extra 'sz'
** bytes plus one for a terminating zero. (The test for "not big enough"
** also gets the case when the computation of 'newsize' overflows.)
*/
static size_t newbuffsize (vmkL_Buffer *B, size_t sz) {
  size_t newsize = (B->size / 2) * 3;  /* buffer size * 1.5 */
  if (l_unlikely(sz > MAX_SIZE - B->n - 1))
    return cast_sizet(vmkL_error(B->L, "resulting string too large"));
  if (newsize < B->n + sz + 1 || newsize > MAX_SIZE) {
    /* newsize was not big enough or too big */
    newsize = B->n + sz + 1;
  }
  return newsize;
}


/*
** Returns a pointer to a free area with at least 'sz' bytes in buffer
** 'B'. 'boxidx' is the relative position in the stack where is the
** buffer's box or its placeholder.
*/
static char *prepbuffsize (vmkL_Buffer *B, size_t sz, int boxidx) {
  checkbufferlevel(B, boxidx);
  if (B->size - B->n >= sz)  /* enough space? */
    return B->b + B->n;
  else {
    vmk_State *L = B->L;
    char *newbuff;
    size_t newsize = newbuffsize(B, sz);
    /* create larger buffer */
    if (buffonstack(B))  /* buffer already has a box? */
      newbuff = (char *)resizebox(L, boxidx, newsize);  /* resize it */
    else {  /* no box yet */
      vmk_remove(L, boxidx);  /* remove placeholder */
      newbox(L);  /* create a new box */
      vmk_insert(L, boxidx);  /* move box to its intended position */
      vmk_toclose(L, boxidx);
      newbuff = (char *)resizebox(L, boxidx, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));  /* copy original content */
    }
    B->b = newbuff;
    B->size = newsize;
    return newbuff + B->n;
  }
}

/*
** returns a pointer to a free area with at least 'sz' bytes
*/
VMKLIB_API char *vmkL_prepbuffsize (vmkL_Buffer *B, size_t sz) {
  return prepbuffsize(B, sz, -1);
}


VMKLIB_API void vmkL_addlstring (vmkL_Buffer *B, const char *s, size_t l) {
  if (l > 0) {  /* avoid 'memcpy' when 's' can be NULL */
    char *b = prepbuffsize(B, l, -1);
    memcpy(b, s, l * sizeof(char));
    vmkL_addsize(B, l);
  }
}


VMKLIB_API void vmkL_addstring (vmkL_Buffer *B, const char *s) {
  vmkL_addlstring(B, s, strlen(s));
}


VMKLIB_API void vmkL_pushresult (vmkL_Buffer *B) {
  vmk_State *L = B->L;
  checkbufferlevel(B, -1);
  if (!buffonstack(B))  /* using static buffer? */
    vmk_pushlstring(L, B->b, B->n);  /* save result as regular string */
  else {  /* reuse buffer already allocated */
    UBox *box = (UBox *)vmk_touserdata(L, -1);
    void *ud;
    vmk_Alloc allocf = vmk_getallocf(L, &ud);  /* fn to free buffer */
    size_t len = B->n;  /* final string length */
    char *s;
    resizebox(L, -1, len + 1);  /* adjust box size to content size */
    s = (char*)box->box;  /* final buffer address */
    s[len] = '\0';  /* add ending zero */
    /* clear box, as Vmk will take control of the buffer */
    box->bsize = 0;  box->box = NULL;
    vmk_pushexternalstring(L, s, len, allocf, ud);
    vmk_closeslot(L, -2);  /* close the box */
    vmk_gc(L, VMK_GCSTEP, len);
  }
  vmk_remove(L, -2);  /* remove box or placeholder from the stack */
}


VMKLIB_API void vmkL_pushresultsize (vmkL_Buffer *B, size_t sz) {
  vmkL_addsize(B, sz);
  vmkL_pushresult(B);
}


/*
** 'vmkL_addvalue' is the only fn in the Buffer system where the
** box (if existent) is not on the top of the stack. So, instead of
** calling 'vmkL_addlstring', it replicates the code using -2 as the
** last argument to 'prepbuffsize', signaling that the box is (or will
** be) below the string being added to the buffer. (Box creation can
** trigger an emergency GC, so we should not remove the string from the
** stack before we have the space guaranteed.)
*/
VMKLIB_API void vmkL_addvalue (vmkL_Buffer *B) {
  vmk_State *L = B->L;
  size_t len;
  const char *s = vmk_tolstring(L, -1, &len);
  char *b = prepbuffsize(B, len, -2);
  memcpy(b, s, len * sizeof(char));
  vmkL_addsize(B, len);
  vmk_pop(L, 1);  /* pop string */
}


VMKLIB_API void vmkL_buffinit (vmk_State *L, vmkL_Buffer *B) {
  B->L = L;
  B->b = B->init.b;
  B->n = 0;
  B->size = VMKL_BUFFERSIZE;
  vmk_pushlightuserdata(L, (void*)B);  /* push placeholder */
}


VMKLIB_API char *vmkL_buffinitsize (vmk_State *L, vmkL_Buffer *B, size_t sz) {
  vmkL_buffinit(L, B);
  return prepbuffsize(B, sz, -1);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/*
** The previously freed references form a linked list: t[1] is the index
** of a first free index, t[t[1]] is the index of the second element,
** etc. A zero signals the end of the list.
*/
VMKLIB_API int vmkL_ref (vmk_State *L, int t) {
  int ref;
  if (vmk_isnil(L, -1)) {
    vmk_pop(L, 1);  /* remove from stack */
    return VMK_REFNIL;  /* 'nil' has a unique fixed reference */
  }
  t = vmk_absindex(L, t);
  if (vmk_rawgeti(L, t, 1) == VMK_TNUMBER)  /* already initialized? */
    ref = (int)vmk_tointeger(L, -1);  /* ref = t[1] */
  else {  /* first access */
    vmk_assert(!vmk_toboolean(L, -1));  /* must be nil or false */
    ref = 0;  /* list is empty */
    vmk_pushinteger(L, 0);  /* initialize as an empty list */
    vmk_rawseti(L, t, 1);  /* ref = t[1] = 0 */
  }
  vmk_pop(L, 1);  /* remove element from stack */
  if (ref != 0) {  /* any free element? */
    vmk_rawgeti(L, t, ref);  /* remove it from list */
    vmk_rawseti(L, t, 1);  /* (t[1] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)vmk_rawlen(L, t) + 1;  /* get a new reference */
  vmk_rawseti(L, t, ref);
  return ref;
}


VMKLIB_API void vmkL_unref (vmk_State *L, int t, int ref) {
  if (ref >= 0) {
    t = vmk_absindex(L, t);
    vmk_rawgeti(L, t, 1);
    vmk_assert(vmk_isinteger(L, -1));
    vmk_rawseti(L, t, ref);  /* t[ref] = t[1] */
    vmk_pushinteger(L, ref);
    vmk_rawseti(L, t, 1);  /* t[1] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  unsigned n;  /* number of pre-read characters */
  FILE *f;  /* file being read */
  char buff[BUFSIZ];  /* area for reading file */
} LoadF;


static const char *getF (vmk_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (vmk_State *L, const char *what, int fnameindex) {
  int err = errno;
  const char *filename = vmk_tostring(L, fnameindex) + 1;
  if (err != 0)
    vmk_pushfstring(L, "cannot %s %s: %s", what, filename, strerror(err));
  else
    vmk_pushfstring(L, "cannot %s %s", what, filename);
  vmk_remove(L, fnameindex);
  return VMK_ERRFILE;
}


/*
** Skip an optional BOM at the start of a stream. If there is an
** incomplete BOM (the first character is correct but the rest is
** not), returns the first character anyway to force an error
** (as no chunk can start with 0xEF).
*/
static int skipBOM (FILE *f) {
  int c = getc(f);  /* read first character */
  if (c == 0xEF && getc(f) == 0xBB && getc(f) == 0xBF)  /* correct BOM? */
    return getc(f);  /* ignore BOM and return next char */
  else  /* no (valid) BOM */
    return c;  /* return first character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (FILE *f, int *cp) {
  int c = *cp = skipBOM(f);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(f);
    } while (c != EOF && c != '\n');
    *cp = getc(f);  /* next character after comment, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


VMKLIB_API int vmkL_loadfilex (vmk_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = vmk_gettop(L) + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    vmk_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    vmk_pushfstring(L, "@%s", filename);
    errno = 0;
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  lf.n = 0;
  if (skipcomment(lf.f, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add newline to correct line numbers */
  if (c == VMK_SIGNATURE[0]) {  /* binary file? */
    lf.n = 0;  /* remove possible newline */
    if (filename) {  /* "real" file? */
      errno = 0;
      lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
      if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
      skipcomment(lf.f, &c);  /* re-read initial portion */
    }
  }
  if (c != EOF)
    lf.buff[lf.n++] = cast_char(c);  /* 'c' is the first character */
  status = vmk_load(L, getF, &lf, vmk_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  errno = 0;  /* no useful error number until here */
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    vmk_settop(L, fnameindex);  /* ignore results from 'vmk_load' */
    return errfile(L, "read", fnameindex);
  }
  vmk_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (vmk_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


VMKLIB_API int vmkL_loadbufferx (vmk_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return vmk_load(L, getS, &ls, name, mode);
}


VMKLIB_API int vmkL_loadstring (vmk_State *L, const char *s) {
  return vmkL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



VMKLIB_API int vmkL_getmetafield (vmk_State *L, int obj, const char *event) {
  if (!vmk_getmetatable(L, obj))  /* no metatable? */
    return VMK_TNIL;
  else {
    int tt;
    vmk_pushstring(L, event);
    tt = vmk_rawget(L, -2);
    if (tt == VMK_TNIL)  /* is metafield nil? */
      vmk_pop(L, 2);  /* remove metatable and metafield */
    else
      vmk_remove(L, -2);  /* remove only metatable */
    return tt;  /* return metafield type */
  }
}


VMKLIB_API int vmkL_callmeta (vmk_State *L, int obj, const char *event) {
  obj = vmk_absindex(L, obj);
  if (vmkL_getmetafield(L, obj, event) == VMK_TNIL)  /* no metafield? */
    return 0;
  vmk_pushvalue(L, obj);
  vmk_call(L, 1, 1);
  return 1;
}


VMKLIB_API vmk_Integer vmkL_len (vmk_State *L, int idx) {
  vmk_Integer l;
  int isnum;
  vmk_len(L, idx);
  l = vmk_tointegerx(L, -1, &isnum);
  if (l_unlikely(!isnum))
    vmkL_error(L, "object length is not an integer");
  vmk_pop(L, 1);  /* remove object */
  return l;
}


VMKLIB_API const char *vmkL_tolstring (vmk_State *L, int idx, size_t *len) {
  idx = vmk_absindex(L,idx);
  if (vmkL_callmeta(L, idx, "__tostring")) {  /* metafield? */
    if (!vmk_isstring(L, -1))
      vmkL_error(L, "'__tostring' must return a string");
  }
  else {
    switch (vmk_type(L, idx)) {
      case VMK_TNUMBER: {
        char buff[VMK_N2SBUFFSZ];
        vmk_numbertocstring(L, idx, buff);
        vmk_pushstring(L, buff);
        break;
      }
      case VMK_TSTRING:
        vmk_pushvalue(L, idx);
        break;
      case VMK_TBOOLEAN:
        vmk_pushstring(L, (vmk_toboolean(L, idx) ? "true" : "false"));
        break;
      case VMK_TNIL:
        vmk_pushliteral(L, "nil");
        break;
      default: {
        int tt = vmkL_getmetafield(L, idx, "__name");  /* try name */
        const char *kind = (tt == VMK_TSTRING) ? vmk_tostring(L, -1) :
                                                 vmkL_typename(L, idx);
        vmk_pushfstring(L, "%s: %p", kind, vmk_topointer(L, idx));
        if (tt != VMK_TNIL)
          vmk_remove(L, -2);  /* remove '__name' */
        break;
      }
    }
  }
  return vmk_tolstring(L, -1, len);
}


/*
** set functions from list 'l' into table at top - 'nup'; each
** fn gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
VMKLIB_API void vmkL_setfuncs (vmk_State *L, const vmkL_Reg *l, int nup) {
  vmkL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    if (l->func == NULL)  /* placeholder? */
      vmk_pushboolean(L, 0);
    else {
      int i;
      for (i = 0; i < nup; i++)  /* copy upvalues to the top */
        vmk_pushvalue(L, -nup);
      vmk_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    }
    vmk_setfield(L, -(nup + 2), l->name);
  }
  vmk_pop(L, nup);  /* remove upvalues */
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
VMKLIB_API int vmkL_getsubtable (vmk_State *L, int idx, const char *fname) {
  if (vmk_getfield(L, idx, fname) == VMK_TTABLE)
    return 1;  /* table already there */
  else {
    vmk_pop(L, 1);  /* remove previous result */
    idx = vmk_absindex(L, idx);
    vmk_newtable(L);
    vmk_pushvalue(L, -1);  /* copy to be left at top */
    vmk_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** Stripped-down 'require': After checking "loaded" table, calls 'openf'
** to open a module, registers the result in 'package.loaded' table and,
** if 'glb' is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
VMKLIB_API void vmkL_requiref (vmk_State *L, const char *modname,
                               vmk_CFunction openf, int glb) {
  vmkL_getsubtable(L, VMK_REGISTRYINDEX, VMK_LOADED_TABLE);
  vmk_getfield(L, -1, modname);  /* LOADED[modname] */
  if (!vmk_toboolean(L, -1)) {  /* package not already loaded? */
    vmk_pop(L, 1);  /* remove field */
    vmk_pushcfunction(L, openf);
    vmk_pushstring(L, modname);  /* argument to open fn */
    vmk_call(L, 1, 1);  /* call 'openf' to open module */
    vmk_pushvalue(L, -1);  /* make copy of module (call result) */
    vmk_setfield(L, -3, modname);  /* LOADED[modname] = module */
  }
  vmk_remove(L, -2);  /* remove LOADED table */
  if (glb) {
    vmk_pushvalue(L, -1);  /* copy of module */
    vmk_setglobal(L, modname);  /* _G[modname] = module */
  }
}


VMKLIB_API void vmkL_addgsub (vmkL_Buffer *b, const char *s,
                                     const char *p, const char *r) {
  const char *wild;
  size_t l = strlen(p);
  while ((wild = strstr(s, p)) != NULL) {
    vmkL_addlstring(b, s, ct_diff2sz(wild - s));  /* push prefix */
    vmkL_addstring(b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after 'p' */
  }
  vmkL_addstring(b, s);  /* push last suffix */
}


VMKLIB_API const char *vmkL_gsub (vmk_State *L, const char *s,
                                  const char *p, const char *r) {
  vmkL_Buffer b;
  vmkL_buffinit(L, &b);
  vmkL_addgsub(&b, s, p, r);
  vmkL_pushresult(&b);
  return vmk_tostring(L, -1);
}


static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}


/*
** Standard panic fn just prints an error message. The test
** with 'vmk_type' avoids possible memory errors in 'vmk_tostring'.
*/
static int panic (vmk_State *L) {
  const char *msg = (vmk_type(L, -1) == VMK_TSTRING)
                  ? vmk_tostring(L, -1)
                  : "error object is not a string";
  vmk_writestringerror("PANIC: unprotected error in call to Vmk API (%s)\n",
                        msg);
  return 0;  /* return to Vmk to abort */
}


/*
** Warning functions:
** warnfoff: warning system is off
** warnfon: ready to start a new message
** warnfcont: previous message is to be continued
*/
static void warnfoff (void *ud, const char *message, int tocont);
static void warnfon (void *ud, const char *message, int tocont);
static void warnfcont (void *ud, const char *message, int tocont);


/*
** Check whether message is a control message. If so, execute the
** control or ignore it if unknown.
*/
static int checkcontrol (vmk_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')  /* not a control message? */
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      vmk_setwarnf(L, warnfoff, L);  /* turn warnings off */
    else if (strcmp(message, "on") == 0)
      vmk_setwarnf(L, warnfon, L);   /* turn warnings on */
    return 1;  /* it was a control message */
  }
}


static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol((vmk_State *)ud, message, tocont);
}


/*
** Writes the message and handle 'tocont', finishing the message
** if needed and setting the next warn fn.
*/
static void warnfcont (void *ud, const char *message, int tocont) {
  vmk_State *L = (vmk_State *)ud;
  vmk_writestringerror("%s", message);  /* write message */
  if (tocont)  /* not the last part? */
    vmk_setwarnf(L, warnfcont, L);  /* to be continued */
  else {  /* last part */
    vmk_writestringerror("%s", "\n");  /* finish message with end-of-line */
    vmk_setwarnf(L, warnfon, L);  /* next call is a new message */
  }
}


static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol((vmk_State *)ud, message, tocont))  /* control message? */
    return;  /* nothing else to be done */
  vmk_writestringerror("%s", "Vmk warning: ");  /* start a new warning */
  warnfcont(ud, message, tocont);  /* finish processing */
}



/*
** A fn to compute an unsigned int with some level of
** randomness. Rely on Address Space Layout Randomization (if present)
** and the current time.
*/
#if !defined(vmki_makeseed)

#include <time.h>


/* Size for the buffer, in bytes */
#define BUFSEEDB	(sizeof(void*) + sizeof(time_t))

/* Size for the buffer in int's, rounded up */
#define BUFSEED		((BUFSEEDB + sizeof(int) - 1) / sizeof(int))

/*
** Copy the contents of variable 'v' into the buffer pointed by 'b'.
** (The '&b[0]' disguises 'b' to fix an absurd warning from clang.)
*/
#define addbuff(b,v)	(memcpy(&b[0], &(v), sizeof(v)), b += sizeof(v))


static unsigned int vmki_makeseed (void) {
  unsigned int buff[BUFSEED];
  unsigned int res;
  unsigned int i;
  time_t t = time(NULL);
  char *b = (char*)buff;
  addbuff(b, b);  /* lck variable's address */
  addbuff(b, t);  /* time */
  /* fill (rare but possible) remain of the buffer with zeros */
  memset(b, 0, sizeof(buff) - BUFSEEDB);
  res = buff[0];
  for (i = 1; i < BUFSEED; i++)
    res ^= (res >> 3) + (res << 7) + buff[i];
  return res;
}

#endif


VMKLIB_API unsigned int vmkL_makeseed (vmk_State *L) {
  (void)L;  /* unused */
  return vmki_makeseed();
}


VMKLIB_API vmk_State *vmkL_newstate (void) {
  vmk_State *L = vmk_newstate(l_alloc, NULL, vmki_makeseed());
  if (l_likely(L)) {
    vmk_atpanic(L, &panic);
    vmk_setwarnf(L, warnfoff, L);  /* default is warnings off */
  }
  return L;
}


VMKLIB_API void vmkL_checkversion_ (vmk_State *L, vmk_Number ver, size_t sz) {
  vmk_Number v = vmk_version(L);
  if (sz != VMKL_NUMSIZES)  /* check numeric types */
    vmkL_error(L, "core and library have incompatible numeric types");
  else if (v != ver)
    vmkL_error(L, "version mismatch: app. needs %f, Vmk core provides %f",
                  (VMKI_UACNUMBER)ver, (VMKI_UACNUMBER)v);
}

