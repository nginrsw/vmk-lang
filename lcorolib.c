/*
** $Id: lcorolib.c $
** Coroutine Library
** See Copyright Notice in vmk.h
*/

#define lcorolib_c
#define VMK_LIB

#include "lprefix.h"


#include <stdlib.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


static vmk_State *getco (vmk_State *L) {
  vmk_State *co = vmk_tothread(L, 1);
  vmkL_argexpected(L, co, 1, "thread");
  return co;
}


/*
** Resumes a coroutine. Returns the number of results for non-error
** cases or -1 for errors.
*/
static int auxresume (vmk_State *L, vmk_State *co, int narg) {
  int status, nres;
  if (l_unlikely(!vmk_checkstack(co, narg))) {
    vmk_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  vmk_xmove(L, co, narg);
  status = vmk_resume(co, L, narg, &nres);
  if (l_likely(status == VMK_OK || status == VMK_YIELD)) {
    if (l_unlikely(!vmk_checkstack(L, nres + 1))) {
      vmk_pop(co, nres);  /* remove results anyway */
      vmk_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    vmk_xmove(co, L, nres);  /* move yielded values */
    return nres;
  }
  else {
    vmk_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}


static int vmkB_coresume (vmk_State *L) {
  vmk_State *co = getco(L);
  int r;
  r = auxresume(L, co, vmk_gettop(L) - 1);
  if (l_unlikely(r < 0)) {
    vmk_pushboolean(L, 0);
    vmk_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    vmk_pushboolean(L, 1);
    vmk_insert(L, -(r + 1));
    return r + 1;  /* return true + 'resume' returns */
  }
}


static int vmkB_auxwrap (vmk_State *L) {
  vmk_State *co = vmk_tothread(L, vmk_upvalueindex(1));
  int r = auxresume(L, co, vmk_gettop(L));
  if (l_unlikely(r < 0)) {  /* error? */
    int stat = vmk_status(co);
    if (stat != VMK_OK && stat != VMK_YIELD) {  /* error in the coroutine? */
      stat = vmk_closethread(co, L);  /* close its tbc variables */
      vmk_assert(stat != VMK_OK);
      vmk_xmove(co, L, 1);  /* move error message to the caller */
    }
    if (stat != VMK_ERRMEM &&  /* not a memory error and ... */
        vmk_type(L, -1) == VMK_TSTRING) {  /* ... error object is a string? */
      vmkL_where(L, 1);  /* add extra info, if available */
      vmk_insert(L, -2);
      vmk_concat(L, 2);
    }
    return vmk_error(L);  /* propagate error */
  }
  return r;
}


static int vmkB_cocreate (vmk_State *L) {
  vmk_State *NL;
  vmkL_checktype(L, 1, VMK_TFUNCTION);
  NL = vmk_newthread(L);
  vmk_pushvalue(L, 1);  /* move fn to top */
  vmk_xmove(L, NL, 1);  /* move fn from L to NL */
  return 1;
}


static int vmkB_cowrap (vmk_State *L) {
  vmkB_cocreate(L);
  vmk_pushcclosure(L, vmkB_auxwrap, 1);
  return 1;
}


static int vmkB_yield (vmk_State *L) {
  return vmk_yield(L, vmk_gettop(L));
}


#define COS_RUN		0
#define COS_DEAD	1
#define COS_YIELD	2
#define COS_NORM	3


static const char *const statname[] =
  {"running", "dead", "suspended", "normal"};


static int auxstatus (vmk_State *L, vmk_State *co) {
  if (L == co) return COS_RUN;
  else {
    switch (vmk_status(co)) {
      case VMK_YIELD:
        return COS_YIELD;
      case VMK_OK: {
        vmk_Debug ar;
        if (vmk_getstack(co, 0, &ar))  /* does it have frames? */
          return COS_NORM;  /* it is running */
        else if (vmk_gettop(co) == 0)
            return COS_DEAD;
        else
          return COS_YIELD;  /* initial state */
      }
      default:  /* some error occurred */
        return COS_DEAD;
    }
  }
}


static int vmkB_costatus (vmk_State *L) {
  vmk_State *co = getco(L);
  vmk_pushstring(L, statname[auxstatus(L, co)]);
  return 1;
}


static int vmkB_yieldable (vmk_State *L) {
  vmk_State *co = vmk_isnone(L, 1) ? L : getco(L);
  vmk_pushboolean(L, vmk_isyieldable(co));
  return 1;
}


static int vmkB_corunning (vmk_State *L) {
  int ismain = vmk_pushthread(L);
  vmk_pushboolean(L, ismain);
  return 2;
}


static int vmkB_close (vmk_State *L) {
  vmk_State *co = getco(L);
  int status = auxstatus(L, co);
  switch (status) {
    case COS_DEAD: case COS_YIELD: {
      status = vmk_closethread(co, L);
      if (status == VMK_OK) {
        vmk_pushboolean(L, 1);
        return 1;
      }
      else {
        vmk_pushboolean(L, 0);
        vmk_xmove(co, L, 1);  /* move error message */
        return 2;
      }
    }
    default:  /* normal or running coroutine */
      return vmkL_error(L, "cannot close a %s coroutine", statname[status]);
  }
}


static const vmkL_Reg co_funcs[] = {
  {"create", vmkB_cocreate},
  {"resume", vmkB_coresume},
  {"running", vmkB_corunning},
  {"status", vmkB_costatus},
  {"wrap", vmkB_cowrap},
  {"yield", vmkB_yield},
  {"isyieldable", vmkB_yieldable},
  {"close", vmkB_close},
  {NULL, NULL}
};



VMKMOD_API int vmkopen_coroutine (vmk_State *L) {
  vmkL_newlib(L, co_funcs);
  return 1;
}

