/*
** $Id: loslib.c $
** Standard Operating System library
** See Copyright Notice in vmk.h
*/

#define loslib_c
#define VMK_LIB

#include "lprefix.h"


#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


/*
** {==================================================================
** List of valid conversion specifiers for the 'strftime' fn;
** options are grouped by length; group of length 2 start with '||'.
** ===================================================================
*/
#if !defined(VMK_STRFTIMEOPTIONS)	/* { */

#if defined(VMK_USE_WINDOWS)
#define VMK_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"  /* two-char options */
#elif defined(VMK_USE_C89)  /* ANSI C 89 (only 1-char options) */
#define VMK_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYZ%"
#else  /* C99 specification */
#define VMK_STRFTIMEOPTIONS  "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"  /* two-char options */
#endif

#endif					/* } */
/* }================================================================== */


/*
** {==================================================================
** Configuration for time-related stuff
** ===================================================================
*/

/*
** type to represent time_t in Vmk
*/
#if !defined(VMK_NUMTIME)	/* { */

#define l_timet			vmk_Integer
#define l_pushtime(L,t)		vmk_pushinteger(L,(vmk_Integer)(t))
#define l_gettime(L,arg)	vmkL_checkinteger(L, arg)

#else				/* }{ */

#define l_timet			vmk_Number
#define l_pushtime(L,t)		vmk_pushnumber(L,(vmk_Number)(t))
#define l_gettime(L,arg)	vmkL_checknumber(L, arg)

#endif				/* } */


#if !defined(l_gmtime)		/* { */
/*
** By default, Vmk uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(VMK_USE_POSIX)	/* { */

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else				/* }{ */

/* ISO C definitions */
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif				/* } */

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Configuration for 'tmpnam':
** By default, Vmk uses tmpnam except when POSIX is available, where
** it uses mkstemp.
** ===================================================================
*/
#if !defined(vmk_tmpnam)	/* { */

#if defined(VMK_USE_POSIX)	/* { */

#include <unistd.h>

#define VMK_TMPNAMBUFSIZE	32

#if !defined(VMK_TMPNAMTEMPLATE)
#define VMK_TMPNAMTEMPLATE	"/tmp/vmk_XXXXXX"
#endif

#define vmk_tmpnam(b,e) { \
        strcpy(b, VMK_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				/* }{ */

/* ISO C definitions */
#define VMK_TMPNAMBUFSIZE	L_tmpnam
#define vmk_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				/* } */

#endif				/* } */
/* }================================================================== */


#if !defined(l_system)
#if defined(VMK_USE_IOS)
/* Despite claiming to be ISO C, iOS does not implement 'system'. */
#define l_system(cmd) ((cmd) == NULL ? 0 : -1)
#else
#define l_system(cmd)	system(cmd)  /* default definition */
#endif
#endif


static int os_execute (vmk_State *L) {
  const char *cmd = vmkL_optstring(L, 1, NULL);
  int stat;
  errno = 0;
  stat = l_system(cmd);
  if (cmd != NULL)
    return vmkL_execresult(L, stat);
  else {
    vmk_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (vmk_State *L) {
  const char *filename = vmkL_checkstring(L, 1);
  errno = 0;
  return vmkL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (vmk_State *L) {
  const char *fromname = vmkL_checkstring(L, 1);
  const char *toname = vmkL_checkstring(L, 2);
  errno = 0;
  return vmkL_fileresult(L, rename(fromname, toname) == 0, NULL);
}


static int os_tmpname (vmk_State *L) {
  char buff[VMK_TMPNAMBUFSIZE];
  int err;
  vmk_tmpnam(buff, err);
  if (l_unlikely(err))
    return vmkL_error(L, "unable to generate a unique filename");
  vmk_pushstring(L, buff);
  return 1;
}


static int os_getenv (vmk_State *L) {
  vmk_pushstring(L, getenv(vmkL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (vmk_State *L) {
  vmk_pushnumber(L, ((vmk_Number)clock())/(vmk_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

/*
** About the overflow check: an overflow cannot occur when time
** is represented by a vmk_Integer, because either vmk_Integer is
** large enough to represent all int fields or it is not large enough
** to represent a time that cause a field to overflow.  However, if
** times are represented as doubles and vmk_Integer is int, then the
** time 0x1.e1853b0d184f6p+55 would cause an overflow when adding 1900
** to compute the year.
*/
static void setfield (vmk_State *L, const char *key, int value, int delta) {
  #if (defined(VMK_NUMTIME) && VMK_MAXINTEGER <= INT_MAX)
    if (l_unlikely(value > VMK_MAXINTEGER - delta))
      vmkL_error(L, "field '%s' is out-of-bound", key);
  #endif
  vmk_pushinteger(L, (vmk_Integer)value + delta);
  vmk_setfield(L, -2, key);
}


static void setboolfield (vmk_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  vmk_pushboolean(L, value);
  vmk_setfield(L, -2, key);
}


/*
** Set all fields from structure 'tm' in the table on top of the stack
*/
static void setallfields (vmk_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}


static int getboolfield (vmk_State *L, const char *key) {
  int res;
  res = (vmk_getfield(L, -1, key) == VMK_TNIL) ? -1 : vmk_toboolean(L, -1);
  vmk_pop(L, 1);
  return res;
}


static int getfield (vmk_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = vmk_getfield(L, -1, key);  /* get field and its type */
  vmk_Integer res = vmk_tointegerx(L, -1, &isnum);
  if (!isnum) {  /* field is not an integer? */
    if (l_unlikely(t != VMK_TNIL))  /* some other value? */
      return vmkL_error(L, "field '%s' is not an integer", key);
    else if (l_unlikely(d < 0))  /* absent field; no default? */
      return vmkL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res))
      return vmkL_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  vmk_pop(L, 1);
  return (int)res;
}


static const char *checkoption (vmk_State *L, const char *conv,
                                ptrdiff_t convlen, char *buff) {
  const char *option = VMK_STRFTIMEOPTIONS;
  unsigned oplen = 1;  /* length of options being checked */
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')  /* next block? */
      oplen++;  /* will check options with next length (+1) */
    else if (memcmp(conv, option, oplen) == 0) {  /* match? */
      memcpy(buff, conv, oplen);  /* copy valid option to buffer */
      buff[oplen] = '\0';
      return conv + oplen;  /* return next item */
    }
  }
  vmkL_argerror(L, 1,
    vmk_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static time_t l_checktime (vmk_State *L, int arg) {
  l_timet t = l_gettime(L, arg);
  vmkL_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}


/* maximum size for an individual 'strftime' item */
#define SIZETIMEFMT	250


static int os_date (vmk_State *L) {
  size_t slen;
  const char *s = vmkL_optlstring(L, 1, "%c", &slen);
  time_t t = vmkL_opt(L, l_checktime, 2, time(NULL));
  const char *se = s + slen;  /* 's' end */
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip '!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    return vmkL_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    vmk_createtable(L, 0, 9);  /* 9 = number of fields */
    setallfields(L, stm);
  }
  else {
    char cc[4];  /* buffer for individual conversion specifiers */
    vmkL_Buffer b;
    cc[0] = '%';
    vmkL_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')  /* not a conversion specifier? */
        vmkL_addchar(&b, *s++);
      else {
        size_t reslen;
        char *buff = vmkL_prepbuffsize(&b, SIZETIMEFMT);
        s++;  /* skip '%' */
        s = checkoption(L, s, se - s, cc + 1);  /* copy specifier to 'cc' */
        reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        vmkL_addsize(&b, reslen);
      }
    }
    vmkL_pushresult(&b);
  }
  return 1;
}


static int os_time (vmk_State *L) {
  time_t t;
  if (vmk_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    vmkL_checktype(L, 1, VMK_TTABLE);
    vmk_settop(L, 1);  /* make sure table is at the top */
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);  /* update fields with normalized values */
  }
  if (t != (time_t)(l_timet)t || t == (time_t)(-1))
    return vmkL_error(L,
                  "time result cannot be represented in this installation");
  l_pushtime(L, t);
  return 1;
}


static int os_difftime (vmk_State *L) {
  time_t t1 = l_checktime(L, 1);
  time_t t2 = l_checktime(L, 2);
  vmk_pushnumber(L, (vmk_Number)difftime(t1, t2));
  return 1;
}

/* }====================================================== */


static int os_setlocale (vmk_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = vmkL_optstring(L, 1, NULL);
  int op = vmkL_checkoption(L, 2, "all", catnames);
  vmk_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (vmk_State *L) {
  int status;
  if (vmk_isboolean(L, 1))
    status = (vmk_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)vmkL_optinteger(L, 1, EXIT_SUCCESS);
  if (vmk_toboolean(L, 2))
    vmk_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static const vmkL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



VMKMOD_API int vmkopen_os (vmk_State *L) {
  vmkL_newlib(L, syslib);
  return 1;
}

