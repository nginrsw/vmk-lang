# Developer's makefile for building Vmk
# see vmkconf.h for further customization

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Warnings valid for both C and C++
CWARNSCPP= \
	-Wfatal-errors \
	-Wextra \
	-Wshadow \
	-Wundef \
	-Wwrite-strings \
	-Wredundant-decls \
	-Wdisabled-optimization \
	-Wdouble-promotion \
	-Wmissing-declarations \
	-Wconversion \
	-Wstrict-overflow=2 \
        # the next warnings might be useful sometimes,
	# but usually they generate too much noise
	# -Werror \
	# -pedantic   # warns if we use jump tables \
	# -Wformat=2 \
	# -Wcast-qual \


# Warnings for gcc, not valid for clang
CWARNGCC= \
	-Wlogical-op \
	-Wno-aggressive-loop-optimizations \


# The next warnings are neither valid nor needed for C++
CWARNSC= -Wdeclaration-after-statement \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wstrict-prototypes \
	-Wc++-compat \
	-Wold-style-definition \


CWARNS= $(CWARNSCPP) $(CWARNSC) $(CWARNGCC)

# Some useful compiler options for internal tests:
# -DVMKI_ASSERT turns on all assertions inside Vmk.
# -DHARDSTACKTESTS forces a reallocation of the stack at every point where
# the stack can be reallocated.
# -DHARDMEMTESTS forces a full collection at all points where the collector
# can run.
# -DEMERGENCYGCTESTS forces an emergency collection at every single allocation.
# -DEXTERNMEMCHECK removes internal consistency checking of blocks being
# deallocated (useful when an external tool like valgrind does the check).
# -DMAXINDEXRK=k limits range of constants in RK instruction operands.
# -DVMK_COMPAT_5_3

# -pg -malign-double
# -DVMK_USE_CTYPE -DVMK_USE_APICHECK

# The following options help detect "undefined behavior"s that seldom
# create problems; some are only available in newer gcc versions. To
# use some of them, we also have to define an environment variable
# ASAN_OPTIONS="detect_invalid_pointer_pairs=2".
# -fsanitize=undefined
# -fsanitize=pointer-subtract -fsanitize=address -fsanitize=pointer-compare
# TESTS= -DVMK_USER_H='"ltests.h"' -Og -g


LOCAL = $(TESTS) $(CWARNS)


# To enable Linux goodies, -DVMK_USE_LINUX
# For C89, "-std=c89 -DVMK_USE_C89"
# Note that Linux/Posix options are not compatible with C89
MYCFLAGS= $(LOCAL) -std=c99 -DVMK_USE_LINUX
MYLDFLAGS= $(LOCAL) -Wl,-E
MYLIBS= -ldl


CC= gcc
CFLAGS= -Wall -O2 $(MYCFLAGS) -fno-stack-protector -fno-common -march=native
AR= ar rc
RANLIB= ranlib
RM= rm -f



# == END OF USER SETTINGS. NO NEED TO CHANGE ANYTHING BELOW THIS LINE =========


LIBS = -lm

CORE_T=	libvmk.a
CORE_O=	lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o \
	lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o \
	ltm.o lundump.o lvm.o lzio.o ltests.o
AUX_O=	lauxlib.o
LIB_O=	lbaselib.o ldblib.o liolib.o lmathlib.o loslib.o ltablib.o lstrlib.o \
	lutf8lib.o loadlib.o lcorolib.o linit.o

VMK_T=	vmk
VMK_O=	vmk.o


ALL_T= $(CORE_T) $(VMK_T)
ALL_O= $(CORE_O) $(VMK_O) $(AUX_O) $(LIB_O)
ALL_A= $(CORE_T)

all:	$(ALL_T)
	touch all

o:	$(ALL_O)

a:	$(ALL_A)

$(CORE_T): $(CORE_O) $(AUX_O) $(LIB_O)
	$(AR) $@ $?
	$(RANLIB) $@

$(VMK_T): $(VMK_O) $(CORE_T)
	$(CC) -o $@ $(MYLDFLAGS) $(VMK_O) $(CORE_T) $(LIBS) $(MYLIBS) $(DL)


clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM *.c

echo:
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "AR = $(AR)"
	@echo "RANLIB = $(RANLIB)"
	@echo "RM = $(RM)"
	@echo "MYCFLAGS = $(MYCFLAGS)"
	@echo "MYLDFLAGS = $(MYLDFLAGS)"
	@echo "MYLIBS = $(MYLIBS)"
	@echo "DL = $(DL)"

$(ALL_O): makefile ltests.h

# DO NOT EDIT
# automatically made with 'gcc -MM l*.c'

lapi.o: lapi.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lstring.h \
 ltable.h lundump.h lvm.h
lauxlib.o: lauxlib.c lprefix.h vmk.h vmkconf.h lauxlib.h llimits.h
lbaselib.o: lbaselib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
lcode.o: lcode.c lprefix.h vmk.h vmkconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lgc.h lstring.h ltable.h lvm.h lopnames.h
lcorolib.o: lcorolib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
lctype.o: lctype.c lprefix.h lctype.h vmk.h vmkconf.h llimits.h
ldblib.o: ldblib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h llimits.h
ldebug.o: ldebug.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lcode.h llex.h lopcodes.h lparser.h \
 ldebug.h ldo.h lfunc.h lstring.h lgc.h ltable.h lvm.h
ldo.o: ldo.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lparser.h lstring.h ltable.h lundump.h lvm.h
ldump.o: ldump.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lgc.h ltable.h lundump.h
lfunc.o: lfunc.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h
lgc.o: lgc.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h llex.h lstring.h \
 ltable.h
linit.o: linit.c lprefix.h vmk.h vmkconf.h vmklib.h lauxlib.h llimits.h
liolib.o: liolib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h llimits.h
llex.o: llex.c lprefix.h vmk.h vmkconf.h lctype.h llimits.h ldebug.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lgc.h llex.h lparser.h \
 lstring.h ltable.h
lmathlib.o: lmathlib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
lmem.o: lmem.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h
loadlib.o: loadlib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
lobject.o: lobject.c lprefix.h vmk.h vmkconf.h lctype.h llimits.h \
 ldebug.h lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h \
 lvm.h
lopcodes.o: lopcodes.c lprefix.h lopcodes.h llimits.h vmk.h vmkconf.h \
 lobject.h
loslib.o: loslib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h llimits.h
lparser.o: lparser.c lprefix.h vmk.h vmkconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lfunc.h lstring.h lgc.h ltable.h
lstate.o: lstate.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h llex.h \
 lstring.h ltable.h
lstring.o: lstring.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h
lstrlib.o: lstrlib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
ltable.o: ltable.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
ltablib.o: ltablib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
ltests.o: ltests.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lauxlib.h lcode.h llex.h lopcodes.h \
 lparser.h lctype.h ldebug.h ldo.h lfunc.h lopnames.h lstring.h lgc.h \
 ltable.h vmklib.h
ltm.o: ltm.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
vmk.o: vmk.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h llimits.h
lundump.o: lundump.c lprefix.h vmk.h vmkconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lstring.h lgc.h \
 ltable.h lundump.h
lutf8lib.o: lutf8lib.c lprefix.h vmk.h vmkconf.h lauxlib.h vmklib.h \
 llimits.h
lvm.o: lvm.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lstring.h ltable.h lvm.h ljumptab.h
lzio.o: lzio.c lprefix.h vmk.h vmkconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h

# (end of Makefile)
