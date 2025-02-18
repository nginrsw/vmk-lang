/*
** $Id: vmk.h $
** VMK - A Vmk Mod Scripting Language
** bitbucket.org/nginr/vmk-lang, KRS-Bdg, Indonesia
** See Copyright Notice at the end of this file
*/


#ifndef vmk_h
#define vmk_h

#include <stdarg.h>
#include <stddef.h>


#define VMK_COPYRIGHT	VMK_RELEASE "  Copyright (C) 2025 bitbucket.org/nginr/vmk-lang, KRS-Bdg"
#define VMK_AUTHORS	"Gillar Ajie Prasatya"


#define VMK_VERSION_MAJOR_N	0
#define VMK_VERSION_MINOR_N	0
#define VMK_VERSION_RELEASE_N	1

#define VMK_VERSION_NUM  (VMK_VERSION_MAJOR_N * 100 + VMK_VERSION_MINOR_N)
#define VMK_VERSION_RELEASE_NUM  (VMK_VERSION_NUM * 100 + VMK_VERSION_RELEASE_N)


#include "vmkconf.h"


/* mark for precompiled code ('<esc>Vmk') */
#define VMK_SIGNATURE	"\x1bVmk"

/* option for multiple returns in 'vmk_pcall' and 'vmk_call' */
#define VMK_MULTRET	(-1)


/*
** Pseudo-indices
** (-VMKI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
#define VMK_REGISTRYINDEX	(-VMKI_MAXSTACK - 1000)
#define vmk_upvalueindex(i)	(VMK_REGISTRYINDEX - (i))


/* thread status */
#define VMK_OK		0
#define VMK_YIELD	1
#define VMK_ERRRUN	2
#define VMK_ERRSYNTAX	3
#define VMK_ERRMEM	4
#define VMK_ERRERR	5


typedef struct vmk_State vmk_State;


/*
** basic types
*/
#define VMK_TNONE		(-1)

#define VMK_TNIL		0
#define VMK_TBOOLEAN		1
#define VMK_TLIGHTUSERDATA	2
#define VMK_TNUMBER		3
#define VMK_TSTRING		4
#define VMK_TTABLE		5
#define VMK_TFUNCTION		6
#define VMK_TUSERDATA		7
#define VMK_TTHREAD		8

#define VMK_NUMTYPES		9



/* minimum Vmk stack available to a C fn */
#define VMK_MINSTACK	20


/* predefined values in the registry */
/* index 1 is reserved for the reference mechanism */
#define VMK_RIDX_GLOBALS	2
#define VMK_RIDX_MAINTHREAD	3
#define VMK_RIDX_LAST		3


/* type of numbers in Vmk */
typedef VMK_NUMBER vmk_Number;


/* type for integer functions */
typedef VMK_INTEGER vmk_Integer;

/* unsigned integer type */
typedef VMK_UNSIGNED vmk_Unsigned;

/* type for continuation-fn contexts */
typedef VMK_KCONTEXT vmk_KContext;


/*
** Type for C functions registered with Vmk
*/
typedef int (*vmk_CFunction) (vmk_State *L);

/*
** Type for continuation functions
*/
typedef int (*vmk_KFunction) (vmk_State *L, int status, vmk_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Vmk chunks
*/
typedef const char * (*vmk_Reader) (vmk_State *L, void *ud, size_t *sz);

typedef int (*vmk_Writer) (vmk_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
*/
typedef void * (*vmk_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/*
** Type for warning functions
*/
typedef void (*vmk_WarnFunction) (void *ud, const char *msg, int tocont);


/*
** Type used by the debug API to collect debug information
*/
typedef struct vmk_Debug vmk_Debug;


/*
** Functions to be called by the debugger in specific events
*/
typedef void (*vmk_Hook) (vmk_State *L, vmk_Debug *ar);


/*
** generic extra include file
*/
#if defined(VMK_USER_H)
#include VMK_USER_H
#endif


/*
** RCS ident string
*/
extern const char vmk_ident[];


/*
** state manipulation
*/
VMK_API vmk_State *(vmk_newstate) (vmk_Alloc f, void *ud, unsigned seed);
VMK_API void       (vmk_close) (vmk_State *L);
VMK_API vmk_State *(vmk_newthread) (vmk_State *L);
VMK_API int        (vmk_closethread) (vmk_State *L, vmk_State *from);

VMK_API vmk_CFunction (vmk_atpanic) (vmk_State *L, vmk_CFunction panicf);


VMK_API vmk_Number (vmk_version) (vmk_State *L);


/*
** basic stack manipulation
*/
VMK_API int   (vmk_absindex) (vmk_State *L, int idx);
VMK_API int   (vmk_gettop) (vmk_State *L);
VMK_API void  (vmk_settop) (vmk_State *L, int idx);
VMK_API void  (vmk_pushvalue) (vmk_State *L, int idx);
VMK_API void  (vmk_rotate) (vmk_State *L, int idx, int n);
VMK_API void  (vmk_copy) (vmk_State *L, int fromidx, int toidx);
VMK_API int   (vmk_checkstack) (vmk_State *L, int n);

VMK_API void  (vmk_xmove) (vmk_State *from, vmk_State *to, int n);


/*
** access functions (stack -> C)
*/

VMK_API int             (vmk_isnumber) (vmk_State *L, int idx);
VMK_API int             (vmk_isstring) (vmk_State *L, int idx);
VMK_API int             (vmk_iscfunction) (vmk_State *L, int idx);
VMK_API int             (vmk_isinteger) (vmk_State *L, int idx);
VMK_API int             (vmk_isuserdata) (vmk_State *L, int idx);
VMK_API int             (vmk_type) (vmk_State *L, int idx);
VMK_API const char     *(vmk_typename) (vmk_State *L, int tp);

VMK_API vmk_Number      (vmk_tonumberx) (vmk_State *L, int idx, int *isnum);
VMK_API vmk_Integer     (vmk_tointegerx) (vmk_State *L, int idx, int *isnum);
VMK_API int             (vmk_toboolean) (vmk_State *L, int idx);
VMK_API const char     *(vmk_tolstring) (vmk_State *L, int idx, size_t *len);
VMK_API vmk_Unsigned    (vmk_rawlen) (vmk_State *L, int idx);
VMK_API vmk_CFunction   (vmk_tocfunction) (vmk_State *L, int idx);
VMK_API void	       *(vmk_touserdata) (vmk_State *L, int idx);
VMK_API vmk_State      *(vmk_tothread) (vmk_State *L, int idx);
VMK_API const void     *(vmk_topointer) (vmk_State *L, int idx);


/*
** Comparison and arithmetic functions
*/

#define VMK_OPADD	0	/* ORDER TM, ORDER OP */
#define VMK_OPSUB	1
#define VMK_OPMUL	2
#define VMK_OPMOD	3
#define VMK_OPPOW	4
#define VMK_OPDIV	5
#define VMK_OPIDIV	6
#define VMK_OPBAND	7
#define VMK_OPBOR	8
#define VMK_OPBXOR	9
#define VMK_OPSHL	10
#define VMK_OPSHR	11
#define VMK_OPUNM	12
#define VMK_OPBNOT	13

VMK_API void  (vmk_arith) (vmk_State *L, int op);

#define VMK_OPEQ	0
#define VMK_OPLT	1
#define VMK_OPLE	2

VMK_API int   (vmk_rawequal) (vmk_State *L, int idx1, int idx2);
VMK_API int   (vmk_compare) (vmk_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
VMK_API void        (vmk_pushnil) (vmk_State *L);
VMK_API void        (vmk_pushnumber) (vmk_State *L, vmk_Number n);
VMK_API void        (vmk_pushinteger) (vmk_State *L, vmk_Integer n);
VMK_API const char *(vmk_pushlstring) (vmk_State *L, const char *s, size_t len);
VMK_API const char *(vmk_pushexternalstring) (vmk_State *L,
		const char *s, size_t len, vmk_Alloc falloc, void *ud);
VMK_API const char *(vmk_pushstring) (vmk_State *L, const char *s);
VMK_API const char *(vmk_pushvfstring) (vmk_State *L, const char *fmt,
                                                      va_list argp);
VMK_API const char *(vmk_pushfstring) (vmk_State *L, const char *fmt, ...);
VMK_API void  (vmk_pushcclosure) (vmk_State *L, vmk_CFunction fn, int n);
VMK_API void  (vmk_pushboolean) (vmk_State *L, int b);
VMK_API void  (vmk_pushlightuserdata) (vmk_State *L, void *p);
VMK_API int   (vmk_pushthread) (vmk_State *L);


/*
** get functions (Vmk -> stack)
*/
VMK_API int (vmk_getglobal) (vmk_State *L, const char *name);
VMK_API int (vmk_gettable) (vmk_State *L, int idx);
VMK_API int (vmk_getfield) (vmk_State *L, int idx, const char *k);
VMK_API int (vmk_geti) (vmk_State *L, int idx, vmk_Integer n);
VMK_API int (vmk_rawget) (vmk_State *L, int idx);
VMK_API int (vmk_rawgeti) (vmk_State *L, int idx, vmk_Integer n);
VMK_API int (vmk_rawgetp) (vmk_State *L, int idx, const void *p);

VMK_API void  (vmk_createtable) (vmk_State *L, int narr, int nrec);
VMK_API void *(vmk_newuserdatauv) (vmk_State *L, size_t sz, int nuvalue);
VMK_API int   (vmk_getmetatable) (vmk_State *L, int objindex);
VMK_API int  (vmk_getiuservalue) (vmk_State *L, int idx, int n);


/*
** set functions (stack -> Vmk)
*/
VMK_API void  (vmk_setglobal) (vmk_State *L, const char *name);
VMK_API void  (vmk_settable) (vmk_State *L, int idx);
VMK_API void  (vmk_setfield) (vmk_State *L, int idx, const char *k);
VMK_API void  (vmk_seti) (vmk_State *L, int idx, vmk_Integer n);
VMK_API void  (vmk_rawset) (vmk_State *L, int idx);
VMK_API void  (vmk_rawseti) (vmk_State *L, int idx, vmk_Integer n);
VMK_API void  (vmk_rawsetp) (vmk_State *L, int idx, const void *p);
VMK_API int   (vmk_setmetatable) (vmk_State *L, int objindex);
VMK_API int   (vmk_setiuservalue) (vmk_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Vmk code)
*/
VMK_API void  (vmk_callk) (vmk_State *L, int nargs, int nresults,
                           vmk_KContext ctx, vmk_KFunction k);
#define vmk_call(L,n,r)		vmk_callk(L, (n), (r), 0, NULL)

VMK_API int   (vmk_pcallk) (vmk_State *L, int nargs, int nresults, int errfunc,
                            vmk_KContext ctx, vmk_KFunction k);
#define vmk_pcall(L,n,r,f)	vmk_pcallk(L, (n), (r), (f), 0, NULL)

VMK_API int   (vmk_load) (vmk_State *L, vmk_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

VMK_API int (vmk_dump) (vmk_State *L, vmk_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
VMK_API int  (vmk_yieldk)     (vmk_State *L, int nresults, vmk_KContext ctx,
                               vmk_KFunction k);
VMK_API int  (vmk_resume)     (vmk_State *L, vmk_State *from, int narg,
                               int *nres);
VMK_API int  (vmk_status)     (vmk_State *L);
VMK_API int (vmk_isyieldable) (vmk_State *L);

#define vmk_yield(L,n)		vmk_yieldk(L, (n), 0, NULL)


/*
** Warning-related functions
*/
VMK_API void (vmk_setwarnf) (vmk_State *L, vmk_WarnFunction f, void *ud);
VMK_API void (vmk_warning)  (vmk_State *L, const char *msg, int tocont);


/*
** garbage-collection options
*/

#define VMK_GCSTOP		0
#define VMK_GCRESTART		1
#define VMK_GCCOLLECT		2
#define VMK_GCCOUNT		3
#define VMK_GCCOUNTB		4
#define VMK_GCSTEP		5
#define VMK_GCISRUNNING		6
#define VMK_GCGEN		7
#define VMK_GCINC		8
#define VMK_GCPARAM		9


/*
** garbage-collection parameters
*/
/* parameters for generational mode */
#define VMK_GCPMINORMUL		0  /* control minor collections */
#define VMK_GCPMAJORMINOR	1  /* control shift major->minor */
#define VMK_GCPMINORMAJOR	2  /* control shift minor->major */

/* parameters for incremental mode */
#define VMK_GCPPAUSE		3  /* size of pause between successive GCs */
#define VMK_GCPSTEPMUL		4  /* GC "speed" */
#define VMK_GCPSTEPSIZE		5  /* GC granularity */

/* number of parameters */
#define VMK_GCPN		6


VMK_API int (vmk_gc) (vmk_State *L, int what, ...);


/*
** miscellaneous functions
*/

VMK_API int   (vmk_error) (vmk_State *L);

VMK_API int   (vmk_next) (vmk_State *L, int idx);

VMK_API void  (vmk_concat) (vmk_State *L, int n);
VMK_API void  (vmk_len)    (vmk_State *L, int idx);

#define VMK_N2SBUFFSZ	64
VMK_API unsigned  (vmk_numbertocstring) (vmk_State *L, int idx, char *buff);
VMK_API size_t  (vmk_stringtonumber) (vmk_State *L, const char *s);

VMK_API vmk_Alloc (vmk_getallocf) (vmk_State *L, void **ud);
VMK_API void      (vmk_setallocf) (vmk_State *L, vmk_Alloc f, void *ud);

VMK_API void (vmk_toclose) (vmk_State *L, int idx);
VMK_API void (vmk_closeslot) (vmk_State *L, int idx);


/*
** {==============================================================
** some useful macros
** ===============================================================
*/

#define vmk_getextraspace(L)	((void *)((char *)(L) - VMK_EXTRASPACE))

#define vmk_tonumber(L,i)	vmk_tonumberx(L,(i),NULL)
#define vmk_tointeger(L,i)	vmk_tointegerx(L,(i),NULL)

#define vmk_pop(L,n)		vmk_settop(L, -(n)-1)

#define vmk_newtable(L)		vmk_createtable(L, 0, 0)

#define vmk_register(L,n,f) (vmk_pushcfunction(L, (f)), vmk_setglobal(L, (n)))

#define vmk_pushcfunction(L,f)	vmk_pushcclosure(L, (f), 0)

#define vmk_isfunction(L,n)	(vmk_type(L, (n)) == VMK_TFUNCTION)
#define vmk_istable(L,n)	(vmk_type(L, (n)) == VMK_TTABLE)
#define vmk_islightuserdata(L,n)	(vmk_type(L, (n)) == VMK_TLIGHTUSERDATA)
#define vmk_isnil(L,n)		(vmk_type(L, (n)) == VMK_TNIL)
#define vmk_isboolean(L,n)	(vmk_type(L, (n)) == VMK_TBOOLEAN)
#define vmk_isthread(L,n)	(vmk_type(L, (n)) == VMK_TTHREAD)
#define vmk_isnone(L,n)		(vmk_type(L, (n)) == VMK_TNONE)
#define vmk_isnoneornil(L, n)	(vmk_type(L, (n)) <= 0)

#define vmk_pushliteral(L, s)	vmk_pushstring(L, "" s)

#define vmk_pushglobaltable(L)  \
	((void)vmk_rawgeti(L, VMK_REGISTRYINDEX, VMK_RIDX_GLOBALS))

#define vmk_tostring(L,i)	vmk_tolstring(L, (i), NULL)


#define vmk_insert(L,idx)	vmk_rotate(L, (idx), 1)

#define vmk_remove(L,idx)	(vmk_rotate(L, (idx), -1), vmk_pop(L, 1))

#define vmk_replace(L,idx)	(vmk_copy(L, -1, (idx)), vmk_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros
** ===============================================================
*/
#if defined(VMK_COMPAT_APIINTCASTS)

#define vmk_pushunsigned(L,n)	vmk_pushinteger(L, (vmk_Integer)(n))
#define vmk_tounsignedx(L,i,is)	((vmk_Unsigned)vmk_tointegerx(L,i,is))
#define vmk_tounsigned(L,i)	vmk_tounsignedx(L,(i),NULL)

#endif

#define vmk_newuserdata(L,s)	vmk_newuserdatauv(L,s,1)
#define vmk_getuservalue(L,idx)	vmk_getiuservalue(L,idx,1)
#define vmk_setuservalue(L,idx)	vmk_setiuservalue(L,idx,1)

#define vmk_resetthread(L)	vmk_closethread(L,NULL)

/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define VMK_HOOKCALL	0
#define VMK_HOOKRET	1
#define VMK_HOOKLINE	2
#define VMK_HOOKCOUNT	3
#define VMK_HOOKTAILCALL 4


/*
** Event masks
*/
#define VMK_MASKCALL	(1 << VMK_HOOKCALL)
#define VMK_MASKRET	(1 << VMK_HOOKRET)
#define VMK_MASKLINE	(1 << VMK_HOOKLINE)
#define VMK_MASKCOUNT	(1 << VMK_HOOKCOUNT)


VMK_API int (vmk_getstack) (vmk_State *L, int level, vmk_Debug *ar);
VMK_API int (vmk_getinfo) (vmk_State *L, const char *what, vmk_Debug *ar);
VMK_API const char *(vmk_getlocal) (vmk_State *L, const vmk_Debug *ar, int n);
VMK_API const char *(vmk_setlocal) (vmk_State *L, const vmk_Debug *ar, int n);
VMK_API const char *(vmk_getupvalue) (vmk_State *L, int funcindex, int n);
VMK_API const char *(vmk_setupvalue) (vmk_State *L, int funcindex, int n);

VMK_API void *(vmk_upvalueid) (vmk_State *L, int fidx, int n);
VMK_API void  (vmk_upvaluejoin) (vmk_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

VMK_API void (vmk_sethook) (vmk_State *L, vmk_Hook func, int mask, int count);
VMK_API vmk_Hook (vmk_gethook) (vmk_State *L);
VMK_API int (vmk_gethookmask) (vmk_State *L);
VMK_API int (vmk_gethookcount) (vmk_State *L);


struct vmk_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) 'global', 'lock', 'field', 'method' */
  const char *what;	/* (S) 'Vmk', 'C', 'main', 'tail' */
  const char *source;	/* (S) */
  size_t srclen;	/* (S) */
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  unsigned char nups;	/* (u) number of upvalues */
  unsigned char nparams;/* (u) number of parameters */
  char isvararg;        /* (u) */
  unsigned char extraargs;  /* (t) number of extra arguments */
  char istailcall;	/* (t) */
  int ftransfer;   /* (r) index of first value transferred */
  int ntransfer;   /* (r) number of transferred values */
  char short_src[VMK_IDSIZE]; /* (S) */
  /* private part */
  struct CallInfo *i_ci;  /* active fn */
};

/* }====================================================================== */


#define VMKI_TOSTRAUX(x)	#x
#define VMKI_TOSTR(x)		VMKI_TOSTRAUX(x)

#define VMK_VERSION_MAJOR	VMKI_TOSTR(VMK_VERSION_MAJOR_N)
#define VMK_VERSION_MINOR	VMKI_TOSTR(VMK_VERSION_MINOR_N)
#define VMK_VERSION_RELEASE	VMKI_TOSTR(VMK_VERSION_RELEASE_N)

#define VMK_VERSION	"Vmk " VMK_VERSION_MAJOR "." VMK_VERSION_MINOR
#define VMK_RELEASE	VMK_VERSION "." VMK_VERSION_RELEASE


/******************************************************************************
* Copyright (C) 2025 bitbucket.org/nginr/vmk-lang, KRS-Bdg.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
