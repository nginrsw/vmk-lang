/*
** $Id: ldo.c $
** Stack and Call structure of Vmk
** See Copyright Notice in vmk.h
*/

#define ldo_c
#define VMK_CORE

#include "lprefix.h"


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"



#define errorstatus(s)	((s) > VMK_YIELD)


/*
** these macros allow user-specific actions when a thread is
** resumed/yielded.
*/
#if !defined(vmki_userstateresume)
#define vmki_userstateresume(L,n)	((void)L)
#endif

#if !defined(vmki_userstateyield)
#define vmki_userstateyield(L,n)	((void)L)
#endif


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** VMKI_THROW/VMKI_TRY define how Vmk does exception handling. By
** default, Vmk handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(VMKI_THROW)				/* { */

#if defined(__cplusplus) && !defined(VMK_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define VMKI_THROW(L,c)		throw(c)
#define VMKI_TRY(L,c,f,ud) \
    try { (f)(L, ud); } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define vmki_jmpbuf		int  /* dummy field */

#elif defined(VMK_USE_POSIX)				/* }{ */

/* in POSIX, try _longjmp/_setjmp (more efficient) */
#define VMKI_THROW(L,c)		_longjmp((c)->b, 1)
#define VMKI_TRY(L,c,f,ud)	if (_setjmp((c)->b) == 0) ((f)(L, ud))
#define vmki_jmpbuf		jmp_buf

#else							/* }{ */

/* ISO C handling with long jumps */
#define VMKI_THROW(L,c)		longjmp((c)->b, 1)
#define VMKI_TRY(L,c,f,ud)	if (setjmp((c)->b) == 0) ((f)(L, ud))
#define vmki_jmpbuf		jmp_buf

#endif							/* } */

#endif							/* } */



/* chain list of long jump buffers */
struct vmk_longjmp {
  struct vmk_longjmp *previous;
  vmki_jmpbuf b;
  volatile int status;  /* error code */
};


void vmkD_seterrorobj (vmk_State *L, int errcode, StkId oldtop) {
  switch (errcode) {
    case VMK_ERRMEM: {  /* memory error? */
      setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
      break;
    }
    case VMK_ERRERR: {
      setsvalue2s(L, oldtop, vmkS_newliteral(L, "error in error handling"));
      break;
    }
    case VMK_OK: {  /* special case only for closing upvalues */
      setnilvalue(s2v(oldtop));  /* no error message */
      break;
    }
    default: {
      vmk_assert(errorstatus(errcode));  /* real error */
      setobjs2s(L, oldtop, L->top.p - 1);  /* error message on current top */
      break;
    }
  }
  L->top.p = oldtop + 1;
}


l_noret vmkD_throw (vmk_State *L, int errcode) {
  if (L->errorJmp) {  /* thread has an error handler? */
    L->errorJmp->status = errcode;  /* set status */
    VMKI_THROW(L, L->errorJmp);  /* jump to it */
  }
  else {  /* thread has no error handler */
    global_State *g = G(L);
    errcode = vmkE_resetthread(L, errcode);  /* close all upvalues */
    L->status = cast_byte(errcode);
    if (g->mainthread->errorJmp) {  /* main thread has a handler? */
      setobjs2s(L, g->mainthread->top.p++, L->top.p - 1);  /* copy error obj. */
      vmkD_throw(g->mainthread, errcode);  /* re-throw in main thread */
    }
    else {  /* no handler at all; abort */
      if (g->panic) {  /* panic fn? */
        vmk_unlock(L);
        g->panic(L);  /* call panic fn (last chance to jump out) */
      }
      abort();
    }
  }
}


int vmkD_rawrunprotected (vmk_State *L, Pfunc f, void *ud) {
  l_uint32 oldnCcalls = L->nCcalls;
  struct vmk_longjmp lj;
  lj.status = VMK_OK;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  VMKI_TRY(L, &lj, f, ud);  /* call 'f' catching errors */
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }====================================================== */


/*
** {==================================================================
** Stack reallocation
** ===================================================================
*/

/* some stack space for error handling */
#define STACKERRSPACE	200


/* maximum stack size that respects size_t */
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

/*
** Minimum between VMKI_MAXSTACK and MAXSTACK_BYSIZET
** (Maximum size for the stack must respect size_t.)
*/
#define MAXSTACK	cast_int(VMKI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? VMKI_MAXSTACK : MAXSTACK_BYSIZET)


/* stack size with extra space for error handling */
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


/*
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before a stack reallocation, all pointers are
** changed to offsets, and after the reallocation they are changed back
** to pointers. As during the reallocation the pointers are invalid, the
** reallocation cannot run emergency collections.
**
*/

#if 1
/*
** Change all pointers to the stack into offsets.
*/
static void relstack (vmk_State *L) {
  CallInfo *ci;
  UpVal *up;
  L->top.offset = savestack(L, L->top.p);
  L->tbclist.offset = savestack(L, L->tbclist.p);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.offset = savestack(L, uplevel(up));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.offset = savestack(L, ci->top.p);
    ci->func.offset = savestack(L, ci->func.p);
  }
}


/*
** Change back all offsets into pointers.
*/
static void correctstack (vmk_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  UNUSED(oldstack);
  L->top.p = restorestack(L, L->top.offset);
  L->tbclist.p = restorestack(L, L->tbclist.offset);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(restorestack(L, up->v.offset));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = restorestack(L, ci->top.offset);
    ci->func.p = restorestack(L, ci->func.offset);
    if (isVmk(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'vmkV_execute' */
  }
}

#else
/*
** Alternatively, we can use the old address after the deallocation.
** That is not strict ISO C, but seems to work fine everywhere.
*/

static void relstack (vmk_State *L) { UNUSED(L); }

static void correctstack (vmk_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  StkId newstack = L->stack.p;
  if (oldstack == newstack)
    return;
  L->top.p = L->top.p - oldstack + newstack;
  L->tbclist.p = L->tbclist.p - oldstack + newstack;
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(uplevel(up) - oldstack + newstack);
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = ci->top.p - oldstack + newstack;
    ci->func.p = ci->func.p - oldstack + newstack;
    if (isVmk(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'vmkV_execute' */
  }
}

#endif


/*
** Reallocate the stack to a new size, correcting all pointers into it.
** In case of allocation error, raise an error or return false according
** to 'raiseerror'.
*/
int vmkD_reallocstack (vmk_State *L, int newsize, int raiseerror) {
  int oldsize = stacksize(L);
  int i;
  StkId newstack;
  StkId oldstack = L->stack.p;
  lu_byte oldgcstop = G(L)->gcstopem;
  vmk_assert(newsize <= MAXSTACK || newsize == ERRORSTACKSIZE);
  relstack(L);  /* change pointers to offsets */
  G(L)->gcstopem = 1;  /* stop emergency collection */
  newstack = vmkM_reallocvector(L, oldstack, oldsize + EXTRA_STACK,
                                   newsize + EXTRA_STACK, StackValue);
  G(L)->gcstopem = oldgcstop;  /* restore emergency collection */
  if (l_unlikely(newstack == NULL)) {  /* reallocation failed? */
    correctstack(L, oldstack);  /* change offsets back to pointers */
    if (raiseerror)
      vmkM_error(L);
    else return 0;  /* do not raise an error */
  }
  L->stack.p = newstack;
  correctstack(L, oldstack);  /* change offsets back to pointers */
  L->stack_last.p = L->stack.p + newsize;
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i)); /* erase new segment */
  return 1;
}


/*
** Try to grow the stack by at least 'n' elements. When 'raiseerror'
** is true, raises any error; otherwise, return 0 in case of errors.
*/
int vmkD_growstack (vmk_State *L, int n, int raiseerror) {
  int size = stacksize(L);
  if (l_unlikely(size > MAXSTACK)) {
    /* if stack is larger than maximum, thread is already using the
       extra space reserved for errors, that is, thread is handling
       a stack error; cannot grow further than that. */
    vmk_assert(stacksize(L) == ERRORSTACKSIZE);
    if (raiseerror)
      vmkD_throw(L, VMK_ERRERR);  /* error inside message handler */
    return 0;  /* if not 'raiseerror', just signal it */
  }
  else if (n < MAXSTACK) {  /* avoids arithmetic overflows */
    int newsize = 2 * size;  /* tentative new size */
    int needed = cast_int(L->top.p - L->stack.p) + n;
    if (newsize > MAXSTACK)  /* cannot cross the limit */
      newsize = MAXSTACK;
    if (newsize < needed)  /* but must respect what was asked for */
      newsize = needed;
    if (l_likely(newsize <= MAXSTACK))
      return vmkD_reallocstack(L, newsize, raiseerror);
  }
  /* else stack overflow */
  /* add extra size to be able to handle the error message */
  vmkD_reallocstack(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    vmkG_runerror(L, "stack overflow");
  return 0;
}


/*
** Compute how much of the stack is being used, by computing the
** maximum top of all call frames in the stack and the current top.
*/
static int stackinuse (vmk_State *L) {
  CallInfo *ci;
  int res;
  StkId lim = L->top.p;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    if (lim < ci->top.p) lim = ci->top.p;
  }
  vmk_assert(lim <= L->stack_last.p + EXTRA_STACK);
  res = cast_int(lim - L->stack.p) + 1;  /* part of stack in use */
  if (res < VMK_MINSTACK)
    res = VMK_MINSTACK;  /* ensure a minimum size */
  return res;
}


/*
** If stack size is more than 3 times the current use, reduce that size
** to twice the current use. (So, the final stack size is at most 2/3 the
** previous size, and half of its entries are empty.)
** As a particular case, if stack was handling a stack overflow and now
** it is not, 'max' (limited by MAXSTACK) will be smaller than
** stacksize (equal to ERRORSTACKSIZE in this case), and so the stack
** will be reduced to a "regular" size.
*/
void vmkD_shrinkstack (vmk_State *L) {
  int inuse = stackinuse(L);
  int max = (inuse > MAXSTACK / 3) ? MAXSTACK : inuse * 3;
  /* if thread is currently not handling a stack overflow and its
     size is larger than maximum "reasonable" size, shrink it */
  if (inuse <= MAXSTACK && stacksize(L) > max) {
    int nsize = (inuse > MAXSTACK / 2) ? MAXSTACK : inuse * 2;
    vmkD_reallocstack(L, nsize, 0);  /* ok if that fails */
  }
  else  /* don't change stack */
    condmovestack(L,(void)0,(void)0);  /* (change only for debugging) */
  vmkE_shrinkCI(L);  /* shrink CI list */
}


void vmkD_inctop (vmk_State *L) {
  L->top.p++;
  vmkD_checkstack(L, 1);
}

/* }================================================================== */


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which trigger this
** fn, can be changed asynchronously by signals.)
*/
void vmkD_hook (vmk_State *L, int event, int line,
                              int ftransfer, int ntransfer) {
  vmk_Hook hook = L->hook;
  if (hook && L->allowhook) {  /* make sure there is a hook */
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top.p);  /* preserve original 'top' */
    ptrdiff_t ci_top = savestack(L, ci->top.p);  /* idem for 'ci->top' */
    vmk_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    L->transferinfo.ftransfer = ftransfer;
    L->transferinfo.ntransfer = ntransfer;
    if (isVmk(ci) && L->top.p < ci->top.p)
      L->top.p = ci->top.p;  /* protect entire activation register */
    vmkD_checkstack(L, VMK_MINSTACK);  /* ensure minimum stack size */
    if (ci->top.p < L->top.p + VMK_MINSTACK)
      ci->top.p = L->top.p + VMK_MINSTACK;
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= CIST_HOOKED;
    vmk_unlock(L);
    (*hook)(L, &ar);
    vmk_lock(L);
    vmk_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top.p = restorestack(L, ci_top);
    L->top.p = restorestack(L, top);
    ci->callstatus &= ~CIST_HOOKED;
  }
}


/*
** Executes a call hook for Vmk functions. This fn is called
** whenever 'hookmask' is not zero, so it checks whether call hooks are
** active.
*/
void vmkD_hookcall (vmk_State *L, CallInfo *ci) {
  L->oldpc = 0;  /* set 'oldpc' for new fn */
  if (L->hookmask & VMK_MASKCALL) {  /* is call hook on? */
    int event = (ci->callstatus & CIST_TAIL) ? VMK_HOOKTAILCALL
                                             : VMK_HOOKCALL;
    Proto *p = ci_func(ci)->p;
    ci->u.l.savedpc++;  /* hooks assume 'pc' is already incremented */
    vmkD_hook(L, event, -1, 1, p->numparams);
    ci->u.l.savedpc--;  /* correct 'pc' */
  }
}


/*
** Executes a return hook for Vmk and C functions and sets/corrects
** 'oldpc'. (Note that this correction is needed by the line hook, so it
** is done even when return hooks are off.)
*/
static void rethook (vmk_State *L, CallInfo *ci, int nres) {
  if (L->hookmask & VMK_MASKRET) {  /* is return hook on? */
    StkId firstres = L->top.p - nres;  /* index of first result */
    int delta = 0;  /* correction for vararg functions */
    int ftransfer;
    if (isVmk(ci)) {
      Proto *p = ci_func(ci)->p;
      if (p->flag & PF_ISVARARG)
        delta = ci->u.l.nextraargs + p->numparams + 1;
    }
    ci->func.p += delta;  /* if vararg, back to virtual 'func' */
    ftransfer = cast_int(firstres - ci->func.p);
    vmkD_hook(L, VMK_HOOKRET, -1, ftransfer, nres);  /* call it */
    ci->func.p -= delta;
  }
  if (isVmk(ci = ci->previous))
    L->oldpc = pcRel(ci->u.l.savedpc, ci_func(ci)->p);  /* set 'oldpc' */
}


/*
** Check whether 'func' has a '__call' metafield. If so, put it in the
** stack, below original 'func', so that 'vmkD_precall' can call it.
** Raise an error if there is no '__call' metafield.
** Bits CIST_CCMT in status count how many _call metamethods were
** invoked and how many corresponding extra arguments were pushed.
** (This count will be saved in the 'callstatus' of the call).
**  Raise an error if this counter overflows.
*/
static unsigned tryfuncTM (vmk_State *L, StkId func, unsigned status) {
  const TValue *tm;
  StkId p;
  tm = vmkT_gettmbyobj(L, s2v(func), TM_CALL);
  if (l_unlikely(ttisnil(tm)))  /* no metamethod? */
    vmkG_callerror(L, s2v(func));
  for (p = L->top.p; p > func; p--)  /* open space for metamethod */
    setobjs2s(L, p, p-1);
  L->top.p++;  /* stack space pre-allocated by the caller */
  setobj2s(L, func, tm);  /* metamethod is the new fn to be called */
  if ((status & MAX_CCMT) == MAX_CCMT)  /* is counter full? */
    vmkG_runerror(L, "'__call' chain too long");
  return status + (1u << CIST_CCMT);  /* increment counter */
}


/* Generic case for 'moveresult' */
l_sinline void genmoveresults (vmk_State *L, StkId res, int nres,
                                             int wanted) {
  StkId firstresult = L->top.p - nres;  /* index of first result */
  int i;
  if (nres > wanted)  /* extra results? */
    nres = wanted;  /* don't need them */
  for (i = 0; i < nres; i++)  /* move all results to correct place */
    setobjs2s(L, res + i, firstresult + i);
  for (; i < wanted; i++)  /* complete wanted number of results */
    setnilvalue(s2v(res + i));
  L->top.p = res + wanted;  /* top points after the last result */
}


/*
** Given 'nres' results at 'firstResult', move 'fwanted-1' of them
** to 'res'.  Handle most typical cases (zero results for commands,
** one result for expressions, multiple results for tail calls/single
** parameters) separated. The flag CIST_TBC in 'fwanted', if set,
** forces the swicth to go to the default case.
*/
l_sinline void moveresults (vmk_State *L, StkId res, int nres,
                                          l_uint32 fwanted) {
  switch (fwanted) {  /* handle typical cases separately */
    case 0 + 1:  /* no values needed */
      L->top.p = res;
      return;
    case 1 + 1:  /* one value needed */
      if (nres == 0)   /* no results? */
        setnilvalue(s2v(res));  /* adjust with nil */
      else  /* at least one result */
        setobjs2s(L, res, L->top.p - nres);  /* move it to proper place */
      L->top.p = res + 1;
      return;
    case VMK_MULTRET + 1:
      genmoveresults(L, res, nres, nres);  /* we want all results */
      break;
    default: {  /* two/more results and/or to-be-closed variables */
      int wanted = get_nresults(fwanted);
      if (fwanted & CIST_TBC) {  /* to-be-closed variables? */
        L->ci->u2.nres = nres;
        L->ci->callstatus |= CIST_CLSRET;  /* in case of yields */
        res = vmkF_close(L, res, CLOSEKTOP, 1);
        L->ci->callstatus &= ~CIST_CLSRET;
        if (L->hookmask) {  /* if needed, call hook after '__close's */
          ptrdiff_t savedres = savestack(L, res);
          rethook(L, L->ci, nres);
          res = restorestack(L, savedres);  /* hook can move stack */
        }
        if (wanted == VMK_MULTRET)
          wanted = nres;  /* we want all results */
      }
      genmoveresults(L, res, nres, wanted);
      break;
    }
  }
}


/*
** Finishes a fn call: calls hook if necessary, moves current
** number of results to proper place, and returns to previous call
** info. If fn has to close variables, hook must be called after
** that.
*/
void vmkD_poscall (vmk_State *L, CallInfo *ci, int nres) {
  l_uint32 fwanted = ci->callstatus & (CIST_TBC | CIST_NRESULTS);
  if (l_unlikely(L->hookmask) && !(fwanted & CIST_TBC))
    rethook(L, ci, nres);
  /* move results to proper place */
  moveresults(L, ci->func.p, nres, fwanted);
  /* fn cannot be in any of these cases when returning */
  vmk_assert(!(ci->callstatus &
        (CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_CLSRET)));
  L->ci = ci->previous;  /* back to caller (after closing variables) */
}



#define next_ci(L)  (L->ci->next ? L->ci->next : vmkE_extendCI(L))


/*
** Allocate and initialize CallInfo structure. At this point, the
** only valid fields in the call status are number of results,
** CIST_C (if it's a C fn), and number of extra arguments.
** (All these bit-fields fit in 16-bit values.)
*/
l_sinline CallInfo *prepCallInfo (vmk_State *L, StkId func, unsigned status,
                                                StkId top) {
  CallInfo *ci = L->ci = next_ci(L);  /* new frame */
  ci->func.p = func;
  vmk_assert((status & ~(CIST_NRESULTS | CIST_C | MAX_CCMT)) == 0);
  ci->callstatus = status;
  ci->top.p = top;
  return ci;
}


/*
** precall for C functions
*/
l_sinline int precallC (vmk_State *L, StkId func, unsigned status,
                                            vmk_CFunction f) {
  int n;  /* number of returns */
  CallInfo *ci;
  checkstackp(L, VMK_MINSTACK, func);  /* ensure minimum stack size */
  L->ci = ci = prepCallInfo(L, func, status | CIST_C,
                               L->top.p + VMK_MINSTACK);
  vmk_assert(ci->top.p <= L->stack_last.p);
  if (l_unlikely(L->hookmask & VMK_MASKCALL)) {
    int narg = cast_int(L->top.p - func) - 1;
    vmkD_hook(L, VMK_HOOKCALL, -1, 1, narg);
  }
  vmk_unlock(L);
  n = (*f)(L);  /* do the actual call */
  vmk_lock(L);
  api_checknelems(L, n);
  vmkD_poscall(L, ci, n);
  return n;
}


/*
** Prepare a fn for a tail call, building its call info on top
** of the current call info. 'narg1' is the number of arguments plus 1
** (so that it includes the fn itself). Return the number of
** results, if it was a C fn, or -1 for a Vmk fn.
*/
int vmkD_pretailcall (vmk_State *L, CallInfo *ci, StkId func,
                                    int narg1, int delta) {
  unsigned status = VMK_MULTRET + 1;
 retry:
  switch (ttypetag(s2v(func))) {
    case VMK_VCCL:  /* C closure */
      return precallC(L, func, status, clCvalue(s2v(func))->f);
    case VMK_VLCF:  /* light C fn */
      return precallC(L, func, status, fvalue(s2v(func)));
    case VMK_VLCL: {  /* Vmk fn */
      Proto *p = clLvalue(s2v(func))->p;
      int fsize = p->maxstacksize;  /* frame size */
      int nfixparams = p->numparams;
      int i;
      checkstackp(L, fsize - delta, func);
      ci->func.p -= delta;  /* restore 'func' (if vararg) */
      for (i = 0; i < narg1; i++)  /* move down fn and arguments */
        setobjs2s(L, ci->func.p + i, func + i);
      func = ci->func.p;  /* moved-down fn */
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));  /* complete missing arguments */
      ci->top.p = func + 1 + fsize;  /* top for new fn */
      vmk_assert(ci->top.p <= L->stack_last.p);
      ci->u.l.savedpc = p->code;  /* starting point */
      ci->callstatus |= CIST_TAIL;
      L->top.p = func + narg1;  /* set top */
      return -1;
    }
    default: {  /* not a fn */
      checkstackp(L, 1, func);  /* space for metamethod */
      status = tryfuncTM(L, func, status);  /* try '__call' metamethod */
      narg1++;
      goto retry;  /* try again */
    }
  }
}


/*
** Prepares the call to a fn (C or Vmk). For C functions, also do
** the call. The fn to be called is at '*func'.  The arguments
** are on the stack, right after the fn.  Returns the CallInfo
** to be executed, if it was a Vmk fn. Otherwise (a C fn)
** returns NULL, with all the results on the stack, starting at the
** original fn position.
*/
CallInfo *vmkD_precall (vmk_State *L, StkId func, int nresults) {
  unsigned status = cast_uint(nresults + 1);
  vmk_assert(status <= MAXRESULTS + 1);
 retry:
  switch (ttypetag(s2v(func))) {
    case VMK_VCCL:  /* C closure */
      precallC(L, func, status, clCvalue(s2v(func))->f);
      return NULL;
    case VMK_VLCF:  /* light C fn */
      precallC(L, func, status, fvalue(s2v(func)));
      return NULL;
    case VMK_VLCL: {  /* Vmk fn */
      CallInfo *ci;
      Proto *p = clLvalue(s2v(func))->p;
      int narg = cast_int(L->top.p - func) - 1;  /* number of real arguments */
      int nfixparams = p->numparams;
      int fsize = p->maxstacksize;  /* frame size */
      checkstackp(L, fsize, func);
      L->ci = ci = prepCallInfo(L, func, status, func + 1 + fsize);
      ci->u.l.savedpc = p->code;  /* starting point */
      for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));  /* complete missing arguments */
      vmk_assert(ci->top.p <= L->stack_last.p);
      return ci;
    }
    default: {  /* not a fn */
      checkstackp(L, 1, func);  /* space for metamethod */
      status = tryfuncTM(L, func, status);  /* try '__call' metamethod */
      goto retry;  /* try again with metamethod */
    }
  }
}


/*
** Call a fn (C or Vmk) through C. 'inc' can be 1 (increment
** number of recursive invocations in the C stack) or nyci (the same
** plus increment number of non-yieldable calls).
** This fn can be called with some use of EXTRA_STACK, so it should
** check the stack before doing anything else. 'vmkD_precall' already
** does that.
*/
l_sinline void ccall (vmk_State *L, StkId func, int nResults, l_uint32 inc) {
  CallInfo *ci;
  L->nCcalls += inc;
  if (l_unlikely(getCcalls(L) >= VMKI_MAXCCALLS)) {
    checkstackp(L, 0, func);  /* free any use of EXTRA_STACK */
    vmkE_checkcstack(L);
  }
  if ((ci = vmkD_precall(L, func, nResults)) != NULL) {  /* Vmk fn? */
    ci->callstatus |= CIST_FRESH;  /* mark that it is a "fresh" execute */
    vmkV_execute(L, ci);  /* call it */
  }
  L->nCcalls -= inc;
}


/*
** External interface for 'ccall'
*/
void vmkD_call (vmk_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, 1);
}


/*
** Similar to 'vmkD_call', but does not allow yields during the call.
*/
void vmkD_callnoyield (vmk_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, nyci);
}


/*
** Finish the job of 'vmk_pcallk' after it was interrupted by an yield.
** (The caller, 'finishCcall', does the final call to 'adjustresults'.)
** The main job is to complete the 'vmkD_pcall' called by 'vmk_pcallk'.
** If a '__close' method yields here, eventually control will be back
** to 'finishCcall' (when that '__close' method finally returns) and
** 'finishpcallk' will run again and close any still pending '__close'
** methods. Similarly, if a '__close' method errs, 'precover' calls
** 'unroll' which calls ''finishCcall' and we are back here again, to
** close any pending '__close' methods.
** Note that, up to the call to 'vmkF_close', the corresponding
** 'CallInfo' is not modified, so that this repeated run works like the
** first one (except that it has at least one less '__close' to do). In
** particular, field CIST_RECST preserves the error status across these
** multiple runs, changing only if there is a new error.
*/
static int finishpcallk (vmk_State *L,  CallInfo *ci) {
  int status = getcistrecst(ci);  /* get original status */
  if (l_likely(status == VMK_OK))  /* no error? */
    status = VMK_YIELD;  /* was interrupted by an yield */
  else {  /* error */
    StkId func = restorestack(L, ci->u2.funcidx);
    L->allowhook = getoah(ci);  /* restore 'allowhook' */
    func = vmkF_close(L, func, status, 1);  /* can yield or raise an error */
    vmkD_seterrorobj(L, status, func);
    vmkD_shrinkstack(L);   /* restore stack size in case of overflow */
    setcistrecst(ci, VMK_OK);  /* clear original status */
  }
  ci->callstatus &= ~CIST_YPCALL;
  L->errfunc = ci->u.c.old_errfunc;
  /* if it is here, there were errors or yields; unlike 'vmk_pcallk',
     do not change status */
  return status;
}


/*
** Completes the execution of a C fn interrupted by an yield.
** The interruption must have happened while the fn was either
** closing its tbc variables in 'moveresults' or executing
** 'vmk_callk'/'vmk_pcallk'. In the first case, it just redoes
** 'vmkD_poscall'. In the second case, the call to 'finishpcallk'
** finishes the interrupted execution of 'vmk_pcallk'.  After that, it
** calls the continuation of the interrupted fn and finally it
** completes the job of the 'vmkD_call' that called the fn.  In
** the call to 'adjustresults', we do not know the number of results
** of the fn called by 'vmk_callk'/'vmk_pcallk', so we are
** conservative and use VMK_MULTRET (always adjust).
*/
static void finishCcall (vmk_State *L, CallInfo *ci) {
  int n;  /* actual number of results from C fn */
  if (ci->callstatus & CIST_CLSRET) {  /* was closing TBC variable? */
    vmk_assert(ci->callstatus & CIST_TBC);
    n = ci->u2.nres;  /* just redo 'vmkD_poscall' */
    /* don't need to reset CIST_CLSRET, as it will be set again anyway */
  }
  else {
    int status = VMK_YIELD;  /* default if there were no errors */
    /* must have a continuation and must be able to call it */
    vmk_assert(ci->u.c.k != NULL && yieldable(L));
    if (ci->callstatus & CIST_YPCALL)   /* was inside a 'vmk_pcallk'? */
      status = finishpcallk(L, ci);  /* finish it */
    adjustresults(L, VMK_MULTRET);  /* finish 'vmk_callk' */
    vmk_unlock(L);
    n = (*ci->u.c.k)(L, status, ci->u.c.ctx);  /* call continuation */
    vmk_lock(L);
    api_checknelems(L, n);
  }
  vmkD_poscall(L, ci, n);  /* finish 'vmkD_call' */
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop).
*/
static void unroll (vmk_State *L, void *ud) {
  CallInfo *ci;
  UNUSED(ud);
  while ((ci = L->ci) != &L->base_ci) {  /* something in the stack */
    if (!isVmk(ci))  /* C fn? */
      finishCcall(L, ci);  /* complete its execution */
    else {  /* Vmk fn */
      vmkV_finishOp(L);  /* finish interrupted instruction */
      vmkV_execute(L, ci);  /* execute down to higher C 'boundary' */
    }
  }
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallInfo *findpcall (vmk_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


/*
** Signal an error in the call to 'vmk_resume', not in the execution
** of the coroutine itself. (Such errors should not be handled by any
** coroutine error handler and should not kill the coroutine.)
*/
static int resume_error (vmk_State *L, const char *msg, int narg) {
  api_checkpop(L, narg);
  L->top.p -= narg;  /* remove args from the stack */
  setsvalue2s(L, L->top.p, vmkS_new(L, msg));  /* push error message */
  api_incr_top(L);
  vmk_unlock(L);
  return VMK_ERRRUN;
}


/*
** Do the work for 'vmk_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** fn), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume (vmk_State *L, void *ud) {
  int n = *(cast(int*, ud));  /* number of arguments */
  StkId firstArg = L->top.p - n;  /* first argument */
  CallInfo *ci = L->ci;
  if (L->status == VMK_OK)  /* starting a coroutine? */
    ccall(L, firstArg - 1, VMK_MULTRET, 0);  /* just call its body */
  else {  /* resuming from previous yield */
    vmk_assert(L->status == VMK_YIELD);
    L->status = VMK_OK;  /* mark that it is running (again) */
    if (isVmk(ci)) {  /* yielded inside a hook? */
      /* undo increment made by 'vmkG_traceexec': instruction was not
         executed yet */
      vmk_assert(ci->callstatus & CIST_HOOKYIELD);
      ci->u.l.savedpc--;
      L->top.p = firstArg;  /* discard arguments */
      vmkV_execute(L, ci);  /* just continue running Vmk code */
    }
    else {  /* 'common' yield */
      if (ci->u.c.k != NULL) {  /* does it have a continuation fn? */
        vmk_unlock(L);
        n = (*ci->u.c.k)(L, VMK_YIELD, ci->u.c.ctx); /* call continuation */
        vmk_lock(L);
        api_checknelems(L, n);
      }
      vmkD_poscall(L, ci, n);  /* finish 'vmkD_call' */
    }
    unroll(L, NULL);  /* run continuation */
  }
}


/*
** Unrolls a coroutine in protected mode while there are recoverable
** errors, that is, errors inside a protected call. (Any error
** interrupts 'unroll', and this loop protects it again so it can
** continue.) Stops with a normal end (status == VMK_OK), an yield
** (status == VMK_YIELD), or an unprotected error ('findpcall' doesn't
** find a recover point).
*/
static int precover (vmk_State *L, int status) {
  CallInfo *ci;
  while (errorstatus(status) && (ci = findpcall(L)) != NULL) {
    L->ci = ci;  /* go down to recovery functions */
    setcistrecst(ci, status);  /* status to finish 'pcall' */
    status = vmkD_rawrunprotected(L, unroll, NULL);
  }
  return status;
}


VMK_API int vmk_resume (vmk_State *L, vmk_State *from, int nargs,
                                      int *nresults) {
  int status;
  vmk_lock(L);
  if (L->status == VMK_OK) {  /* may be starting a coroutine */
    if (L->ci != &L->base_ci)  /* not in base level? */
      return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    else if (L->top.p - (L->ci->func.p + 1) == nargs)  /* no fn? */
      return resume_error(L, "cannot resume dead coroutine", nargs);
  }
  else if (L->status != VMK_YIELD)  /* ended with errors? */
    return resume_error(L, "cannot resume dead coroutine", nargs);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  if (getCcalls(L) >= VMKI_MAXCCALLS)
    return resume_error(L, "C stack overflow", nargs);
  L->nCcalls++;
  vmki_userstateresume(L, nargs);
  api_checkpop(L, (L->status == VMK_OK) ? nargs + 1 : nargs);
  status = vmkD_rawrunprotected(L, resume, &nargs);
   /* continue running after recoverable errors */
  status = precover(L, status);
  if (l_likely(!errorstatus(status)))
    vmk_assert(status == L->status);  /* normal end or yield */
  else {  /* unrecoverable error */
    L->status = cast_byte(status);  /* mark thread as 'dead' */
    vmkD_seterrorobj(L, status, L->top.p);  /* push error message */
    L->ci->top.p = L->top.p;
  }
  *nresults = (status == VMK_YIELD) ? L->ci->u2.nyield
                                    : cast_int(L->top.p - (L->ci->func.p + 1));
  vmk_unlock(L);
  return status;
}


VMK_API int vmk_isyieldable (vmk_State *L) {
  return yieldable(L);
}


VMK_API int vmk_yieldk (vmk_State *L, int nresults, vmk_KContext ctx,
                        vmk_KFunction k) {
  CallInfo *ci;
  vmki_userstateyield(L, nresults);
  vmk_lock(L);
  ci = L->ci;
  api_checkpop(L, nresults);
  if (l_unlikely(!yieldable(L))) {
    if (L != G(L)->mainthread)
      vmkG_runerror(L, "attempt to yield across a C-call boundary");
    else
      vmkG_runerror(L, "attempt to yield from outside a coroutine");
  }
  L->status = VMK_YIELD;
  ci->u2.nyield = nresults;  /* save number of results */
  if (isVmk(ci)) {  /* inside a hook? */
    vmk_assert(!isVmkcode(ci));
    api_check(L, nresults == 0, "hooks cannot yield values");
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->u.c.k = k) != NULL)  /* is there a continuation? */
      ci->u.c.ctx = ctx;  /* save context */
    vmkD_throw(L, VMK_YIELD);
  }
  vmk_assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  vmk_unlock(L);
  return 0;  /* return to 'vmkD_hook' */
}


/*
** Auxiliary structure to call 'vmkF_close' in protected mode.
*/
struct CloseP {
  StkId level;
  int status;
};


/*
** Auxiliary fn to call 'vmkF_close' in protected mode.
*/
static void closepaux (vmk_State *L, void *ud) {
  struct CloseP *pcl = cast(struct CloseP *, ud);
  vmkF_close(L, pcl->level, pcl->status, 0);
}


/*
** Calls 'vmkF_close' in protected mode. Return the original status
** or, in case of errors, the new status.
*/
int vmkD_closeprotected (vmk_State *L, ptrdiff_t level, int status) {
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  for (;;) {  /* keep closing upvalues until no more errors */
    struct CloseP pcl;
    pcl.level = restorestack(L, level); pcl.status = status;
    status = vmkD_rawrunprotected(L, &closepaux, &pcl);
    if (l_likely(status == VMK_OK))  /* no more errors? */
      return pcl.status;
    else {  /* an error occurred; restore saved state and repeat */
      L->ci = old_ci;
      L->allowhook = old_allowhooks;
    }
  }
}


/*
** Call the C fn 'func' in protected mode, restoring basic
** thread information ('allowhook', etc.) and in particular
** its stack level in case of errors.
*/
int vmkD_pcall (vmk_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = vmkD_rawrunprotected(L, func, u);
  if (l_unlikely(status != VMK_OK)) {  /* an error occurred? */
    L->ci = old_ci;
    L->allowhook = old_allowhooks;
    status = vmkD_closeprotected(L, old_top, status);
    vmkD_seterrorobj(L, status, restorestack(L, old_top));
    vmkD_shrinkstack(L);   /* restore stack size in case of overflow */
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
struct SParser {  /* data to 'f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};


static void checkmode (vmk_State *L, const char *mode, const char *x) {
  if (strchr(mode, x[0]) == NULL) {
    vmkO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    vmkD_throw(L, VMK_ERRSYNTAX);
  }
}


static void f_parser (vmk_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  const char *mode = p->mode ? p->mode : "bt";
  int c = zgetc(p->z);  /* read first character */
  if (c == VMK_SIGNATURE[0]) {
    int fixed = 0;
    if (strchr(mode, 'B') != NULL)
      fixed = 1;
    else
      checkmode(L, mode, "binary");
    cl = vmkU_undump(L, p->z, p->name, fixed);
  }
  else {
    checkmode(L, mode, "text");
    cl = vmkY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  vmk_assert(cl->nupvalues == cl->p->sizeupvalues);
  vmkF_initupvals(L, cl);
}


int vmkD_protectedparser (vmk_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  struct SParser p;
  int status;
  incnny(L);  /* cannot yield during parsing */
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  vmkZ_initbuffer(L, &p.buff);
  status = vmkD_pcall(L, f_parser, &p, savestack(L, L->top.p), L->errfunc);
  vmkZ_freebuffer(L, &p.buff);
  vmkM_freearray(L, p.dyd.actvar.arr, cast_sizet(p.dyd.actvar.size));
  vmkM_freearray(L, p.dyd.gt.arr, cast_sizet(p.dyd.gt.size));
  vmkM_freearray(L, p.dyd.label.arr, cast_sizet(p.dyd.label.size));
  decnny(L);
  return status;
}


