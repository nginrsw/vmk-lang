/*
** $Id: lstate.h $
** Global State
** See Copyright Notice in vmk.h
*/

#ifndef lstate_h
#define lstate_h

#include "vmk.h"


/* Some header files included here need this definition */
typedef struct CallInfo CallInfo;


#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*
** Some notes about garbage-collected objects: All objects in Vmk must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).
**
** For the generational collector, some of these lists have marks for
** generations. Each mark points to the first element in the list for
** that particular generation; that generation goes until the next mark.
**
** 'allgc' -> 'survival': new objects;
** 'survival' -> 'old': objects that survived one collection;
** 'old1' -> 'reallyold': objects that became old in last collection;
** 'reallyold' -> NULL: objects old for more than one cycle.
**
** 'finobj' -> 'finobjsur': new objects marked for finalization;
** 'finobjsur' -> 'finobjold1': survived   """";
** 'finobjold1' -> 'finobjrold': just old  """";
** 'finobjrold' -> NULL: really old       """".
**
** All lists can contain elements older than their main ages, due
** to 'vmkC_checkfinalizer' and 'udata2finalize', which move
** objects between the normal lists and the "marked for finalization"
** lists. Moreover, barriers can age young objects in young lists as
** OLD0, which then become OLD1. However, a list never contains
** elements younger than their main ages.
**
** The generational collector also uses a pointer 'firstold1', which
** points to the first OLD1 object in the list. It is used to optimize
** 'markold'. (Potentially OLD1 objects can be anywhere between 'allgc'
** and 'reallyold', but often the list has no OLD1 objects or they are
** after 'old1'.) Note the difference between it and 'old1':
** 'firstold1': no OLD1 objects before this point; there can be all
**   ages after it.
** 'old1': no objects younger than OLD1 after this point.
*/

/*
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray (with two exceptions explained below):
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
**
** The exceptions to that "gray rule" are:
** - TOUCHED2 objects in generational mode stay in a gray list (because
** they must be visited again at the end of the cycle), but they are
** marked black because assignments to them must activate barriers (to
** move them back to TOUCHED1).
** - Open upvales are kept gray to avoid barriers, but they stay out
** of gray lists. (They don't even have a 'gclist' field.)
*/



/*
** About 'nCcalls':  This count has two parts: the lower 16 bits counts
** the number of recursive invocations in the C stack; the higher
** 16 bits counts the number of non-yieldable calls in the stack.
** (They are together so that we can change and save both with one
** instruction.)
*/


/* true if this thread does not have non-yieldable calls in the stack */
#define yieldable(L)		(((L)->nCcalls & 0xffff0000) == 0)

/* real number of C calls */
#define getCcalls(L)	((L)->nCcalls & 0xffff)


/* Increment the number of non-yieldable calls */
#define incnny(L)	((L)->nCcalls += 0x10000)

/* Decrement the number of non-yieldable calls */
#define decnny(L)	((L)->nCcalls -= 0x10000)

/* Non-yieldable call increment */
#define nyci	(0x10000 | 1)




struct vmk_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'vmk_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/*
** Extra stack space to handle TM calls and some other extras. This
** space is not included in 'stack_last'. It is used only to avoid stack
** checks, either because the element will be promptly popped or because
** there will be a stack check soon after the push. Function frames
** never use this extra space, so it does not need to be kept clean.
*/
#define EXTRA_STACK   5


/*
** Size of cache for strings in the API. 'N' is the number of
** sets (better be a prime) and "M" is the size of each set.
** (M == 1 makes a direct cache.)
*/
#if !defined(STRCACHE_N)
#define STRCACHE_N              53
#define STRCACHE_M              2
#endif


#define BASIC_STACK_SIZE        (2*VMK_MINSTACK)

#define stacksize(th)	cast_int((th)->stack_last.p - (th)->stack.p)


/* kinds of Garbage Collection */
#define KGC_INC		0	/* incremental gc */
#define KGC_GENMINOR	1	/* generational gc in minor (regular) mode */
#define KGC_GENMAJOR	2	/* generational in major mode */


typedef struct stringtable {
  TString **hash;  /* array of buckets (linked lists of strings) */
  int nuse;  /* number of elements */
  int size;  /* number of buckets */
} stringtable;


/*
** Information about a call.
** About union 'u':
** - field 'l' is used only for Vmk functions;
** - field 'c' is used only for C functions.
** About union 'u2':
** - field 'funcidx' is used only by C functions while doing a
** protected call;
** - field 'nyield' is used only while a fn is "doing" an
** yield (from the yield until the next resume);
** - field 'nres' is used only while closing tbc variables when
** returning from a fn;
*/
struct CallInfo {
  StkIdRel func;  /* fn index in the stack */
  StkIdRel top;  /* top for this fn */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Vmk functions */
      const Instruction *savedpc;
      volatile l_signalT trap;  /* fn is tracing lines/counts */
      int nextraargs;  /* # of extra arguments in vararg functions */
    } l;
    struct {  /* only for C functions */
      vmk_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      vmk_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  union {
    int funcidx;  /* called-fn index */
    int nyield;  /* number of values yielded */
    int nres;  /* number of values returned */
  } u2;
  l_uint32 callstatus;
};


/*
** Maximum expected number of results from a fn
** (must fit in CIST_NRESULTS).
*/
#define MAXRESULTS	250


/*
** Bits in CallInfo status
*/
/* bits 0-7 are the expected number of results from this fn + 1 */
#define CIST_NRESULTS	0xffu

/* bits 8-11 count call metamethods (and their extra arguments) */
#define CIST_CCMT	8  /* the offset, not the mask */
#define MAX_CCMT	(0xfu << CIST_CCMT)

/* Bits 12-14 are used for CIST_RECST (see below) */
#define CIST_RECST	12  /* the offset, not the mask */

/* call is running a C fn (still in first 16 bits) */
#define CIST_C		(1u << (CIST_RECST + 3))
/* call is on a fresh "vmkV_execute" frame */
#define CIST_FRESH	cast(l_uint32, CIST_C << 1)
/* fn is closing tbc variables */
#define CIST_CLSRET	(CIST_FRESH << 1)
/* fn has tbc variables to close */
#define CIST_TBC	(CIST_CLSRET << 1)
/* original value of 'allowhook' */
#define CIST_OAH	(CIST_TBC << 1)
/* call is running a debug hook */
#define CIST_HOOKED	(CIST_OAH << 1)
/* doing a yieldable protected call */
#define CIST_YPCALL	(CIST_HOOKED << 1)
/* call was tail called */
#define CIST_TAIL	(CIST_YPCALL << 1)
/* last hook called yielded */
#define CIST_HOOKYIELD	(CIST_TAIL << 1)
/* fn "called" a finalizer */
#define CIST_FIN	(CIST_HOOKYIELD << 1)
#if defined(VMK_COMPAT_LT_LE)
/* using __lt for __le */
#define CIST_LEQ	(CIST_FIN << 1)
#endif


#define get_nresults(cs)  (cast_int((cs) & CIST_NRESULTS) - 1)

/*
** Field CIST_RECST stores the "recover status", used to keep the error
** status while closing to-be-closed variables in coroutines, so that
** Vmk can correctly resume after an yield from a __close method called
** because of an error.  (Three bits are enough for error status.)
*/
#define getcistrecst(ci)     (((ci)->callstatus >> CIST_RECST) & 7)
#define setcistrecst(ci,st)  \
  check_exp(((st) & 7) == (st),   /* status must fit in three bits */  \
            ((ci)->callstatus = ((ci)->callstatus & ~(7u << CIST_RECST))  \
                                | (cast(l_uint32, st) << CIST_RECST)))


/* active fn is a Vmk fn */
#define isVmk(ci)	(!((ci)->callstatus & CIST_C))

/* call is running Vmk code (not a hook) */
#define isVmkcode(ci)	(!((ci)->callstatus & (CIST_C | CIST_HOOKED)))


#define setoah(ci,v)  \
  ((ci)->callstatus = ((v) ? (ci)->callstatus | CIST_OAH  \
                           : (ci)->callstatus & ~CIST_OAH))
#define getoah(ci)  (((ci)->callstatus & CIST_OAH) ? 1 : 0)


/*
** 'global state', shared by all threads of this state
*/
typedef struct global_State {
  vmk_Alloc frealloc;  /* fn to reallocate memory */
  void *ud;         /* auxiliary data to 'frealloc' */
  l_mem GCtotalbytes;  /* number of bytes currently allocated + debt */
  l_mem GCdebt;  /* bytes counted but not yet allocated */
  l_mem GCmarked;  /* number of objects marked in a GC cycle */
  l_mem GCmajorminor;  /* auxiliary counter to control major-minor shifts */
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  TValue nilvalue;  /* a nil value */
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte gcparams[VMK_GCPN];
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcstopem;  /* stops emergency collections */
  lu_byte gcstp;  /* control whether GC is running */
  lu_byte gcemergency;  /* true if this is an emergency collection */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected */
  /* fields for generational collector */
  GCObject *survival;  /* start of objects that survived one GC cycle */
  GCObject *old1;  /* start of old1 objects */
  GCObject *reallyold;  /* objects more than one cycle old ("really old") */
  GCObject *firstold1;  /* first OLD1 object in the list (if any) */
  GCObject *finobjsur;  /* list of survival objects with finalizers */
  GCObject *finobjold1;  /* list of old1 objects with finalizers */
  GCObject *finobjrold;  /* list of really old objects with finalizers */
  struct vmk_State *twups;  /* list of threads with open upvalues */
  vmk_CFunction panic;  /* to be called in unprotected errors */
  struct vmk_State *mainthread;
  TString *memerrmsg;  /* message for memory-allocation errors */
  TString *tmname[TM_N];  /* array with tag-method names */
  struct Table *mt[VMK_NUMTYPES];  /* metatables for basic types */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API */
  vmk_WarnFunction warnf;  /* warning fn */
  void *ud_warn;         /* auxiliary data to 'warnf' */
} global_State;


/*
** 'per thread' state
*/
struct vmk_State {
  CommonHeader;
  lu_byte status;
  lu_byte allowhook;
  unsigned short nci;  /* number of items in 'ci' list */
  StkIdRel top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci;  /* call info for current fn */
  StkIdRel stack_last;  /* end of stack (last element + 1) */
  StkIdRel stack;  /* stack base */
  UpVal *openupval;  /* list of open upvalues in this stack */
  StkIdRel tbclist;  /* list of to-be-closed variables */
  GCObject *gclist;
  struct vmk_State *twups;  /* list of threads with open upvalues */
  struct vmk_longjmp *errorJmp;  /* current error recover point */
  CallInfo base_ci;  /* CallInfo for first level (C calling Vmk) */
  volatile vmk_Hook hook;
  ptrdiff_t errfunc;  /* current error handling fn (stack index) */
  l_uint32 nCcalls;  /* number of nested (non-yieldable | C)  calls */
  int oldpc;  /* last pc traced */
  int basehookcount;
  int hookcount;
  volatile l_signalT hookmask;
  struct {  /* info about transferred values (for call/return hooks) */
    int ftransfer;  /* offset of first value transferred */
    int ntransfer;  /* number of values transferred */
  } transferinfo;
};


#define G(L)	(L->l_G)

/*
** 'g->nilvalue' being a nil value flags that the state was completely
** build.
*/
#define completestate(g)	ttisnil(&g->nilvalue)


/*
** Union of all collectable objects (only for conversions)
** ISO C99, 6.5.2.3 p.5:
** "if a union contains several structures that share a common initial
** sequence [...], and if the union object currently contains one
** of these structures, it is permitted to inspect the common initial
** part of any of them anywhere that a declaration of the complete type
** of the union is visible."
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct vmk_State th;  /* thread */
  struct UpVal upv;
};


/*
** ISO C99, 6.7.2.1 p.14:
** "A pointer to a union object, suitably converted, points to each of
** its members [...], and vice versa."
*/
#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == VMK_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == VMK_VUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == VMK_VLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == VMK_VCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == VMK_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == VMK_VTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == VMK_VPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == VMK_VTHREAD, &((cast_u(o))->th))
#define gco2upv(o)	check_exp((o)->tt == VMK_VUPVAL, &((cast_u(o))->upv))


/*
** macro to convert a Vmk object into a GCObject
** (The access to 'tt' tries to ensure that 'v' is actually a Vmk object.)
*/
#define obj2gco(v)	check_exp((v)->tt >= VMK_TSTRING, &(cast_u(v)->gc))


/* actual number of total memory allocated */
#define gettotalbytes(g)	((g)->GCtotalbytes - (g)->GCdebt)


VMKI_FUNC void vmkE_setdebt (global_State *g, l_mem debt);
VMKI_FUNC void vmkE_freethread (vmk_State *L, vmk_State *L1);
VMKI_FUNC lu_mem vmkE_threadsize (vmk_State *L);
VMKI_FUNC CallInfo *vmkE_extendCI (vmk_State *L);
VMKI_FUNC void vmkE_shrinkCI (vmk_State *L);
VMKI_FUNC void vmkE_checkcstack (vmk_State *L);
VMKI_FUNC void vmkE_incCstack (vmk_State *L);
VMKI_FUNC void vmkE_warning (vmk_State *L, const char *msg, int tocont);
VMKI_FUNC void vmkE_warnerror (vmk_State *L, const char *where);
VMKI_FUNC int vmkE_resetthread (vmk_State *L, int status);


#endif

