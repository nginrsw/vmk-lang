/*
** $Id: ltests.h $
** Internal Header for Debugging of the Vmk Implementation
** See Copyright Notice in vmk.h
*/

#ifndef ltests_h
#define ltests_h


#include <stdio.h>
#include <stdlib.h>

/* test Vmk with compatibility code */
#define VMK_COMPAT_MATHLIB
#define VMK_COMPAT_LT_LE


#define VMK_DEBUG


/* turn on assertions */
#define VMKI_ASSERT


/* to avoid warnings, and to make sure value is really unused */
#define UNUSED(x)       (x=0, (void)(x))


/* test for sizes in 'l_sprintf' (make sure whole buffer is available) */
#undef l_sprintf
#if !defined(VMK_USE_C89)
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), snprintf(s,sz,f,i))
#else
#define l_sprintf(s,sz,f,i)	(memset(s,0xAB,sz), sprintf(s,f,i))
#endif


/* get a chance to test code without jump tables */
#define VMK_USE_JUMPTABLE	0


/* use 32-bit integers in random generator */
#define VMK_RAND32


/* memory-allocator control variables */
typedef struct Memcontrol {
  int failnext;
  unsigned long numblocks;
  unsigned long total;
  unsigned long maxmem;
  unsigned long memlimit;
  unsigned long countlimit;
  unsigned long objcount[VMK_NUMTYPES];
} Memcontrol;

VMK_API Memcontrol l_memcontrol;


#define vmki_tracegc(L,f)		vmki_tracegctest(L, f)
VMKI_FUNC void vmki_tracegctest (vmk_State *L, int first);


/*
** generic variable for debug tricks
*/
extern void *l_Trick;


/*
** Function to traverse and check all memory used by Vmk
*/
VMKI_FUNC int vmk_checkmemory (vmk_State *L);

/*
** Function to print an object GC-friendly
*/
struct GCObject;
VMKI_FUNC void vmk_printobj (vmk_State *L, struct GCObject *o);


/*
** Function to print a value
*/
struct TValue;
VMKI_FUNC void vmk_printvalue (struct TValue *v);

/*
** Function to print the stack
*/
VMKI_FUNC void vmk_printstack (vmk_State *L);


/* test for locked/unlock */

struct L_EXTRA { int locked; int *plock; };
#undef VMK_EXTRASPACE
#define VMK_EXTRASPACE	sizeof(struct L_EXTRA)
#define getlock(l)	cast(struct L_EXTRA*, vmk_getextraspace(l))
#define vmki_userstateopen(l)  \
	(getlock(l)->locked = 0, getlock(l)->plock = &(getlock(l)->locked))
#define vmki_userstateclose(l)  \
  vmk_assert(getlock(l)->locked == 1 && getlock(l)->plock == &(getlock(l)->locked))
#define vmki_userstatethread(l,l1) \
  vmk_assert(getlock(l1)->plock == getlock(l)->plock)
#define vmki_userstatefree(l,l1) \
  vmk_assert(getlock(l)->plock == getlock(l1)->plock)
#define vmk_lock(l)     vmk_assert((*getlock(l)->plock)++ == 0)
#define vmk_unlock(l)   vmk_assert(--(*getlock(l)->plock) == 0)



VMK_API int vmkB_opentests (vmk_State *L);

VMK_API void *debug_realloc (void *ud, void *block,
                             size_t osize, size_t nsize);

#if defined(vmk_c)
#define vmkL_newstate()  \
	vmk_newstate(debug_realloc, &l_memcontrol, vmkL_makeseed(NULL))
#define vmki_openlibs(L)  \
  {  vmkL_openlibs(L); \
     vmkL_requiref(L, "T", vmkB_opentests, 1); \
     vmk_pop(L, 1); }
#endif



/* change some sizes to give some bugs a chance */

#undef VMKL_BUFFERSIZE
#define VMKL_BUFFERSIZE		23
#define MINSTRTABSIZE		2
#define MAXIWTHABS		3

#define STRCACHE_N	23
#define STRCACHE_M	5

#undef VMKI_USER_ALIGNMENT_T
#define VMKI_USER_ALIGNMENT_T   union { char b[sizeof(void*) * 8]; }


/*
** This one is not compatible with tests for opcode optimizations,
** as it blocks some optimizations
#define MAXINDEXRK	0
*/


/* make stack-overflow tests run faster */
#undef VMKI_MAXSTACK
#define VMKI_MAXSTACK   50000


/* test mode uses more stack space */
#undef VMKI_MAXCCALLS
#define VMKI_MAXCCALLS	180


/* force Vmk to use its own implementations */
#undef vmk_strx2number
#undef vmk_number2strx


#endif

