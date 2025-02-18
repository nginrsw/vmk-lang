/*
** $Id: lzio.h $
** Buffered streams
** See Copyright Notice in vmk.h
*/


#ifndef lzio_h
#define lzio_h

#include "vmk.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : vmkZ_fill(z))


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define vmkZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define vmkZ_buffer(buff)	((buff)->buffer)
#define vmkZ_sizebuffer(buff)	((buff)->buffsize)
#define vmkZ_bufflen(buff)	((buff)->n)

#define vmkZ_buffremove(buff,i)	((buff)->n -= cast_sizet(i))
#define vmkZ_resetbuffer(buff) ((buff)->n = 0)


#define vmkZ_resizebuffer(L, buff, size) \
	((buff)->buffer = vmkM_reallocvchar(L, (buff)->buffer, \
				(buff)->buffsize, size), \
	(buff)->buffsize = size)

#define vmkZ_freebuffer(L, buff)	vmkZ_resizebuffer(L, buff, 0)


VMKI_FUNC void vmkZ_init (vmk_State *L, ZIO *z, vmk_Reader reader,
                                        void *data);
VMKI_FUNC size_t vmkZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */

VMKI_FUNC const void *vmkZ_getaddr (ZIO* z, size_t n);


/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  vmk_Reader reader;		/* reader fn */
  void *data;			/* additional data */
  vmk_State *L;			/* Vmk state (for reader) */
};


VMKI_FUNC int vmkZ_fill (ZIO *z);

#endif
