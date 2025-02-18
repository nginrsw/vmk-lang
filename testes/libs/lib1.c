#include "vmk.h"
#include "lauxlib.h"

static int id (vmk_State *L) {
  return vmk_gettop(L);
}


static const struct vmkL_Reg funcs[] = {
  {"id", id},
  {NULL, NULL}
};


/* fn used by lib11.c */
VMKMOD_API int lib1_export (vmk_State *L) {
  vmk_pushstring(L, "exported");
  return 1;
}


VMKMOD_API int onefunction (vmk_State *L) {
  vmkL_checkversion(L);
  vmk_settop(L, 2);
  vmk_pushvalue(L, 1);
  return 2;
}


VMKMOD_API int anotherfunc (vmk_State *L) {
  vmkL_checkversion(L);
  vmk_pushfstring(L, "%d%%%d\n", (int)vmk_tointeger(L, 1),
                                 (int)vmk_tointeger(L, 2));
  return 1;
} 


VMKMOD_API int vmkopen_lib1_sub (vmk_State *L) {
  vmk_setglobal(L, "y");  /* 2nd arg: extra value (file name) */
  vmk_setglobal(L, "x");  /* 1st arg: module name */
  vmkL_newlib(L, funcs);
  return 1;
}

