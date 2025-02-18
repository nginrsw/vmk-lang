/*
** $Id: ltm.h $
** Tag methods
** See Copyright Notice in vmk.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/
typedef enum {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,  /* last tag method with fast access */
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_MOD,
  TM_POW,
  TM_DIV,
  TM_IDIV,
  TM_BAND,
  TM_BOR,
  TM_BXOR,
  TM_SHL,
  TM_SHR,
  TM_UNM,
  TM_BNOT,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_CLOSE,
  TM_N		/* number of elements in the enum */
} TMS;


/*
** Mask with 1 in all fast-access methods. A 1 in any of these bits
** in the flag of a (meta)table means the metatable does not have the
** corresponding metamethod field. (Bit 6 of the flag indicates that
** the table is using the dummy node; bit 7 is used for 'isrealasize'.)
*/
#define maskflags	cast_byte(~(~0u << (TM_EQ + 1)))


/*
** Test whether there is no tagmethod.
** (Because tagmethods use raw accesses, the result may be an "empty" nil.)
*/
#define notm(tm)	ttisnil(tm)

#define checknoTM(mt,e)	((mt) == NULL || (mt)->flags & (1u<<(e)))

#define gfasttm(g,mt,e)  \
  (checknoTM(mt, e) ? NULL : vmkT_gettm(mt, e, (g)->tmname[e]))

#define fasttm(l,mt,e)	gfasttm(G(l), mt, e)

#define ttypename(x)	vmkT_typenames_[(x) + 1]

VMKI_DDEC(const char *const vmkT_typenames_[VMK_TOTALTYPES];)


VMKI_FUNC const char *vmkT_objtypename (vmk_State *L, const TValue *o);

VMKI_FUNC const TValue *vmkT_gettm (Table *events, TMS event, TString *ename);
VMKI_FUNC const TValue *vmkT_gettmbyobj (vmk_State *L, const TValue *o,
                                                       TMS event);
VMKI_FUNC void vmkT_init (vmk_State *L);

VMKI_FUNC void vmkT_callTM (vmk_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
VMKI_FUNC lu_byte vmkT_callTMres (vmk_State *L, const TValue *f,
                               const TValue *p1, const TValue *p2, StkId p3);
VMKI_FUNC void vmkT_trybinTM (vmk_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
VMKI_FUNC void vmkT_tryconcatTM (vmk_State *L);
VMKI_FUNC void vmkT_trybinassocTM (vmk_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
VMKI_FUNC void vmkT_trybiniTM (vmk_State *L, const TValue *p1, vmk_Integer i2,
                               int inv, StkId res, TMS event);
VMKI_FUNC int vmkT_callorderTM (vmk_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
VMKI_FUNC int vmkT_callorderiTM (vmk_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

VMKI_FUNC void vmkT_adjustvarargs (vmk_State *L, int nfixparams,
                                   struct CallInfo *ci, const Proto *p);
VMKI_FUNC void vmkT_getvarargs (vmk_State *L, struct CallInfo *ci,
                                              StkId where, int wanted);


#endif
