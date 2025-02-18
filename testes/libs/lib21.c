#include "vmk.h"


int vmkopen_lib2 (vmk_State *L);

VMKMOD_API int vmkopen_lib21 (vmk_State *L) {
  return vmkopen_lib2(L);
}


