/*
** $Id: lapi.h $
** Auxiliary functions from Vmk API
** See Copyright Notice in vmk.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


#if defined(VMK_USE_APICHECK)
#include <assert.h>
#define api_check(l,e,msg)	assert(e)
#else	/* for testing */
#define api_check(l,e,msg)	((void)(l), vmk_assert((e) && msg))
#endif



/* Increments 'L->top.p', checking for stack overflows */
#define api_incr_top(L)  \
    (L->top.p++, api_check(L, L->top.p <= L->ci->top.p, "stack overflow"))


/*
** macros that are executed whenever program enters the Vmk core
** ('vmk_lock') and leaves the core ('vmk_unlock')
*/
#if !defined(vmk_lock)
#define vmk_lock(L)	((void) 0)
#define vmk_unlock(L)	((void) 0)
#endif



/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this macro
** increases its stack space ('L->ci->top.p').
*/
#define adjustresults(L,nres) \
    { if ((nres) <= VMK_MULTRET && L->ci->top.p < L->top.p) \
	L->ci->top.p = L->top.p; }


/* Ensure the stack has at least 'n' elements */
#define api_checknelems(L,n) \
       api_check(L, (n) < (L->top.p - L->ci->func.p), \
                         "not enough elements in the stack")


/* Ensure the stack has at least 'n' elements to be popped. (Some
** functions only update a slot after checking it for popping, but that
** is only an optimization for a pop followed by a push.)
*/
#define api_checkpop(L,n) \
	api_check(L, (n) < L->top.p - L->ci->func.p &&  \
                     L->tbclist.p < L->top.p - (n), \
			  "not enough free elements in the stack")

#endif
