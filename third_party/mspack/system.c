/* This file is part of libmspack.
 * (C) 2003-2004 Stuart Caie.
 *
 * libmspack is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (LGPL) version 2.1
 *
 * For further details, see the file COPYING.LIB distributed with libmspack
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "system.h"

#if !LARGEFILE_SUPPORT
const char *largefile_msg = "library not compiled to support large files.";
#endif


int mspack_version(int entity) {
  switch (entity) {
   /* CHM decoder version 1 -> 2 changes:
    * - added mschmd_sec_mscompressed::spaninfo
    * - added mschmd_header::first_pmgl
    * - added mschmd_header::last_pmgl
    * - added mschmd_header::chunk_cache;
    */
  case MSPACK_VER_MSCHMD:
  /* CAB decoder version 1 -> 2 changes:
   * - added MSCABD_PARAM_SALVAGE
   */
  case MSPACK_VER_MSCABD:
    return 2;
  case MSPACK_VER_LIBRARY:
  case MSPACK_VER_SYSTEM:
  case MSPACK_VER_MSSZDDD:
  case MSPACK_VER_MSKWAJD:
  case MSPACK_VER_MSOABD:
    return 1;
  case MSPACK_VER_MSCABC:
  case MSPACK_VER_MSCHMC:
  case MSPACK_VER_MSLITD:
  case MSPACK_VER_MSLITC:
  case MSPACK_VER_MSHLPD:
  case MSPACK_VER_MSHLPC:
  case MSPACK_VER_MSSZDDC:
  case MSPACK_VER_MSKWAJC:
  case MSPACK_VER_MSOABC:
    return 0;
  }
  return -1;
}

int mspack_sys_selftest_internal(int offt_size) {
  return (sizeof(off_t) == offt_size) ? MSPACK_ERR_OK : MSPACK_ERR_SEEK;
}

/* validates a system structure */
int mspack_valid_system(struct mspack_system *sys) {
  return (sys != NULL) && (sys->open != NULL) && (sys->close != NULL) &&
    (sys->read != NULL) && (sys->write != NULL) && (sys->seek != NULL) &&
    (sys->tell != NULL) && (sys->message != NULL) && (sys->alloc != NULL) &&
    (sys->free != NULL) && (sys->copy != NULL) && (sys->null_ptr == NULL);
}

/* returns the length of a file opened for reading */
int mspack_sys_filelen(struct mspack_system *system,
                       struct mspack_file *file, off_t *length)
{
  off_t current;

  if (!system || !file || !length) return MSPACK_ERR_OPEN;

  /* get current offset */
  current = system->tell(file);

  /* seek to end of file */
  if (system->seek(file, (off_t) 0, MSPACK_SYS_SEEK_END)) {
    return MSPACK_ERR_SEEK;
  }

  /* get offset of end of file */
  *length = system->tell(file);

  /* seek back to original offset */
  if (system->seek(file, current, MSPACK_SYS_SEEK_START)) {
    return MSPACK_ERR_SEEK;
  }

  return MSPACK_ERR_OK;
}



/* definition of mspack_default_system -- if the library is compiled with
 * MSPACK_NO_DEFAULT_SYSTEM, no default system will be provided. Otherwise,
 * an appropriate default system (e.g. the standard C library, or some native
 * API calls)
 */

#ifdef MSPACK_NO_DEFAULT_SYSTEM
struct mspack_system *mspack_default_system = NULL;
#else

/* implementation of mspack_default_system for standard C library */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

struct mspack_file_p {
  FILE *fh;
  const char *name;
};

static struct mspack_file *msp_open(struct mspack_system *self,
                                    const char *filename, int mode)
{
  struct mspack_file_p *fh;
  const char *fmode;

  switch (mode) {
  case MSPACK_SYS_OPEN_READ:   fmode = "rb";  break;
  case MSPACK_SYS_OPEN_WRITE:  fmode = "wb";  break;
  case MSPACK_SYS_OPEN_UPDATE: fmode = "r+b"; break;
  case MSPACK_SYS_OPEN_APPEND: fmode = "ab";  break;
  default: return NULL;
  }

  if ((fh = (struct mspack_file_p *) malloc(sizeof(struct mspack_file_p)))) {
    fh->name = filename;
    if ((fh->fh = fopen(filename, fmode))) return (struct mspack_file *) fh;
    free(fh);
  }
  return NULL;
}

static void msp_close(struct mspack_file *file) {
  struct mspack_file_p *self = (struct mspack_file_p *) file;
  if (self) {
    fclose(self->fh);
    free(self);
  }
}

static int msp_read(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *self = (struct mspack_file_p *) file;
  if (self && buffer && bytes >= 0) {
    size_t count = fread(buffer, 1, (size_t) bytes, self->fh);
    if (!ferror(self->fh)) return (int) count;
  }
  return -1;
}

static int msp_write(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *self = (struct mspack_file_p *) file;
  if (self && buffer && bytes >= 0) {
    size_t count = fwrite(buffer, 1, (size_t) bytes, self->fh);
    if (!ferror(self->fh)) return (int) count;
  }
  return -1;
}

static int msp_seek(struct mspack_file *file, off_t offset, int mode) {
  struct mspack_file_p *self = (struct mspack_file_p *) file;
  if (self) {
    switch (mode) {
    case MSPACK_SYS_SEEK_START: mode = SEEK_SET; break;
    case MSPACK_SYS_SEEK_CUR:   mode = SEEK_CUR; break;
    case MSPACK_SYS_SEEK_END:   mode = SEEK_END; break;
    default: return -1;
    }
#if HAVE_FSEEKO
    return fseeko(self->fh, offset, mode);
#else
    return fseek(self->fh, offset, mode);
#endif
  }
  return -1;
}

static off_t msp_tell(struct mspack_file *file) {
  struct mspack_file_p *self = (struct mspack_file_p *) file;
#if HAVE_FSEEKO
  return (self) ? (off_t) ftello(self->fh) : 0;
#else
  return (self) ? (off_t) ftell(self->fh) : 0;
#endif
}

static void msp_msg(struct mspack_file *file, const char *format, ...) {
  va_list ap;
  if (file) fprintf(stderr, "%s: ", ((struct mspack_file_p *) file)->name);
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc((int) '\n', stderr);
  fflush(stderr);
}

static void *msp_alloc(struct mspack_system *self, size_t bytes) {
#if DEBUG
  /* make uninitialised data obvious */
  char *buf = malloc(bytes + 8);
  if (buf) memset(buf, 0xDC, bytes);
  *((size_t *)buf) = bytes;
  return &buf[8];
#else
  return malloc(bytes);
#endif
}

static void msp_free(void *buffer) {
#if DEBUG
  char *buf = buffer;
  size_t bytes;
  if (buf) {
    buf -= 8;
    bytes = *((size_t *)buf);
    /* make freed data obvious */
    memset(buf, 0xED, bytes);
    free(buf);
  }
#else
  free(buffer);
#endif
}

static void msp_copy(void *src, void *dest, size_t bytes) {
  memcpy(dest, src, bytes);
}

static struct mspack_system msp_system = {
  &msp_open, &msp_close, &msp_read,  &msp_write, &msp_seek,
  &msp_tell, &msp_msg, &msp_alloc, &msp_free, &msp_copy, NULL
};

struct mspack_system *mspack_default_system = &msp_system;

#endif
