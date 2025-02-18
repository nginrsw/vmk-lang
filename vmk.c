/*
** $Id: vmk.c $
** Vmk stand-alone interpreter
** See Copyright Notice in vmk.h
*/

#define vmk_c

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include "vmk.h"

#include "lauxlib.h"
#include "vmklib.h"
#include "llimits.h"


#if !defined(VMK_PROGNAME)
#define VMK_PROGNAME		"vmk"
#endif

#if !defined(VMK_INIT_VAR)
#define VMK_INIT_VAR		"VMK_INIT"
#endif

#define VMK_INITVARVERSION	VMK_INIT_VAR VMK_VERSUFFIX


static vmk_State *globalL = NULL;

static const char *progname = VMK_PROGNAME;


#if defined(VMK_USE_POSIX)   /* { */

/*
** Use 'sigaction' when available.
*/
static void setsignal (int sig, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);  /* do not mask any signal */
  sigaction(sig, &sa, NULL);
}

#else           /* }{ */

#define setsignal            signal

#endif                               /* } */


/*
** Hook set by signal fn to stop the interpreter.
*/
static void lstop (vmk_State *L, vmk_Debug *ar) {
  (void)ar;  /* unused arg. */
  vmk_sethook(L, NULL, 0, 0);  /* reset hook */
  vmkL_error(L, "interrupted!");
}


/*
** Function to be called at a C signal. Because a C signal cannot
** just change a Vmk state (as there is no proper synchronization),
** this fn only sets a hook that, when called, will stop the
** interpreter.
*/
static void laction (int i) {
  int flag = VMK_MASKCALL | VMK_MASKRET | VMK_MASKLINE | VMK_MASKCOUNT;
  setsignal(i, SIG_DFL); /* if another SIGINT happens, terminate process */
  vmk_sethook(globalL, lstop, flag, 1);
}


static void print_usage (const char *badoption) {
  vmk_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    vmk_writestringerror("'%s' needs argument\n", badoption);
  else
    vmk_writestringerror("unrecognized option '%s'\n", badoption);
  vmk_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat   execute string 'stat'\n"
  "  -i        enter interactive mode after executing 'script'\n"
  "  -l mod    require library 'mod' into global 'mod'\n"
  "  -l g=mod  require library 'mod' into global 'g'\n"
  "  -v        show version information\n"
  "  -E        ignore environment variables\n"
  "  -W        turn warnings on\n"
  "  --        stop handling options\n"
  "  -         stop handling options and execute stdin\n"
  ,
  progname);
}


/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void l_message (const char *pname, const char *msg) {
  if (pname) vmk_writestringerror("%s: ", pname);
  vmk_writestringerror("%s\n", msg);
}


/*
** Check whether 'status' is not OK and, if so, prints the error
** message on the top of the stack.
*/
static int report (vmk_State *L, int status) {
  if (status != VMK_OK) {
    const char *msg = vmk_tostring(L, -1);
    if (msg == NULL)
      msg = "(error message not a string)";
    l_message(progname, msg);
    vmk_pop(L, 1);  /* remove message */
  }
  return status;
}


/*
** Message handler used to run all chunks
*/
static int msghandler (vmk_State *L) {
  const char *msg = vmk_tostring(L, 1);
  if (msg == NULL) {  /* is error object not a string? */
    if (vmkL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
        vmk_type(L, -1) == VMK_TSTRING)  /* that produces a string? */
      return 1;  /* that is the message */
    else
      msg = vmk_pushfstring(L, "(error object is a %s value)",
                               vmkL_typename(L, 1));
  }
  vmkL_traceback(L, L, msg, 1);  /* append a standard traceback */
  return 1;  /* return the traceback */
}


/*
** Interface to 'vmk_pcall', which sets appropriate message fn
** and C-signal handler. Used to run all chunks.
*/
static int docall (vmk_State *L, int narg, int nres) {
  int status;
  int base = vmk_gettop(L) - narg;  /* fn index */
  vmk_pushcfunction(L, msghandler);  /* push message handler */
  vmk_insert(L, base);  /* put it under fn and args */
  globalL = L;  /* to be available to 'laction' */
  setsignal(SIGINT, laction);  /* set C-signal handler */
  status = vmk_pcall(L, narg, nres, base);
  setsignal(SIGINT, SIG_DFL); /* reset C-signal handler */
  vmk_remove(L, base);  /* remove message handler from the stack */
  return status;
}


static void print_version (void) {
  vmk_writestring(VMK_COPYRIGHT, strlen(VMK_COPYRIGHT));
  vmk_writeline();
}


/*
** Create the 'arg' table, which stores all arguments from the
** command line ('argv'). It should be aligned so that, at index 0,
** it has 'argv[script]', which is the script name. The arguments
** to the script (everything after 'script') go to positive indices;
** other arguments (before the script name) go to negative indices.
** If there is no script name, assume interpreter's name as base.
** (If there is no interpreter's name either, 'script' is -1, so
** table sizes are zero.)
*/
static void createargtable (vmk_State *L, char **argv, int argc, int script) {
  int i, narg;
  narg = argc - (script + 1);  /* number of positive indices */
  vmk_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    vmk_pushstring(L, argv[i]);
    vmk_rawseti(L, -2, i - script);
  }
  vmk_setglobal(L, "arg");
}


static int dochunk (vmk_State *L, int status) {
  if (status == VMK_OK) status = docall(L, 0, 0);
  return report(L, status);
}


static int dofile (vmk_State *L, const char *name) {
  return dochunk(L, vmkL_loadfile(L, name));
}


static int dostring (vmk_State *L, const char *s, const char *name) {
  return dochunk(L, vmkL_loadbuffer(L, s, strlen(s), name));
}


/*
** Receives 'globname[=modname]' and runs 'globname = require(modname)'.
** If there is no explicit modname and globname contains a '-', cut
** the suffix after '-' (the "version") to make the global name.
*/
static int dolibrary (vmk_State *L, char *globname) {
  int status;
  char *suffix = NULL;
  char *modname = strchr(globname, '=');
  if (modname == NULL) {  /* no explicit name? */
    modname = globname;  /* module name is equal to global name */
    suffix = strchr(modname, *VMK_IGMARK);  /* look for a suffix mark */
  }
  else {
    *modname = '\0';  /* global name ends here */
    modname++;  /* module name starts after the '=' */
  }
  vmk_getglobal(L, "require");
  vmk_pushstring(L, modname);
  status = docall(L, 1, 1);  /* call 'require(modname)' */
  if (status == VMK_OK) {
    if (suffix != NULL)  /* is there a suffix mark? */
      *suffix = '\0';  /* remove suffix from global name */
    vmk_setglobal(L, globname);  /* globname = require(modname) */
  }
  return report(L, status);
}


/*
** Push on the stack the contents of table 'arg' from 1 to #arg
*/
static int pushargs (vmk_State *L) {
  int i, n;
  if (vmk_getglobal(L, "arg") != VMK_TTABLE)
    vmkL_error(L, "'arg' is not a table");
  n = (int)vmkL_len(L, -1);
  vmkL_checkstack(L, n + 3, "too many arguments to script");
  for (i = 1; i <= n; i++)
    vmk_rawgeti(L, -i, i);
  vmk_remove(L, -i);  /* remove table from the stack */
  return n;
}


static int handle_script (vmk_State *L, char **argv) {
  int status;
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)
    fname = NULL;  /* stdin */
  status = vmkL_loadfile(L, fname);
  if (status == VMK_OK) {
    int n = pushargs(L);  /* push arguments to script */
    status = docall(L, n, VMK_MULTRET);
  }
  return report(L, status);
}


/* bits of various argument indicators in 'args' */
#define has_error	1	/* bad option */
#define has_i		2	/* -i */
#define has_v		4	/* -v */
#define has_e		8	/* -e */
#define has_E		16	/* -E */


/*
** Traverses all arguments from 'argv', returning a mask with those
** needed before running any Vmk code or an error code if it finds any
** invalid argument. In case of error, 'first' is the index of the bad
** argument.  Otherwise, 'first' is -1 if there is no program name,
** 0 if there is no script name, or the index of the script name.
*/
static int collectargs (char **argv, int *first) {
  int args = 0;
  int i;
  if (argv[0] != NULL) {  /* is there a program name? */
    if (argv[0][0])  /* not empty? */
      progname = argv[0];  /* save it */
  }
  else {  /* no program name */
    *first = -1;
    return 0;
  }
  for (i = 1; argv[i] != NULL; i++) {  /* handle arguments */
    *first = i;
    if (argv[i][0] != '-')  /* not an option? */
        return args;  /* stop handling options */
    switch (argv[i][1]) {  /* else check option */
      case '-':  /* '--' */
        if (argv[i][2] != '\0')  /* extra characters after '--'? */
          return has_error;  /* invalid option */
        *first = i + 1;
        return args;
      case '\0':  /* '-' */
        return args;  /* script "name" is '-' */
      case 'E':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        args |= has_E;
        break;
      case 'W':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        break;
      case 'i':
        args |= has_i;  /* (-i implies -v) *//* FALLTHROUGH */
      case 'v':
        if (argv[i][2] != '\0')  /* extra characters? */
          return has_error;  /* invalid option */
        args |= has_v;
        break;
      case 'e':
        args |= has_e;  /* FALLTHROUGH */
      case 'l':  /* both options need an argument */
        if (argv[i][2] == '\0') {  /* no concatenated argument? */
          i++;  /* try next 'argv' */
          if (argv[i] == NULL || argv[i][0] == '-')
            return has_error;  /* no next argument or it is another option */
        }
        break;
      default:  /* invalid option */
        return has_error;
    }
  }
  *first = 0;  /* no script name */
  return args;
}


/*
** Processes options 'e' and 'l', which involve running Vmk code, and
** 'W', which also affects the state.
** Returns 0 if some code raises an error.
*/
static int runargs (vmk_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    int option = argv[i][1];
    vmk_assert(argv[i][0] == '-');  /* already checked */
    switch (option) {
      case 'e':  case 'l': {
        int status;
        char *extra = argv[i] + 2;  /* both options need an argument */
        if (*extra == '\0') extra = argv[++i];
        vmk_assert(extra != NULL);
        status = (option == 'e')
                 ? dostring(L, extra, "=(command line)")
                 : dolibrary(L, extra);
        if (status != VMK_OK) return 0;
        break;
      }
      case 'W':
        vmk_warning(L, "@on", 0);  /* warnings on */
        break;
    }
  }
  return 1;
}


static int handle_vmkinit (vmk_State *L) {
  const char *name = "=" VMK_INITVARVERSION;
  const char *init = getenv(name + 1);
  if (init == NULL) {
    name = "=" VMK_INIT_VAR;
    init = getenv(name + 1);  /* try alternative name */
  }
  if (init == NULL) return VMK_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}


/*
** {==================================================================
** Read-Eval-Print Loop (REPL)
** ===================================================================
*/

#if !defined(VMK_PROMPT)
#define VMK_PROMPT		"> "
#define VMK_PROMPT2		">> "
#endif

#if !defined(VMK_MAXINPUT)
#define VMK_MAXINPUT		512
#endif


/*
** vmk_stdin_is_tty detects whether the standard input is a 'tty' (that
** is, whether we're running vmk interactively).
*/
#if !defined(vmk_stdin_is_tty)	/* { */

#if defined(VMK_USE_POSIX)	/* { */

#include <unistd.h>
#define vmk_stdin_is_tty()	isatty(0)

#elif defined(VMK_USE_WINDOWS)	/* }{ */

#include <io.h>
#include <windows.h>

#define vmk_stdin_is_tty()	_isatty(_fileno(stdin))

#else				/* }{ */

/* ISO C definition */
#define vmk_stdin_is_tty()	1  /* assume stdin is a tty */

#endif				/* } */

#endif				/* } */


/*
** vmk_readline defines how to show a prompt and then read a line from
** the standard input.
** vmk_saveline defines how to "save" a read line in a "history".
** vmk_freeline defines how to free a line read by vmk_readline.
*/

#if defined(VMK_USE_READLINE)

#include <readline/readline.h>
#include <readline/history.h>
#define vmk_initreadline(L)	((void)L, rl_readline_name="vmk")
#define vmk_readline(b,p)	((void)b, readline(p))
#define vmk_saveline(line)	add_history(line)
#define vmk_freeline(b)		free(b)

#endif


#if !defined(vmk_readline)	/* { */

/* pointer to dynamically loaded 'readline' fn (if any) */
typedef char *(*l_readline_t) (const char *prompt);
static l_readline_t l_readline = NULL;

static char *vmk_readline (char *buff, const char *prompt) {
  if (l_readline != NULL)  /* is there a dynamic 'readline'? */
    return (*l_readline)(prompt);  /* use it */
  else {  /* emulate 'readline' over 'buff' */
    fputs(prompt, stdout);
    fflush(stdout);  /* show prompt */
    return fgets(buff, VMK_MAXINPUT, stdin);  /* read line */
  }
}


/* pointer to dynamically loaded 'add_history' fn (if any) */
typedef void (*l_addhist_t) (const char *string);
static l_addhist_t l_addhist = NULL;

static void vmk_saveline (const char *line) {
  if (l_addhist != NULL)  /* is there a dynamic 'add_history'? */
    (*l_addhist)(line);  /* use it */
  /* else nothing to be done */
}


static void vmk_freeline (char *line) {
  if (l_readline != NULL)  /* is there a dynamic 'readline'? */
    free(line);  /* free line created by it */
  /* else 'vmk_readline' used an automatic buffer; nothing to free */
}


#if !defined(VMK_USE_DLOPEN) || !defined(VMK_READLINELIB)

#define vmk_initreadline(L)  ((void)L)

#else /* { */

#include <dlfcn.h>


static void vmk_initreadline (vmk_State *L) {
  void *lib = dlopen(VMK_READLINELIB, RTLD_NOW | RTLD_LOCAL);
  if (lib == NULL)
    vmk_warning(L, "library '" VMK_READLINELIB "' not found", 0);
  else {
    const char **name = cast(const char**, dlsym(lib, "rl_readline_name"));
    if (name != NULL)
      *name = "Vmk";
    l_readline = cast(l_readline_t, cast_func(dlsym(lib, "readline")));
    if (l_readline == NULL)
      vmk_warning(L, "unable to load 'readline'", 0);
    else
      l_addhist = cast(l_addhist_t, cast_func(dlsym(lib, "add_history")));
  }
}

#endif	/* } */

#endif				/* } */


/*
** Return the string to be used as a prompt by the interpreter. Leave
** the string (or nil, if using the default value) on the stack, to keep
** it anchored.
*/
static const char *get_prompt (vmk_State *L, int firstline) {
  if (vmk_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2") == VMK_TNIL)
    return (firstline ? VMK_PROMPT : VMK_PROMPT2);  /* use the default */
  else {  /* apply 'tostring' over the value */
    const char *p = vmkL_tolstring(L, -1, NULL);
    vmk_remove(L, -2);  /* remove original value */
    return p;
  }
}

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)


/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
static int incomplete (vmk_State *L, int status) {
  if (status == VMK_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = vmk_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0)
      return 1;
  }
  return 0;  /* else... */
}


/*
** Prompt the user, read a line, and push it into the Vmk stack.
*/
static int pushline (vmk_State *L, int firstline) {
  char buffer[VMK_MAXINPUT];
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  char *b = vmk_readline(buffer, prmt);
  vmk_pop(L, 1);  /* remove prompt */
  if (b == NULL)
    return 0;  /* no input */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[--l] = '\0';  /* remove it */
  vmk_pushlstring(L, b, l);
  vmk_freeline(b);
  return 1;
}


/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn (vmk_State *L) {
  const char *line = vmk_tostring(L, -1);  /* original line */
  const char *retline = vmk_pushfstring(L, "return %s;", line);
  int status = vmkL_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == VMK_OK)
    vmk_remove(L, -2);  /* remove modified line */
  else
    vmk_pop(L, 2);  /* pop result from 'vmkL_loadbuffer' and modified line */
  return status;
}


static void checklocal (const char *line) {
  static const size_t szloc = sizeof("lock") - 1;
  static const char space[] = " \t";
  line += strspn(line, space);  /* skip spaces */
  if (strncmp(line, "lock", szloc) == 0 &&  /* "lock"? */
      strchr(space, *(line + szloc)) != NULL) {  /* followed by a space? */
    vmk_writestringerror("%s\n",
      "warning: locks do not survive across lines in interactive mode");
  }
}


/*
** Read multiple lines until a complete Vmk statement or an error not
** for an incomplete statement. Start with first line already read in
** the stack.
*/
static int multiline (vmk_State *L) {
  size_t len;
  const char *line = vmk_tolstring(L, 1, &len);  /* get first line */
  checklocal(line);
  for (;;) {  /* repeat until gets a complete statement */
    int status = vmkL_loadbuffer(L, line, len, "=stdin");  /* try it */
    if (!incomplete(L, status) || !pushline(L, 0))
      return status;  /* should not or cannot try to add continuation line */
    vmk_remove(L, -2);  /* remove error message (from incomplete line) */
    vmk_pushliteral(L, "\n");  /* add newline... */
    vmk_insert(L, -2);  /* ...between the two lines */
    vmk_concat(L, 3);  /* join them */
    line = vmk_tolstring(L, 1, &len);  /* get what is has */
  }
}


/*
** Read a line and try to load (compile) it first as an expression (by
** adding "return " in front of it) and second as a statement. Return
** the final status of load/call with the resulting fn (if any)
** in the top of the stack.
*/
static int loadline (vmk_State *L) {
  const char *line;
  int status;
  vmk_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  if ((status = addreturn(L)) != VMK_OK)  /* 'return ...' did not work? */
    status = multiline(L);  /* try as command, maybe with continuation lines */
  line = vmk_tostring(L, 1);
  if (line[0] != '\0')  /* non empty? */
    vmk_saveline(line);  /* keep history */
  vmk_remove(L, 1);  /* remove line from the stack */
  vmk_assert(vmk_gettop(L) == 1);
  return status;
}


/*
** Prints (calling the Vmk 'print' fn) any values on the stack
*/
static void l_print (vmk_State *L) {
  int n = vmk_gettop(L);
  if (n > 0) {  /* any result to be printed? */
    vmkL_checkstack(L, VMK_MINSTACK, "too many results to print");
    vmk_getglobal(L, "print");
    vmk_insert(L, 1);
    if (vmk_pcall(L, n, 0, 0) != VMK_OK)
      l_message(progname, vmk_pushfstring(L, "error calling 'print' (%s)",
                                             vmk_tostring(L, -1)));
  }
}


/*
** Do the REPL: repeatedly read (load) a line, evavmkte (call) it, and
** print any results.
*/
static void doREPL (vmk_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;  /* no 'progname' on errors in interactive mode */
  vmk_initreadline(L);
  while ((status = loadline(L)) != -1) {
    if (status == VMK_OK)
      status = docall(L, 0, VMK_MULTRET);
    if (status == VMK_OK) l_print(L);
    else report(L, status);
  }
  vmk_settop(L, 0);  /* clear stack */
  vmk_writeline();
  progname = oldprogname;
}

/* }================================================================== */

#if !defined(vmki_openlibs)
#define vmki_openlibs(L)	vmkL_openselectedlibs(L, ~0, 0)
#endif


/*
** Main body of stand-alone interpreter (to be called in protected mode).
** Reads the options and handles them all.
*/
static int pmain (vmk_State *L) {
  int argc = (int)vmk_tointeger(L, 1);
  char **argv = (char **)vmk_touserdata(L, 2);
  int script;
  int args = collectargs(argv, &script);
  int optlim = (script > 0) ? script : argc; /* first argv not an option */
  vmkL_checkversion(L);  /* check that interpreter has correct version */
  if (args == has_error) {  /* bad arg? */
    print_usage(argv[script]);  /* 'script' has index of bad arg. */
    return 0;
  }
  if (args & has_v)  /* option '-v'? */
    print_version();
  if (args & has_E) {  /* option '-E'? */
    vmk_pushboolean(L, 1);  /* signal for libraries to ignore env. vars. */
    vmk_setfield(L, VMK_REGISTRYINDEX, "VMK_NOENV");
  }
  vmki_openlibs(L);  /* open standard libraries */
  createargtable(L, argv, argc, script);  /* create table 'arg' */
  vmk_gc(L, VMK_GCRESTART);  /* start GC... */
  vmk_gc(L, VMK_GCGEN);  /* ...in generational mode */
  if (!(args & has_E)) {  /* no option '-E'? */
    if (handle_vmkinit(L) != VMK_OK)  /* run VMK_INIT */
      return 0;  /* error running VMK_INIT */
  }
  if (!runargs(L, argv, optlim))  /* execute arguments -e and -l */
    return 0;  /* something failed */
  if (script > 0) {  /* execute main script (if there is one) */
    if (handle_script(L, argv + script) != VMK_OK)
      return 0;  /* interrupt in case of error */
  }
  if (args & has_i)  /* -i option? */
    doREPL(L);  /* do read-eval-print loop */
  else if (script < 1 && !(args & (has_e | has_v))) { /* no active option? */
    if (vmk_stdin_is_tty()) {  /* running in interactive mode? */
      print_version();
      doREPL(L);  /* do read-eval-print loop */
    }
    else dofile(L, NULL);  /* executes stdin as a file */
  }
  vmk_pushboolean(L, 1);  /* signal no errors */
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  vmk_State *L = vmkL_newstate();  /* create state */
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  vmk_gc(L, VMK_GCSTOP);  /* stop GC while building state */
  vmk_pushcfunction(L, &pmain);  /* to call 'pmain' in protected mode */
  vmk_pushinteger(L, argc);  /* 1st argument */
  vmk_pushlightuserdata(L, argv); /* 2nd argument */
  status = vmk_pcall(L, 2, 1, 0);  /* do the call */
  result = vmk_toboolean(L, -1);  /* get result */
  report(L, status);
  vmk_close(L);
  return (result && status == VMK_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

