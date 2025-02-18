/*
** $Id: lauxlib.h $
** Auxiliary functions for building Vmk libraries
** See Copyright Notice in vmk.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "vmkconf.h"
#include "vmk.h"


/* global table */
#define VMK_GNAME	"_G"


typedef struct vmkL_Buffer vmkL_Buffer;


/* extra error code for 'vmkL_loadfilex' */
#define VMK_ERRFILE     (VMK_ERRERR+1)


/* key, in the registry, for table of loaded modules */
#define VMK_LOADED_TABLE	"_LOADED"


/* key, in the registry, for table of preloaded loaders */
#define VMK_PRELOAD_TABLE	"_PRELOAD"


typedef struct vmkL_Reg {
  const char *name;
  vmk_CFunction func;
} vmkL_Reg;


#define VMKL_NUMSIZES	(sizeof(vmk_Integer)*16 + sizeof(vmk_Number))

VMKLIB_API void (vmkL_checkversion_) (vmk_State *L, vmk_Number ver, size_t sz);
#define vmkL_checkversion(L)  \
	  vmkL_checkversion_(L, VMK_VERSION_NUM, VMKL_NUMSIZES)

VMKLIB_API int (vmkL_getmetafield) (vmk_State *L, int obj, const char *e);
VMKLIB_API int (vmkL_callmeta) (vmk_State *L, int obj, const char *e);
VMKLIB_API const char *(vmkL_tolstring) (vmk_State *L, int idx, size_t *len);
VMKLIB_API int (vmkL_argerror) (vmk_State *L, int arg, const char *extramsg);
VMKLIB_API int (vmkL_typeerror) (vmk_State *L, int arg, const char *tname);
VMKLIB_API const char *(vmkL_checklstring) (vmk_State *L, int arg,
                                                          size_t *l);
VMKLIB_API const char *(vmkL_optlstring) (vmk_State *L, int arg,
                                          const char *def, size_t *l);
VMKLIB_API vmk_Number (vmkL_checknumber) (vmk_State *L, int arg);
VMKLIB_API vmk_Number (vmkL_optnumber) (vmk_State *L, int arg, vmk_Number def);

VMKLIB_API vmk_Integer (vmkL_checkinteger) (vmk_State *L, int arg);
VMKLIB_API vmk_Integer (vmkL_optinteger) (vmk_State *L, int arg,
                                          vmk_Integer def);

VMKLIB_API void (vmkL_checkstack) (vmk_State *L, int sz, const char *msg);
VMKLIB_API void (vmkL_checktype) (vmk_State *L, int arg, int t);
VMKLIB_API void (vmkL_checkany) (vmk_State *L, int arg);

VMKLIB_API int   (vmkL_newmetatable) (vmk_State *L, const char *tname);
VMKLIB_API void  (vmkL_setmetatable) (vmk_State *L, const char *tname);
VMKLIB_API void *(vmkL_testudata) (vmk_State *L, int ud, const char *tname);
VMKLIB_API void *(vmkL_checkudata) (vmk_State *L, int ud, const char *tname);

VMKLIB_API void (vmkL_where) (vmk_State *L, int lvl);
VMKLIB_API int (vmkL_error) (vmk_State *L, const char *fmt, ...);

VMKLIB_API int (vmkL_checkoption) (vmk_State *L, int arg, const char *def,
                                   const char *const lst[]);

VMKLIB_API int (vmkL_fileresult) (vmk_State *L, int stat, const char *fname);
VMKLIB_API int (vmkL_execresult) (vmk_State *L, int stat);


/* predefined references */
#define VMK_NOREF       (-2)
#define VMK_REFNIL      (-1)

VMKLIB_API int (vmkL_ref) (vmk_State *L, int t);
VMKLIB_API void (vmkL_unref) (vmk_State *L, int t, int ref);

VMKLIB_API int (vmkL_loadfilex) (vmk_State *L, const char *filename,
                                               const char *mode);

#define vmkL_loadfile(L,f)	vmkL_loadfilex(L,f,NULL)

VMKLIB_API int (vmkL_loadbufferx) (vmk_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
VMKLIB_API int (vmkL_loadstring) (vmk_State *L, const char *s);

VMKLIB_API vmk_State *(vmkL_newstate) (void);

VMKLIB_API unsigned vmkL_makeseed (vmk_State *L);

VMKLIB_API vmk_Integer (vmkL_len) (vmk_State *L, int idx);

VMKLIB_API void (vmkL_addgsub) (vmkL_Buffer *b, const char *s,
                                     const char *p, const char *r);
VMKLIB_API const char *(vmkL_gsub) (vmk_State *L, const char *s,
                                    const char *p, const char *r);

VMKLIB_API void (vmkL_setfuncs) (vmk_State *L, const vmkL_Reg *l, int nup);

VMKLIB_API int (vmkL_getsubtable) (vmk_State *L, int idx, const char *fname);

VMKLIB_API void (vmkL_traceback) (vmk_State *L, vmk_State *L1,
                                  const char *msg, int level);

VMKLIB_API void (vmkL_requiref) (vmk_State *L, const char *modname,
                                 vmk_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define vmkL_newlibtable(L,l)	\
  vmk_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define vmkL_newlib(L,l)  \
  (vmkL_checkversion(L), vmkL_newlibtable(L,l), vmkL_setfuncs(L,l,0))

#define vmkL_argcheck(L, cond,arg,extramsg)	\
	((void)(vmki_likely(cond) || vmkL_argerror(L, (arg), (extramsg))))

#define vmkL_argexpected(L,cond,arg,tname)	\
	((void)(vmki_likely(cond) || vmkL_typeerror(L, (arg), (tname))))

#define vmkL_checkstring(L,n)	(vmkL_checklstring(L, (n), NULL))
#define vmkL_optstring(L,n,d)	(vmkL_optlstring(L, (n), (d), NULL))

#define vmkL_typename(L,i)	vmk_typename(L, vmk_type(L,(i)))

#define vmkL_dofile(L, fn) \
	(vmkL_loadfile(L, fn) || vmk_pcall(L, 0, VMK_MULTRET, 0))

#define vmkL_dostring(L, s) \
	(vmkL_loadstring(L, s) || vmk_pcall(L, 0, VMK_MULTRET, 0))

#define vmkL_getmetatable(L,n)	(vmk_getfield(L, VMK_REGISTRYINDEX, (n)))

#define vmkL_opt(L,f,n,d)	(vmk_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define vmkL_loadbuffer(L,s,sz,n)	vmkL_loadbufferx(L,s,sz,n,NULL)


/*
** Perform arithmetic operations on vmk_Integer values with wrap-around
** semantics, as the Vmk core does.
*/
#define vmkL_intop(op,v1,v2)  \
	((vmk_Integer)((vmk_Unsigned)(v1) op (vmk_Unsigned)(v2)))


/* push the value used to represent failure/error */
#define vmkL_pushfail(L)	vmk_pushnil(L)



/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

struct vmkL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  vmk_State *L;
  union {
    VMKI_MAXALIGN;  /* ensure maximum alignment for buffer */
    char b[VMKL_BUFFERSIZE];  /* initial buffer */
  } init;
};


#define vmkL_bufflen(bf)	((bf)->n)
#define vmkL_buffaddr(bf)	((bf)->b)


#define vmkL_addchar(B,c) \
  ((void)((B)->n < (B)->size || vmkL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define vmkL_addsize(B,s)	((B)->n += (s))

#define vmkL_buffsub(B,s)	((B)->n -= (s))

VMKLIB_API void (vmkL_buffinit) (vmk_State *L, vmkL_Buffer *B);
VMKLIB_API char *(vmkL_prepbuffsize) (vmkL_Buffer *B, size_t sz);
VMKLIB_API void (vmkL_addlstring) (vmkL_Buffer *B, const char *s, size_t l);
VMKLIB_API void (vmkL_addstring) (vmkL_Buffer *B, const char *s);
VMKLIB_API void (vmkL_addvalue) (vmkL_Buffer *B);
VMKLIB_API void (vmkL_pushresult) (vmkL_Buffer *B);
VMKLIB_API void (vmkL_pushresultsize) (vmkL_Buffer *B, size_t sz);
VMKLIB_API char *(vmkL_buffinitsize) (vmk_State *L, vmkL_Buffer *B, size_t sz);

#define vmkL_prepbuffer(B)	vmkL_prepbuffsize(B, VMKL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'VMK_FILEHANDLE' and
** initial structure 'vmkL_Stream' (it may contain other fields
** after that initial structure).
*/

#define VMK_FILEHANDLE          "FILE*"


typedef struct vmkL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  vmk_CFunction closef;  /* to close stream (NULL for closed streams) */
} vmkL_Stream;

/* }====================================================== */


/*
** {============================================================
** Compatibility with deprecated conversions
** =============================================================
*/
#if defined(VMK_COMPAT_APIINTCASTS)

#define vmkL_checkunsigned(L,a)	((vmk_Unsigned)vmkL_checkinteger(L,a))
#define vmkL_optunsigned(L,a,d)	\
	((vmk_Unsigned)vmkL_optinteger(L,a,(vmk_Integer)(d)))

#define vmkL_checkint(L,n)	((int)vmkL_checkinteger(L, (n)))
#define vmkL_optint(L,n,d)	((int)vmkL_optinteger(L, (n), (d)))

#define vmkL_checklong(L,n)	((long)vmkL_checkinteger(L, (n)))
#define vmkL_optlong(L,n,d)	((long)vmkL_optinteger(L, (n), (d)))

#endif
/* }============================================================ */



#endif


