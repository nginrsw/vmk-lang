#include "vmk.h"
#include "lauxlib.h"

static int id (vmk_State *L) {
  return vmk_gettop(L);
}


static const struct vmkL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


VMKMOD_API int vmkopen_lib2 (vmk_State *L) {
  vmk_settop(L, 2);
  vmk_setglobal(L, "y");  /* y gets 2nd parameter */
  vmk_setglobal(L, "x");  /* x gets 1st parameter */
  vmkL_newlib(L, funcs);
  return 1;
}


