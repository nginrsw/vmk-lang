/*
** $Id: ldebug.h $
** Auxiliary functions from Debug Interface module
** See Copyright Notice in vmk.h
*/

#ifndef ldebug_h
#define ldebug_h


#include "lstate.h"


#define pcRel(pc, p)	(cast_int((pc) - (p)->code) - 1)


/* Active Vmk fn (given call info) */
#define ci_func(ci)		(clLvalue(s2v((ci)->func.p)))


#define resethookcount(L)	(L->hookcount = L->basehookcount)

/*
** mark for entries in 'lineinfo' array that has absolute information in
** 'abslineinfo' array
*/
#define ABSLINEINFO	(-0x80)


/*
** MAXimum number of successive Instructions WiTHout ABSolute line
** information. (A power of two allows fast divisions.)
*/
#if !defined(MAXIWTHABS)
#define MAXIWTHABS	128
#endif


VMKI_FUNC int vmkG_getfuncline (const Proto *f, int pc);
VMKI_FUNC const char *vmkG_findlocal (vmk_State *L, CallInfo *ci, int n,
                                                    StkId *pos);
VMKI_FUNC l_noret vmkG_typeerror (vmk_State *L, const TValue *o,
                                                const char *opname);
VMKI_FUNC l_noret vmkG_callerror (vmk_State *L, const TValue *o);
VMKI_FUNC l_noret vmkG_forerror (vmk_State *L, const TValue *o,
                                               const char *what);
VMKI_FUNC l_noret vmkG_concaterror (vmk_State *L, const TValue *p1,
                                                  const TValue *p2);
VMKI_FUNC l_noret vmkG_opinterror (vmk_State *L, const TValue *p1,
                                                 const TValue *p2,
                                                 const char *msg);
VMKI_FUNC l_noret vmkG_tointerror (vmk_State *L, const TValue *p1,
                                                 const TValue *p2);
VMKI_FUNC l_noret vmkG_ordererror (vmk_State *L, const TValue *p1,
                                                 const TValue *p2);
VMKI_FUNC l_noret vmkG_runerror (vmk_State *L, const char *fmt, ...);
VMKI_FUNC const char *vmkG_addinfo (vmk_State *L, const char *msg,
                                                  TString *src, int line);
VMKI_FUNC l_noret vmkG_errormsg (vmk_State *L);
VMKI_FUNC int vmkG_traceexec (vmk_State *L, const Instruction *pc);
VMKI_FUNC int vmkG_tracecall (vmk_State *L);


#endif
