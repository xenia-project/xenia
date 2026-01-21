/* This file is part of libmspack.
 * (C) 2003-2018 Stuart Caie.
 *
 * libmspack is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (LGPL) version 2.1
 *
 * For further details, see the file COPYING.LIB distributed with libmspack
 */

#ifndef MSPACK_SYSTEM_H
#define MSPACK_SYSTEM_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* ensure config.h is read before mspack.h */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <mspack.h>

/* assume <string.h> exists */
#include <string.h>

/* fix for problem with GCC 4 and glibc (thanks to Ville Skytta)
 * http://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=150429
 */
#ifdef read
# undef read
#endif

/* Old GCCs don't have __func__, but __FUNCTION__:
 * http://gcc.gnu.org/onlinedocs/gcc/Function-Names.html
 */
#if __STDC_VERSION__ < 199901L
# if __GNUC__ >= 2
#  define __func__ __FUNCTION__
# else
#  define __func__ "<unknown>"
# endif
#endif

#if DEBUG
# include <stdio.h>
# define D(x) do { printf("%s:%d (%s) ",__FILE__, __LINE__, __func__); \
                   printf x ; fputc('\n', stdout); fflush(stdout);} while (0);
#else
# define D(x)
#endif

/* CAB supports searching through files over 4GB in size, and the CHM file
 * format actively uses 64-bit offsets. These can only be fully supported
 * if the system the code runs on supports large files. If not, the library
 * will work as normal using only 32-bit arithmetic, but if an offset
 * greater than 2GB is detected, an error message indicating the library
 * can't support the file should be printed.
 */
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# define PRId64 "lld"
# define PRIu64 "llu"
# define PRId32 "ld"
# define PRIu32 "lu"
#endif

#include <limits.h>
#if ((defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS >= 64) || \
     (defined(FILESIZEBITS)      && FILESIZEBITS      >= 64) || \
     defined(_LARGEFILE_SOURCE) || defined(_LARGEFILE64_SOURCE) || \
     SIZEOF_OFF_T >= 8)
# define LARGEFILE_SUPPORT 1
# define LD PRId64
# define LU PRIu64
#else
extern const char *largefile_msg;
# define LD PRId32
# define LU PRIu32
#endif

/* endian-neutral reading of little-endian data */
#define __egi32(a,n) ( ((((unsigned char *) a)[n+3]) << 24) | \
                       ((((unsigned char *) a)[n+2]) << 16) | \
                       ((((unsigned char *) a)[n+1]) <<  8) | \
                       ((((unsigned char *) a)[n+0])))
#define EndGetI64(a) ((((unsigned long long int) __egi32(a,4)) << 32) | \
                      ((unsigned int) __egi32(a,0)))
#define EndGetI32(a) __egi32(a,0)
#define EndGetI16(a) ((((a)[1])<<8)|((a)[0]))

/* endian-neutral reading of big-endian data */
#define EndGetM32(a) (((((unsigned char *) a)[0]) << 24) | \
                      ((((unsigned char *) a)[1]) << 16) | \
                      ((((unsigned char *) a)[2]) <<  8) | \
                      ((((unsigned char *) a)[3])))
#define EndGetM16(a) ((((a)[0])<<8)|((a)[1]))

extern struct mspack_system *mspack_default_system;

/* returns the length of a file opened for reading */
extern int mspack_sys_filelen(struct mspack_system *system,
                              struct mspack_file *file, off_t *length);

/* validates a system structure */
extern int mspack_valid_system(struct mspack_system *sys);

#ifdef __cplusplus
}
#endif

#endif
