/*
** $Id: loadlib.c $
** Dynamic library loader for Vmk
** See Copyright Notice in vmk.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Windows, and a stub for other
** systems.
*/

#define loadlib_c
#define VMK_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


/*
** VMK_CSUBSEP is the character that replaces dots in submodule names
** when searching for a C loader.
** VMK_LSUBSEP is the character that replaces dots in submodule names
** when searching for a Vmk loader.
*/
#if !defined(VMK_CSUBSEP)
#define VMK_CSUBSEP		VMK_DIRSEP
#endif

#if !defined(VMK_LSUBSEP)
#define VMK_LSUBSEP		VMK_DIRSEP
#endif


/* prefix for open functions in C libraries */
#define VMK_POF		"vmkopen_"

/* separator for open functions in C libraries */
#define VMK_OFSEP	"_"


/*
** key for table in the registry that keeps handles
** for all loaded C libraries
*/
static const char *const CLIBS = "_CLIBS";

#define LIB_FAIL	"open"


#define setprogdir(L)           ((void)0)


/* cast void* to a Vmk fn */
#define cast_Lfunc(p)	cast(vmk_CFunction, cast_func(p))


/*
** system-dependent functions
*/

/*
** unload library 'lib'
*/
static void lsys_unloadlib (void *lib);

/*
** load C library in file 'path'. If 'seeglb', load with all names in
** the library global.
** Returns the library; in case of error, returns NULL plus an
** error string in the stack.
*/
static void *lsys_load (vmk_State *L, const char *path, int seeglb);

/*
** Try to find a fn named 'sym' in library 'lib'.
** Returns the fn; in case of error, returns NULL plus an
** error string in the stack.
*/
static vmk_CFunction lsys_sym (vmk_State *L, void *lib, const char *sym);




#if defined(VMK_USE_DLOPEN)	/* { */
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface,
** which is available in all POSIX systems.
** =========================================================================
*/

#include <dlfcn.h>


static void lsys_unloadlib (void *lib) {
  dlclose(lib);
}


static void *lsys_load (vmk_State *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (l_unlikely(lib == NULL))
    vmk_pushstring(L, dlerror());
  return lib;
}


static vmk_CFunction lsys_sym (vmk_State *L, void *lib, const char *sym) {
  vmk_CFunction f = cast_Lfunc(dlsym(lib, sym));
  if (l_unlikely(f == NULL))
    vmk_pushstring(L, dlerror());
  return f;
}

/* }====================================================== */



#elif defined(VMK_DL_DLL)	/* }{ */
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

#include <windows.h>


/*
** optional flags for LoadLibraryEx
*/
#if !defined(VMK_LLE_FLAGS)
#define VMK_LLE_FLAGS	0
#endif


#undef setprogdir


/*
** Replace in the path (on the top of the stack) any occurrence
** of VMK_EXEC_DIR with the executable's path.
*/
static void setprogdir (vmk_State *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileNameA(NULL, buff, nsize);  /* get exec. name */
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    vmkL_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';  /* cut name on the last '\\' to get the path */
    vmkL_gsub(L, vmk_tostring(L, -1), VMK_EXEC_DIR, buff);
    vmk_remove(L, -2);  /* remove original string */
  }
}




static void pusherror (vmk_State *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, 0, buffer, sizeof(buffer)/sizeof(char), NULL))
    vmk_pushstring(L, buffer);
  else
    vmk_pushfstring(L, "system error %d\n", error);
}

static void lsys_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}


static void *lsys_load (vmk_State *L, const char *path, int seeglb) {
  HMODULE lib = LoadLibraryExA(path, NULL, VMK_LLE_FLAGS);
  (void)(seeglb);  /* not used: symbols are 'global' by default */
  if (lib == NULL) pusherror(L);
  return lib;
}


static vmk_CFunction lsys_sym (vmk_State *L, void *lib, const char *sym) {
  vmk_CFunction f = cast_Lfunc(GetProcAddress((HMODULE)lib, sym));
  if (f == NULL) pusherror(L);
  return f;
}

/* }====================================================== */


#else				/* }{ */
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Vmk installation"


static void lsys_unloadlib (void *lib) {
  (void)(lib);  /* not used */
}


static void *lsys_load (vmk_State *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);  /* not used */
  vmk_pushliteral(L, DLMSG);
  return NULL;
}


static vmk_CFunction lsys_sym (vmk_State *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);  /* not used */
  vmk_pushliteral(L, DLMSG);
  return NULL;
}

/* }====================================================== */
#endif				/* } */


/*
** {==================================================================
** Set Paths
** ===================================================================
*/

/*
** VMK_PATH_VAR and VMK_CPATH_VAR are the names of the environment
** variables that Vmk check to set its paths.
*/
#if !defined(VMK_PATH_VAR)
#define VMK_PATH_VAR    "VMK_PATH"
#endif

#if !defined(VMK_CPATH_VAR)
#define VMK_CPATH_VAR   "VMK_CPATH"
#endif



/*
** return registry.VMK_NOENV as a boolean
*/
static int noenv (vmk_State *L) {
  int b;
  vmk_getfield(L, VMK_REGISTRYINDEX, "VMK_NOENV");
  b = vmk_toboolean(L, -1);
  vmk_pop(L, 1);  /* remove value */
  return b;
}


/*
** Set a path. (If using the default path, assume it is a string
** literal in C and create it as an external string.)
*/
static void setpath (vmk_State *L, const char *fieldname,
                                   const char *envname,
                                   const char *dft) {
  const char *dftmark;
  const char *nver = vmk_pushfstring(L, "%s%s", envname, VMK_VERSUFFIX);
  const char *path = getenv(nver);  /* try versioned name */
  if (path == NULL)  /* no versioned environment variable? */
    path = getenv(envname);  /* try unversioned name */
  if (path == NULL || noenv(L))  /* no environment variable? */
    vmk_pushexternalstring(L, dft, strlen(dft), NULL, NULL);  /* use default */
  else if ((dftmark = strstr(path, VMK_PATH_SEP VMK_PATH_SEP)) == NULL)
    vmk_pushstring(L, path);  /* nothing to change */
  else {  /* path contains a ";;": insert default path in its place */
    size_t len = strlen(path);
    vmkL_Buffer b;
    vmkL_buffinit(L, &b);
    if (path < dftmark) {  /* is there a prefix before ';;'? */
      vmkL_addlstring(&b, path, ct_diff2sz(dftmark - path));  /* add it */
      vmkL_addchar(&b, *VMK_PATH_SEP);
    }
    vmkL_addstring(&b, dft);  /* add default */
    if (dftmark < path + len - 2) {  /* is there a suffix after ';;'? */
      vmkL_addchar(&b, *VMK_PATH_SEP);
      vmkL_addlstring(&b, dftmark + 2, ct_diff2sz((path + len - 2) - dftmark));
    }
    vmkL_pushresult(&b);
  }
  setprogdir(L);
  vmk_setfield(L, -3, fieldname);  /* package[fieldname] = path value */
  vmk_pop(L, 1);  /* pop versioned variable name ('nver') */
}

/* }================================================================== */


/*
** return registry.CLIBS[path]
*/
static void *checkclib (vmk_State *L, const char *path) {
  void *plib;
  vmk_getfield(L, VMK_REGISTRYINDEX, CLIBS);
  vmk_getfield(L, -1, path);
  plib = vmk_touserdata(L, -1);  /* plib = CLIBS[path] */
  vmk_pop(L, 2);  /* pop CLIBS table and 'plib' */
  return plib;
}


/*
** registry.CLIBS[path] = plib        -- for queries
** registry.CLIBS[#CLIBS + 1] = plib  -- also keep a list of all libraries
*/
static void addtoclib (vmk_State *L, const char *path, void *plib) {
  vmk_getfield(L, VMK_REGISTRYINDEX, CLIBS);
  vmk_pushlightuserdata(L, plib);
  vmk_pushvalue(L, -1);
  vmk_setfield(L, -3, path);  /* CLIBS[path] = plib */
  vmk_rawseti(L, -2, vmkL_len(L, -2) + 1);  /* CLIBS[#CLIBS + 1] = plib */
  vmk_pop(L, 1);  /* pop CLIBS table */
}


/*
** __gc tag method for CLIBS table: calls 'lsys_unloadlib' for all lib
** handles in list CLIBS
*/
static int gctm (vmk_State *L) {
  vmk_Integer n = vmkL_len(L, 1);
  for (; n >= 1; n--) {  /* for each handle, in reverse order */
    vmk_rawgeti(L, 1, n);  /* get handle CLIBS[n] */
    lsys_unloadlib(vmk_touserdata(L, -1));
    vmk_pop(L, 1);  /* pop handle */
  }
  return 0;
}



/* error codes for 'lookforfunc' */
#define ERRLIB		1
#define ERRFUNC		2

/*
** Look for a C fn named 'sym' in a dynamically loaded library
** 'path'.
** First, check whether the library is already loaded; if not, try
** to load it.
** Then, if 'sym' is '*', return true (as library has been loaded).
** Otherwise, look for symbol 'sym' in the library and push a
** C fn with that symbol.
** Return 0 and 'true' or a fn in the stack; in case of
** errors, return an error code and an error message in the stack.
*/
static int lookforfunc (vmk_State *L, const char *path, const char *sym) {
  void *reg = checkclib(L, path);  /* check loaded C libraries */
  if (reg == NULL) {  /* must load library? */
    reg = lsys_load(L, path, *sym == '*');  /* global symbols if 'sym'=='*' */
    if (reg == NULL) return ERRLIB;  /* unable to load library */
    addtoclib(L, path, reg);
  }
  if (*sym == '*') {  /* loading only library (no fn)? */
    vmk_pushboolean(L, 1);  /* return 'true' */
    return 0;  /* no errors */
  }
  else {
    vmk_CFunction f = lsys_sym(L, reg, sym);
    if (f == NULL)
      return ERRFUNC;  /* unable to find fn */
    vmk_pushcfunction(L, f);  /* else create new fn */
    return 0;  /* no errors */
  }
}


static int ll_loadlib (vmk_State *L) {
  const char *path = vmkL_checkstring(L, 1);
  const char *init = vmkL_checkstring(L, 2);
  int stat = lookforfunc(L, path, init);
  if (l_likely(stat == 0))  /* no errors? */
    return 1;  /* return the loaded fn */
  else {  /* error; error message is on stack top */
    vmkL_pushfail(L);
    vmk_insert(L, -2);
    vmk_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;  /* return fail, error message, and where */
  }
}



/*
** {======================================================
** 'require' fn
** =======================================================
*/


static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  /* try to open file */
  if (f == NULL) return 0;  /* open failed */
  fclose(f);
  return 1;
}


/*
** Get the next name in '*path' = 'name1;name2;name3;...', changing
** the ending ';' to '\0' to create a zero-terminated string. Return
** NULL when list ends.
*/
static const char *getnextfilename (char **path, char *end) {
  char *sep;
  char *name = *path;
  if (name == end)
    return NULL;  /* no more names */
  else if (*name == '\0') {  /* from previous iteration? */
    *name = *VMK_PATH_SEP;  /* restore separator */
    name++;  /* skip it */
  }
  sep = strchr(name, *VMK_PATH_SEP);  /* find next separator */
  if (sep == NULL)  /* separator not found? */
    sep = end;  /* name goes until the end */
  *sep = '\0';  /* finish file name */
  *path = sep;  /* will start next search from here */
  return name;
}


/*
** Given a path such as ";blabla.so;blublu.so", pushes the string
**
** no file 'blabla.so'
**	no file 'blublu.so'
*/
static void pusherrornotfound (vmk_State *L, const char *path) {
  vmkL_Buffer b;
  vmkL_buffinit(L, &b);
  vmkL_addstring(&b, "no file '");
  vmkL_addgsub(&b, path, VMK_PATH_SEP, "'\n\tno file '");
  vmkL_addstring(&b, "'");
  vmkL_pushresult(&b);
}


static const char *searchpath (vmk_State *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  vmkL_Buffer buff;
  char *pathname;  /* path with name inserted */
  char *endpathname;  /* its end */
  const char *filename;
  /* separator is non-empty and appears in 'name'? */
  if (*sep != '\0' && strchr(name, *sep) != NULL)
    name = vmkL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
  vmkL_buffinit(L, &buff);
  /* add path to the buffer, replacing marks ('?') with the file name */
  vmkL_addgsub(&buff, path, VMK_PATH_MARK, name);
  vmkL_addchar(&buff, '\0');
  pathname = vmkL_buffaddr(&buff);  /* writable list of file names */
  endpathname = pathname + vmkL_bufflen(&buff) - 1;
  while ((filename = getnextfilename(&pathname, endpathname)) != NULL) {
    if (readable(filename))  /* does file exist and is readable? */
      return vmk_pushstring(L, filename);  /* save and return name */
  }
  vmkL_pushresult(&buff);  /* push path to create error message */
  pusherrornotfound(L, vmk_tostring(L, -1));  /* create error message */
  return NULL;  /* not found */
}


static int ll_searchpath (vmk_State *L) {
  const char *f = searchpath(L, vmkL_checkstring(L, 1),
                                vmkL_checkstring(L, 2),
                                vmkL_optstring(L, 3, "."),
                                vmkL_optstring(L, 4, VMK_DIRSEP));
  if (f != NULL) return 1;
  else {  /* error message is on top of the stack */
    vmkL_pushfail(L);
    vmk_insert(L, -2);
    return 2;  /* return fail + error message */
  }
}


static const char *findfile (vmk_State *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  const char *path;
  vmk_getfield(L, vmk_upvalueindex(1), pname);
  path = vmk_tostring(L, -1);
  if (l_unlikely(path == NULL))
    vmkL_error(L, "'package.%s' must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}


static int checkload (vmk_State *L, int stat, const char *filename) {
  if (l_likely(stat)) {  /* module loaded successfully? */
    vmk_pushstring(L, filename);  /* will be 2nd argument to module */
    return 2;  /* return open fn and file name */
  }
  else
    return vmkL_error(L, "error loading module '%s' from file '%s':\n\t%s",
                          vmk_tostring(L, 1), filename, vmk_tostring(L, -1));
}


static int searcher_Vmk (vmk_State *L) {
  const char *filename;
  const char *name = vmkL_checkstring(L, 1);
  filename = findfile(L, name, "path", VMK_LSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (vmkL_loadfile(L, filename) == VMK_OK), filename);
}


/*
** Try to find a load fn for module 'modname' at file 'filename'.
** First, change '.' to '_' in 'modname'; then, if 'modname' has
** the form X-Y (that is, it has an "ignore mark"), build a fn
** name "vmkopen_X" and look for it. (For compatibility, if that
** fails, it also tries "vmkopen_Y".) If there is no ignore mark,
** look for a fn named "vmkopen_modname".
*/
static int loadfunc (vmk_State *L, const char *filename, const char *modname) {
  const char *openfunc;
  const char *mark;
  modname = vmkL_gsub(L, modname, ".", VMK_OFSEP);
  mark = strchr(modname, *VMK_IGMARK);
  if (mark) {
    int stat;
    openfunc = vmk_pushlstring(L, modname, ct_diff2sz(mark - modname));
    openfunc = vmk_pushfstring(L, VMK_POF"%s", openfunc);
    stat = lookforfunc(L, filename, openfunc);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;  /* else go ahead and try old-style name */
  }
  openfunc = vmk_pushfstring(L, VMK_POF"%s", modname);
  return lookforfunc(L, filename, openfunc);
}


static int searcher_C (vmk_State *L) {
  const char *name = vmkL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", VMK_CSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}


static int searcher_Croot (vmk_State *L) {
  const char *filename;
  const char *name = vmkL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;  /* is root */
  vmk_pushlstring(L, name, ct_diff2sz(p - name));
  filename = findfile(L, vmk_tostring(L, -1), "cpath", VMK_CSUBSEP);
  if (filename == NULL) return 1;  /* root not found */
  if ((stat = loadfunc(L, filename, name)) != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);  /* real error */
    else {  /* open fn not found */
      vmk_pushfstring(L, "no module '%s' in file '%s'", name, filename);
      return 1;
    }
  }
  vmk_pushstring(L, filename);  /* will be 2nd argument to module */
  return 2;
}


static int searcher_preload (vmk_State *L) {
  const char *name = vmkL_checkstring(L, 1);
  vmk_getfield(L, VMK_REGISTRYINDEX, VMK_PRELOAD_TABLE);
  if (vmk_getfield(L, -1, name) == VMK_TNIL) {  /* not found? */
    vmk_pushfstring(L, "no field package.preload['%s']", name);
    return 1;
  }
  else {
    vmk_pushliteral(L, ":preload:");
    return 2;
  }
}


static void findloader (vmk_State *L, const char *name) {
  int i;
  vmkL_Buffer msg;  /* to build error message */
  /* push 'package.searchers' to index 3 in the stack */
  if (l_unlikely(vmk_getfield(L, vmk_upvalueindex(1), "searchers")
                 != VMK_TTABLE))
    vmkL_error(L, "'package.searchers' must be a table");
  vmkL_buffinit(L, &msg);
  vmkL_addstring(&msg, "\n\t");  /* error-message prefix for first message */
  /*  iterate over available searchers to find a loader */
  for (i = 1; ; i++) {
    if (l_unlikely(vmk_rawgeti(L, 3, i) == VMK_TNIL)) {  /* no more searchers? */
      vmk_pop(L, 1);  /* remove nil */
      vmkL_buffsub(&msg, 2);  /* remove last prefix */
      vmkL_pushresult(&msg);  /* create error message */
      vmkL_error(L, "module '%s' not found:%s", name, vmk_tostring(L, -1));
    }
    vmk_pushstring(L, name);
    vmk_call(L, 1, 2);  /* call it */
    if (vmk_isfunction(L, -2))  /* did it find a loader? */
      return;  /* module loader found */
    else if (vmk_isstring(L, -2)) {  /* searcher returned error message? */
      vmk_pop(L, 1);  /* remove extra return */
      vmkL_addvalue(&msg);  /* concatenate error message */
      vmkL_addstring(&msg, "\n\t");  /* prefix for next message */
    }
    else  /* no error message */
      vmk_pop(L, 2);  /* remove both returns */
  }
}


static int ll_require (vmk_State *L) {
  const char *name = vmkL_checkstring(L, 1);
  vmk_settop(L, 1);  /* LOADED table will be at index 2 */
  vmk_getfield(L, VMK_REGISTRYINDEX, VMK_LOADED_TABLE);
  vmk_getfield(L, 2, name);  /* LOADED[name] */
  if (vmk_toboolean(L, -1))  /* is it there? */
    return 1;  /* package is already loaded */
  /* else must load package */
  vmk_pop(L, 1);  /* remove 'getfield' result */
  findloader(L, name);
  vmk_rotate(L, -2, 1);  /* fn <-> loader data */
  vmk_pushvalue(L, 1);  /* name is 1st argument to module loader */
  vmk_pushvalue(L, -3);  /* loader data is 2nd argument */
  /* stack: ...; loader data; loader fn; mod. name; loader data */
  vmk_call(L, 2, 1);  /* run loader to load module */
  /* stack: ...; loader data; result from loader */
  if (!vmk_isnil(L, -1))  /* non-nil return? */
    vmk_setfield(L, 2, name);  /* LOADED[name] = returned value */
  else
    vmk_pop(L, 1);  /* pop nil */
  if (vmk_getfield(L, 2, name) == VMK_TNIL) {   /* module set no value? */
    vmk_pushboolean(L, 1);  /* use true as result */
    vmk_copy(L, -1, -2);  /* replace loader result */
    vmk_setfield(L, 2, name);  /* LOADED[name] = true */
  }
  vmk_rotate(L, -2, 1);  /* loader data <-> module result  */
  return 2;  /* return module result and loader data */
}

/* }====================================================== */




static const vmkL_Reg pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"searchpath", ll_searchpath},
  /* placeholders */
  {"preload", NULL},
  {"cpath", NULL},
  {"path", NULL},
  {"searchers", NULL},
  {"loaded", NULL},
  {NULL, NULL}
};


static const vmkL_Reg ll_funcs[] = {
  {"require", ll_require},
  {NULL, NULL}
};


static void createsearcherstable (vmk_State *L) {
  static const vmk_CFunction searchers[] = {
    searcher_preload,
    searcher_Vmk,
    searcher_C,
    searcher_Croot,
    NULL
  };
  int i;
  /* create 'searchers' table */
  vmk_createtable(L, sizeof(searchers)/sizeof(searchers[0]) - 1, 0);
  /* fill it with predefined searchers */
  for (i=0; searchers[i] != NULL; i++) {
    vmk_pushvalue(L, -2);  /* set 'package' as upvalue for all searchers */
    vmk_pushcclosure(L, searchers[i], 1);
    vmk_rawseti(L, -2, i+1);
  }
  vmk_setfield(L, -2, "searchers");  /* put it in field 'searchers' */
}


/*
** create table CLIBS to keep track of loaded C libraries,
** setting a finalizer to close all libraries when closing state.
*/
static void createclibstable (vmk_State *L) {
  vmkL_getsubtable(L, VMK_REGISTRYINDEX, CLIBS);  /* create CLIBS table */
  vmk_createtable(L, 0, 1);  /* create metatable for CLIBS */
  vmk_pushcfunction(L, gctm);
  vmk_setfield(L, -2, "__gc");  /* set finalizer for CLIBS table */
  vmk_setmetatable(L, -2);
}


VMKMOD_API int vmkopen_package (vmk_State *L) {
  createclibstable(L);
  vmkL_newlib(L, pk_funcs);  /* create 'package' table */
  createsearcherstable(L);
  /* set paths */
  setpath(L, "path", VMK_PATH_VAR, VMK_PATH_DEFAULT);
  setpath(L, "cpath", VMK_CPATH_VAR, VMK_CPATH_DEFAULT);
  /* store config information */
  vmk_pushliteral(L, VMK_DIRSEP "\n" VMK_PATH_SEP "\n" VMK_PATH_MARK "\n"
                     VMK_EXEC_DIR "\n" VMK_IGMARK "\n");
  vmk_setfield(L, -2, "config");
  /* set field 'loaded' */
  vmkL_getsubtable(L, VMK_REGISTRYINDEX, VMK_LOADED_TABLE);
  vmk_setfield(L, -2, "loaded");
  /* set field 'preload' */
  vmkL_getsubtable(L, VMK_REGISTRYINDEX, VMK_PRELOAD_TABLE);
  vmk_setfield(L, -2, "preload");
  vmk_pushglobaltable(L);
  vmk_pushvalue(L, -2);  /* set 'package' as upvalue for next lib */
  vmkL_setfuncs(L, ll_funcs, 1);  /* open lib into global table */
  vmk_pop(L, 1);  /* pop global table */
  return 1;  /* return 'package' table */
}

