/*
** $Id: lstring.h $
** String table (keep all strings handled by Vmk)
** See Copyright Notice in vmk.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"


/*
** Maximum length for short strings, that is, strings that are
** internalized. (Cannot be smaller than reserved words or tags for
** metamethods, as these strings must be internalized;
** #("fn") = 8, #("__newindex") = 10.)
*/
#if !defined(VMKI_MAXSHORTLEN)
#define VMKI_MAXSHORTLEN	40
#endif


/*
** Size of a short TString: Size of the header plus space for the string
** itself (including final '\0').
*/
#define sizestrshr(l)  \
	(offsetof(TString, contents) + ((l) + 1) * sizeof(char))


#define vmkS_newliteral(L, s)	(vmkS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	(strisshr(s) && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == VMK_VSHRSTR, (a) == (b))


VMKI_FUNC unsigned vmkS_hash (const char *str, size_t l, unsigned seed);
VMKI_FUNC unsigned vmkS_hashlongstr (TString *ts);
VMKI_FUNC int vmkS_eqlngstr (TString *a, TString *b);
VMKI_FUNC void vmkS_resize (vmk_State *L, int newsize);
VMKI_FUNC void vmkS_clearcache (global_State *g);
VMKI_FUNC void vmkS_init (vmk_State *L);
VMKI_FUNC void vmkS_remove (vmk_State *L, TString *ts);
VMKI_FUNC Udata *vmkS_newudata (vmk_State *L, size_t s,
                                              unsigned short nuvalue);
VMKI_FUNC TString *vmkS_newlstr (vmk_State *L, const char *str, size_t l);
VMKI_FUNC TString *vmkS_new (vmk_State *L, const char *str);
VMKI_FUNC TString *vmkS_createlngstrobj (vmk_State *L, size_t l);
VMKI_FUNC TString *vmkS_newextlstr (vmk_State *L,
		const char *s, size_t len, vmk_Alloc falloc, void *ud);
VMKI_FUNC size_t vmkS_sizelngstr (size_t len, int kind);

#endif
