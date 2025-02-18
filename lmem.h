/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in vmk.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "vmk.h"


#define vmkM_error(L)	vmkD_throw(L, VMK_ERRMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define vmkM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

#define vmkM_checksize(L,n,e)  \
	(vmkM_testsize(n,e) ? vmkM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
#define vmkM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_int((MAX_SIZET/sizeof(t))))


/*
** Arrays of chars do not need any test
*/
#define vmkM_reallocvchar(L,b,on,n)  \
  cast_charp(vmkM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

#define vmkM_freemem(L, b, s)	vmkM_free_(L, (b), (s))
#define vmkM_free(L, b)		vmkM_free_(L, (b), sizeof(*(b)))
#define vmkM_freearray(L, b, n)   vmkM_free_(L, (b), (n)*sizeof(*(b)))

#define vmkM_new(L,t)		cast(t*, vmkM_malloc_(L, sizeof(t), 0))
#define vmkM_newvector(L,n,t)	cast(t*, vmkM_malloc_(L, (n)*sizeof(t), 0))
#define vmkM_newvectorchecked(L,n,t) \
  (vmkM_checksize(L,n,sizeof(t)), vmkM_newvector(L,n,t))

#define vmkM_newobject(L,tag,s)	vmkM_malloc_(L, (s), tag)

#define vmkM_newblock(L, size)	vmkM_newvector(L, size, char)

#define vmkM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, vmkM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         vmkM_limitN(limit,t),e)))

#define vmkM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, vmkM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

#define vmkM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, vmkM_shrinkvector_(L, v, &(size), fs, sizeof(t))))

VMKI_FUNC l_noret vmkM_toobig (vmk_State *L);

/* not to be called directly */
VMKI_FUNC void *vmkM_realloc_ (vmk_State *L, void *block, size_t oldsize,
                                                          size_t size);
VMKI_FUNC void *vmkM_saferealloc_ (vmk_State *L, void *block, size_t oldsize,
                                                              size_t size);
VMKI_FUNC void vmkM_free_ (vmk_State *L, void *block, size_t osize);
VMKI_FUNC void *vmkM_growaux_ (vmk_State *L, void *block, int nelems,
                               int *size, unsigned size_elem, int limit,
                               const char *what);
VMKI_FUNC void *vmkM_shrinkvector_ (vmk_State *L, void *block, int *nelem,
                                    int final_n, unsigned size_elem);
VMKI_FUNC void *vmkM_malloc_ (vmk_State *L, size_t size, int tag);

#endif

