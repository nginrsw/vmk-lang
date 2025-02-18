/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in vmk.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


#define sizeCclosure(n)  \
	(offsetof(CClosure, upvalue) + sizeof(TValue) * cast_uint(n))

#define sizeLclosure(n)  \
	(offsetof(LClosure, upvals) + sizeof(UpVal *) * cast_uint(n))


/* test whether thread is in 'twups' list */
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Vmk). (Value
** must fit in a VM register.)
*/
#define MAXUPVAL	255


#define upisopen(up)	((up)->v.p != &(up)->u.value)


#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v.p))


/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
#define MAXMISS		10



/* special status to close upvalues preserving the top of the stack */
#define CLOSEKTOP	(-1)


VMKI_FUNC Proto *vmkF_newproto (vmk_State *L);
VMKI_FUNC CClosure *vmkF_newCclosure (vmk_State *L, int nupvals);
VMKI_FUNC LClosure *vmkF_newLclosure (vmk_State *L, int nupvals);
VMKI_FUNC void vmkF_initupvals (vmk_State *L, LClosure *cl);
VMKI_FUNC UpVal *vmkF_findupval (vmk_State *L, StkId level);
VMKI_FUNC void vmkF_newtbcupval (vmk_State *L, StkId level);
VMKI_FUNC void vmkF_closeupval (vmk_State *L, StkId level);
VMKI_FUNC StkId vmkF_close (vmk_State *L, StkId level, int status, int yy);
VMKI_FUNC void vmkF_unlinkupval (UpVal *uv);
VMKI_FUNC lu_mem vmkF_protosize (Proto *p);
VMKI_FUNC void vmkF_freeproto (vmk_State *L, Proto *f);
VMKI_FUNC const char *vmkF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
