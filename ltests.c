/*
** $Id: ltests.c $
** Internal Module for Debugging of the Vmk Implementation
** See Copyright Notice in vmk.h
*/

#define ltests_c
#define VMK_CORE

#include "lprefix.h"


#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmk.h"

#include "lapi.h"
#include "lauxlib.h"
#include "lcode.h"
#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "vmklib.h"



/*
** The whole module only makes sense with VMK_DEBUG on
*/
#if defined(VMK_DEBUG)


void *l_Trick = 0;


#define obj_at(L,k)	s2v(L->ci->func.p + (k))


static int runC (vmk_State *L, vmk_State *L1, const char *pc);


static void setnameval (vmk_State *L, const char *name, int val) {
  vmk_pushinteger(L, val);
  vmk_setfield(L, -2, name);
}


static void pushobject (vmk_State *L, const TValue *o) {
  setobj2s(L, L->top.p, o);
  api_incr_top(L);
}


static void badexit (const char *fmt, const char *s1, const char *s2) {
  fprintf(stderr, fmt, s1);
  if (s2)
    fprintf(stderr, "extra info: %s\n", s2);
  /* avoid assertion failures when exiting */
  l_memcontrol.numblocks = l_memcontrol.total = 0;
  exit(EXIT_FAILURE);
}


static int tpanic (vmk_State *L) {
  const char *msg = (vmk_type(L, -1) == VMK_TSTRING)
                  ? vmk_tostring(L, -1)
                  : "error object is not a string";
  return (badexit("PANIC: unprotected error in call to Vmk API (%s)\n",
                   msg, NULL),
          0);  /* do not return to Vmk */
}


/*
** Warning fn for tests. First, it concatenates all parts of
** a warning in buffer 'buff'. Then, it has three modes:
** - 0.normal: messages starting with '#' are shown on standard output;
** - other messages abort the tests (they represent real warning
** conditions; the standard tests should not generate these conditions
** unexpectedly);
** - 1.allow: all messages are shown;
** - 2.store: all warnings go to the global '_WARN';
*/
static void warnf (void *ud, const char *msg, int tocont) {
  vmk_State *L = cast(vmk_State *, ud);
  static char buff[200] = "";  /* should be enough for tests... */
  static int onoff = 0;
  static int mode = 0;  /* start in normal mode */
  static int lasttocont = 0;
  if (!lasttocont && !tocont && *msg == '@') {  /* control message? */
    if (buff[0] != '\0')
      badexit("Control warning during warning: %s\naborting...\n", msg, buff);
    if (strcmp(msg, "@off") == 0)
      onoff = 0;
    else if (strcmp(msg, "@on") == 0)
      onoff = 1;
    else if (strcmp(msg, "@normal") == 0)
      mode = 0;
    else if (strcmp(msg, "@allow") == 0)
      mode = 1;
    else if (strcmp(msg, "@store") == 0)
      mode = 2;
    else
      badexit("Invalid control warning in test mode: %s\naborting...\n",
              msg, NULL);
    return;
  }
  lasttocont = tocont;
  if (strlen(msg) >= sizeof(buff) - strlen(buff))
    badexit("warnf-buffer overflow (%s)\n", msg, buff);
  strcat(buff, msg);  /* add new message to current warning */
  if (!tocont) {  /* message finished? */
    vmk_unlock(L);
    vmkL_checkstack(L, 1, "warn stack space");
    vmk_getglobal(L, "_WARN");
    if (!vmk_toboolean(L, -1))
      vmk_pop(L, 1);  /* ok, no previous unexpected warning */
    else {
      badexit("Unhandled warning in store mode: %s\naborting...\n",
              vmk_tostring(L, -1), buff);
    }
    vmk_lock(L);
    switch (mode) {
      case 0: {  /* normal */
        if (buff[0] != '#' && onoff)  /* unexpected warning? */
          badexit("Unexpected warning in test mode: %s\naborting...\n",
                  buff, NULL);
      }  /* FALLTHROUGH */
      case 1: {  /* allow */
        if (onoff)
          fprintf(stderr, "Vmk warning: %s\n", buff);  /* print warning */
        break;
      }
      case 2: {  /* store */
        vmk_unlock(L);
        vmkL_checkstack(L, 1, "warn stack space");
        vmk_pushstring(L, buff);
        vmk_setglobal(L, "_WARN");  /* assign message to global '_WARN' */
        vmk_lock(L);
        break;
      }
    }
    buff[0] = '\0';  /* prepare buffer for next warning */
  }
}


/*
** {======================================================================
** Controlled version for realloc.
** =======================================================================
*/

#define MARK		0x55  /* 01010101 (a nice pattern) */

typedef union Header {
  VMKI_MAXALIGN;
  struct {
    size_t size;
    int type;
  } d;
} Header;


#if !defined(EXTERNMEMCHECK)

/* full memory check */
#define MARKSIZE	16  /* size of marks after each block */
#define fillmem(mem,size)	memset(mem, -MARK, size)

#else

/* external memory check: don't do it twice */
#define MARKSIZE	0
#define fillmem(mem,size)	/* empty */

#endif


Memcontrol l_memcontrol =
  {0, 0UL, 0UL, 0UL, 0UL, (~0UL),
   {0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL}};


static void freeblock (Memcontrol *mc, Header *block) {
  if (block) {
    size_t size = block->d.size;
    int i;
    for (i = 0; i < MARKSIZE; i++)  /* check marks after block */
      vmk_assert(*(cast_charp(block + 1) + size + i) == MARK);
    mc->objcount[block->d.type]--;
    fillmem(block, sizeof(Header) + size + MARKSIZE);  /* erase block */
    free(block);  /* actually free block */
    mc->numblocks--;  /* update counts */
    mc->total -= size;
  }
}


void *debug_realloc (void *ud, void *b, size_t oldsize, size_t size) {
  Memcontrol *mc = cast(Memcontrol *, ud);
  Header *block = cast(Header *, b);
  int type;
  if (mc->memlimit == 0) {  /* first time? */
    char *limit = getenv("MEMLIMIT");  /* initialize memory limit */
    mc->memlimit = limit ? strtoul(limit, NULL, 10) : ULONG_MAX;
  }
  if (block == NULL) {
    type = (oldsize < VMK_NUMTYPES) ? cast_int(oldsize) : 0;
    oldsize = 0;
  }
  else {
    block--;  /* go to real header */
    type = block->d.type;
    vmk_assert(oldsize == block->d.size);
  }
  if (size == 0) {
    freeblock(mc, block);
    return NULL;
  }
  if (mc->failnext) {
    mc->failnext = 0;
    return NULL;  /* fake a single memory allocation error */
  }
  if (mc->countlimit != ~0UL && size != oldsize) {  /* count limit in use? */
    if (mc->countlimit == 0)
      return NULL;  /* fake a memory allocation error */
    mc->countlimit--;
  }
  if (size > oldsize && mc->total+size-oldsize > mc->memlimit)
    return NULL;  /* fake a memory allocation error */
  else {
    Header *newblock;
    int i;
    size_t commonsize = (oldsize < size) ? oldsize : size;
    size_t realsize = sizeof(Header) + size + MARKSIZE;
    if (realsize < size) return NULL;  /* arithmetic overflow! */
    newblock = cast(Header *, malloc(realsize));  /* alloc a new block */
    if (newblock == NULL)
      return NULL;  /* really out of memory? */
    if (block) {
      memcpy(newblock + 1, block + 1, commonsize);  /* copy old contents */
      freeblock(mc, block);  /* erase (and check) old copy */
    }
    /* initialize new part of the block with something weird */
    fillmem(cast_charp(newblock + 1) + commonsize, size - commonsize);
    /* initialize marks after block */
    for (i = 0; i < MARKSIZE; i++)
      *(cast_charp(newblock + 1) + size + i) = MARK;
    newblock->d.size = size;
    newblock->d.type = type;
    mc->total += size;
    if (mc->total > mc->maxmem)
      mc->maxmem = mc->total;
    mc->numblocks++;
    mc->objcount[type]++;
    return newblock + 1;
  }
}


/* }====================================================================== */



/*
** {=====================================================================
** Functions to check memory consistency.
** Most of these checks are done through asserts, so this code does
** not make sense with asserts off. For this reason, it uses 'assert'
** directly, instead of 'vmk_assert'.
** ======================================================================
*/

#include <assert.h>

/*
** Check GC invariants. For incremental mode, a black object cannot
** point to a white one. For generational mode, really old objects
** cannot point to young objects. Both old1 and touched2 objects
** cannot point to new objects (but can point to survivals).
** (Threads and open upvalues, despite being marked "really old",
** continue to be visited in all collections, and therefore can point to
** new objects. They, and only they, are old but gray.)
*/
static int testobjref1 (global_State *g, GCObject *f, GCObject *t) {
  if (isdead(g,t)) return 0;
  if (issweepphase(g))
    return 1;  /* no invariants */
  else if (g->gckind != KGC_GENMINOR)
    return !(isblack(f) && iswhite(t));  /* basic incremental invariant */
  else {  /* generational mode */
    if ((getage(f) == G_OLD && isblack(f)) && !isold(t))
      return 0;
    if ((getage(f) == G_OLD1 || getage(f) == G_TOUCHED2) &&
         getage(t) == G_NEW)
      return 0;
    return 1;
  }
}


static void printobj (global_State *g, GCObject *o) {
  printf("||%s(%p)-%c%c(%02X)||",
           ttypename(novariant(o->tt)), (void *)o,
           isdead(g,o) ? 'd' : isblack(o) ? 'b' : iswhite(o) ? 'w' : 'g',
           "ns01oTt"[getage(o)], o->marked);
  if (o->tt == VMK_VSHRSTR || o->tt == VMK_VLNGSTR)
    printf(" '%s'", getstr(gco2ts(o)));
}


void vmk_printobj (vmk_State *L, struct GCObject *o) {
  printobj(G(L), o);
}


void vmk_printvalue (TValue *v) {
  switch (ttype(v)) {
    case VMK_TNUMBER: {
      char buff[VMK_N2SBUFFSZ];
      unsigned len = vmkO_tostringbuff(v, buff);
      buff[len] = '\0';
      printf("%s", buff);
      break;
    }
    case VMK_TSTRING: {
      printf("'%s'", getstr(tsvalue(v)));
      break;
    }
    case VMK_TBOOLEAN: {
      printf("%s", (!l_isfalse(v) ? "true" : "false"));
      break;
    }
    case VMK_TLIGHTUSERDATA: {
      printf("light udata: %p", pvalue(v));
      break;
    }
    case VMK_TNIL: {
      printf("nil");
      break;
    }
    default: {
      if (ttislcf(v))
        printf("light C fn: %p", fvalue(v));
      else  /* must be collectable */
        printf("%s: %p", ttypename(ttype(v)), gcvalue(v));
      break;
    }
  }
}


static int testobjref (global_State *g, GCObject *f, GCObject *t) {
  int r1 = testobjref1(g, f, t);
  if (!r1) {
    printf("%d(%02X) - ", g->gcstate, g->currentwhite);
    printobj(g, f);
    printf("  ->  ");
    printobj(g, t);
    printf("\n");
  }
  return r1;
}


static void checkobjref (global_State *g, GCObject *f, GCObject *t) {
    assert(testobjref(g, f, t));
}


/*
** Version where 't' can be NULL. In that case, it should not apply the
** macro 'obj2gco' over the object. ('t' may have several types, so this
** definition must be a macro.)  Most checks need this version, because
** the check may run while an object is still being created.
*/
#define checkobjrefN(g,f,t)	{ if (t) checkobjref(g,f,obj2gco(t)); }


static void checkvalref (global_State *g, GCObject *f, const TValue *t) {
  assert(!iscollectable(t) || (righttt(t) && testobjref(g, f, gcvalue(t))));
}


static void checktable (global_State *g, Table *h) {
  unsigned int i;
  unsigned int asize = h->asize;
  Node *n, *limit = gnode(h, sizenode(h));
  GCObject *hgc = obj2gco(h);
  checkobjrefN(g, hgc, h->metatable);
  for (i = 0; i < asize; i++) {
    TValue aux;
    arr2obj(h, i, &aux);
    checkvalref(g, hgc, &aux);
  }
  for (n = gnode(h, 0); n < limit; n++) {
    if (!isempty(gval(n))) {
      TValue k;
      getnodekey(g->mainthread, &k, n);
      assert(!keyisnil(n));
      checkvalref(g, hgc, &k);
      checkvalref(g, hgc, gval(n));
    }
  }
}


static void checkudata (global_State *g, Udata *u) {
  int i;
  GCObject *hgc = obj2gco(u);
  checkobjrefN(g, hgc, u->metatable);
  for (i = 0; i < u->nuvalue; i++)
    checkvalref(g, hgc, &u->uv[i].uv);
}


static void checkproto (global_State *g, Proto *f) {
  int i;
  GCObject *fgc = obj2gco(f);
  checkobjrefN(g, fgc, f->source);
  for (i=0; i<f->sizek; i++) {
    if (iscollectable(f->k + i))
      checkobjref(g, fgc, gcvalue(f->k + i));
  }
  for (i=0; i<f->sizeupvalues; i++)
    checkobjrefN(g, fgc, f->upvalues[i].name);
  for (i=0; i<f->sizep; i++)
    checkobjrefN(g, fgc, f->p[i]);
  for (i=0; i<f->sizelocvars; i++)
    checkobjrefN(g, fgc, f->locvars[i].varname);
}


static void checkCclosure (global_State *g, CClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  for (i = 0; i < cl->nupvalues; i++)
    checkvalref(g, clgc, &cl->upvalue[i]);
}


static void checkLclosure (global_State *g, LClosure *cl) {
  GCObject *clgc = obj2gco(cl);
  int i;
  checkobjrefN(g, clgc, cl->p);
  for (i=0; i<cl->nupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    if (uv) {
      checkobjrefN(g, clgc, uv);
      if (!upisopen(uv))
        checkvalref(g, obj2gco(uv), uv->v.p);
    }
  }
}


static int vmk_checkpc (CallInfo *ci) {
  if (!isVmk(ci)) return 1;
  else {
    StkId f = ci->func.p;
    Proto *p = clLvalue(s2v(f))->p;
    return p->code <= ci->u.l.savedpc &&
           ci->u.l.savedpc <= p->code + p->sizecode;
  }
}


static void checkstack (global_State *g, vmk_State *L1) {
  StkId o;
  CallInfo *ci;
  UpVal *uv;
  assert(!isdead(g, L1));
  if (L1->stack.p == NULL) {  /* incomplete thread? */
    assert(L1->openupval == NULL && L1->ci == NULL);
    return;
  }
  for (uv = L1->openupval; uv != NULL; uv = uv->u.open.next)
    assert(upisopen(uv));  /* must be open */
  assert(L1->top.p <= L1->stack_last.p);
  assert(L1->tbclist.p <= L1->top.p);
  for (ci = L1->ci; ci != NULL; ci = ci->previous) {
    assert(ci->top.p <= L1->stack_last.p);
    assert(vmk_checkpc(ci));
  }
  for (o = L1->stack.p; o < L1->stack_last.p; o++)
    checkliveness(L1, s2v(o));  /* entire stack must have valid values */
}


static void checkrefs (global_State *g, GCObject *o) {
  switch (o->tt) {
    case VMK_VUSERDATA: {
      checkudata(g, gco2u(o));
      break;
    }
    case VMK_VUPVAL: {
      checkvalref(g, o, gco2upv(o)->v.p);
      break;
    }
    case VMK_VTABLE: {
      checktable(g, gco2t(o));
      break;
    }
    case VMK_VTHREAD: {
      checkstack(g, gco2th(o));
      break;
    }
    case VMK_VLCL: {
      checkLclosure(g, gco2lcl(o));
      break;
    }
    case VMK_VCCL: {
      checkCclosure(g, gco2ccl(o));
      break;
    }
    case VMK_VPROTO: {
      checkproto(g, gco2p(o));
      break;
    }
    case VMK_VSHRSTR:
    case VMK_VLNGSTR: {
      assert(!isgray(o));  /* strings are never gray */
      break;
    }
    default: assert(0);
  }
}


/*
** Check consistency of an object:
** - Dead objects can only happen in the 'allgc' list during a sweep
** phase (controlled by the caller through 'maybedead').
** - During pause, all objects must be white.
** - In generational mode:
**   * objects must be old enough for their lists ('listage').
**   * old objects cannot be white.
**   * old objects must be black, except for 'touched1', 'old0',
**     threads, and open upvalues.
**   * 'touched1' objects must be gray.
*/
static void checkobject (global_State *g, GCObject *o, int maybedead,
                         int listage) {
  if (isdead(g, o))
    assert(maybedead);
  else {
    assert(g->gcstate != GCSpause || iswhite(o));
    if (g->gckind == KGC_GENMINOR) {  /* generational mode? */
      assert(getage(o) >= listage);
      if (isold(o)) {
        assert(!iswhite(o));
        assert(isblack(o) ||
        getage(o) == G_TOUCHED1 ||
        getage(o) == G_OLD0 ||
        o->tt == VMK_VTHREAD ||
        (o->tt == VMK_VUPVAL && upisopen(gco2upv(o))));
      }
      assert(getage(o) != G_TOUCHED1 || isgray(o));
    }
    checkrefs(g, o);
  }
}


static l_mem checkgraylist (global_State *g, GCObject *o) {
  int total = 0;  /* count number of elements in the list */
  cast_void(g);  /* better to keep it if we need to print an object */
  while (o) {
    assert(!!isgray(o) ^ (getage(o) == G_TOUCHED2));
    assert(!testbit(o->marked, TESTBIT));
    if (keepinvariant(g))
      l_setbit(o->marked, TESTBIT);  /* mark that object is in a gray list */
    total++;
    switch (o->tt) {
      case VMK_VTABLE: o = gco2t(o)->gclist; break;
      case VMK_VLCL: o = gco2lcl(o)->gclist; break;
      case VMK_VCCL: o = gco2ccl(o)->gclist; break;
      case VMK_VTHREAD: o = gco2th(o)->gclist; break;
      case VMK_VPROTO: o = gco2p(o)->gclist; break;
      case VMK_VUSERDATA:
        assert(gco2u(o)->nuvalue > 0);
        o = gco2u(o)->gclist;
        break;
      default: assert(0);  /* other objects cannot be in a gray list */
    }
  }
  return total;
}


/*
** Check objects in gray lists.
*/
static l_mem checkgrays (global_State *g) {
  l_mem total = 0;  /* count number of elements in all lists */
  if (!keepinvariant(g)) return total;
  total += checkgraylist(g, g->gray);
  total += checkgraylist(g, g->grayagain);
  total += checkgraylist(g, g->weak);
  total += checkgraylist(g, g->allweak);
  total += checkgraylist(g, g->ephemeron);
  return total;
}


/*
** Check whether 'o' should be in a gray list. If so, increment
** 'count' and check its TESTBIT. (It must have been previously set by
** 'checkgraylist'.)
*/
static void incifingray (global_State *g, GCObject *o, l_mem *count) {
  if (!keepinvariant(g))
    return;  /* gray lists not being kept in these phases */
  if (o->tt == VMK_VUPVAL) {
    /* only open upvalues can be gray */
    assert(!isgray(o) || upisopen(gco2upv(o)));
    return;  /* upvalues are never in gray lists */
  }
  /* these are the ones that must be in gray lists */
  if (isgray(o) || getage(o) == G_TOUCHED2) {
    (*count)++;
    assert(testbit(o->marked, TESTBIT));
    resetbit(o->marked, TESTBIT);  /* prepare for next cycle */
  }
}


static l_mem checklist (global_State *g, int maybedead, int tof,
  GCObject *newl, GCObject *survival, GCObject *old, GCObject *reallyold) {
  GCObject *o;
  l_mem total = 0;  /* number of object that should be in  gray lists */
  for (o = newl; o != survival; o = o->next) {
    checkobject(g, o, maybedead, G_NEW);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = survival; o != old; o = o->next) {
    checkobject(g, o, 0, G_SURVIVAL);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = old; o != reallyold; o = o->next) {
    checkobject(g, o, 0, G_OLD1);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  for (o = reallyold; o != NULL; o = o->next) {
    checkobject(g, o, 0, G_OLD);
    incifingray(g, o, &total);
    assert(!tof == !tofinalize(o));
  }
  return total;
}


int vmk_checkmemory (vmk_State *L) {
  global_State *g = G(L);
  GCObject *o;
  int maybedead;
  l_mem totalin;  /* total of objects that are in gray lists */
  l_mem totalshould;  /* total of objects that should be in gray lists */
  if (keepinvariant(g)) {
    assert(!iswhite(g->mainthread));
    assert(!iswhite(gcvalue(&g->l_registry)));
  }
  assert(!isdead(g, gcvalue(&g->l_registry)));
  assert(g->sweepgc == NULL || issweepphase(g));
  totalin = checkgrays(g);

  /* check 'fixedgc' list */
  for (o = g->fixedgc; o != NULL; o = o->next) {
    assert(o->tt == VMK_VSHRSTR && isgray(o) && getage(o) == G_OLD);
  }

  /* check 'allgc' list */
  maybedead = (GCSatomic < g->gcstate && g->gcstate <= GCSswpallgc);
  totalshould = checklist(g, maybedead, 0, g->allgc,
                             g->survival, g->old1, g->reallyold);

  /* check 'finobj' list */
  totalshould += checklist(g, 0, 1, g->finobj,
                              g->finobjsur, g->finobjold1, g->finobjrold);

  /* check 'tobefnz' list */
  for (o = g->tobefnz; o != NULL; o = o->next) {
    checkobject(g, o, 0, G_NEW);
    incifingray(g, o, &totalshould);
    assert(tofinalize(o));
    assert(o->tt == VMK_VUSERDATA || o->tt == VMK_VTABLE);
  }
  if (keepinvariant(g))
    assert(totalin == totalshould);
  return 0;
}

/* }====================================================== */



/*
** {======================================================
** Disassembler
** =======================================================
*/


static char *buildop (Proto *p, int pc, char *buff) {
  char *obuff = buff;
  Instruction i = p->code[pc];
  OpCode o = GET_OPCODE(i);
  const char *name = opnames[o];
  int line = vmkG_getfuncline(p, pc);
  int lineinfo = (p->lineinfo != NULL) ? p->lineinfo[pc] : 0;
  if (lineinfo == ABSLINEINFO)
    buff += sprintf(buff, "(__");
  else
    buff += sprintf(buff, "(%2d", lineinfo);
  buff += sprintf(buff, " - %4d) %4d - ", line, pc);
  switch (getOpMode(o)) {
    case iABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              GETARG_A(i), GETARG_B(i), GETARG_C(i),
              GETARG_k(i) ? " (k)" : "");
      break;
    case ivABC:
      sprintf(buff, "%-12s%4d %4d %4d%s", name,
              GETARG_A(i), GETARG_vB(i), GETARG_vC(i),
              GETARG_k(i) ? " (k)" : "");
      break;
    case iABx:
      sprintf(buff, "%-12s%4d %4d", name, GETARG_A(i), GETARG_Bx(i));
      break;
    case iAsBx:
      sprintf(buff, "%-12s%4d %4d", name, GETARG_A(i), GETARG_sBx(i));
      break;
    case iAx:
      sprintf(buff, "%-12s%4d", name, GETARG_Ax(i));
      break;
    case isJ:
      sprintf(buff, "%-12s%4d", name, GETARG_sJ(i));
      break;
  }
  return obuff;
}


#if 0
void vmkI_printcode (Proto *pt, int size) {
  int pc;
  for (pc=0; pc<size; pc++) {
    char buff[100];
    printf("%s\n", buildop(pt, pc, buff));
  }
  printf("-------\n");
}


void vmkI_printinst (Proto *pt, int pc) {
  char buff[100];
  printf("%s\n", buildop(pt, pc, buff));
}
#endif


static int listcode (vmk_State *L) {
  int pc;
  Proto *p;
  vmkL_argcheck(L, vmk_isfunction(L, 1) && !vmk_iscfunction(L, 1),
                 1, "Vmk fn expected");
  p = getproto(obj_at(L, 1));
  vmk_newtable(L);
  setnameval(L, "maxstack", p->maxstacksize);
  setnameval(L, "numparams", p->numparams);
  for (pc=0; pc<p->sizecode; pc++) {
    char buff[100];
    vmk_pushinteger(L, pc+1);
    vmk_pushstring(L, buildop(p, pc, buff));
    vmk_settable(L, -3);
  }
  return 1;
}


static int printcode (vmk_State *L) {
  int pc;
  Proto *p;
  vmkL_argcheck(L, vmk_isfunction(L, 1) && !vmk_iscfunction(L, 1),
                 1, "Vmk fn expected");
  p = getproto(obj_at(L, 1));
  printf("maxstack: %d\n", p->maxstacksize);
  printf("numparams: %d\n", p->numparams);
  for (pc=0; pc<p->sizecode; pc++) {
    char buff[100];
    printf("%s\n", buildop(p, pc, buff));
  }
  return 0;
}


static int listk (vmk_State *L) {
  Proto *p;
  int i;
  vmkL_argcheck(L, vmk_isfunction(L, 1) && !vmk_iscfunction(L, 1),
                 1, "Vmk fn expected");
  p = getproto(obj_at(L, 1));
  vmk_createtable(L, p->sizek, 0);
  for (i=0; i<p->sizek; i++) {
    pushobject(L, p->k+i);
    vmk_rawseti(L, -2, i+1);
  }
  return 1;
}


static int listabslineinfo (vmk_State *L) {
  Proto *p;
  int i;
  vmkL_argcheck(L, vmk_isfunction(L, 1) && !vmk_iscfunction(L, 1),
                 1, "Vmk fn expected");
  p = getproto(obj_at(L, 1));
  vmkL_argcheck(L, p->abslineinfo != NULL, 1, "fn has no debug info");
  vmk_createtable(L, 2 * p->sizeabslineinfo, 0);
  for (i=0; i < p->sizeabslineinfo; i++) {
    vmk_pushinteger(L, p->abslineinfo[i].pc);
    vmk_rawseti(L, -2, 2 * i + 1);
    vmk_pushinteger(L, p->abslineinfo[i].line);
    vmk_rawseti(L, -2, 2 * i + 2);
  }
  return 1;
}


static int listlocals (vmk_State *L) {
  Proto *p;
  int pc = cast_int(vmkL_checkinteger(L, 2)) - 1;
  int i = 0;
  const char *name;
  vmkL_argcheck(L, vmk_isfunction(L, 1) && !vmk_iscfunction(L, 1),
                 1, "Vmk fn expected");
  p = getproto(obj_at(L, 1));
  while ((name = vmkF_getlocalname(p, ++i, pc)) != NULL)
    vmk_pushstring(L, name);
  return i-1;
}

/* }====================================================== */



void vmk_printstack (vmk_State *L) {
  int i;
  int n = vmk_gettop(L);
  printf("stack: >>\n");
  for (i = 1; i <= n; i++) {
    printf("%3d: ", i);
    vmk_printvalue(s2v(L->ci->func.p + i));
    printf("\n");
  }
  printf("<<\n");
}


static int get_limits (vmk_State *L) {
  vmk_createtable(L, 0, 5);
  setnameval(L, "IS32INT", VMKI_IS32INT);
  setnameval(L, "MAXARG_Ax", MAXARG_Ax);
  setnameval(L, "MAXARG_Bx", MAXARG_Bx);
  setnameval(L, "OFFSET_sBx", OFFSET_sBx);
  setnameval(L, "NUM_OPCODES", NUM_OPCODES);
  return 1;
}


static int mem_query (vmk_State *L) {
  if (vmk_isnone(L, 1)) {
    vmk_pushinteger(L, cast(vmk_Integer, l_memcontrol.total));
    vmk_pushinteger(L, cast(vmk_Integer, l_memcontrol.numblocks));
    vmk_pushinteger(L, cast(vmk_Integer, l_memcontrol.maxmem));
    return 3;
  }
  else if (vmk_isnumber(L, 1)) {
    unsigned long limit = cast(unsigned long, vmkL_checkinteger(L, 1));
    if (limit == 0) limit = ULONG_MAX;
    l_memcontrol.memlimit = limit;
    return 0;
  }
  else {
    const char *t = vmkL_checkstring(L, 1);
    int i;
    for (i = VMK_NUMTYPES - 1; i >= 0; i--) {
      if (strcmp(t, ttypename(i)) == 0) {
        vmk_pushinteger(L, cast(vmk_Integer, l_memcontrol.objcount[i]));
        return 1;
      }
    }
    return vmkL_error(L, "unknown type '%s'", t);
  }
}


static int alloc_count (vmk_State *L) {
  if (vmk_isnone(L, 1))
    l_memcontrol.countlimit = cast(unsigned long, ~0L);
  else
    l_memcontrol.countlimit = cast(unsigned long, vmkL_checkinteger(L, 1));
  return 0;
}


static int alloc_failnext (vmk_State *L) {
  UNUSED(L);
  l_memcontrol.failnext = 1;
  return 0;
}


static int settrick (vmk_State *L) {
  if (ttisnil(obj_at(L, 1)))
    l_Trick = NULL;
  else
    l_Trick = gcvalue(obj_at(L, 1));
  return 0;
}


static int gc_color (vmk_State *L) {
  TValue *o;
  vmkL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    vmk_pushstring(L, "no collectable");
  else {
    GCObject *obj = gcvalue(o);
    vmk_pushstring(L, isdead(G(L), obj) ? "dead" :
                      iswhite(obj) ? "white" :
                      isblack(obj) ? "black" : "gray");
  }
  return 1;
}


static int gc_age (vmk_State *L) {
  TValue *o;
  vmkL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    vmk_pushstring(L, "no collectable");
  else {
    static const char *gennames[] = {"new", "survival", "old0", "old1",
                                     "old", "touched1", "touched2"};
    GCObject *obj = gcvalue(o);
    vmk_pushstring(L, gennames[getage(obj)]);
  }
  return 1;
}


static int gc_printobj (vmk_State *L) {
  TValue *o;
  vmkL_checkany(L, 1);
  o = obj_at(L, 1);
  if (!iscollectable(o))
    printf("no collectable\n");
  else {
    GCObject *obj = gcvalue(o);
    printobj(G(L), obj);
    printf("\n");
  }
  return 0;
}


static const char *statenames[] = {
  "propagate", "enteratomic", "atomic", "sweepallgc", "sweepfinobj",
  "sweeptobefnz", "sweepend", "callfin", "pause", ""};

static int gc_state (vmk_State *L) {
  static const int states[] = {
    GCSpropagate, GCSenteratomic, GCSatomic, GCSswpallgc, GCSswpfinobj,
    GCSswptobefnz, GCSswpend, GCScallfin, GCSpause, -1};
  int option = states[vmkL_checkoption(L, 1, "", statenames)];
  global_State *g = G(L);
  if (option == -1) {
    vmk_pushstring(L, statenames[g->gcstate]);
    return 1;
  }
  else {
    if (g->gckind != KGC_INC)
      vmkL_error(L, "cannot change states in generational mode");
    vmk_lock(L);
    if (option < g->gcstate) {  /* must cross 'pause'? */
      vmkC_runtilstate(L, GCSpause, 1);  /* run until pause */
    }
    vmkC_runtilstate(L, option, 0);  /* do not skip propagation state */
    vmk_assert(g->gcstate == option);
    vmk_unlock(L);
    return 0;
  }
}


static int tracinggc = 0;
void vmki_tracegctest (vmk_State *L, int first) {
  if (!tracinggc) return;
  else {
    global_State *g = G(L);
    vmk_unlock(L);
    g->gcstp = GCSTPGC;
    vmk_checkstack(L, 10);
    vmk_getfield(L, VMK_REGISTRYINDEX, "tracegc");
    vmk_pushboolean(L, first);
    vmk_call(L, 1, 0);
    g->gcstp = 0;
    vmk_lock(L);
  }
}


static int tracegc (vmk_State *L) {
  if (vmk_isnil(L, 1))
    tracinggc = 0;
  else {
    tracinggc = 1;
    vmk_setfield(L, VMK_REGISTRYINDEX, "tracegc");
  }
  return 0;
}


static int hash_query (vmk_State *L) {
  if (vmk_isnone(L, 2)) {
    vmkL_argcheck(L, vmk_type(L, 1) == VMK_TSTRING, 1, "string expected");
    vmk_pushinteger(L, cast_int(tsvalue(obj_at(L, 1))->hash));
  }
  else {
    TValue *o = obj_at(L, 1);
    Table *t;
    vmkL_checktype(L, 2, VMK_TTABLE);
    t = hvalue(obj_at(L, 2));
    vmk_pushinteger(L, cast(vmk_Integer, vmkH_mainposition(t, o) - t->node));
  }
  return 1;
}


static int stacklevel (vmk_State *L) {
  int a = 0;
  vmk_pushinteger(L, cast(vmk_Integer, L->top.p - L->stack.p));
  vmk_pushinteger(L, stacksize(L));
  vmk_pushinteger(L, cast(vmk_Integer, L->nCcalls));
  vmk_pushinteger(L, L->nci);
  vmk_pushinteger(L, (vmk_Integer)(size_t)&a);
  return 5;
}


static int table_query (vmk_State *L) {
  const Table *t;
  int i = cast_int(vmkL_optinteger(L, 2, -1));
  unsigned int asize;
  vmkL_checktype(L, 1, VMK_TTABLE);
  t = hvalue(obj_at(L, 1));
  asize = t->asize;
  if (i == -1) {
    vmk_pushinteger(L, cast(vmk_Integer, asize));
    vmk_pushinteger(L, cast(vmk_Integer, allocsizenode(t)));
    vmk_pushinteger(L, cast(vmk_Integer, asize > 0 ? *lenhint(t) : 0));
    return 3;
  }
  else if (cast_uint(i) < asize) {
    vmk_pushinteger(L, i);
    if (!tagisempty(*getArrTag(t, i)))
      arr2obj(t, cast_uint(i), s2v(L->top.p));
    else
      setnilvalue(s2v(L->top.p));
    api_incr_top(L);
    vmk_pushnil(L);
  }
  else if (cast_uint(i -= cast_int(asize)) < sizenode(t)) {
    TValue k;
    getnodekey(L, &k, gnode(t, i));
    if (!isempty(gval(gnode(t, i))) ||
        ttisnil(&k) ||
        ttisnumber(&k)) {
      pushobject(L, &k);
    }
    else
      vmk_pushliteral(L, "<undef>");
    if (!isempty(gval(gnode(t, i))))
      pushobject(L, gval(gnode(t, i)));
    else
      vmk_pushnil(L);
    vmk_pushinteger(L, gnext(&t->node[i]));
  }
  return 3;
}


static int gc_query (vmk_State *L) {
  global_State *g = G(L);
  vmk_pushstring(L, g->gckind == KGC_INC ? "inc"
                  : g->gckind == KGC_GENMAJOR ? "genmajor"
                  : "genminor");
  vmk_pushstring(L, statenames[g->gcstate]);
  vmk_pushinteger(L, cast_st2S(gettotalbytes(g)));
  vmk_pushinteger(L, cast_st2S(g->GCdebt));
  vmk_pushinteger(L, cast_st2S(g->GCmarked));
  vmk_pushinteger(L, cast_st2S(g->GCmajorminor));
  return 6;
}


static int test_codeparam (vmk_State *L) {
  vmk_Integer p = vmkL_checkinteger(L, 1);
  vmk_pushinteger(L, vmkO_codeparam(cast_uint(p)));
  return 1;
}


static int test_applyparam (vmk_State *L) {
  vmk_Integer p = vmkL_checkinteger(L, 1);
  vmk_Integer x = vmkL_checkinteger(L, 2);
  vmk_pushinteger(L, cast(vmk_Integer, vmkO_applyparam(cast_byte(p), x)));
  return 1;
}


static int string_query (vmk_State *L) {
  stringtable *tb = &G(L)->strt;
  int s = cast_int(vmkL_optinteger(L, 1, 0)) - 1;
  if (s == -1) {
    vmk_pushinteger(L ,tb->size);
    vmk_pushinteger(L ,tb->nuse);
    return 2;
  }
  else if (s < tb->size) {
    TString *ts;
    int n = 0;
    for (ts = tb->hash[s]; ts != NULL; ts = ts->u.hnext) {
      setsvalue2s(L, L->top.p, ts);
      api_incr_top(L);
      n++;
    }
    return n;
  }
  else return 0;
}


static int getreftable (vmk_State *L) {
  if (vmk_istable(L, 2))  /* is there a table as second argument? */
    return 2;  /* use it as the table */
  else
    return VMK_REGISTRYINDEX;  /* default is to use the register */
}


static int tref (vmk_State *L) {
  int t = getreftable(L);
  int level = vmk_gettop(L);
  vmkL_checkany(L, 1);
  vmk_pushvalue(L, 1);
  vmk_pushinteger(L, vmkL_ref(L, t));
  cast_void(level);  /* to avoid warnings */
  vmk_assert(vmk_gettop(L) == level+1);  /* +1 for result */
  return 1;
}


static int getref (vmk_State *L) {
  int t = getreftable(L);
  int level = vmk_gettop(L);
  vmk_rawgeti(L, t, vmkL_checkinteger(L, 1));
  cast_void(level);  /* to avoid warnings */
  vmk_assert(vmk_gettop(L) == level+1);
  return 1;
}

static int unref (vmk_State *L) {
  int t = getreftable(L);
  int level = vmk_gettop(L);
  vmkL_unref(L, t, cast_int(vmkL_checkinteger(L, 1)));
  cast_void(level);  /* to avoid warnings */
  vmk_assert(vmk_gettop(L) == level);
  return 0;
}


static int upvalue (vmk_State *L) {
  int n = cast_int(vmkL_checkinteger(L, 2));
  vmkL_checktype(L, 1, VMK_TFUNCTION);
  if (vmk_isnone(L, 3)) {
    const char *name = vmk_getupvalue(L, 1, n);
    if (name == NULL) return 0;
    vmk_pushstring(L, name);
    return 2;
  }
  else {
    const char *name = vmk_setupvalue(L, 1, n);
    vmk_pushstring(L, name);
    return 1;
  }
}


static int newuserdata (vmk_State *L) {
  size_t size = cast_sizet(vmkL_optinteger(L, 1, 0));
  int nuv = cast_int(vmkL_optinteger(L, 2, 0));
  char *p = cast_charp(vmk_newuserdatauv(L, size, nuv));
  while (size--) *p++ = '\0';
  return 1;
}


static int pushuserdata (vmk_State *L) {
  vmk_Integer u = vmkL_checkinteger(L, 1);
  vmk_pushlightuserdata(L, cast_voidp(cast_sizet(u)));
  return 1;
}


static int udataval (vmk_State *L) {
  vmk_pushinteger(L, cast(vmk_Integer, cast(size_t, vmk_touserdata(L, 1))));
  return 1;
}


static int doonnewstack (vmk_State *L) {
  vmk_State *L1 = vmk_newthread(L);
  size_t l;
  const char *s = vmkL_checklstring(L, 1, &l);
  int status = vmkL_loadbuffer(L1, s, l, s);
  if (status == VMK_OK)
    status = vmk_pcall(L1, 0, 0, 0);
  vmk_pushinteger(L, status);
  return 1;
}


static int s2d (vmk_State *L) {
  vmk_pushnumber(L, cast_num(*cast(const double *, vmkL_checkstring(L, 1))));
  return 1;
}


static int d2s (vmk_State *L) {
  double d = cast(double, vmkL_checknumber(L, 1));
  vmk_pushlstring(L, cast_charp(&d), sizeof(d));
  return 1;
}


static int num2int (vmk_State *L) {
  vmk_pushinteger(L, vmk_tointeger(L, 1));
  return 1;
}


static int makeseed (vmk_State *L) {
  vmk_pushinteger(L, cast(vmk_Integer, vmkL_makeseed(L)));
  return 1;
}


static int newstate (vmk_State *L) {
  void *ud;
  vmk_Alloc f = vmk_getallocf(L, &ud);
  vmk_State *L1 = vmk_newstate(f, ud, 0);
  if (L1) {
    vmk_atpanic(L1, tpanic);
    vmk_pushlightuserdata(L, L1);
  }
  else
    vmk_pushnil(L);
  return 1;
}


static vmk_State *getstate (vmk_State *L) {
  vmk_State *L1 = cast(vmk_State *, vmk_touserdata(L, 1));
  vmkL_argcheck(L, L1 != NULL, 1, "state expected");
  return L1;
}


static int loadlib (vmk_State *L) {
  vmk_State *L1 = getstate(L);
  int load = cast_int(vmkL_checkinteger(L, 2));
  int preload = cast_int(vmkL_checkinteger(L, 3));
  vmkL_openselectedlibs(L1, load, preload);
  vmkL_requiref(L1, "T", vmkB_opentests, 0);
  vmk_assert(vmk_type(L1, -1) == VMK_TTABLE);
  /* 'requiref' should not reload module already loaded... */
  vmkL_requiref(L1, "T", NULL, 1);  /* seg. fault if it reloads */
  /* ...but should return the same module */
  vmk_assert(vmk_compare(L1, -1, -2, VMK_OPEQ));
  return 0;
}

static int closestate (vmk_State *L) {
  vmk_State *L1 = getstate(L);
  vmk_close(L1);
  return 0;
}

static int doremote (vmk_State *L) {
  vmk_State *L1 = getstate(L);
  size_t lcode;
  const char *code = vmkL_checklstring(L, 2, &lcode);
  int status;
  vmk_settop(L1, 0);
  status = vmkL_loadbuffer(L1, code, lcode, code);
  if (status == VMK_OK)
    status = vmk_pcall(L1, 0, VMK_MULTRET, 0);
  if (status != VMK_OK) {
    vmk_pushnil(L);
    vmk_pushstring(L, vmk_tostring(L1, -1));
    vmk_pushinteger(L, status);
    return 3;
  }
  else {
    int i = 0;
    while (!vmk_isnone(L1, ++i))
      vmk_pushstring(L, vmk_tostring(L1, i));
    vmk_pop(L1, i-1);
    return i-1;
  }
}


static int log2_aux (vmk_State *L) {
  unsigned int x = (unsigned int)vmkL_checkinteger(L, 1);
  vmk_pushinteger(L, vmkO_ceillog2(x));
  return 1;
}


struct Aux { jmp_buf jb; const char *paniccode; vmk_State *L; };

/*
** does a long-jump back to "main program".
*/
static int panicback (vmk_State *L) {
  struct Aux *b;
  vmk_checkstack(L, 1);  /* open space for 'Aux' struct */
  vmk_getfield(L, VMK_REGISTRYINDEX, "_jmpbuf");  /* get 'Aux' struct */
  b = (struct Aux *)vmk_touserdata(L, -1);
  vmk_pop(L, 1);  /* remove 'Aux' struct */
  runC(b->L, L, b->paniccode);  /* run optional panic code */
  longjmp(b->jb, 1);
  return 1;  /* to avoid warnings */
}

static int checkpanic (vmk_State *L) {
  struct Aux b;
  void *ud;
  vmk_State *L1;
  const char *code = vmkL_checkstring(L, 1);
  vmk_Alloc f = vmk_getallocf(L, &ud);
  b.paniccode = vmkL_optstring(L, 2, "");
  b.L = L;
  L1 = vmk_newstate(f, ud, 0);  /* create new state */
  if (L1 == NULL) {  /* error? */
    vmk_pushstring(L, MEMERRMSG);
    return 1;
  }
  vmk_atpanic(L1, panicback);  /* set its panic fn */
  vmk_pushlightuserdata(L1, &b);
  vmk_setfield(L1, VMK_REGISTRYINDEX, "_jmpbuf");  /* store 'Aux' struct */
  if (setjmp(b.jb) == 0) {  /* set jump buffer */
    runC(L, L1, code);  /* run code unprotected */
    vmk_pushliteral(L, "no errors");
  }
  else {  /* error handling */
    /* move error message to original state */
    vmk_pushstring(L, vmk_tostring(L1, -1));
  }
  vmk_close(L1);
  return 1;
}


static int externKstr (vmk_State *L) {
  size_t len;
  const char *s = vmkL_checklstring(L, 1, &len);
  vmk_pushexternalstring(L, s, len, NULL, NULL);
  return 1;
}


/*
** Create a buffer with the content of a given string and then
** create an external string using that buffer. Use the allocation
** fn from Vmk to create and free the buffer.
*/
static int externstr (vmk_State *L) {
  size_t len;
  const char *s = vmkL_checklstring(L, 1, &len);
  void *ud;
  vmk_Alloc allocf = vmk_getallocf(L, &ud);  /* get allocation fn */
  /* create the buffer */
  char *buff = cast_charp((*allocf)(ud, NULL, 0, len + 1));
  if (buff == NULL) {  /* memory error? */
    vmk_pushliteral(L, "not enough memory");
    vmk_error(L);  /* raise a memory error */
  }
  /* copy string content to buffer, including ending 0 */
  memcpy(buff, s, (len + 1) * sizeof(char));
  /* create external string */
  vmk_pushexternalstring(L, buff, len, allocf, ud);
  return 1;
}


/*
** {====================================================================
** fn to test the API with C. It interprets a kind of assembler
** language with calls to the API, so the test can be driven by Vmk code
** =====================================================================
*/


static void sethookaux (vmk_State *L, int mask, int count, const char *code);

static const char *const delimits = " \t\n,;";

static void skip (const char **pc) {
  for (;;) {
    if (**pc != '\0' && strchr(delimits, **pc)) (*pc)++;
    else if (**pc == '#') {  /* comment? */
      while (**pc != '\n' && **pc != '\0') (*pc)++;  /* until end-of-line */
    }
    else break;
  }
}

static int getnum_aux (vmk_State *L, vmk_State *L1, const char **pc) {
  int res = 0;
  int sig = 1;
  skip(pc);
  if (**pc == '.') {
    res = cast_int(vmk_tointeger(L1, -1));
    vmk_pop(L1, 1);
    (*pc)++;
    return res;
  }
  else if (**pc == '*') {
    res = vmk_gettop(L1);
    (*pc)++;
    return res;
  }
  else if (**pc == '!') {
    (*pc)++;
    if (**pc == 'G')
      res = VMK_RIDX_GLOBALS;
    else if (**pc == 'M')
      res = VMK_RIDX_MAINTHREAD;
    else vmk_assert(0);
    (*pc)++;
    return res;
  }
  else if (**pc == '-') {
    sig = -1;
    (*pc)++;
  }
  if (!lisdigit(cast_uchar(**pc)))
    vmkL_error(L, "number expected (%s)", *pc);
  while (lisdigit(cast_uchar(**pc))) res = res*10 + (*(*pc)++) - '0';
  return sig*res;
}

static const char *getstring_aux (vmk_State *L, char *buff, const char **pc) {
  int i = 0;
  skip(pc);
  if (**pc == '"' || **pc == '\'') {  /* quoted string? */
    int quote = *(*pc)++;
    while (**pc != quote) {
      if (**pc == '\0') vmkL_error(L, "unfinished string in C script");
      buff[i++] = *(*pc)++;
    }
    (*pc)++;
  }
  else {
    while (**pc != '\0' && !strchr(delimits, **pc))
      buff[i++] = *(*pc)++;
  }
  buff[i] = '\0';
  return buff;
}


static int getindex_aux (vmk_State *L, vmk_State *L1, const char **pc) {
  skip(pc);
  switch (*(*pc)++) {
    case 'R': return VMK_REGISTRYINDEX;
    case 'U': return vmk_upvalueindex(getnum_aux(L, L1, pc));
    default: {
      int n;
      (*pc)--;  /* to read again */
      n = getnum_aux(L, L1, pc);
      if (n == 0) return 0;
      else return vmk_absindex(L1, n);
    }
  }
}


static const char *const statcodes[] = {"OK", "YIELD", "ERRRUN",
    "ERRSYNTAX", MEMERRMSG, "ERRERR"};

/*
** Avoid these stat codes from being collected, to avoid possible
** memory error when pushing them.
*/
static void regcodes (vmk_State *L) {
  unsigned int i;
  for (i = 0; i < sizeof(statcodes) / sizeof(statcodes[0]); i++) {
    vmk_pushboolean(L, 1);
    vmk_setfield(L, VMK_REGISTRYINDEX, statcodes[i]);
  }
}


#define EQ(s1)	(strcmp(s1, inst) == 0)

#define getnum		(getnum_aux(L, L1, &pc))
#define getstring	(getstring_aux(L, buff, &pc))
#define getindex	(getindex_aux(L, L1, &pc))


static int testC (vmk_State *L);
static int Cfunck (vmk_State *L, int status, vmk_KContext ctx);

/*
** arithmetic operation encoding for 'arith' instruction
** VMK_OPIDIV  -> \
** VMK_OPSHL   -> <
** VMK_OPSHR   -> >
** VMK_OPUNM   -> _
** VMK_OPBNOT  -> !
*/
static const char ops[] = "+-*%^/\\&|~<>_!";

static int runC (vmk_State *L, vmk_State *L1, const char *pc) {
  char buff[300];
  int status = 0;
  if (pc == NULL) return vmkL_error(L, "attempt to runC null script");
  for (;;) {
    const char *inst = getstring;
    if EQ("") return 0;
    else if EQ("absindex") {
      vmk_pushinteger(L1, getindex);
    }
    else if EQ("append") {
      int t = getindex;
      int i = cast_int(vmk_rawlen(L1, t));
      vmk_rawseti(L1, t, i + 1);
    }
    else if EQ("arith") {
      int op;
      skip(&pc);
      op = cast_int(strchr(ops, *pc++) - ops);
      vmk_arith(L1, op);
    }
    else if EQ("call") {
      int narg = getnum;
      int nres = getnum;
      vmk_call(L1, narg, nres);
    }
    else if EQ("callk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      vmk_callk(L1, narg, nres, i, Cfunck);
    }
    else if EQ("checkstack") {
      int sz = getnum;
      const char *msg = getstring;
      if (*msg == '\0')
        msg = NULL;  /* to test 'vmkL_checkstack' with no message */
      vmkL_checkstack(L1, sz, msg);
    }
    else if EQ("rawcheckstack") {
      int sz = getnum;
      vmk_pushboolean(L1, vmk_checkstack(L1, sz));
    }
    else if EQ("compare") {
      const char *opt = getstring;  /* EQ, LT, or LE */
      int op = (opt[0] == 'E') ? VMK_OPEQ
                               : (opt[1] == 'T') ? VMK_OPLT : VMK_OPLE;
      int a = getindex;
      int b = getindex;
      vmk_pushboolean(L1, vmk_compare(L1, a, b, op));
    }
    else if EQ("concat") {
      vmk_concat(L1, getnum);
    }
    else if EQ("copy") {
      int f = getindex;
      vmk_copy(L1, f, getindex);
    }
    else if EQ("func2num") {
      vmk_CFunction func = vmk_tocfunction(L1, getindex);
      vmk_pushinteger(L1, cast(vmk_Integer, cast(size_t, func)));
    }
    else if EQ("getfield") {
      int t = getindex;
      int tp = vmk_getfield(L1, t, getstring);
      vmk_assert(tp == vmk_type(L1, -1));
    }
    else if EQ("getglobal") {
      vmk_getglobal(L1, getstring);
    }
    else if EQ("getmetatable") {
      if (vmk_getmetatable(L1, getindex) == 0)
        vmk_pushnil(L1);
    }
    else if EQ("gettable") {
      int tp = vmk_gettable(L1, getindex);
      vmk_assert(tp == vmk_type(L1, -1));
    }
    else if EQ("gettop") {
      vmk_pushinteger(L1, vmk_gettop(L1));
    }
    else if EQ("gsub") {
      int a = getnum; int b = getnum; int c = getnum;
      vmkL_gsub(L1, vmk_tostring(L1, a),
                    vmk_tostring(L1, b),
                    vmk_tostring(L1, c));
    }
    else if EQ("insert") {
      vmk_insert(L1, getnum);
    }
    else if EQ("iscfunction") {
      vmk_pushboolean(L1, vmk_iscfunction(L1, getindex));
    }
    else if EQ("isfunction") {
      vmk_pushboolean(L1, vmk_isfunction(L1, getindex));
    }
    else if EQ("isnil") {
      vmk_pushboolean(L1, vmk_isnil(L1, getindex));
    }
    else if EQ("isnull") {
      vmk_pushboolean(L1, vmk_isnone(L1, getindex));
    }
    else if EQ("isnumber") {
      vmk_pushboolean(L1, vmk_isnumber(L1, getindex));
    }
    else if EQ("isstring") {
      vmk_pushboolean(L1, vmk_isstring(L1, getindex));
    }
    else if EQ("istable") {
      vmk_pushboolean(L1, vmk_istable(L1, getindex));
    }
    else if EQ("isudataval") {
      vmk_pushboolean(L1, vmk_islightuserdata(L1, getindex));
    }
    else if EQ("isuserdata") {
      vmk_pushboolean(L1, vmk_isuserdata(L1, getindex));
    }
    else if EQ("len") {
      vmk_len(L1, getindex);
    }
    else if EQ("Llen") {
      vmk_pushinteger(L1, vmkL_len(L1, getindex));
    }
    else if EQ("loadfile") {
      vmkL_loadfile(L1, vmkL_checkstring(L1, getnum));
    }
    else if EQ("loadstring") {
      size_t slen;
      const char *s = vmkL_checklstring(L1, getnum, &slen);
      const char *name = getstring;
      const char *mode = getstring;
      vmkL_loadbufferx(L1, s, slen, name, mode);
    }
    else if EQ("newmetatable") {
      vmk_pushboolean(L1, vmkL_newmetatable(L1, getstring));
    }
    else if EQ("newtable") {
      vmk_newtable(L1);
    }
    else if EQ("newthread") {
      vmk_newthread(L1);
    }
    else if EQ("resetthread") {
      vmk_pushinteger(L1, vmk_resetthread(L1));  /* deprecated */
    }
    else if EQ("newuserdata") {
      vmk_newuserdata(L1, cast_sizet(getnum));
    }
    else if EQ("next") {
      vmk_next(L1, -2);
    }
    else if EQ("objsize") {
      vmk_pushinteger(L1, l_castU2S(vmk_rawlen(L1, getindex)));
    }
    else if EQ("pcall") {
      int narg = getnum;
      int nres = getnum;
      status = vmk_pcall(L1, narg, nres, getnum);
    }
    else if EQ("pcallk") {
      int narg = getnum;
      int nres = getnum;
      int i = getindex;
      status = vmk_pcallk(L1, narg, nres, 0, i, Cfunck);
    }
    else if EQ("pop") {
      vmk_pop(L1, getnum);
    }
    else if EQ("printstack") {
      int n = getnum;
      if (n != 0) {
        vmk_printvalue(s2v(L->ci->func.p + n));
        printf("\n");
      }
      else vmk_printstack(L1);
    }
    else if EQ("print") {
      const char *msg = getstring;
      printf("%s\n", msg);
    }
    else if EQ("warningC") {
      const char *msg = getstring;
      vmk_warning(L1, msg, 1);
    }
    else if EQ("warning") {
      const char *msg = getstring;
      vmk_warning(L1, msg, 0);
    }
    else if EQ("pushbool") {
      vmk_pushboolean(L1, getnum);
    }
    else if EQ("pushcclosure") {
      vmk_pushcclosure(L1, testC, getnum);
    }
    else if EQ("pushint") {
      vmk_pushinteger(L1, getnum);
    }
    else if EQ("pushnil") {
      vmk_pushnil(L1);
    }
    else if EQ("pushnum") {
      vmk_pushnumber(L1, (vmk_Number)getnum);
    }
    else if EQ("pushstatus") {
      vmk_pushstring(L1, statcodes[status]);
    }
    else if EQ("pushstring") {
      vmk_pushstring(L1, getstring);
    }
    else if EQ("pushupvalueindex") {
      vmk_pushinteger(L1, vmk_upvalueindex(getnum));
    }
    else if EQ("pushvalue") {
      vmk_pushvalue(L1, getindex);
    }
    else if EQ("pushfstringI") {
      vmk_pushfstring(L1, vmk_tostring(L, -2), (int)vmk_tointeger(L, -1));
    }
    else if EQ("pushfstringS") {
      vmk_pushfstring(L1, vmk_tostring(L, -2), vmk_tostring(L, -1));
    }
    else if EQ("pushfstringP") {
      vmk_pushfstring(L1, vmk_tostring(L, -2), vmk_topointer(L, -1));
    }
    else if EQ("rawget") {
      int t = getindex;
      vmk_rawget(L1, t);
    }
    else if EQ("rawgeti") {
      int t = getindex;
      vmk_rawgeti(L1, t, getnum);
    }
    else if EQ("rawgetp") {
      int t = getindex;
      vmk_rawgetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("rawset") {
      int t = getindex;
      vmk_rawset(L1, t);
    }
    else if EQ("rawseti") {
      int t = getindex;
      vmk_rawseti(L1, t, getnum);
    }
    else if EQ("rawsetp") {
      int t = getindex;
      vmk_rawsetp(L1, t, cast_voidp(cast_sizet(getnum)));
    }
    else if EQ("remove") {
      vmk_remove(L1, getnum);
    }
    else if EQ("replace") {
      vmk_replace(L1, getindex);
    }
    else if EQ("resume") {
      int i = getindex;
      int nres;
      status = vmk_resume(vmk_tothread(L1, i), L, getnum, &nres);
    }
    else if EQ("traceback") {
      const char *msg = getstring;
      int level = getnum;
      vmkL_traceback(L1, L1, msg, level);
    }
    else if EQ("threadstatus") {
      vmk_pushstring(L1, statcodes[vmk_status(L1)]);
    }
    else if EQ("alloccount") {
      l_memcontrol.countlimit = cast_uint(getnum);
    }
    else if EQ("return") {
      int n = getnum;
      if (L1 != L) {
        int i;
        for (i = 0; i < n; i++) {
          int idx = -(n - i);
          switch (vmk_type(L1, idx)) {
            case VMK_TBOOLEAN:
              vmk_pushboolean(L, vmk_toboolean(L1, idx));
              break;
            default:
              vmk_pushstring(L, vmk_tostring(L1, idx));
              break;
          }
        }
      }
      return n;
    }
    else if EQ("rotate") {
      int i = getindex;
      vmk_rotate(L1, i, getnum);
    }
    else if EQ("setfield") {
      int t = getindex;
      const char *s = getstring;
      vmk_setfield(L1, t, s);
    }
    else if EQ("seti") {
      int t = getindex;
      vmk_seti(L1, t, getnum);
    }
    else if EQ("setglobal") {
      const char *s = getstring;
      vmk_setglobal(L1, s);
    }
    else if EQ("sethook") {
      int mask = getnum;
      int count = getnum;
      const char *s = getstring;
      sethookaux(L1, mask, count, s);
    }
    else if EQ("setmetatable") {
      int idx = getindex;
      vmk_setmetatable(L1, idx);
    }
    else if EQ("settable") {
      vmk_settable(L1, getindex);
    }
    else if EQ("settop") {
      vmk_settop(L1, getnum);
    }
    else if EQ("testudata") {
      int i = getindex;
      vmk_pushboolean(L1, vmkL_testudata(L1, i, getstring) != NULL);
    }
    else if EQ("error") {
      vmk_error(L1);
    }
    else if EQ("abort") {
      abort();
    }
    else if EQ("throw") {
#if defined(__cplusplus)
static struct X { int x; } x;
      throw x;
#else
      vmkL_error(L1, "C++");
#endif
      break;
    }
    else if EQ("tobool") {
      vmk_pushboolean(L1, vmk_toboolean(L1, getindex));
    }
    else if EQ("tocfunction") {
      vmk_pushcfunction(L1, vmk_tocfunction(L1, getindex));
    }
    else if EQ("tointeger") {
      vmk_pushinteger(L1, vmk_tointeger(L1, getindex));
    }
    else if EQ("tonumber") {
      vmk_pushnumber(L1, vmk_tonumber(L1, getindex));
    }
    else if EQ("topointer") {
      vmk_pushlightuserdata(L1, cast_voidp(vmk_topointer(L1, getindex)));
    }
    else if EQ("touserdata") {
      vmk_pushlightuserdata(L1, vmk_touserdata(L1, getindex));
    }
    else if EQ("tostring") {
      const char *s = vmk_tostring(L1, getindex);
      const char *s1 = vmk_pushstring(L1, s);
      cast_void(s1);  /* to avoid warnings */
      vmk_longassert((s == NULL && s1 == NULL) || strcmp(s, s1) == 0);
    }
    else if EQ("Ltolstring") {
      vmkL_tolstring(L1, getindex, NULL);
    }
    else if EQ("type") {
      vmk_pushstring(L1, vmkL_typename(L1, getnum));
    }
    else if EQ("xmove") {
      int f = getindex;
      int t = getindex;
      vmk_State *fs = (f == 0) ? L1 : vmk_tothread(L1, f);
      vmk_State *ts = (t == 0) ? L1 : vmk_tothread(L1, t);
      int n = getnum;
      if (n == 0) n = vmk_gettop(fs);
      vmk_xmove(fs, ts, n);
    }
    else if EQ("isyieldable") {
      vmk_pushboolean(L1, vmk_isyieldable(vmk_tothread(L1, getindex)));
    }
    else if EQ("yield") {
      return vmk_yield(L1, getnum);
    }
    else if EQ("yieldk") {
      int nres = getnum;
      int i = getindex;
      return vmk_yieldk(L1, nres, i, Cfunck);
    }
    else if EQ("toclose") {
      vmk_toclose(L1, getnum);
    }
    else if EQ("closeslot") {
      vmk_closeslot(L1, getnum);
    }
    else if EQ("argerror") {
      int arg = getnum;
      vmkL_argerror(L1, arg, getstring);
    }
    else vmkL_error(L, "unknown instruction %s", buff);
  }
  return 0;
}


static int testC (vmk_State *L) {
  vmk_State *L1;
  const char *pc;
  if (vmk_isuserdata(L, 1)) {
    L1 = getstate(L);
    pc = vmkL_checkstring(L, 2);
  }
  else if (vmk_isthread(L, 1)) {
    L1 = vmk_tothread(L, 1);
    pc = vmkL_checkstring(L, 2);
  }
  else {
    L1 = L;
    pc = vmkL_checkstring(L, 1);
  }
  return runC(L, L1, pc);
}


static int Cfunc (vmk_State *L) {
  return runC(L, L, vmk_tostring(L, vmk_upvalueindex(1)));
}


static int Cfunck (vmk_State *L, int status, vmk_KContext ctx) {
  vmk_pushstring(L, statcodes[status]);
  vmk_setglobal(L, "status");
  vmk_pushinteger(L, cast(vmk_Integer, ctx));
  vmk_setglobal(L, "ctx");
  return runC(L, L, vmk_tostring(L, cast_int(ctx)));
}


static int makeCfunc (vmk_State *L) {
  vmkL_checkstring(L, 1);
  vmk_pushcclosure(L, Cfunc, vmk_gettop(L));
  return 1;
}


/* }====================================================== */


/*
** {======================================================
** tests for C hooks
** =======================================================
*/

/*
** C hook that runs the C script stored in registry.C_HOOK[L]
*/
static void Chook (vmk_State *L, vmk_Debug *ar) {
  const char *scpt;
  const char *const events [] = {"call", "ret", "line", "count", "tailcall"};
  vmk_getfield(L, VMK_REGISTRYINDEX, "C_HOOK");
  vmk_pushlightuserdata(L, L);
  vmk_gettable(L, -2);  /* get C_HOOK[L] (script saved by sethookaux) */
  scpt = vmk_tostring(L, -1);  /* not very religious (string will be popped) */
  vmk_pop(L, 2);  /* remove C_HOOK and script */
  vmk_pushstring(L, events[ar->event]);  /* may be used by script */
  vmk_pushinteger(L, ar->currentline);  /* may be used by script */
  runC(L, L, scpt);  /* run script from C_HOOK[L] */
}


/*
** sets 'registry.C_HOOK[L] = scpt' and sets 'Chook' as a hook
*/
static void sethookaux (vmk_State *L, int mask, int count, const char *scpt) {
  if (*scpt == '\0') {  /* no script? */
    vmk_sethook(L, NULL, 0, 0);  /* turn off hooks */
    return;
  }
  vmk_getfield(L, VMK_REGISTRYINDEX, "C_HOOK");  /* get C_HOOK table */
  if (!vmk_istable(L, -1)) {  /* no hook table? */
    vmk_pop(L, 1);  /* remove previous value */
    vmk_newtable(L);  /* create new C_HOOK table */
    vmk_pushvalue(L, -1);
    vmk_setfield(L, VMK_REGISTRYINDEX, "C_HOOK");  /* register it */
  }
  vmk_pushlightuserdata(L, L);
  vmk_pushstring(L, scpt);
  vmk_settable(L, -3);  /* C_HOOK[L] = script */
  vmk_sethook(L, Chook, mask, count);
}


static int sethook (vmk_State *L) {
  if (vmk_isnoneornil(L, 1))
    vmk_sethook(L, NULL, 0, 0);  /* turn off hooks */
  else {
    const char *scpt = vmkL_checkstring(L, 1);
    const char *smask = vmkL_checkstring(L, 2);
    int count = cast_int(vmkL_optinteger(L, 3, 0));
    int mask = 0;
    if (strchr(smask, 'c')) mask |= VMK_MASKCALL;
    if (strchr(smask, 'r')) mask |= VMK_MASKRET;
    if (strchr(smask, 'l')) mask |= VMK_MASKLINE;
    if (count > 0) mask |= VMK_MASKCOUNT;
    sethookaux(L, mask, count, scpt);
  }
  return 0;
}


static int coresume (vmk_State *L) {
  int status, nres;
  vmk_State *co = vmk_tothread(L, 1);
  vmkL_argcheck(L, co, 1, "coroutine expected");
  status = vmk_resume(co, L, 0, &nres);
  if (status != VMK_OK && status != VMK_YIELD) {
    vmk_pushboolean(L, 0);
    vmk_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    vmk_pushboolean(L, 1);
    return 1;
  }
}

/* }====================================================== */



static const struct vmkL_Reg tests_funcs[] = {
  {"checkmemory", vmk_checkmemory},
  {"closestate", closestate},
  {"d2s", d2s},
  {"doonnewstack", doonnewstack},
  {"doremote", doremote},
  {"gccolor", gc_color},
  {"gcage", gc_age},
  {"gcstate", gc_state},
  {"tracegc", tracegc},
  {"pobj", gc_printobj},
  {"getref", getref},
  {"hash", hash_query},
  {"log2", log2_aux},
  {"limits", get_limits},
  {"listcode", listcode},
  {"printcode", printcode},
  {"listk", listk},
  {"listabslineinfo", listabslineinfo},
  {"listlocals", listlocals},
  {"loadlib", loadlib},
  {"checkpanic", checkpanic},
  {"newstate", newstate},
  {"newuserdata", newuserdata},
  {"num2int", num2int},
  {"makeseed", makeseed},
  {"pushuserdata", pushuserdata},
  {"gcquery", gc_query},
  {"querystr", string_query},
  {"querytab", table_query},
  {"codeparam", test_codeparam},
  {"applyparam", test_applyparam},
  {"ref", tref},
  {"resume", coresume},
  {"s2d", s2d},
  {"sethook", sethook},
  {"stacklevel", stacklevel},
  {"testC", testC},
  {"makeCfunc", makeCfunc},
  {"totalmem", mem_query},
  {"alloccount", alloc_count},
  {"allocfailnext", alloc_failnext},
  {"trick", settrick},
  {"udataval", udataval},
  {"unref", unref},
  {"upvalue", upvalue},
  {"externKstr", externKstr},
  {"externstr", externstr},
  {NULL, NULL}
};


static void checkfinalmem (void) {
  vmk_assert(l_memcontrol.numblocks == 0);
  vmk_assert(l_memcontrol.total == 0);
}


int vmkB_opentests (vmk_State *L) {
  void *ud;
  vmk_Alloc f = vmk_getallocf(L, &ud);
  vmk_atpanic(L, &tpanic);
  vmk_setwarnf(L, &warnf, L);
  vmk_pushboolean(L, 0);
  vmk_setglobal(L, "_WARN");  /* _WARN = false */
  regcodes(L);
  atexit(checkfinalmem);
  vmk_assert(f == debug_realloc && ud == cast_voidp(&l_memcontrol));
  vmk_setallocf(L, f, ud);  /* exercise this fn */
  vmkL_newlib(L, tests_funcs);
  return 1;
}

#endif

