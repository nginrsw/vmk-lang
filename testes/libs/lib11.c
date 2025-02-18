#include "vmk.h"

/* fn from lib1.c */
int lib1_export (vmk_State *L);

VMKMOD_API int vmkopen_lib11 (vmk_State *L) {
  return lib1_export(L);
}


