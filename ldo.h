/*
** $Id: ldo.h $
** Stack and Call structure of Vmk
** See Copyright Notice in vmk.h
*/

#ifndef ldo_h
#define ldo_h


#include "llimits.h"
#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** It also allows the running of one GC step when the stack is
** reallocated.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/

#if !defined(HARDSTACKTESTS)
#define condmovestack(L,pre,pos)	((void)0)
#else
/* realloc stack keeping its size */
#define condmovestack(L,pre,pos)  \
  { int sz_ = stacksize(L); pre; vmkD_reallocstack((L), sz_, 0); pos; }
#endif

#define vmkD_checkstackaux(L,n,pre,pos)  \
	if (l_unlikely(L->stack_last.p - L->top.p <= (n))) \
	  { pre; vmkD_growstack(L, n, 1); pos; } \
	else { condmovestack(L,pre,pos); }

/* In general, 'pre'/'pos' are empty (nothing to save) */
#define vmkD_checkstack(L,n)	vmkD_checkstackaux(L,n,(void)0,(void)0)



#define savestack(L,pt)		(cast_charp(pt) - cast_charp(L->stack.p))
#define restorestack(L,n)	cast(StkId, cast_charp(L->stack.p) + (n))


/* macro to check stack size, preserving 'p' */
#define checkstackp(L,n,p)  \
  vmkD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p),  /* save 'p' */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/*
** Maximum depth for nested C calls, syntactical nested non-terminals,
** and other features implemented through recursion in C. (Value must
** fit in a 16-bit unsigned integer. It must also be compatible with
** the size of the C stack.)
*/
#if !defined(VMKI_MAXCCALLS)
#define VMKI_MAXCCALLS		200
#endif


/* type of protected functions, to be ran by 'runprotected' */
typedef void (*Pfunc) (vmk_State *L, void *ud);

VMKI_FUNC void vmkD_seterrorobj (vmk_State *L, int errcode, StkId oldtop);
VMKI_FUNC int vmkD_protectedparser (vmk_State *L, ZIO *z, const char *name,
                                                  const char *mode);
VMKI_FUNC void vmkD_hook (vmk_State *L, int event, int line,
                                        int fTransfer, int nTransfer);
VMKI_FUNC void vmkD_hookcall (vmk_State *L, CallInfo *ci);
VMKI_FUNC int vmkD_pretailcall (vmk_State *L, CallInfo *ci, StkId func,
                                              int narg1, int delta);
VMKI_FUNC CallInfo *vmkD_precall (vmk_State *L, StkId func, int nResults);
VMKI_FUNC void vmkD_call (vmk_State *L, StkId func, int nResults);
VMKI_FUNC void vmkD_callnoyield (vmk_State *L, StkId func, int nResults);
VMKI_FUNC int vmkD_closeprotected (vmk_State *L, ptrdiff_t level, int status);
VMKI_FUNC int vmkD_pcall (vmk_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
VMKI_FUNC void vmkD_poscall (vmk_State *L, CallInfo *ci, int nres);
VMKI_FUNC int vmkD_reallocstack (vmk_State *L, int newsize, int raiseerror);
VMKI_FUNC int vmkD_growstack (vmk_State *L, int n, int raiseerror);
VMKI_FUNC void vmkD_shrinkstack (vmk_State *L);
VMKI_FUNC void vmkD_inctop (vmk_State *L);

VMKI_FUNC l_noret vmkD_throw (vmk_State *L, int errcode);
VMKI_FUNC int vmkD_rawrunprotected (vmk_State *L, Pfunc f, void *ud);

#endif

