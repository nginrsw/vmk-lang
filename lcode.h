/*
** $Id: lcode.h $
** Code generator for Vmk
** See Copyright Notice in vmk.h
*/

#ifndef lcode_h
#define lcode_h

#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
#define NO_JUMP (-1)


/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
typedef enum BinOpr {
  /* arithmetic operators */
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,
  /* bitwise operators */
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  /* string operator */
  OPR_CONCAT,
  /* comparison operators */
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  /* logical operators */
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;


/* true if operation is foldable (that is, it is arithmetic or bitwise) */
#define foldbinop(op)	((op) <= OPR_SHR)


#define vmkK_codeABC(fs,o,a,b,c)	vmkK_codeABCk(fs,o,a,b,c,0)


typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


/* get (pointer to) instruction of given 'expdesc' */
#define getinstruction(fs,e)	((fs)->f->code[(e)->u.info])


#define vmkK_setmultret(fs,e)	vmkK_setreturns(fs, e, VMK_MULTRET)

#define vmkK_jumpto(fs,t)	vmkK_patchlist(fs, vmkK_jump(fs), t)

VMKI_FUNC int vmkK_code (FuncState *fs, Instruction i);
VMKI_FUNC int vmkK_codeABx (FuncState *fs, OpCode o, int A, int Bx);
VMKI_FUNC int vmkK_codeABCk (FuncState *fs, OpCode o, int A, int B, int C,
                                            int k);
VMKI_FUNC int vmkK_codevABCk (FuncState *fs, OpCode o, int A, int B, int C,
                                             int k);
VMKI_FUNC int vmkK_exp2const (FuncState *fs, const expdesc *e, TValue *v);
VMKI_FUNC void vmkK_fixline (FuncState *fs, int line);
VMKI_FUNC void vmkK_nil (FuncState *fs, int from, int n);
VMKI_FUNC void vmkK_reserveregs (FuncState *fs, int n);
VMKI_FUNC void vmkK_checkstack (FuncState *fs, int n);
VMKI_FUNC void vmkK_int (FuncState *fs, int reg, vmk_Integer n);
VMKI_FUNC void vmkK_dischargevars (FuncState *fs, expdesc *e);
VMKI_FUNC int vmkK_exp2anyreg (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_exp2anyregup (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_exp2nextreg (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_exp2val (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_self (FuncState *fs, expdesc *e, expdesc *key);
VMKI_FUNC void vmkK_indexed (FuncState *fs, expdesc *t, expdesc *k);
VMKI_FUNC void vmkK_goiftrue (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_goiffalse (FuncState *fs, expdesc *e);
VMKI_FUNC void vmkK_storevar (FuncState *fs, expdesc *var, expdesc *e);
VMKI_FUNC void vmkK_setreturns (FuncState *fs, expdesc *e, int nresults);
VMKI_FUNC void vmkK_setoneret (FuncState *fs, expdesc *e);
VMKI_FUNC int vmkK_jump (FuncState *fs);
VMKI_FUNC void vmkK_ret (FuncState *fs, int first, int nret);
VMKI_FUNC void vmkK_patchlist (FuncState *fs, int list, int target);
VMKI_FUNC void vmkK_patchtohere (FuncState *fs, int list);
VMKI_FUNC void vmkK_concat (FuncState *fs, int *l1, int l2);
VMKI_FUNC int vmkK_getlabel (FuncState *fs);
VMKI_FUNC void vmkK_prefix (FuncState *fs, UnOpr op, expdesc *v, int line);
VMKI_FUNC void vmkK_infix (FuncState *fs, BinOpr op, expdesc *v);
VMKI_FUNC void vmkK_posfix (FuncState *fs, BinOpr op, expdesc *v1,
                            expdesc *v2, int line);
VMKI_FUNC void vmkK_settablesize (FuncState *fs, int pc,
                                  int ra, int asize, int hsize);
VMKI_FUNC void vmkK_setlist (FuncState *fs, int base, int nelems, int tostore);
VMKI_FUNC void vmkK_finish (FuncState *fs);
VMKI_FUNC l_noret vmkK_semerror (LexState *ls, const char *msg);


#endif
