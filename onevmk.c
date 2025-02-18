/*
** Vmk core, libraries, and interpreter in a single file.
** Compiling just this file generates a complete Vmk stand-alone
** program:
**
** $ gcc -O2 -std=c99 -o vmk onevmk.c -lm
**
** or
**
** $ gcc -O2 -std=c89 -DVMK_USE_C89 -o vmk onevmk.c -lm
**
*/

/* default is to build the full interpreter */
#ifndef MAKE_LIB
#ifndef MAKE_VMKC
#ifndef MAKE_VMK
#define MAKE_VMK
#endif
#endif
#endif


/*
** Choose suitable platform-specific features. Default is no
** platform-specific features. Some of these options may need extra
** libraries such as -ldl -lreadline -lncurses
*/
#if 0
#define VMK_USE_LINUX
#define VMK_USE_MACOSX
#define VMK_USE_POSIX
#define VMK_ANSI
#endif


/* no need to change anything below this line ----------------------------- */

#include "lprefix.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* setup for vmkconf.h */
#define VMK_CORE
#define VMK_LIB
#define ltable_c
#define lvm_c
#include "vmkconf.h"

/* do not export internal symbols */
#undef VMKI_FUNC
#undef VMKI_DDEC
#undef VMKI_DDEF
#define VMKI_FUNC	static
#define VMKI_DDEC(def)	/* empty */
#define VMKI_DDEF	static

/* core -- used by all */
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"

/* auxiliary library -- used by all */
#include "lauxlib.c"

/* standard library  -- not used by vmkc */
#ifndef MAKE_VMKC
#include "lbaselib.c"
#include "lcorolib.c"
#include "ldblib.c"
#include "liolib.c"
#include "lmathlib.c"
#include "loadlib.c"
#include "loslib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
#include "linit.c"
#endif

/* vmk */
#ifdef MAKE_VMK
#include "vmk.c"
#endif

/* vmkc */
#ifdef MAKE_VMKC
#include "vmkc.c"
#endif
