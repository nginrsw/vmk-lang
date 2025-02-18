/*
** $Id: lstate.c $
** Global State
** See Copyright Notice in vmk.h
*/

#define lstate_c
#define VMK_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

#include "vmk.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"



/*
** thread state + extra space
*/
typedef struct LX {
  lu_byte extra_[VMK_EXTRASPACE];
  vmk_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** these macros allow user-specific actions when a thread is
** created/deleted
*/
#if !defined(vmki_userstateopen)
#define vmki_userstateopen(L)		((void)L)
#endif

#if !defined(vmki_userstateclose)
#define vmki_userstateclose(L)		((void)L)
#endif

#if !defined(vmki_userstatethread)
#define vmki_userstatethread(L,L1)	((void)L)
#endif

#if !defined(vmki_userstatefree)
#define vmki_userstatefree(L,L1)	((void)L)
#endif


/*
** set GCdebt to a new value keeping the real number of allocated
** objects (GCtotalobjs - GCdebt) invariant and avoiding overflows in
** 'GCtotalobjs'.
*/
void vmkE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);
  vmk_assert(tb > 0);
  if (debt > MAX_LMEM - tb)
    debt = MAX_LMEM - tb;  /* will make GCtotalbytes == MAX_LMEM */
  g->GCtotalbytes = tb + debt;
  g->GCdebt = debt;
}


CallInfo *vmkE_extendCI (vmk_State *L) {
  CallInfo *ci;
  vmk_assert(L->ci->next == NULL);
  ci = vmkM_new(L, CallInfo);
  vmk_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  ci->u.l.trap = 0;
  L->nci++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
static void freeCI (vmk_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    vmkM_free(L, ci);
    L->nci--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread,
** keeping the first one.
*/
void vmkE_shrinkCI (vmk_State *L) {
  CallInfo *ci = L->ci->next;  /* first free CallInfo */
  CallInfo *next;
  if (ci == NULL)
    return;  /* no extra elements */
  while ((next = ci->next) != NULL) {  /* two extra elements? */
    CallInfo *next2 = next->next;  /* next's next */
    ci->next = next2;  /* remove next from the list */
    L->nci--;
    vmkM_free(L, next);  /* free next */
    if (next2 == NULL)
      break;  /* no more elements */
    else {
      next2->previous = ci;
      ci = next2;  /* continue */
    }
  }
}


/*
** Called when 'getCcalls(L)' larger or equal to VMKI_MAXCCALLS.
** If equal, raises an overflow error. If value is larger than
** VMKI_MAXCCALLS (which means it is handling an overflow) but
** not much larger, does not report an error (to allow overflow
** handling to work).
*/
void vmkE_checkcstack (vmk_State *L) {
  if (getCcalls(L) == VMKI_MAXCCALLS)
    vmkG_runerror(L, "C stack overflow");
  else if (getCcalls(L) >= (VMKI_MAXCCALLS / 10 * 11))
    vmkD_throw(L, VMK_ERRERR);  /* error while handling stack error */
}


VMKI_FUNC void vmkE_incCstack (vmk_State *L) {
  L->nCcalls++;
  if (l_unlikely(getCcalls(L) >= VMKI_MAXCCALLS))
    vmkE_checkcstack(L);
}


static void stack_init (vmk_State *L1, vmk_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack.p = vmkM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  L1->tbclist.p = L1->stack.p;
  for (i = 0; i < BASIC_STACK_SIZE + EXTRA_STACK; i++)
    setnilvalue(s2v(L1->stack.p + i));  /* erase new stack */
  L1->top.p = L1->stack.p;
  L1->stack_last.p = L1->stack.p + BASIC_STACK_SIZE;
  /* initialize first ci */
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = CIST_C;
  ci->func.p = L1->top.p;
  ci->u.c.k = NULL;
  setnilvalue(s2v(L1->top.p));  /* 'fn' entry for this 'ci' */
  L1->top.p++;
  ci->top.p = L1->top.p + VMK_MINSTACK;
  L1->ci = ci;
}


static void freestack (vmk_State *L) {
  if (L->stack.p == NULL)
    return;  /* stack not completely built yet */
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  freeCI(L);
  vmk_assert(L->nci == 0);
  /* free stack */
  vmkM_freearray(L, L->stack.p, cast_sizet(stacksize(L) + EXTRA_STACK));
}


/*
** Create registry table and its predefined values
*/
static void init_registry (vmk_State *L, global_State *g) {
  /* create registry */
  TValue aux;
  Table *registry = vmkH_new(L);
  sethvalue(L, &g->l_registry, registry);
  vmkH_resize(L, registry, VMK_RIDX_LAST, 0);
  /* registry[1] = false */
  setbfvalue(&aux);
  vmkH_setint(L, registry, 1, &aux);
  /* registry[VMK_RIDX_MAINTHREAD] = L */
  setthvalue(L, &aux, L);
  vmkH_setint(L, registry, VMK_RIDX_MAINTHREAD, &aux);
  /* registry[VMK_RIDX_GLOBALS] = new table (table of globals) */
  sethvalue(L, &aux, vmkH_new(L));
  vmkH_setint(L, registry, VMK_RIDX_GLOBALS, &aux);
}


/*
** open parts of the state that may cause memory-allocation errors.
*/
static void f_vmkopen (vmk_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  vmkS_init(L);
  vmkT_init(L);
  vmkX_init(L);
  g->gcstp = 0;  /* allow gc */
  setnilvalue(&g->nilvalue);  /* now state is complete */
  vmki_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread (vmk_State *L, global_State *g) {
  G(L) = g;
  L->stack.p = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->twups = L;  /* thread has no upvalues */
  L->nCcalls = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->status = VMK_OK;
  L->errfunc = 0;
  L->oldpc = 0;
}


lu_mem vmkE_threadsize (vmk_State *L) {
  lu_mem sz = cast(lu_mem, sizeof(LX))
            + cast_uint(L->nci) * sizeof(CallInfo);
  if (L->stack.p != NULL)
    sz += cast_uint(stacksize(L) + EXTRA_STACK) * sizeof(StackValue);
  return sz;
}


static void close_state (vmk_State *L) {
  global_State *g = G(L);
  if (!completestate(g))  /* closing a partially built state? */
    vmkC_freeallobjects(L);  /* just collect its objects */
  else {  /* closing a fully built state */
    L->ci = &L->base_ci;  /* unwind CallInfo list */
    vmkD_closeprotected(L, 1, VMK_OK);  /* close all upvalues */
    vmkC_freeallobjects(L);  /* collect all objects */
    vmki_userstateclose(L);
  }
  vmkM_freearray(L, G(L)->strt.hash, cast_sizet(G(L)->strt.size));
  freestack(L);
  vmk_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


VMK_API vmk_State *vmk_newthread (vmk_State *L) {
  global_State *g = G(L);
  GCObject *o;
  vmk_State *L1;
  vmk_lock(L);
  vmkC_checkGC(L);
  /* create new thread */
  o = vmkC_newobjdt(L, VMK_TTHREAD, sizeof(LX), offsetof(LX, l));
  L1 = gco2th(o);
  /* anchor it on L stack */
  setthvalue2s(L, L->top.p, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(vmk_getextraspace(L1), vmk_getextraspace(g->mainthread),
         VMK_EXTRASPACE);
  vmki_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  vmk_unlock(L);
  return L1;
}


void vmkE_freethread (vmk_State *L, vmk_State *L1) {
  LX *l = fromstate(L1);
  vmkF_closeupval(L1, L1->stack.p);  /* close all upvalues */
  vmk_assert(L1->openupval == NULL);
  vmki_userstatefree(L, L1);
  freestack(L1);
  vmkM_free(L, l);
}


int vmkE_resetthread (vmk_State *L, int status) {
  CallInfo *ci = L->ci = &L->base_ci;  /* unwind CallInfo list */
  setnilvalue(s2v(L->stack.p));  /* 'fn' entry for basic 'ci' */
  ci->func.p = L->stack.p;
  ci->callstatus = CIST_C;
  if (status == VMK_YIELD)
    status = VMK_OK;
  L->status = VMK_OK;  /* so it can run __close metamethods */
  status = vmkD_closeprotected(L, 1, status);
  if (status != VMK_OK)  /* errors? */
    vmkD_seterrorobj(L, status, L->stack.p + 1);
  else
    L->top.p = L->stack.p + 1;
  ci->top.p = L->top.p + VMK_MINSTACK;
  vmkD_reallocstack(L, cast_int(ci->top.p - L->stack.p), 0);
  return status;
}


VMK_API int vmk_closethread (vmk_State *L, vmk_State *from) {
  int status;
  vmk_lock(L);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  status = vmkE_resetthread(L, L->status);
  vmk_unlock(L);
  return status;
}


VMK_API vmk_State *vmk_newstate (vmk_Alloc f, void *ud, unsigned seed) {
  int i;
  vmk_State *L;
  global_State *g;
  LG *l = cast(LG *, (*f)(ud, NULL, VMK_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->tt = VMK_VTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = vmkC_white(g);
  preinit_thread(L, g);
  g->allgc = obj2gco(L);  /* by now, only object is the main thread */
  L->next = NULL;
  incnny(L);  /* main thread is always non yieldable */
  g->frealloc = f;
  g->ud = ud;
  g->warnf = NULL;
  g->ud_warn = NULL;
  g->mainthread = L;
  g->seed = seed;
  g->gcstp = GCSTPGC;  /* no GC while building state */
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->gcstopem = 0;
  g->gcemergency = 0;
  g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->firstold1 = g->survival = g->old1 = g->reallyold = NULL;
  g->finobjsur = g->finobjold1 = g->finobjrold = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->GCtotalbytes = sizeof(LG);
  g->GCmarked = 0;
  g->GCdebt = 0;
  setivalue(&g->nilvalue, 0);  /* to signal that state is not yet built */
  setgcparam(g, PAUSE, VMKI_GCPAUSE);
  setgcparam(g, STEPMUL, VMKI_GCMUL);
  setgcparam(g, STEPSIZE, VMKI_GCSTEPSIZE);
  setgcparam(g, MINORMUL, VMKI_GENMINORMUL);
  setgcparam(g, MINORMAJOR, VMKI_MINORMAJOR);
  setgcparam(g, MAJORMINOR, VMKI_MAJORMINOR);
  for (i=0; i < VMK_NUMTYPES; i++) g->mt[i] = NULL;
  if (vmkD_rawrunprotected(L, f_vmkopen, NULL) != VMK_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


VMK_API void vmk_close (vmk_State *L) {
  vmk_lock(L);
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


void vmkE_warning (vmk_State *L, const char *msg, int tocont) {
  vmk_WarnFunction wf = G(L)->warnf;
  if (wf != NULL)
    wf(G(L)->ud_warn, msg, tocont);
}


/*
** Generate a warning from an error message
*/
void vmkE_warnerror (vmk_State *L, const char *where) {
  TValue *errobj = s2v(L->top.p - 1);  /* error object */
  const char *msg = (ttisstring(errobj))
                  ? getstr(tsvalue(errobj))
                  : "error object is not a string";
  /* produce warning "error in %s (%s)" (where, msg) */
  vmkE_warning(L, "error in ", 1);
  vmkE_warning(L, where, 1);
  vmkE_warning(L, " (", 1);
  vmkE_warning(L, msg, 1);
  vmkE_warning(L, ")", 0);
}

