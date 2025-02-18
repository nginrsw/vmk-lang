/*
** $Id: lundump.h $
** load precompiled Vmk chunks
** See Copyright Notice in vmk.h
*/

#ifndef lundump_h
#define lundump_h

#include <limits.h>

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/* data to catch conversion errors */
#define VMKC_DATA	"\x19\x93\r\n\x1a\n"

#define VMKC_INT	0x5678
#define VMKC_NUM	cast_num(370.5)

/*
** Encode major-minor version in one byte, one nibble for each
*/
#define VMKC_VERSION	(VMK_VERSION_MAJOR_N*16+VMK_VERSION_MINOR_N)

#define VMKC_FORMAT	0	/* this is the official format */


/* load one chunk; from lundump.c */
VMKI_FUNC LClosure* vmkU_undump (vmk_State* L, ZIO* Z, const char* name,
                                               int fixed);

/* dump one chunk; from ldump.c */
VMKI_FUNC int vmkU_dump (vmk_State* L, const Proto* f, vmk_Writer w,
                         void* data, int strip);

#endif
