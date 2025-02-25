/*
** $Id: vmklib.h $
** Vmk standard libraries
** See Copyright Notice in vmk.h
*/


#ifndef vmklib_h
#define vmklib_h

#include "vmk.h"


/* version suffix for environment variable names */
#define VMK_VERSUFFIX          "_" VMK_VERSION_MAJOR "_" VMK_VERSION_MINOR

#define VMK_GLIBK		1
VMKMOD_API int (vmkopen_base) (vmk_State *L);

#define VMK_LOADLIBNAME	"package"
#define VMK_LOADLIBK	(VMK_GLIBK << 1)
VMKMOD_API int (vmkopen_package) (vmk_State *L);


#define VMK_COLIBNAME	"coroutine"
#define VMK_COLIBK	(VMK_LOADLIBK << 1)
VMKMOD_API int (vmkopen_coroutine) (vmk_State *L);

#define VMK_DBLIBNAME	"debug"
#define VMK_DBLIBK	(VMK_COLIBK << 1)
VMKMOD_API int (vmkopen_debug) (vmk_State *L);

#define VMK_IOLIBNAME	"io"
#define VMK_IOLIBK	(VMK_DBLIBK << 1)
VMKMOD_API int (vmkopen_io) (vmk_State *L);

#define VMK_MATHLIBNAME	"math"
#define VMK_MATHLIBK	(VMK_IOLIBK << 1)
VMKMOD_API int (vmkopen_math) (vmk_State *L);

#define VMK_OSLIBNAME	"os"
#define VMK_OSLIBK	(VMK_MATHLIBK << 1)
VMKMOD_API int (vmkopen_os) (vmk_State *L);

#define VMK_STRLIBNAME	"str" // change string to str for short terms
#define VMK_STRLIBK	(VMK_OSLIBK << 1)
VMKMOD_API int (vmkopen_string) (vmk_State *L);

#define VMK_TABLIBNAME	"table"
#define VMK_TABLIBK	(VMK_STRLIBK << 1)
VMKMOD_API int (vmkopen_table) (vmk_State *L);

#define VMK_UTF8LIBNAME	"utf8"
#define VMK_UTF8LIBK	(VMK_TABLIBK << 1)
VMKMOD_API int (vmkopen_utf8) (vmk_State *L);


/* open selected libraries */
VMKLIB_API void (vmkL_openselectedlibs) (vmk_State *L, int load, int preload);

/* open all libraries */
#define vmkL_openlibs(L)	vmkL_openselectedlibs(L, ~0, 0)


#endif
