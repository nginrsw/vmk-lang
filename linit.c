/*
** $Id: linit.c $
** Initialization of libraries for vmk.c and other clients
** See Copyright Notice in vmk.h
*/


#define linit_c
#define VMK_LIB


#include "lprefix.h"


#include <stddef.h>

#include "vmk.h"

#include "vmklib.h"
#include "lauxlib.h"
#include "llimits.h"


/*
** Standard Libraries. (Must be listed in the same ORDER of their
** respective constants VMK_<libname>K.)
*/
static const vmkL_Reg stdlibs[] = {
  {VMK_GNAME, vmkopen_base},
  {VMK_LOADLIBNAME, vmkopen_package},
  {VMK_COLIBNAME, vmkopen_coroutine},
  {VMK_DBLIBNAME, vmkopen_debug},
  {VMK_IOLIBNAME, vmkopen_io},
  {VMK_MATHLIBNAME, vmkopen_math},
  {VMK_OSLIBNAME, vmkopen_os},
  {VMK_STRLIBNAME, vmkopen_string},
  {VMK_TABLIBNAME, vmkopen_table},
  {VMK_UTF8LIBNAME, vmkopen_utf8},
  {NULL, NULL}
};


/*
** require and preload selected standard libraries
*/
VMKLIB_API void vmkL_openselectedlibs (vmk_State *L, int load, int preload) {
  int mask;
  const vmkL_Reg *lib;
  vmkL_getsubtable(L, VMK_REGISTRYINDEX, VMK_PRELOAD_TABLE);
  for (lib = stdlibs, mask = 1; lib->name != NULL; lib++, mask <<= 1) {
    if (load & mask) {  /* selected? */
      vmkL_requiref(L, lib->name, lib->func, 1);  /* require library */
      vmk_pop(L, 1);  /* remove result from the stack */
    }
    else if (preload & mask) {  /* selected? */
      vmk_pushcfunction(L, lib->func);
      vmk_setfield(L, -2, lib->name);  /* add library to PRELOAD table */
    }
  }
  vmk_assert((mask >> 1) == VMK_UTF8LIBK);
  vmk_pop(L, 1);  /* remove PRELOAD table */
}

