/*
** $Id: lvm.c $
** Vmk virtual machine
** See Copyright Notice in vmk.h
*/

#define lvm_c
#define VMK_CORE

#include "lprefix.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/*
** By default, use jump tables in the main interpreter loop on gcc
** and compatible compilers.
*/
#if !defined(VMK_USE_JUMPTABLE)
#if defined(__GNUC__)
#define VMK_USE_JUMPTABLE	1
#else
#define VMK_USE_JUMPTABLE	0
#endif
#endif



/* limit for table tag-method chains (to avoid infinite loops) */
#define MAXTAGLOOP	2000


/*
** 'l_intfitsf' checks whether a given integer is in the range that
** can be converted to a float without rounding. Used in comparisons.
*/

/* number of bits in the mantissa of a float */
#define NBM		(l_floatatt(MANT_DIG))

/*
** Check whether some integers may not fit in a float, testing whether
** (maxinteger >> NBM) > 0. (That implies (1 << NBM) <= maxinteger.)
** (The shifts are done in parts, to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(long) == 32.)
*/
#if ((((VMK_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

/* limit for integers that fit in a float */
#define MAXINTFITSF	((vmk_Unsigned)1 << NBM)

/* check whether 'i' is in the interval [-MAXINTFITSF, MAXINTFITSF] */
#define l_intfitsf(i)	((MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF))

#else  /* all integers fit in a float precisely */

#define l_intfitsf(i)	1

#endif


/*
** Try to convert a value from string to a number value.
** If the value is not a string or is a string not representing
** a valid numeral (or if coercions from strings to numbers
** are disabled via macro 'cvt2num'), do not modify 'result'
** and return 0.
*/
static int l_strton (const TValue *obj, TValue *result) {
  vmk_assert(obj != result);
  if (!cvt2num(obj))  /* is object not a string? */
    return 0;
  else {
    TString *st = tsvalue(obj);
    size_t stlen;
    const char *s = getlstr(st, stlen);
    return (vmkO_str2num(s, result) == stlen + 1);
  }
}


/*
** Try to convert a value to a float. The float case is already handled
** by the macro 'tonumber'.
*/
int vmkV_tonumber_ (const TValue *obj, vmk_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (l_strton(obj, &v)) {  /* string coercible to number? */
    *n = nvalue(&v);  /* convert result of 'vmkO_str2num' to a float */
    return 1;
  }
  else
    return 0;  /* conversion failed */
}


/*
** try to convert a float to an integer, rounding according to 'mode'.
*/
int vmkV_flttointeger (vmk_Number n, vmk_Integer *p, F2Imod mode) {
  vmk_Number f = l_floor(n);
  if (n != f) {  /* not an integral value? */
    if (mode == F2Ieq) return 0;  /* fails if mode demands integral value */
    else if (mode == F2Iceil)  /* needs ceiling? */
      f += 1;  /* convert floor to ceiling (remember: n != f) */
  }
  return vmk_numbertointeger(f, p);
}


/*
** try to convert a value to an integer, rounding according to 'mode',
** without string coercion.
** ("Fast track" handled by macro 'tointegerns'.)
*/
int vmkV_tointegerns (const TValue *obj, vmk_Integer *p, F2Imod mode) {
  if (ttisfloat(obj))
    return vmkV_flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else
    return 0;
}


/*
** try to convert a value to an integer.
*/
int vmkV_tointeger (const TValue *obj, vmk_Integer *p, F2Imod mode) {
  TValue v;
  if (l_strton(obj, &v))  /* does 'obj' point to a numerical string? */
    obj = &v;  /* change it to point to its corresponding number */
  return vmkV_tointegerns(obj, p, mode);
}


/*
** Try to convert a 'for' limit to an integer, preserving the semantics
** of the loop. Return true if the loop must not run; otherwise, '*p'
** gets the integer limit.
** (The following explanation assumes a positive step; it is valid for
** negative steps mutatis mutandis.)
** If the limit is an integer or can be converted to an integer,
** rounding down, that is the limit.
** Otherwise, check whether the limit can be converted to a float. If
** the float is too large, clip it to VMK_MAXINTEGER.  If the float
** is too negative, the loop should not run, because any initial
** integer value is greater than such limit; so, the fn returns
** true to signal that. (For this latter case, no integer limit would be
** correct; even a limit of VMK_MININTEGER would run the loop once for
** an initial value equal to VMK_MININTEGER.)
*/
static int forlimit (vmk_State *L, vmk_Integer init, const TValue *lim,
                                   vmk_Integer *p, vmk_Integer step) {
  if (!vmkV_tointeger(lim, p, (step < 0 ? F2Iceil : F2Ifloor))) {
    /* not coercible to in integer */
    vmk_Number flim;  /* try to convert to float */
    if (!tonumber(lim, &flim)) /* cannot convert to float? */
      vmkG_forerror(L, lim, "limit");
    /* else 'flim' is a float out of integer bounds */
    if (vmki_numlt(0, flim)) {  /* if it is positive, it is too large */
      if (step < 0) return 1;  /* initial value must be less than it */
      *p = VMK_MAXINTEGER;  /* truncate */
    }
    else {  /* it is less than min integer */
      if (step > 0) return 1;  /* initial value must be greater than it */
      *p = VMK_MININTEGER;  /* truncate */
    }
  }
  return (step > 0 ? init > *p : init < *p);  /* not to run? */
}


/*
** Prepare a numerical for loop (opcode OP_FORPREP).
** Before execution, stack is as follows:
**   ra     : initial value
**   ra + 1 : limit
**   ra + 2 : step
** Return true to skip the loop. Otherwise,
** after preparation, stack will be as follows:
**   ra     : loop counter (integer loops) or limit (float loops)
**   ra + 1 : step
**   ra + 2 : control variable
*/
static int forprep (vmk_State *L, StkId ra) {
  TValue *pinit = s2v(ra);
  TValue *plimit = s2v(ra + 1);
  TValue *pstep = s2v(ra + 2);
  if (ttisinteger(pinit) && ttisinteger(pstep)) { /* integer loop? */
    vmk_Integer init = ivalue(pinit);
    vmk_Integer step = ivalue(pstep);
    vmk_Integer limit;
    if (step == 0)
      vmkG_runerror(L, "'for' step is zero");
    if (forlimit(L, init, plimit, &limit, step))
      return 1;  /* skip the loop */
    else {  /* prepare loop counter */
      vmk_Unsigned count;
      if (step > 0) {  /* ascending loop? */
        count = l_castS2U(limit) - l_castS2U(init);
        if (step != 1)  /* avoid division in the too common case */
          count /= l_castS2U(step);
      }
      else {  /* step < 0; descending loop */
        count = l_castS2U(init) - l_castS2U(limit);
        /* 'step+1' avoids negating 'mininteger' */
        count /= l_castS2U(-(step + 1)) + 1u;
      }
      /* use 'chgivalue' for places that for sure had integers */
      chgivalue(s2v(ra), l_castU2S(count));  /* change init to count */
      setivalue(s2v(ra + 1), step);  /* change limit to step */
      chgivalue(s2v(ra + 2), init);  /* change step to init */
    }
  }
  else {  /* try making all values floats */
    vmk_Number init; vmk_Number limit; vmk_Number step;
    if (l_unlikely(!tonumber(plimit, &limit)))
      vmkG_forerror(L, plimit, "limit");
    if (l_unlikely(!tonumber(pstep, &step)))
      vmkG_forerror(L, pstep, "step");
    if (l_unlikely(!tonumber(pinit, &init)))
      vmkG_forerror(L, pinit, "initial value");
    if (step == 0)
      vmkG_runerror(L, "'for' step is zero");
    if (vmki_numlt(0, step) ? vmki_numlt(limit, init)
                            : vmki_numlt(init, limit))
      return 1;  /* skip the loop */
    else {
      /* make sure all values are floats */
      setfltvalue(s2v(ra), limit);
      setfltvalue(s2v(ra + 1), step);
      setfltvalue(s2v(ra + 2), init);  /* control variable */
    }
  }
  return 0;
}


/*
** Execute a step of a float numerical for loop, returning
** true iff the loop must continue. (The integer case is
** written online with opcode OP_FORLOOP, for performance.)
*/
static int floatforloop (StkId ra) {
  vmk_Number step = fltvalue(s2v(ra + 1));
  vmk_Number limit = fltvalue(s2v(ra));
  vmk_Number idx = fltvalue(s2v(ra + 2));  /* control variable */
  idx = vmki_numadd(L, idx, step);  /* increment index */
  if (vmki_numlt(0, step) ? vmki_numle(idx, limit)
                          : vmki_numle(limit, idx)) {
    chgfltvalue(s2v(ra + 2), idx);  /* update control variable */
    return 1;  /* jump back */
  }
  else
    return 0;  /* finish the loop */
}


/*
** Finish the table access 'val = t[key]' and return the tag of the result.
*/
lu_byte vmkV_finishget (vmk_State *L, const TValue *t, TValue *key,
                                      StkId val, lu_byte tag) {
  int loop;  /* counter to avoid infinite loops */
  const TValue *tm;  /* metamethod */
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (tag == VMK_VNOTABLE) {  /* 't' is not a table? */
      vmk_assert(!ttistable(t));
      tm = vmkT_gettmbyobj(L, t, TM_INDEX);
      if (l_unlikely(notm(tm)))
        vmkG_typeerror(L, t, "index");  /* no metamethod */
      /* else will try the metamethod */
    }
    else {  /* 't' is a table */
      tm = fasttm(L, hvalue(t)->metatable, TM_INDEX);  /* table's metamethod */
      if (tm == NULL) {  /* no metamethod? */
        setnilvalue(s2v(val));  /* result is nil */
        return VMK_VNIL;
      }
      /* else will try the metamethod */
    }
    if (ttisfunction(tm)) {  /* is metamethod a fn? */
      tag = vmkT_callTMres(L, tm, t, key, val);  /* call it */
      return tag;  /* return tag of the result */
    }
    t = tm;  /* else try to access 'tm[key]' */
    vmkV_fastget(t, key, s2v(val), vmkH_get, tag);
    if (!tagisempty(tag))
      return tag;  /* done */
    /* else repeat (tail call 'vmkV_finishget') */
  }
  vmkG_runerror(L, "'__index' chain too long; possible loop");
  return 0;  /* to avoid warnings */
}


/*
** Finish a table assignment 't[key] = val'.
*/
void vmkV_finishset (vmk_State *L, const TValue *t, TValue *key,
                      TValue *val, int hres) {
  int loop;  /* counter to avoid infinite loops */
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;  /* '__newindex' metamethod */
    if (hres != HNOTATABLE) {  /* is 't' a table? */
      Table *h = hvalue(t);  /* save 't' table */
      tm = fasttm(L, h->metatable, TM_NEWINDEX);  /* get metamethod */
      if (tm == NULL) {  /* no metamethod? */
        vmkH_finishset(L, h, key, val, hres);  /* set new value */
        invalidateTMcache(h);
        vmkC_barrierback(L, obj2gco(h), val);
        return;
      }
      /* else will try the metamethod */
    }
    else {  /* not a table; check metamethod */
      tm = vmkT_gettmbyobj(L, t, TM_NEWINDEX);
      if (l_unlikely(notm(tm)))
        vmkG_typeerror(L, t, "index");
    }
    /* try the metamethod */
    if (ttisfunction(tm)) {
      vmkT_callTM(L, tm, t, key, val);
      return;
    }
    t = tm;  /* else repeat assignment over 'tm' */
    vmkV_fastset(t, key, val, hres, vmkH_pset);
    if (hres == HOK)
      return;  /* done */
    /* else 'return vmkV_finishset(L, t, key, val, slot)' (loop) */
  }
  vmkG_runerror(L, "'__newindex' chain too long; possible loop");
}


/*
** Compare two strings 'ts1' x 'ts2', returning an integer less-equal-
** -greater than zero if 'ts1' is less-equal-greater than 'ts2'.
** The code is a little tricky because it allows '\0' in the strings
** and it uses 'strcoll' (to respect locales) for each segment
** of the strings. Note that segments can compare equal but still
** have different lengths.
*/
static int l_strcmp (const TString *ts1, const TString *ts2) {
  size_t rl1;  /* real length */
  const char *s1 = getlstr(ts1, rl1);
  size_t rl2;
  const char *s2 = getlstr(ts2, rl2);
  for (;;) {  /* for each segment */
    int temp = strcoll(s1, s2);
    if (temp != 0)  /* not equal? */
      return temp;  /* done */
    else {  /* strings are equal up to a '\0' */
      size_t zl1 = strlen(s1);  /* index of first '\0' in 's1' */
      size_t zl2 = strlen(s2);  /* index of first '\0' in 's2' */
      if (zl2 == rl2)  /* 's2' is finished? */
        return (zl1 == rl1) ? 0 : 1;  /* check 's1' */
      else if (zl1 == rl1)  /* 's1' is finished? */
        return -1;  /* 's1' is less than 's2' ('s2' is not finished) */
      /* both strings longer than 'zl'; go on comparing after the '\0' */
      zl1++; zl2++;
      s1 += zl1; rl1 -= zl1; s2 += zl2; rl2 -= zl2;
    }
  }
}


/*
** Check whether integer 'i' is less than float 'f'. If 'i' has an
** exact representation as a float ('l_intfitsf'), compare numbers as
** floats. Otherwise, use the equivalence 'i < f <=> i < ceil(f)'.
** If 'ceil(f)' is out of integer range, either 'f' is greater than
** all integers or less than all integers.
** (The test with 'l_intfitsf' is only for performance; the else
** case is correct for all values, but it is slow due to the conversion
** from float to int.)
** When 'f' is NaN, comparisons must result in false.
*/
l_sinline int LTintfloat (vmk_Integer i, vmk_Number f) {
  if (l_intfitsf(i))
    return vmki_numlt(cast_num(i), f);  /* compare them as floats */
  else {  /* i < f <=> i < ceil(f) */
    vmk_Integer fi;
    if (vmkV_flttointeger(f, &fi, F2Iceil))  /* fi = ceil(f) */
      return i < fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous fn.
*/
l_sinline int LEintfloat (vmk_Integer i, vmk_Number f) {
  if (l_intfitsf(i))
    return vmki_numle(cast_num(i), f);  /* compare them as floats */
  else {  /* i <= f <=> i <= floor(f) */
    vmk_Integer fi;
    if (vmkV_flttointeger(f, &fi, F2Ifloor))  /* fi = floor(f) */
      return i <= fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/*
** Check whether float 'f' is less than integer 'i'.
** See comments on previous fn.
*/
l_sinline int LTfloatint (vmk_Number f, vmk_Integer i) {
  if (l_intfitsf(i))
    return vmki_numlt(f, cast_num(i));  /* compare them as floats */
  else {  /* f < i <=> floor(f) < i */
    vmk_Integer fi;
    if (vmkV_flttointeger(f, &fi, F2Ifloor))  /* fi = floor(f) */
      return fi < i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/*
** Check whether float 'f' is less than or equal to integer 'i'.
** See comments on previous fn.
*/
l_sinline int LEfloatint (vmk_Number f, vmk_Integer i) {
  if (l_intfitsf(i))
    return vmki_numle(f, cast_num(i));  /* compare them as floats */
  else {  /* f <= i <=> ceil(f) <= i */
    vmk_Integer fi;
    if (vmkV_flttointeger(f, &fi, F2Iceil))  /* fi = ceil(f) */
      return fi <= i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/*
** Return 'l < r', for numbers.
*/
l_sinline int LTnum (const TValue *l, const TValue *r) {
  vmk_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisinteger(l)) {
    vmk_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li < ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LTintfloat(li, fltvalue(r));  /* l < r ? */
  }
  else {
    vmk_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return vmki_numlt(lf, fltvalue(r));  /* both are float */
    else  /* 'l' is float and 'r' is int */
      return LTfloatint(lf, ivalue(r));
  }
}


/*
** Return 'l <= r', for numbers.
*/
l_sinline int LEnum (const TValue *l, const TValue *r) {
  vmk_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisinteger(l)) {
    vmk_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li <= ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LEintfloat(li, fltvalue(r));  /* l <= r ? */
  }
  else {
    vmk_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return vmki_numle(lf, fltvalue(r));  /* both are float */
    else  /* 'l' is float and 'r' is int */
      return LEfloatint(lf, ivalue(r));
  }
}


/*
** return 'l < r' for non-numbers.
*/
static int lessthanothers (vmk_State *L, const TValue *l, const TValue *r) {
  vmk_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) < 0;
  else
    return vmkT_callorderTM(L, l, r, TM_LT);
}


/*
** Main operation less than; return 'l < r'.
*/
int vmkV_lessthan (vmk_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LTnum(l, r);
  else return lessthanothers(L, l, r);
}


/*
** return 'l <= r' for non-numbers.
*/
static int lessequalothers (vmk_State *L, const TValue *l, const TValue *r) {
  vmk_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) <= 0;
  else
    return vmkT_callorderTM(L, l, r, TM_LE);
}


/*
** Main operation less than or equal to; return 'l <= r'.
*/
int vmkV_lessequal (vmk_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LEnum(l, r);
  else return lessequalothers(L, l, r);
}


/*
** Main operation for equality of Vmk values; return 't1 == t2'.
** L == NULL means raw equality (no metamethods)
*/
int vmkV_equalobj (vmk_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  if (ttypetag(t1) != ttypetag(t2)) {  /* not the same variant? */
    if (ttype(t1) != ttype(t2) || ttype(t1) != VMK_TNUMBER)
      return 0;  /* only numbers can be equal with different variants */
    else {  /* two numbers with different variants */
      /* One of them is an integer. If the other does not have an
         integer value, they cannot be equal; otherwise, compare their
         integer values. */
      vmk_Integer i1, i2;
      return (vmkV_tointegerns(t1, &i1, F2Ieq) &&
              vmkV_tointegerns(t2, &i2, F2Ieq) &&
              i1 == i2);
    }
  }
  /* values have same type and same variant */
  switch (ttypetag(t1)) {
    case VMK_VNIL: case VMK_VFALSE: case VMK_VTRUE: return 1;
    case VMK_VNUMINT: return (ivalue(t1) == ivalue(t2));
    case VMK_VNUMFLT: return vmki_numeq(fltvalue(t1), fltvalue(t2));
    case VMK_VLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case VMK_VLCF: return fvalue(t1) == fvalue(t2);
    case VMK_VSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));
    case VMK_VLNGSTR: return vmkS_eqlngstr(tsvalue(t1), tsvalue(t2));
    case VMK_VUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = fasttm(L, uvalue(t1)->metatable, TM_EQ);
      if (tm == NULL)
        tm = fasttm(L, uvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    case VMK_VTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = fasttm(L, hvalue(t1)->metatable, TM_EQ);
      if (tm == NULL)
        tm = fasttm(L, hvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    default:
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL)  /* no TM? */
    return 0;  /* objects are different */
  else {
    int tag = vmkT_callTMres(L, tm, t1, t2, L->top.p);  /* call TM */
    return !tagisfalse(tag);
  }
}


/* macro used by 'vmkV_concat' to ensure that element at 'o' is a string */
#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (vmkO_tostring(L, o), 1)))

#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

/* copy strings in stack from top - n up to top - 1 to buffer */
static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;  /* size already copied */
  do {
    TString *st = tsvalue(s2v(top - n));
    size_t l;  /* length of string being copied */
    const char *s = getlstr(st, l);
    memcpy(buff + tl, s, l * sizeof(char));
    tl += l;
  } while (--n > 0);
}


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->top.p - total' up to 'L->top.p - 1'.
*/
void vmkV_concat (vmk_State *L, int total) {
  if (total == 1)
    return;  /* "all" values already concatenated */
  do {
    StkId top = L->top.p;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(s2v(top - 2)) || cvt2str(s2v(top - 2))) ||
        !tostring(L, s2v(top - 1)))
      vmkT_tryconcatTM(L);  /* may invalidate 'top' */
    else if (isemptystr(s2v(top - 1)))  /* second operand is empty? */
      cast_void(tostring(L, s2v(top - 2)));  /* result is first operand */
    else if (isemptystr(s2v(top - 2))) {  /* first operand is empty string? */
      setobjs2s(L, top - 2, top - 1);  /* result is second op. */
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = tsslen(tsvalue(s2v(top - 1)));
      TString *ts;
      /* collect total length and number of strings */
      for (n = 1; n < total && tostring(L, s2v(top - n - 1)); n++) {
        size_t l = tsslen(tsvalue(s2v(top - n - 1)));
        if (l_unlikely(l >= MAX_SIZE - sizeof(TString) - tl)) {
          L->top.p = top - total;  /* pop strings to avoid wasting stack */
          vmkG_runerror(L, "string length overflow");
        }
        tl += l;
      }
      if (tl <= VMKI_MAXSHORTLEN) {  /* is result a short string? */
        char buff[VMKI_MAXSHORTLEN];
        copy2buff(top, n, buff);  /* copy strings to buffer */
        ts = vmkS_newlstr(L, buff, tl);
      }
      else {  /* long string; copy strings directly to final result */
        ts = vmkS_createlngstrobj(L, tl);
        copy2buff(top, n, getlngstr(ts));
      }
      setsvalue2s(L, top - n, ts);  /* create result */
    }
    total -= n - 1;  /* got 'n' strings to create one new */
    L->top.p -= n - 1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


/*
** Main operation 'ra = #rb'.
*/
void vmkV_objlen (vmk_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypetag(rb)) {
    case VMK_VTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;  /* metamethod? break switch to call it */
      setivalue(s2v(ra), l_castU2S(vmkH_getn(h)));  /* else primitive len */
      return;
    }
    case VMK_VSHRSTR: {
      setivalue(s2v(ra), tsvalue(rb)->shrlen);
      return;
    }
    case VMK_VLNGSTR: {
      setivalue(s2v(ra), cast_st2S(tsvalue(rb)->u.lnglen));
      return;
    }
    default: {  /* try metamethod */
      tm = vmkT_gettmbyobj(L, rb, TM_LEN);
      if (l_unlikely(notm(tm)))  /* no metamethod? */
        vmkG_typeerror(L, rb, "get length of");
      break;
    }
  }
  vmkT_callTMres(L, tm, rb, rb, ra);
}


/*
** Integer division; return 'm // n', that is, floor(m/n).
** C division truncates its result (rounds towards zero).
** 'floor(q) == trunc(q)' when 'q >= 0' or when 'q' is integer,
** otherwise 'floor(q) == trunc(q) - 1'.
*/
vmk_Integer vmkV_idiv (vmk_State *L, vmk_Integer m, vmk_Integer n) {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  /* special cases: -1 or 0 */
    if (n == 0)
      vmkG_runerror(L, "attempt to divide by zero");
    return intop(-, 0, m);   /* n==-1; avoid overflow with 0x80000...//-1 */
  }
  else {
    vmk_Integer q = m / n;  /* perform C division */
    if ((m ^ n) < 0 && m % n != 0)  /* 'm/n' would be negative non-integer? */
      q -= 1;  /* correct result for different rounding */
    return q;
  }
}


/*
** Integer modulus; return 'm % n'. (Assume that C '%' with
** negative operands follows C99 behavior. See previous comment
** about vmkV_idiv.)
*/
vmk_Integer vmkV_mod (vmk_State *L, vmk_Integer m, vmk_Integer n) {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  /* special cases: -1 or 0 */
    if (n == 0)
      vmkG_runerror(L, "attempt to perform 'n%%0'");
    return 0;   /* m % -1 == 0; avoid overflow with 0x80000...%-1 */
  }
  else {
    vmk_Integer r = m % n;
    if (r != 0 && (r ^ n) < 0)  /* 'm/n' would be non-integer negative? */
      r += n;  /* correct result for different rounding */
    return r;
  }
}


/*
** Float modulus
*/
vmk_Number vmkV_modf (vmk_State *L, vmk_Number m, vmk_Number n) {
  vmk_Number r;
  vmki_nummod(L, m, n, r);
  return r;
}


/* number of bits in an integer */
#define NBITS	cast_int(sizeof(vmk_Integer) * CHAR_BIT)


/*
** Shift left operation. (Shift right just negates 'y'.)
*/
vmk_Integer vmkV_shiftl (vmk_Integer x, vmk_Integer y) {
  if (y < 0) {  /* shift right? */
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {  /* shift left */
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}


/*
** create a new Vmk closure, push it in the stack, and initialize
** its upvalues.
*/
static void pushclosure (vmk_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  LClosure *ncl = vmkF_newLclosure(L, nup);
  ncl->p = p;
  setclLvalue2s(L, ra, ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to lock variable? */
      ncl->upvals[i] = vmkF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing fn */
      ncl->upvals[i] = encup[uv[i].idx];
    vmkC_objbarrier(L, ncl, ncl->upvals[i]);
  }
}


/*
** finish execution of an opcode interrupted by a yield
*/
void vmkV_finishOp (vmk_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->func.p + 1;
  Instruction inst = *(ci->u.l.savedpc - 1);  /* interrupted instruction */
  OpCode op = GET_OPCODE(inst);
  switch (op) {  /* finish its execution */
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      setobjs2s(L, base + GETARG_A(*(ci->u.l.savedpc - 2)), --L->top.p);
      break;
    }
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_GETI:
    case OP_GETFIELD: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top.p);
      break;
    }
    case OP_LT: case OP_LE:
    case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_EQ: {  /* note that 'OP_EQI'/'OP_EQK' cannot yield */
      int res = !l_isfalse(s2v(L->top.p - 1));
      L->top.p--;
#if defined(VMK_COMPAT_LT_LE)
      if (ci->callstatus & CIST_LEQ) {  /* "<=" using "<" instead? */
        ci->callstatus ^= CIST_LEQ;  /* clear mark */
        res = !res;  /* negate result */
      }
#endif
      vmk_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_k(inst))  /* condition failed? */
        ci->u.l.savedpc++;  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top.p - 1;  /* top when 'vmkT_tryconcatTM' was called */
      int a = GETARG_A(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + a));  /* yet to concatenate */
      setobjs2s(L, top - 2, top);  /* put TM result in proper position */
      L->top.p = top - 1;  /* top is one after last element (at top-2) */
      vmkV_concat(L, total);  /* concat them (may yield again) */
      break;
    }
    case OP_CLOSE: {  /* yielded closing variables */
      ci->u.l.savedpc--;  /* repeat instruction to close other vars. */
      break;
    }
    case OP_RETURN: {  /* yielded closing variables */
      StkId ra = base + GETARG_A(inst);
      /* adjust top to signal correct number of returns, in case the
         return is "up to top" ('isIT') */
      L->top.p = ra + ci->u2.nres;
      /* repeat instruction to close other vars. and complete the return */
      ci->u.l.savedpc--;
      break;
    }
    default: {
      /* only these other opcodes can yield */
      vmk_assert(op == OP_TFORCALL || op == OP_CALL ||
           op == OP_TAILCALL || op == OP_SETTABUP || op == OP_SETTABLE ||
           op == OP_SETI || op == OP_SETFIELD);
      break;
    }
  }
}




/*
** {==================================================================
** Macros for arithmetic/bitwise/comparison opcodes in 'vmkV_execute'
** ===================================================================
*/

#define l_addi(L,a,b)	intop(+, a, b)
#define l_subi(L,a,b)	intop(-, a, b)
#define l_muli(L,a,b)	intop(*, a, b)
#define l_band(a,b)	intop(&, a, b)
#define l_bor(a,b)	intop(|, a, b)
#define l_bxor(a,b)	intop(^, a, b)

#define l_lti(a,b)	(a < b)
#define l_lei(a,b)	(a <= b)
#define l_gti(a,b)	(a > b)
#define l_gei(a,b)	(a >= b)


/*
** Arithmetic operations with immediate operands. 'iop' is the integer
** operation, 'fop' is the float operation.
*/
#define op_arithI(L,iop,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  int imm = GETARG_sC(i);  \
  if (ttisinteger(v1)) {  \
    vmk_Integer iv1 = ivalue(v1);  \
    pc++; setivalue(s2v(ra), iop(L, iv1, imm));  \
  }  \
  else if (ttisfloat(v1)) {  \
    vmk_Number nb = fltvalue(v1);  \
    vmk_Number fimm = cast_num(imm);  \
    pc++; setfltvalue(s2v(ra), fop(L, nb, fimm)); \
  }}


/*
** Auxiliary fn for arithmetic operations over floats and others
** with two operands.
*/
#define op_arithf_aux(L,v1,v2,fop) {  \
  vmk_Number n1; vmk_Number n2;  \
  if (tonumberns(v1, n1) && tonumberns(v2, n2)) {  \
    pc++; setfltvalue(s2v(ra), fop(L, n1, n2));  \
  }}


/*
** Arithmetic operations over floats and others with register operands.
*/
#define op_arithf(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arithf_aux(L, v1, v2, fop); }


/*
** Arithmetic operations with K operands for floats.
*/
#define op_arithfK(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); vmk_assert(ttisnumber(v2));  \
  op_arithf_aux(L, v1, v2, fop); }


/*
** Arithmetic operations over integers and floats.
*/
#define op_arith_aux(L,v1,v2,iop,fop) {  \
  StkId ra = RA(i); \
  if (ttisinteger(v1) && ttisinteger(v2)) {  \
    vmk_Integer i1 = ivalue(v1); vmk_Integer i2 = ivalue(v2);  \
    pc++; setivalue(s2v(ra), iop(L, i1, i2));  \
  }  \
  else op_arithf_aux(L, v1, v2, fop); }


/*
** Arithmetic operations with register operands.
*/
#define op_arith(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arith_aux(L, v1, v2, iop, fop); }


/*
** Arithmetic operations with K operands.
*/
#define op_arithK(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); vmk_assert(ttisnumber(v2));  \
  op_arith_aux(L, v1, v2, iop, fop); }


/*
** Bitwise operations with constant operand.
*/
#define op_bitwiseK(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i);  \
  vmk_Integer i1;  \
  vmk_Integer i2 = ivalue(v2);  \
  if (tointegerns(v1, &i1)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}


/*
** Bitwise operations with register operands.
*/
#define op_bitwise(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  vmk_Integer i1; vmk_Integer i2;  \
  if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}


/*
** Order operations with register operands. 'opn' actually works
** for all numbers, but the fast track improves performance for
** integers.
*/
#define op_order(L,opi,opn,other) {  \
  StkId ra = RA(i); \
  int cond;  \
  TValue *rb = vRB(i);  \
  if (ttisinteger(s2v(ra)) && ttisinteger(rb)) {  \
    vmk_Integer ia = ivalue(s2v(ra));  \
    vmk_Integer ib = ivalue(rb);  \
    cond = opi(ia, ib);  \
  }  \
  else if (ttisnumber(s2v(ra)) && ttisnumber(rb))  \
    cond = opn(s2v(ra), rb);  \
  else  \
    Protect(cond = other(L, s2v(ra), rb));  \
  docondjump(); }


/*
** Order operations with immediate operand. (Immediate operand is
** always small enough to have an exact representation as a float.)
*/
#define op_orderI(L,opi,opf,inv,tm) {  \
  StkId ra = RA(i); \
  int cond;  \
  int im = GETARG_sB(i);  \
  if (ttisinteger(s2v(ra)))  \
    cond = opi(ivalue(s2v(ra)), im);  \
  else if (ttisfloat(s2v(ra))) {  \
    vmk_Number fa = fltvalue(s2v(ra));  \
    vmk_Number fim = cast_num(im);  \
    cond = opf(fa, fim);  \
  }  \
  else {  \
    int isf = GETARG_C(i);  \
    Protect(cond = vmkT_callorderiTM(L, s2v(ra), im, inv, isf, tm));  \
  }  \
  docondjump(); }

/* }================================================================== */


/*
** {==================================================================
** Function 'vmkV_execute': main interpreter loop
** ===================================================================
*/

/*
** some macros for common tasks in 'vmkV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define RB(i)	(base+GETARG_B(i))
#define vRB(i)	s2v(RB(i))
#define KB(i)	(k+GETARG_B(i))
#define RC(i)	(base+GETARG_C(i))
#define vRC(i)	s2v(RC(i))
#define KC(i)	(k+GETARG_C(i))
#define RKC(i)	((TESTARG_k(i)) ? k + GETARG_C(i) : s2v(base + GETARG_C(i)))



#define updatetrap(ci)  (trap = ci->u.l.trap)

#define updatebase(ci)	(base = ci->func.p + 1)


#define updatestack(ci)  \
	{ if (l_unlikely(trap)) { updatebase(ci); ra = RA(i); } }


/*
** Execute a jump instruction. The 'updatetrap' allows signals to stop
** tight loops. (Without it, the lock copy of 'trap' could never change.)
*/
#define dojump(ci,i,e)	{ pc += GETARG_sJ(i) + e; updatetrap(ci); }


/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ Instruction ni = *pc; dojump(ci, ni, 1); }

/*
** do a conditional jump: skip next instruction if 'cond' is not what
** was expected (parameter 'k'), else do next instruction, which must
** be a jump.
*/
#define docondjump()	if (cond != GETARG_k(i)) pc++; else donextjump(ci);


/*
** Correct global 'pc'.
*/
#define savepc(L)	(ci->u.l.savedpc = pc)


/*
** Whenever code can raise errors, the global 'pc' and the global
** 'top' must be correct to report occasional errors.
*/
#define savestate(L,ci)		(savepc(L), L->top.p = ci->top.p)


/*
** Protect code that, in general, can raise errors, reallocate the
** stack, and change the hooks.
*/
#define Protect(exp)  (savestate(L,ci), (exp), updatetrap(ci))

/* special version that does not change the top */
#define ProtectNT(exp)  (savepc(L), (exp), updatetrap(ci))

/*
** Protect code that can only raise errors. (That is, it cannot change
** the stack or hooks.)
*/
#define halfProtect(exp)  (savestate(L,ci), (exp))

/*
** macro executed during Vmk functions at points where the
** fn can yield.
*/
#if !defined(vmki_threadyield)
#define vmki_threadyield(L)	{vmk_unlock(L); vmk_lock(L);}
#endif

/* 'c' is the limit of live values in the stack */
#define checkGC(L,c)  \
	{ vmkC_condGC(L, (savepc(L), L->top.p = (c)), \
                         updatetrap(ci)); \
           vmki_threadyield(L); }


/* fetch an instruction and prepare its execution */
#define vmfetch()	{ \
  if (l_unlikely(trap)) {  /* stack reallocation or hooks? */ \
    trap = vmkG_traceexec(L, pc);  /* handle hooks */ \
    updatebase(ci);  /* correct stack */ \
  } \
  i = *(pc++); \
}

#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


void vmkV_execute (vmk_State *L, CallInfo *ci) {
  LClosure *cl;
  TValue *k;
  StkId base;
  const Instruction *pc;
  int trap;
#if VMK_USE_JUMPTABLE
#include "ljumptab.h"
#endif
 startfunc:
  trap = L->hookmask;
 returning:  /* trap already set */
  cl = ci_func(ci);
  k = cl->p->k;
  pc = ci->u.l.savedpc;
  if (l_unlikely(trap))
    trap = vmkG_tracecall(L);
  base = ci->func.p + 1;
  /* main loop of interpreter */
  for (;;) {
    Instruction i;  /* instruction being executed */
    vmfetch();
    #if 0
    { /* low-level line tracing for debugging Vmk */
      #include "lopnames.h"
      int pcrel = pcRel(pc, cl->p);
      printf("line: %d; %s (%d)\n", vmkG_getfuncline(cl->p, pcrel),
             opnames[GET_OPCODE(i)], pcrel);
    }
    #endif
    vmk_assert(base == ci->func.p + 1);
    vmk_assert(base <= L->top.p && L->top.p <= L->stack_last.p);
    /* for tests, invalidate top for instructions not expecting it */
    vmk_assert(vmkP_isIT(i) || (cast_void(L->top.p = base), 1));
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE) {
        StkId ra = RA(i);
        setobjs2s(L, ra, RB(i));
        vmbreak;
      }
      vmcase(OP_LOADI) {
        StkId ra = RA(i);
        vmk_Integer b = GETARG_sBx(i);
        setivalue(s2v(ra), b);
        vmbreak;
      }
      vmcase(OP_LOADF) {
        StkId ra = RA(i);
        int b = GETARG_sBx(i);
        setfltvalue(s2v(ra), cast_num(b));
        vmbreak;
      }
      vmcase(OP_LOADK) {
        StkId ra = RA(i);
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADKX) {
        StkId ra = RA(i);
        TValue *rb;
        rb = k + GETARG_Ax(*pc); pc++;
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADFALSE) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LFALSESKIP) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        pc++;  /* skip next instruction */
        vmbreak;
      }
      vmcase(OP_LOADTRUE) {
        StkId ra = RA(i);
        setbtvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        do {
          setnilvalue(s2v(ra++));
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v.p);
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {
        StkId ra = RA(i);
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v.p, s2v(ra));
        vmkC_barrier(L, uv, s2v(ra));
        vmbreak;
      }
      vmcase(OP_GETTABUP) {
        StkId ra = RA(i);
        TValue *upval = cl->upvals[GETARG_B(i)]->v.p;
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        lu_byte tag;
        vmkV_fastget(upval, key, s2v(ra), vmkH_getshortstr, tag);
        if (tagisempty(tag))
          Protect(vmkV_finishget(L, upval, rc, ra, tag));
        vmbreak;
      }
      vmcase(OP_GETTABLE) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        lu_byte tag;
        if (ttisinteger(rc)) {  /* fast track for integers? */
          vmkV_fastgeti(rb, ivalue(rc), s2v(ra), tag);
        }
        else
          vmkV_fastget(rb, rc, s2v(ra), vmkH_get, tag);
        if (tagisempty(tag))
          Protect(vmkV_finishget(L, rb, rc, ra, tag));
        vmbreak;
      }
      vmcase(OP_GETI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int c = GETARG_C(i);
        lu_byte tag;
        vmkV_fastgeti(rb, c, s2v(ra), tag);
        if (tagisempty(tag)) {
          TValue key;
          setivalue(&key, c);
          Protect(vmkV_finishget(L, rb, &key, ra, tag));
        }
        vmbreak;
      }
      vmcase(OP_GETFIELD) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        lu_byte tag;
        vmkV_fastget(rb, key, s2v(ra), vmkH_getshortstr, tag);
        if (tagisempty(tag))
          Protect(vmkV_finishget(L, rb, rc, ra, tag));
        vmbreak;
      }
      vmcase(OP_SETTABUP) {
        int hres;
        TValue *upval = cl->upvals[GETARG_A(i)]->v.p;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        vmkV_fastset(upval, key, rc, hres, vmkH_psetshortstr);
        if (hres == HOK)
          vmkV_finishfastset(L, upval, rc);
        else
          Protect(vmkV_finishset(L, upval, rb, rc, hres));
        vmbreak;
      }
      vmcase(OP_SETTABLE) {
        StkId ra = RA(i);
        int hres;
        TValue *rb = vRB(i);  /* key (table is in 'ra') */
        TValue *rc = RKC(i);  /* value */
        if (ttisinteger(rb)) {  /* fast track for integers? */
          vmkV_fastseti(s2v(ra), ivalue(rb), rc, hres);
        }
        else {
          vmkV_fastset(s2v(ra), rb, rc, hres, vmkH_pset);
        }
        if (hres == HOK)
          vmkV_finishfastset(L, s2v(ra), rc);
        else
          Protect(vmkV_finishset(L, s2v(ra), rb, rc, hres));
        vmbreak;
      }
      vmcase(OP_SETI) {
        StkId ra = RA(i);
        int hres;
        int b = GETARG_B(i);
        TValue *rc = RKC(i);
        vmkV_fastseti(s2v(ra), b, rc, hres);
        if (hres == HOK)
          vmkV_finishfastset(L, s2v(ra), rc);
        else {
          TValue key;
          setivalue(&key, b);
          Protect(vmkV_finishset(L, s2v(ra), &key, rc, hres));
        }
        vmbreak;
      }
      vmcase(OP_SETFIELD) {
        StkId ra = RA(i);
        int hres;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        vmkV_fastset(s2v(ra), key, rc, hres, vmkH_psetshortstr);
        if (hres == HOK)
          vmkV_finishfastset(L, s2v(ra), rc);
        else
          Protect(vmkV_finishset(L, s2v(ra), rb, rc, hres));
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        StkId ra = RA(i);
        unsigned b = cast_uint(GETARG_vB(i));  /* log2(hash size) + 1 */
        unsigned c = cast_uint(GETARG_vC(i));  /* array size */
        Table *t;
        if (b > 0)
          b = 1u << (b - 1);  /* hash size is 2^(b - 1) */
        if (TESTARG_k(i)) {  /* non-zero extra argument? */
          vmk_assert(GETARG_Ax(*pc) != 0);
          /* add it to array size */
          c += cast_uint(GETARG_Ax(*pc)) * (MAXARG_vC + 1);
        }
        pc++;  /* skip extra argument */
        L->top.p = ra + 1;  /* correct top in case of emergency GC */
        t = vmkH_new(L);  /* memory allocation */
        sethvalue2s(L, ra, t);
        if (b != 0 || c != 0)
          vmkH_resize(L, t, c, b);  /* idem */
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {
        StkId ra = RA(i);
        lu_byte tag;
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        setobj2s(L, ra + 1, rb);
        vmkV_fastget(rb, key, s2v(ra), vmkH_getshortstr, tag);
        if (tagisempty(tag))
          Protect(vmkV_finishget(L, rb, rc, ra, tag));
        vmbreak;
      }
      vmcase(OP_ADDI) {
        op_arithI(L, l_addi, vmki_numadd);
        vmbreak;
      }
      vmcase(OP_ADDK) {
        op_arithK(L, l_addi, vmki_numadd);
        vmbreak;
      }
      vmcase(OP_SUBK) {
        op_arithK(L, l_subi, vmki_numsub);
        vmbreak;
      }
      vmcase(OP_MULK) {
        op_arithK(L, l_muli, vmki_nummul);
        vmbreak;
      }
      vmcase(OP_MODK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(L, vmkV_mod, vmkV_modf);
        vmbreak;
      }
      vmcase(OP_POWK) {
        op_arithfK(L, vmki_numpow);
        vmbreak;
      }
      vmcase(OP_DIVK) {
        op_arithfK(L, vmki_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIVK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(L, vmkV_idiv, vmki_numidiv);
        vmbreak;
      }
      vmcase(OP_BANDK) {
        op_bitwiseK(L, l_band);
        vmbreak;
      }
      vmcase(OP_BORK) {
        op_bitwiseK(L, l_bor);
        vmbreak;
      }
      vmcase(OP_BXORK) {
        op_bitwiseK(L, l_bxor);
        vmbreak;
      }
      vmcase(OP_SHRI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        vmk_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), vmkV_shiftl(ib, -ic));
        }
        vmbreak;
      }
      vmcase(OP_SHLI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        vmk_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), vmkV_shiftl(ic, ib));
        }
        vmbreak;
      }
      vmcase(OP_ADD) {
        op_arith(L, l_addi, vmki_numadd);
        vmbreak;
      }
      vmcase(OP_SUB) {
        op_arith(L, l_subi, vmki_numsub);
        vmbreak;
      }
      vmcase(OP_MUL) {
        op_arith(L, l_muli, vmki_nummul);
        vmbreak;
      }
      vmcase(OP_MOD) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(L, vmkV_mod, vmkV_modf);
        vmbreak;
      }
      vmcase(OP_POW) {
        op_arithf(L, vmki_numpow);
        vmbreak;
      }
      vmcase(OP_DIV) {  /* float division (always with floats) */
        op_arithf(L, vmki_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIV) {  /* floor division */
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(L, vmkV_idiv, vmki_numidiv);
        vmbreak;
      }
      vmcase(OP_BAND) {
        op_bitwise(L, l_band);
        vmbreak;
      }
      vmcase(OP_BOR) {
        op_bitwise(L, l_bor);
        vmbreak;
      }
      vmcase(OP_BXOR) {
        op_bitwise(L, l_bxor);
        vmbreak;
      }
      vmcase(OP_SHR) {
        op_bitwise(L, vmkV_shiftr);
        vmbreak;
      }
      vmcase(OP_SHL) {
        op_bitwise(L, vmkV_shiftl);
        vmbreak;
      }
      vmcase(OP_MMBIN) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *rb = vRB(i);
        TMS tm = (TMS)GETARG_C(i);
        StkId result = RA(pi);
        vmk_assert(OP_ADD <= GET_OPCODE(pi) && GET_OPCODE(pi) <= OP_SHR);
        Protect(vmkT_trybinTM(L, s2v(ra), rb, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINI) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        int imm = GETARG_sB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(vmkT_trybiniTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINK) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *imm = KB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(vmkT_trybinassocTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_UNM) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        vmk_Number nb;
        if (ttisinteger(rb)) {
          vmk_Integer ib = ivalue(rb);
          setivalue(s2v(ra), intop(-, 0, ib));
        }
        else if (tonumberns(rb, nb)) {
          setfltvalue(s2v(ra), vmki_numunm(L, nb));
        }
        else
          Protect(vmkT_trybinTM(L, rb, rb, ra, TM_UNM));
        vmbreak;
      }
      vmcase(OP_BNOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        vmk_Integer ib;
        if (tointegerns(rb, &ib)) {
          setivalue(s2v(ra), intop(^, ~l_castS2U(0), ib));
        }
        else
          Protect(vmkT_trybinTM(L, rb, rb, ra, TM_BNOT));
        vmbreak;
      }
      vmcase(OP_NOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb))
          setbtvalue(s2v(ra));
        else
          setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LEN) {
        StkId ra = RA(i);
        Protect(vmkV_objlen(L, ra, vRB(i)));
        vmbreak;
      }
      vmcase(OP_CONCAT) {
        StkId ra = RA(i);
        int n = GETARG_B(i);  /* number of elements to concatenate */
        L->top.p = ra + n;  /* mark the end of concat operands */
        ProtectNT(vmkV_concat(L, n));
        checkGC(L, L->top.p); /* 'vmkV_concat' ensures correct top */
        vmbreak;
      }
      vmcase(OP_CLOSE) {
        StkId ra = RA(i);
        vmk_assert(!GETARG_B(i));  /* 'close must be alive */
        Protect(vmkF_close(L, ra, VMK_OK, 1));
        vmbreak;
      }
      vmcase(OP_TBC) {
        StkId ra = RA(i);
        /* create new to-be-closed upvalue */
        halfProtect(vmkF_newtbcupval(L, ra));
        vmbreak;
      }
      vmcase(OP_JMP) {
        dojump(ci, i, 0);
        vmbreak;
      }
      vmcase(OP_EQ) {
        StkId ra = RA(i);
        int cond;
        TValue *rb = vRB(i);
        Protect(cond = vmkV_equalobj(L, s2v(ra), rb));
        docondjump();
        vmbreak;
      }
      vmcase(OP_LT) {
        op_order(L, l_lti, LTnum, lessthanothers);
        vmbreak;
      }
      vmcase(OP_LE) {
        op_order(L, l_lei, LEnum, lessequalothers);
        vmbreak;
      }
      vmcase(OP_EQK) {
        StkId ra = RA(i);
        TValue *rb = KB(i);
        /* basic types do not use '__eq'; we can use raw equality */
        int cond = vmkV_rawequalobj(s2v(ra), rb);
        docondjump();
        vmbreak;
      }
      vmcase(OP_EQI) {
        StkId ra = RA(i);
        int cond;
        int im = GETARG_sB(i);
        if (ttisinteger(s2v(ra)))
          cond = (ivalue(s2v(ra)) == im);
        else if (ttisfloat(s2v(ra)))
          cond = vmki_numeq(fltvalue(s2v(ra)), cast_num(im));
        else
          cond = 0;  /* other types cannot be equal to a number */
        docondjump();
        vmbreak;
      }
      vmcase(OP_LTI) {
        op_orderI(L, l_lti, vmki_numlt, 0, TM_LT);
        vmbreak;
      }
      vmcase(OP_LEI) {
        op_orderI(L, l_lei, vmki_numle, 0, TM_LE);
        vmbreak;
      }
      vmcase(OP_GTI) {
        op_orderI(L, l_gti, vmki_numgt, 1, TM_LT);
        vmbreak;
      }
      vmcase(OP_GEI) {
        op_orderI(L, l_gei, vmki_numge, 1, TM_LE);
        vmbreak;
      }
      vmcase(OP_TEST) {
        StkId ra = RA(i);
        int cond = !l_isfalse(s2v(ra));
        docondjump();
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb) == GETARG_k(i))
          pc++;
        else {
          setobj2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {
        StkId ra = RA(i);
        CallInfo *newci;
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0)  /* fixed number of arguments? */
          L->top.p = ra + b;  /* top signals number of arguments */
        /* else previous instruction set top */
        savepc(L);  /* in case of errors */
        if ((newci = vmkD_precall(L, ra, nresults)) == NULL)
          updatetrap(ci);  /* C call; nothing else to be done */
        else {  /* Vmk call: run fn in this same C frame */
          ci = newci;
          goto startfunc;
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);  /* number of arguments + 1 (fn) */
        int n;  /* number of results when calling a C fn */
        int nparams1 = GETARG_C(i);
        /* delta is virtual 'func' - real 'func' (vararg functions) */
        int delta = (nparams1) ? ci->u.l.nextraargs + nparams1 : 0;
        if (b != 0)
          L->top.p = ra + b;
        else  /* previous instruction set top */
          b = cast_int(L->top.p - ra);
        savepc(ci);  /* several calls here can raise errors */
        if (TESTARG_k(i)) {
          vmkF_closeupval(L, base);  /* close upvalues from current call */
          vmk_assert(L->tbclist.p < base);  /* no pending tbc variables */
          vmk_assert(base == ci->func.p + 1);
        }
        if ((n = vmkD_pretailcall(L, ci, ra, b, delta)) < 0)  /* Vmk fn? */
          goto startfunc;  /* execute the callee */
        else {  /* C fn? */
          ci->func.p -= delta;  /* restore 'func' (if vararg) */
          vmkD_poscall(L, ci, n);  /* finish caller */
          updatetrap(ci);  /* 'vmkD_poscall' can change hooks */
          goto ret;  /* caller returns after the tail call */
        }
      }
      vmcase(OP_RETURN) {
        StkId ra = RA(i);
        int n = GETARG_B(i) - 1;  /* number of results */
        int nparams1 = GETARG_C(i);
        if (n < 0)  /* not fixed? */
          n = cast_int(L->top.p - ra);  /* get what is available */
        savepc(ci);
        if (TESTARG_k(i)) {  /* may there be open upvalues? */
          ci->u2.nres = n;  /* save number of returns */
          if (L->top.p < ci->top.p)
            L->top.p = ci->top.p;
          vmkF_close(L, base, CLOSEKTOP, 1);
          updatetrap(ci);
          updatestack(ci);
        }
        if (nparams1)  /* vararg fn? */
          ci->func.p -= ci->u.l.nextraargs + nparams1;
        L->top.p = ra + n;  /* set call for 'vmkD_poscall' */
        vmkD_poscall(L, ci, n);
        updatetrap(ci);  /* 'vmkD_poscall' can change hooks */
        goto ret;
      }
      vmcase(OP_RETURN0) {
        if (l_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra;
          savepc(ci);
          vmkD_poscall(L, ci, 0);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres = get_nresults(ci->callstatus);
          L->ci = ci->previous;  /* back to caller */
          L->top.p = base - 1;
          for (; l_unlikely(nres > 0); nres--)
            setnilvalue(s2v(L->top.p++));  /* all results are nil */
        }
        goto ret;
      }
      vmcase(OP_RETURN1) {
        if (l_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra + 1;
          savepc(ci);
          vmkD_poscall(L, ci, 1);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres = get_nresults(ci->callstatus);
          L->ci = ci->previous;  /* back to caller */
          if (nres == 0)
            L->top.p = base - 1;  /* asked for no results */
          else {
            StkId ra = RA(i);
            setobjs2s(L, base - 1, ra);  /* at least this result */
            L->top.p = base;
            for (; l_unlikely(nres > 1); nres--)
              setnilvalue(s2v(L->top.p++));  /* complete missing results */
          }
        }
       ret:  /* return from a Vmk fn */
        if (ci->callstatus & CIST_FRESH)
          return;  /* end this frame */
        else {
          ci = ci->previous;
          goto returning;  /* continue running caller in this frame */
        }
      }
      vmcase(OP_FORLOOP) {
        StkId ra = RA(i);
        if (ttisinteger(s2v(ra + 1))) {  /* integer loop? */
          vmk_Unsigned count = l_castS2U(ivalue(s2v(ra)));
          if (count > 0) {  /* still more iterations? */
            vmk_Integer step = ivalue(s2v(ra + 1));
            vmk_Integer idx = ivalue(s2v(ra + 2));  /* control variable */
            chgivalue(s2v(ra), l_castU2S(count - 1));  /* update counter */
            idx = intop(+, idx, step);  /* add step to index */
            chgivalue(s2v(ra + 2), idx);  /* update control variable */
            pc -= GETARG_Bx(i);  /* jump back */
          }
        }
        else if (floatforloop(ra))  /* float loop */
          pc -= GETARG_Bx(i);  /* jump back */
        updatetrap(ci);  /* allows a signal to break the loop */
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        StkId ra = RA(i);
        savestate(L, ci);  /* in case of errors */
        if (forprep(L, ra))
          pc += GETARG_Bx(i) + 1;  /* skip the loop */
        vmbreak;
      }
      vmcase(OP_TFORPREP) {
       /* before: 'ra' has the iterator fn, 'ra + 1' has the state,
          'ra + 2' has the initial value for the control variable, and
          'ra + 3' has the closing variable. This opcode then swaps the
          control and the closing variables and marks the closing variable
          as to-be-closed.
       */
       StkId ra = RA(i);
       TValue temp;  /* to swap control and closing variables */
       setobj(L, &temp, s2v(ra + 3));
       setobjs2s(L, ra + 3, ra + 2);
       setobj2s(L, ra + 2, &temp);
        /* create to-be-closed upvalue (if closing var. is not nil) */
        halfProtect(vmkF_newtbcupval(L, ra + 2));
        pc += GETARG_Bx(i);  /* go to end of the loop */
        i = *(pc++);  /* fetch next instruction */
        vmk_assert(GET_OPCODE(i) == OP_TFORCALL && ra == RA(i));
        goto l_tforcall;
      }
      vmcase(OP_TFORCALL) {
       l_tforcall: {
        /* 'ra' has the iterator fn, 'ra + 1' has the state,
           'ra + 2' has the closing variable, and 'ra + 3' has the control
           variable. The call will use the stack starting at 'ra + 3',
           so that it preserves the first three values, and the first
           return will be the new value for the control variable.
        */
        StkId ra = RA(i);
        setobjs2s(L, ra + 5, ra + 3);  /* copy the control variable */
        setobjs2s(L, ra + 4, ra + 1);  /* copy state */
        setobjs2s(L, ra + 3, ra);  /* copy fn */
        L->top.p = ra + 3 + 3;
        ProtectNT(vmkD_call(L, ra + 3, GETARG_C(i)));  /* do the call */
        updatestack(ci);  /* stack may have changed */
        i = *(pc++);  /* go to next instruction */
        vmk_assert(GET_OPCODE(i) == OP_TFORLOOP && ra == RA(i));
        goto l_tforloop;
      }}
      vmcase(OP_TFORLOOP) {
       l_tforloop: {
        StkId ra = RA(i);
        if (!ttisnil(s2v(ra + 3)))  /* continue loop? */
          pc -= GETARG_Bx(i);  /* jump back */
        vmbreak;
      }}
      vmcase(OP_SETLIST) {
        StkId ra = RA(i);
        unsigned n = cast_uint(GETARG_vB(i));
        unsigned int last = cast_uint(GETARG_vC(i));
        Table *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_uint(L->top.p - ra) - 1;  /* get up to the top */
        else
          L->top.p = ci->top.p;  /* correct top in case of emergency GC */
        last += n;
        if (TESTARG_k(i)) {
          last += cast_uint(GETARG_Ax(*pc)) * (MAXARG_vC + 1);
          pc++;
        }
        /* when 'n' is known, table should have proper size */
        if (last > h->asize) {  /* needs more space? */
          /* fixed-size sets should have space preallocated */
          vmk_assert(GETARG_vB(i) == 0);
          vmkH_resizearray(L, h, last);  /* preallocate it at once */
        }
        for (; n > 0; n--) {
          TValue *val = s2v(ra + n);
          obj2arr(h, last - 1, val);
          last--;
          vmkC_barrierback(L, obj2gco(h), val);
        }
        vmbreak;
      }
      vmcase(OP_CLOSURE) {
        StkId ra = RA(i);
        Proto *p = cl->p->p[GETARG_Bx(i)];
        halfProtect(pushclosure(L, p, cl->upvals, base, ra));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {
        StkId ra = RA(i);
        int n = GETARG_C(i) - 1;  /* required results */
        Protect(vmkT_getvarargs(L, ci, ra, n));
        vmbreak;
      }
      vmcase(OP_VARARGPREP) {
        ProtectNT(vmkT_adjustvarargs(L, GETARG_A(i), ci, cl->p));
        if (l_unlikely(trap)) {  /* previous "Protect" updated trap */
          vmkD_hookcall(L, ci);
          L->oldpc = 1;  /* next opcode will be seen as a "new" line */
        }
        updatebase(ci);  /* fn has new base after adjustment */
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        vmk_assert(0);
        vmbreak;
      }
    }
  }
}

/* }================================================================== */
