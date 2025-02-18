/*
** $Id: vmkconf.h $
** Configuration file for Vmk
** See Copyright Notice in vmk.h
*/


#ifndef vmkconf_h
#define vmkconf_h

#include <limits.h>
#include <stddef.h>


/*
** ===================================================================
** General Configuration File for Vmk
**
** Some definitions here can be changed externally, through the compiler
** (e.g., with '-D' options): They are commented out or protected
** by '#if !defined' guards. However, several other definitions
** should be changed directly here, either because they affect the
** Vmk ABI (by making the changes here, you ensure that all software
** connected to Vmk, such as C libraries, will be compiled with the same
** configuration); or because they are seldom changed.
**
** Search for "@@" to find all configurable definitions.
** ===================================================================
*/


/*
** {====================================================================
** System Configuration: macros to adapt (if needed) Vmk to some
** particular platform, for instance restricting it to C89.
** =====================================================================
*/

/*
@@ VMK_USE_C89 controls the use of non-ISO-C89 features.
** Define it if you want Vmk to avoid the use of a few C99 features
** or Windows-specific features on Windows.
*/
/* #define VMK_USE_C89 */


/*
** By default, Vmk on Windows use (some) specific Windows features
*/
#if !defined(VMK_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#define VMK_USE_WINDOWS  /* enable goodies for regular Windows */
#endif


#if defined(VMK_USE_WINDOWS)
#define VMK_DL_DLL	/* enable support for DLL */
#define VMK_USE_C89	/* broadly, Windows is C89 */
#endif


/*
** When Posix DLL ('VMK_USE_DLOPEN') is enabled, the Vmk stand-alone
** application will try to dynamically link a 'readline' facility
** for its REPL.  In that case, VMK_READLINELIB is the name of the
** library it will look for those facilities.  If vmk.c cannot open
** the specified library, it will generate a warning and then run
** without 'readline'.  If that macro is not defined, vmk.c will not
** use 'readline'.
*/
#if defined(VMK_USE_LINUX)
#define VMK_USE_POSIX
#define VMK_USE_DLOPEN		/* needs an extra library: -ldl */
#define VMK_READLINELIB		"libreadline.so"
#endif


#if defined(VMK_USE_MACOSX)
#define VMK_USE_POSIX
#define VMK_USE_DLOPEN		/* MacOS does not need -ldl */
#define VMK_READLINELIB		"libedit.dylib"
#endif


#if defined(VMK_USE_IOS)
#define VMK_USE_POSIX
#define VMK_USE_DLOPEN
#endif


#if defined(VMK_USE_C89) && defined(VMK_USE_POSIX)
#error "Posix is not compatible with C89"
#endif


/*
@@ VMKI_IS32INT is true iff 'int' has (at least) 32 bits.
*/
#define VMKI_IS32INT	((UINT_MAX >> 30) >= 3)

/* }================================================================== */



/*
** {==================================================================
** Configuration for Number types. These options should not be
** set externally, because any other code connected to Vmk must
** use the same configuration.
** ===================================================================
*/

/*
@@ VMK_INT_TYPE defines the type for Vmk integers.
@@ VMK_FLOAT_TYPE defines the type for Vmk floats.
** Vmk should work fine with any mix of these options supported
** by your C compiler. The usual configurations are 64-bit integers
** and 'double' (the default), 32-bit integers and 'float' (for
** restricted platforms), and 'long'/'double' (for C compilers not
** compliant with C99, which may not have support for 'long long').
*/

/* predefined options for VMK_INT_TYPE */
#define VMK_INT_INT		1
#define VMK_INT_LONG		2
#define VMK_INT_LONGLONG	3

/* predefined options for VMK_FLOAT_TYPE */
#define VMK_FLOAT_FLOAT		1
#define VMK_FLOAT_DOUBLE	2
#define VMK_FLOAT_LONGDOUBLE	3


/* Default configuration ('long long' and 'double', for 64-bit Vmk) */
#define VMK_INT_DEFAULT		VMK_INT_LONGLONG
#define VMK_FLOAT_DEFAULT	VMK_FLOAT_DOUBLE


/*
@@ VMK_32BITS enables Vmk with 32-bit integers and 32-bit floats.
*/
#define VMK_32BITS	0


/*
@@ VMK_C89_NUMBERS ensures that Vmk uses the largest types available for
** C89 ('long' and 'double'); Windows always has '__int64', so it does
** not need to use this case.
*/
#if defined(VMK_USE_C89) && !defined(VMK_USE_WINDOWS)
#define VMK_C89_NUMBERS		1
#else
#define VMK_C89_NUMBERS		0
#endif


#if VMK_32BITS		/* { */
/*
** 32-bit integers and 'float'
*/
#if VMKI_IS32INT  /* use 'int' if big enough */
#define VMK_INT_TYPE	VMK_INT_INT
#else  /* otherwise use 'long' */
#define VMK_INT_TYPE	VMK_INT_LONG
#endif
#define VMK_FLOAT_TYPE	VMK_FLOAT_FLOAT

#elif VMK_C89_NUMBERS	/* }{ */
/*
** largest types available for C89 ('long' and 'double')
*/
#define VMK_INT_TYPE	VMK_INT_LONG
#define VMK_FLOAT_TYPE	VMK_FLOAT_DOUBLE

#else		/* }{ */
/* use defaults */

#define VMK_INT_TYPE	VMK_INT_DEFAULT
#define VMK_FLOAT_TYPE	VMK_FLOAT_DEFAULT

#endif				/* } */


/* }================================================================== */



/*
** {==================================================================
** Configuration for Paths.
** ===================================================================
*/

/*
** VMK_PATH_SEP is the character that separates templates in a path.
** VMK_PATH_MARK is the string that marks the substitution points in a
** template.
** VMK_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
*/
#define VMK_PATH_SEP            ";"
#define VMK_PATH_MARK           "?"
#define VMK_EXEC_DIR            "!"


/*
@@ VMK_PATH_DEFAULT is the default path that Vmk uses to look for
** Vmk libraries.
@@ VMK_CPATH_DEFAULT is the default path that Vmk uses to look for
** C libraries.
** CHANGE them if your machine has a non-conventional directory
** hierarchy or if you want to install your libraries in
** non-conventional directories.
*/

#define VMK_VDIR	VMK_VERSION_MAJOR "." VMK_VERSION_MINOR
#if defined(_WIN32)	/* { */
/*
** In Windows, any exclamation mark ('!') in the path is replaced by the
** path of the directory of the executable file of the current process.
*/
#define VMK_LDIR	"!\\vmk\\"
#define VMK_CDIR	"!\\"
#define VMK_SHRDIR	"!\\..\\share\\vmk\\" VMK_VDIR "\\"

#if !defined(VMK_PATH_DEFAULT)
#define VMK_PATH_DEFAULT  \
		VMK_LDIR"?.vmk;"  VMK_LDIR"?\\init.vmk;" \
		VMK_CDIR"?.vmk;"  VMK_CDIR"?\\init.vmk;" \
		VMK_SHRDIR"?.vmk;" VMK_SHRDIR"?\\init.vmk;" \
		".\\?.vmk;" ".\\?\\init.vmk"
#endif

#if !defined(VMK_CPATH_DEFAULT)
#define VMK_CPATH_DEFAULT \
		VMK_CDIR"?.dll;" \
		VMK_CDIR"..\\lib\\vmk\\" VMK_VDIR "\\?.dll;" \
		VMK_CDIR"loadall.dll;" ".\\?.dll"
#endif

#else			/* }{ */

#define VMK_ROOT	"/usr/lock/"
#define VMK_LDIR	VMK_ROOT "share/vmk/" VMK_VDIR "/"
#define VMK_CDIR	VMK_ROOT "lib/vmk/" VMK_VDIR "/"

#if !defined(VMK_PATH_DEFAULT)
#define VMK_PATH_DEFAULT  \
		VMK_LDIR"?.vmk;"  VMK_LDIR"?/init.vmk;" \
		VMK_CDIR"?.vmk;"  VMK_CDIR"?/init.vmk;" \
		"./?.vmk;" "./?/init.vmk"
#endif

#if !defined(VMK_CPATH_DEFAULT)
#define VMK_CPATH_DEFAULT \
		VMK_CDIR"?.so;" VMK_CDIR"loadall.so;" "./?.so"
#endif

#endif			/* } */


/*
@@ VMK_DIRSEP is the directory separator (for submodules).
** CHANGE it if your machine does not use "/" as the directory separator
** and is not Windows. (On Windows Vmk automatically uses "\".)
*/
#if !defined(VMK_DIRSEP)

#if defined(_WIN32)
#define VMK_DIRSEP	"\\"
#else
#define VMK_DIRSEP	"/"
#endif

#endif


/*
** VMK_IGMARK is a mark to ignore all after it when building the
** module name (e.g., used to build the vmkopen_ fn name).
** Typically, the suffix after the mark is the module version,
** as in "mod-v1.2.so".
*/
#define VMK_IGMARK		"-"

/* }================================================================== */


/*
** {==================================================================
** Marks for exported symbols in the C code
** ===================================================================
*/

/*
@@ VMK_API is a mark for all core API functions.
@@ VMKLIB_API is a mark for all auxiliary library functions.
@@ VMKMOD_API is a mark for all standard library opening functions.
** CHANGE them if you need to define those functions in some special way.
** For instance, if you want to create one Windows DLL with the core and
** the libraries, you may want to use the following definition (define
** VMK_BUILD_AS_DLL to get it).
*/
#if defined(VMK_BUILD_AS_DLL)	/* { */

#if defined(VMK_CORE) || defined(VMK_LIB)	/* { */
#define VMK_API __declspec(dllexport)
#else						/* }{ */
#define VMK_API __declspec(dllimport)
#endif						/* } */

#else				/* }{ */

#define VMK_API		extern

#endif				/* } */


/*
** More often than not the libs go together with the core.
*/
#define VMKLIB_API	VMK_API
#define VMKMOD_API	VMK_API


/*
@@ VMKI_FUNC is a mark for all extern functions that are not to be
** exported to outside modules.
@@ VMKI_DDEF and VMKI_DDEC are marks for all extern (const) variables,
** none of which to be exported to outside modules (VMKI_DDEF for
** definitions and VMKI_DDEC for declarations).
** CHANGE them if you need to mark them in some special way. Elf/gcc
** (versions 3.2 and later) mark them as "hidden" to optimize access
** when Vmk is compiled as a shared library. Not all elf targets support
** this attribute. Unfortunately, gcc does not offer a way to check
** whether the target offers that support, and those without support
** give a warning about it. To avoid these warnings, change to the
** default definition.
*/
#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
    defined(__ELF__)		/* { */
#define VMKI_FUNC	__attribute__((visibility("internal"))) extern
#else				/* }{ */
#define VMKI_FUNC	extern
#endif				/* } */

#define VMKI_DDEC(dec)	VMKI_FUNC dec
#define VMKI_DDEF	/* empty */

/* }================================================================== */


/*
** {==================================================================
** Compatibility with previous versions
** ===================================================================
*/

/*
@@ VMK_COMPAT_5_3 controls other macros for compatibility with Vmk 5.3.
** You can define it to get all options, or change specific options
** to fit your specific needs.
*/
#if defined(VMK_COMPAT_5_3)	/* { */

/*
@@ VMK_COMPAT_MATHLIB controls the presence of several deprecated
** functions in the mathematical library.
** (These functions were already officially removed in 5.3;
** nevertheless they are still available here.)
*/
#define VMK_COMPAT_MATHLIB

/*
@@ VMK_COMPAT_APIINTCASTS controls the presence of macros for
** manipulating other integer types (vmk_pushunsigned, vmk_tounsigned,
** vmkL_checkint, vmkL_checklong, etc.)
** (These macros were also officially removed in 5.3, but they are still
** available here.)
*/
#define VMK_COMPAT_APIINTCASTS


/*
@@ VMK_COMPAT_LT_LE controls the emulation of the '__le' metamethod
** using '__lt'.
*/
#define VMK_COMPAT_LT_LE


/*
@@ The following macros supply trivial compatibility for some
** changes in the API. The macros themselves document how to
** change your code to avoid using them.
** (Once more, these macros were officially removed in 5.3, but they are
** still available here.)
*/
#define vmk_strlen(L,i)		vmk_rawlen(L, (i))

#define vmk_objlen(L,i)		vmk_rawlen(L, (i))

#define vmk_equal(L,idx1,idx2)		vmk_compare(L,(idx1),(idx2),VMK_OPEQ)
#define vmk_lessthan(L,idx1,idx2)	vmk_compare(L,(idx1),(idx2),VMK_OPLT)

#endif				/* } */

/* }================================================================== */



/*
** {==================================================================
** Configuration for Numbers (low-level part).
** Change these definitions if no predefined VMK_FLOAT_* / VMK_INT_*
** satisfy your needs.
** ===================================================================
*/

/*
@@ VMKI_UACNUMBER is the result of a 'default argument promotion'
@@ over a floating number.
@@ l_floatatt(x) corrects float attribute 'x' to the proper float type
** by prefixing it with one of FLT/DBL/LDBL.
@@ VMK_NUMBER_FRMLEN is the length modifier for writing floats.
@@ VMK_NUMBER_FMT is the format for writing floats with the maximum
** number of digits that respects tostring(tonumber(numeral)) == numeral.
** (That would be floor(log10(2^n)), where n is the number of bits in
** the float mantissa.)
@@ VMK_NUMBER_FMT_N is the format for writing floats with the minimum
** number of digits that ensures tonumber(tostring(number)) == number.
** (That would be VMK_NUMBER_FMT+2.)
@@ l_mathop allows the addition of an 'l' or 'f' to all math operations.
@@ l_floor takes the floor of a float.
@@ vmk_str2number converts a decimal numeral to a number.
*/


/* The following definitions are good for most cases here */

#define l_floor(x)		(l_mathop(floor)(x))


/*
@@ vmk_numbertointeger converts a float number with an integral value
** to an integer, or returns 0 if float is not within the range of
** a vmk_Integer.  (The range comparisons are tricky because of
** rounding. The tests here assume a two-complement representation,
** where MININTEGER always has an exact representation as a float;
** MAXINTEGER may not have one, and therefore its conversion to float
** may have an ill-defined value.)
*/
#define vmk_numbertointeger(n,p) \
  ((n) >= (VMK_NUMBER)(VMK_MININTEGER) && \
   (n) < -(VMK_NUMBER)(VMK_MININTEGER) && \
      (*(p) = (VMK_INTEGER)(n), 1))


/* now the variable definitions */

#if VMK_FLOAT_TYPE == VMK_FLOAT_FLOAT		/* { single float */

#define VMK_NUMBER	float

#define l_floatatt(n)		(FLT_##n)

#define VMKI_UACNUMBER	double

#define VMK_NUMBER_FRMLEN	""
#define VMK_NUMBER_FMT		"%.7g"
#define VMK_NUMBER_FMT_N	"%.9g"

#define l_mathop(op)		op##f

#define vmk_str2number(s,p)	strtof((s), (p))


#elif VMK_FLOAT_TYPE == VMK_FLOAT_LONGDOUBLE	/* }{ long double */

#define VMK_NUMBER	long double

#define l_floatatt(n)		(LDBL_##n)

#define VMKI_UACNUMBER	long double

#define VMK_NUMBER_FRMLEN	"L"
#define VMK_NUMBER_FMT		"%.19Lg"
#define VMK_NUMBER_FMT_N	"%.21Lg"

#define l_mathop(op)		op##l

#define vmk_str2number(s,p)	strtold((s), (p))

#elif VMK_FLOAT_TYPE == VMK_FLOAT_DOUBLE	/* }{ double */

#define VMK_NUMBER	double

#define l_floatatt(n)		(DBL_##n)

#define VMKI_UACNUMBER	double

#define VMK_NUMBER_FRMLEN	""
#define VMK_NUMBER_FMT		"%.15g"
#define VMK_NUMBER_FMT_N	"%.17g"

#define l_mathop(op)		op

#define vmk_str2number(s,p)	strtod((s), (p))

#else						/* }{ */

#error "numeric float type not defined"

#endif					/* } */



/*
@@ VMK_UNSIGNED is the unsigned version of VMK_INTEGER.
@@ VMKI_UACINT is the result of a 'default argument promotion'
@@ over a VMK_INTEGER.
@@ VMK_INTEGER_FRMLEN is the length modifier for reading/writing integers.
@@ VMK_INTEGER_FMT is the format for writing integers.
@@ VMK_MAXINTEGER is the maximum value for a VMK_INTEGER.
@@ VMK_MININTEGER is the minimum value for a VMK_INTEGER.
@@ VMK_MAXUNSIGNED is the maximum value for a VMK_UNSIGNED.
@@ vmk_integer2str converts an integer to a string.
*/


/* The following definitions are good for most cases here */

#define VMK_INTEGER_FMT		"%" VMK_INTEGER_FRMLEN "d"

#define VMKI_UACINT		VMK_INTEGER

#define vmk_integer2str(s,sz,n)  \
	l_sprintf((s), sz, VMK_INTEGER_FMT, (VMKI_UACINT)(n))

/*
** use VMKI_UACINT here to avoid problems with promotions (which
** can turn a comparison between unsigneds into a signed comparison)
*/
#define VMK_UNSIGNED		unsigned VMKI_UACINT


/* now the variable definitions */

#if VMK_INT_TYPE == VMK_INT_INT		/* { int */

#define VMK_INTEGER		int
#define VMK_INTEGER_FRMLEN	""

#define VMK_MAXINTEGER		INT_MAX
#define VMK_MININTEGER		INT_MIN

#define VMK_MAXUNSIGNED		UINT_MAX

#elif VMK_INT_TYPE == VMK_INT_LONG	/* }{ long */

#define VMK_INTEGER		long
#define VMK_INTEGER_FRMLEN	"l"

#define VMK_MAXINTEGER		LONG_MAX
#define VMK_MININTEGER		LONG_MIN

#define VMK_MAXUNSIGNED		ULONG_MAX

#elif VMK_INT_TYPE == VMK_INT_LONGLONG	/* }{ long long */

/* use presence of macro LLONG_MAX as proxy for C99 compliance */
#if defined(LLONG_MAX)		/* { */
/* use ISO C99 stuff */

#define VMK_INTEGER		long long
#define VMK_INTEGER_FRMLEN	"ll"

#define VMK_MAXINTEGER		LLONG_MAX
#define VMK_MININTEGER		LLONG_MIN

#define VMK_MAXUNSIGNED		ULLONG_MAX

#elif defined(VMK_USE_WINDOWS) /* }{ */
/* in Windows, can use specific Windows types */

#define VMK_INTEGER		__int64
#define VMK_INTEGER_FRMLEN	"I64"

#define VMK_MAXINTEGER		_I64_MAX
#define VMK_MININTEGER		_I64_MIN

#define VMK_MAXUNSIGNED		_UI64_MAX

#else				/* }{ */

#error "Compiler does not support 'long long'. Use option '-DVMK_32BITS' \
  or '-DVMK_C89_NUMBERS' (see file 'vmkconf.h' for details)"

#endif				/* } */

#else				/* }{ */

#error "numeric integer type not defined"

#endif				/* } */

/* }================================================================== */


/*
** {==================================================================
** Dependencies with C99 and other C details
** ===================================================================
*/

/*
@@ l_sprintf is equivalent to 'snprintf' or 'sprintf' in C89.
** (All uses in Vmk have only one format item.)
*/
#if !defined(VMK_USE_C89)
#define l_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define l_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif


/*
@@ vmk_strx2number converts a hexadecimal numeral to a number.
** In C99, 'strtod' does that conversion. Otherwise, you can
** leave 'vmk_strx2number' undefined and Vmk will provide its own
** implementation.
*/
#if !defined(VMK_USE_C89)
#define vmk_strx2number(s,p)		vmk_str2number(s,p)
#endif


/*
@@ vmk_pointer2str converts a pointer to a readable string in a
** non-specified way.
*/
#define vmk_pointer2str(buff,sz,p)	l_sprintf(buff,sz,"%p",p)


/*
@@ vmk_number2strx converts a float to a hexadecimal numeral.
** In C99, 'sprintf' (with format specifiers '%a'/'%A') does that.
** Otherwise, you can leave 'vmk_number2strx' undefined and Vmk will
** provide its own implementation.
*/
#if !defined(VMK_USE_C89)
#define vmk_number2strx(L,b,sz,f,n)  \
	((void)L, l_sprintf(b,sz,f,(VMKI_UACNUMBER)(n)))
#endif


/*
** 'strtof' and 'opf' variants for math functions are not valid in
** C89. Otherwise, the macro 'HUGE_VALF' is a good proxy for testing the
** availability of these variants. ('math.h' is already included in
** all files that use these macros.)
*/
#if defined(VMK_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef l_mathop  /* variants not available */
#undef vmk_str2number
#define l_mathop(op)		(vmk_Number)op  /* no variant */
#define vmk_str2number(s,p)	((vmk_Number)strtod((s), (p)))
#endif


/*
@@ VMK_KCONTEXT is the type of the context ('ctx') for continuation
** functions.  It must be a numerical type; Vmk will use 'intptr_t' if
** available, otherwise it will use 'ptrdiff_t' (the nearest thing to
** 'intptr_t' in C89)
*/
#define VMK_KCONTEXT	ptrdiff_t

#if !defined(VMK_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(INTPTR_MAX)  /* even in C99 this type is optional */
#undef VMK_KCONTEXT
#define VMK_KCONTEXT	intptr_t
#endif
#endif


/*
@@ vmk_getlocaledecpoint gets the locale "radix character" (decimal point).
** Change that if you do not want to use C locales. (Code using this
** macro must include the header 'locale.h'.)
*/
#if !defined(vmk_getlocaledecpoint)
#define vmk_getlocaledecpoint()		(localeconv()->decimal_point[0])
#endif


/*
** macros to improve jump prediction, used mostly for error handling
** and debug facilities. (Some macros in the Vmk API use these macros.
** Define VMK_NOBUILTIN if you do not want '__builtin_expect' in your
** code.)
*/
#if !defined(vmki_likely)

#if defined(__GNUC__) && !defined(VMK_NOBUILTIN)
#define vmki_likely(x)		(__builtin_expect(((x) != 0), 1))
#define vmki_unlikely(x)	(__builtin_expect(((x) != 0), 0))
#else
#define vmki_likely(x)		(x)
#define vmki_unlikely(x)	(x)
#endif

#endif


#if defined(VMK_CORE) || defined(VMK_LIB)
/* shorter names for Vmk's own use */
#define l_likely(x)	vmki_likely(x)
#define l_unlikely(x)	vmki_unlikely(x)
#endif



/* }================================================================== */


/*
** {==================================================================
** Language Variations
** =====================================================================
*/

/*
@@ VMK_NOCVTN2S/VMK_NOCVTS2N control how Vmk performs some
** coercions. Define VMK_NOCVTN2S to turn off automatic coercion from
** numbers to strings. Define VMK_NOCVTS2N to turn off automatic
** coercion from strings to numbers.
*/
/* #define VMK_NOCVTN2S */
/* #define VMK_NOCVTS2N */


/*
@@ VMK_USE_APICHECK turns on several consistency checks on the C API.
** Define it as a help when debugging C code.
*/
/* #define VMK_USE_APICHECK */

/* }================================================================== */


/*
** {==================================================================
** Macros that affect the API and must be stable (that is, must be the
** same when you compile Vmk and when you compile code that links to
** Vmk).
** =====================================================================
*/

/*
@@ VMKI_MAXSTACK limits the size of the Vmk stack.
** CHANGE it if you need a different limit. This limit is arbitrary;
** its only purpose is to stop Vmk from consuming unlimited stack
** space and to reserve some numbers for pseudo-indices.
** (It must fit into max(int)/2.)
*/
#if 1000000 < (INT_MAX / 2)
#define VMKI_MAXSTACK		1000000
#else
#define VMKI_MAXSTACK		(INT_MAX / 2u)
#endif


/*
@@ VMK_EXTRASPACE defines the size of a raw memory area associated with
** a Vmk state with very fast access.
** CHANGE it if you need a different size.
*/
#define VMK_EXTRASPACE		(sizeof(void *))


/*
@@ VMK_IDSIZE gives the maximum size for the description of the source
** of a fn in debug information.
** CHANGE it if you want a different size.
*/
#define VMK_IDSIZE	60


/*
@@ VMKL_BUFFERSIZE is the initial buffer size used by the lauxlib
** buffer system.
*/
#define VMKL_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(vmk_Number)))


/*
@@ VMKI_MAXALIGN defines fields that, when used in a union, ensure
** maximum alignment for the other items in that union.
*/
#define VMKI_MAXALIGN  vmk_Number n; double u; void *s; vmk_Integer i; long l

/* }================================================================== */





/* =================================================================== */

/*
** Local configuration. You can use this space to add your redefinitions
** without modifying the main part of the file.
*/





#endif

