/* libmspack -- a library for working with Microsoft compression formats.
 * (C) 2003-2004 Stuart Caie <kyzer@4u.net>
 *
 * libmspack is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (LGPL) version 2.1
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/** \mainpage
 *
 * \section intro Introduction
 *
 * libmspack is a library which provides compressors and decompressors,
 * archivers and dearchivers for Microsoft compression formats.
 *
 * \section errors Error codes
 *
 * All compressors and decompressors use the same set of error codes. Most
 * methods return an error code directly. For methods which do not
 * return error codes directly, the error code can be obtained with the
 * last_error() method.
 *
 * - #MSPACK_ERR_OK is used to indicate success. This error code is defined
 *   as zero, all other code are non-zero.
 * - #MSPACK_ERR_ARGS indicates that a method was called with inappropriate
 *   arguments.
 * - #MSPACK_ERR_OPEN indicates that mspack_system::open() failed.
 * - #MSPACK_ERR_READ indicates that mspack_system::read() failed.
 * - #MSPACK_ERR_WRITE indicates that mspack_system::write() failed.
 * - #MSPACK_ERR_SEEK indicates that mspack_system::seek() failed.
 * - #MSPACK_ERR_NOMEMORY indicates that mspack_system::alloc() failed.
 * - #MSPACK_ERR_SIGNATURE indicates that the file being read does not
 *   have the correct "signature". It is probably not a valid file for
 *   whatever format is being read.
 * - #MSPACK_ERR_DATAFORMAT indicates that the file being used or read
 *   is corrupt.
 * - #MSPACK_ERR_CHECKSUM indicates that a data checksum has failed.
 * - #MSPACK_ERR_CRUNCH indicates an error occured during compression.
 * - #MSPACK_ERR_DECRUNCH indicates an error occured during decompression.
 */

#ifndef LIB_MSPACK_H
#define LIB_MSPACK_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdlib.h>

/* --- file I/O abstraction ------------------------------------------------ */

/**
 * A structure which abstracts file I/O and memory management.
 *
 * The library always uses the mspack_system structure for interaction
 * with the file system and to allocate, free and copy all memory. It also
 * uses it to send literal messages to the library user.
 *
 * When the library is compiled normally, passing NULL to a compressor or
 * decompressor constructor will result in a default mspack_system being
 * used, where all methods are implemented with the standard C library.
 * However, all constructors support being given a custom created
 * mspack_system structure, with the library user's own methods. This
 * allows for more abstract interaction, such as reading and writing files
 * directly to memory, or from a network socket or pipe.
 *
 * Implementors of an mspack_system structure should read all
 * documentation entries for every structure member, and write methods
 * which conform to those standards.
 */
struct mspack_system {
  /**
   * Opens a file for reading, writing, appending or updating.
   *
   * @param this     a self-referential pointer to the mspack_system
   *                 structure whose open() method is being called. If
   *                 this pointer is required by close(), read(), write(),
   *                 seek() or tell(), it should be stored in the result
   *                 structure at this time.
   * @param filename the file to be opened. It is passed directly from the
   *                 library caller without being modified, so it is up to
   *                 the caller what this parameter actually represents.
   * @param mode     one of #MSPACK_SYS_OPEN_READ (open an existing file
   *                 for reading), #MSPACK_SYS_OPEN_WRITE (open a new file
   *                 for writing), #MSPACK_SYS_OPEN_UPDATE (open an existing
   *                 file for reading/writing from the start of the file) or
   *                 #MSPACK_SYS_OPEN_APPEND (open an existing file for
   *                 reading/writing from the end of the file)
   * @return a pointer to a mspack_file structure. This structure officially
   *         contains no members, its true contents are up to the
   *         mspack_system implementor. It should contain whatever is needed
   *         for other mspack_system methods to operate.
   * @see close(), read(), write(), seek(), tell(), message()
   */
  struct mspack_file * (*open)(struct mspack_system *sys,
			       char *filename,
			       int mode);

  /**
   * Closes a previously opened file. If any memory was allocated for this
   * particular file handle, it should be freed at this time.
   * 
   * @param file the file to close
   * @see open()
   */
  void (*close)(struct mspack_file *file);

  /**
   * Reads a given number of bytes from an open file.
   *
   * @param file    the file to read from
   * @param buffer  the location where the read bytes should be stored
   * @param bytes   the number of bytes to read from the file.
   * @return the number of bytes successfully read (this can be less than
   *         the number requested), zero to mark the end of file, or less
   *         than zero to indicate an error.
   * @see open(), write()
   */
  int (*read)(struct mspack_file *file,
	      void *buffer,
	      int bytes);

  /**
   * Writes a given number of bytes to an open file.
   *
   * @param file    the file to write to
   * @param buffer  the location where the written bytes should be read from
   * @param bytes   the number of bytes to write to the file.
   * @return the number of bytes successfully written, this can be less
   *         than the number requested. Zero or less can indicate an error
   *         where no bytes at all could be written. All cases where less
   *         bytes were written than requested are considered by the library
   *         to be an error.
   * @see open(), read()
   */
  int (*write)(struct mspack_file *file,
	       void *buffer,
	       int bytes);

  /**
   * Seeks to a specific file offset within an open file.
   *
   * Sometimes the library needs to know the length of a file. It does
   * this by seeking to the end of the file with seek(file, 0,
   * MSPACK_SYS_SEEK_END), then calling tell(). Implementations may want
   * to make a special case for this.
   *
   * Due to the potentially varying 32/64 bit datatype off_t on some
   * architectures, the #MSPACK_SYS_SELFTEST macro MUST be used before
   * using the library. If not, the error caused by the library passing an
   * inappropriate stackframe to seek() is subtle and hard to trace.
   *
   * @param file   the file to be seeked
   * @param offset an offset to seek, measured in bytes
   * @param mode   one of #MSPACK_SYS_SEEK_START (the offset should be
   *               measured from the start of the file), #MSPACK_SYS_SEEK_CUR
   *               (the offset should be measured from the current file offset)
   *               or #MSPACK_SYS_SEEK_END (the offset should be measured from
   *               the end of the file)
   * @return zero for success, non-zero for an error
   * @see open(), tell()
   */
  int (*seek)(struct mspack_file *file,
	      off_t offset,
	      int mode);

  /**
   * Returns the current file position (in bytes) of the given file.
   *
   * @param file the file whose file position is wanted
   * @return the current file position of the file
   * @see open(), seek()
   */
  off_t (*tell)(struct mspack_file *file);
  
  /**
   * Used to send messages from the library to the user.
   *
   * Occasionally, the library generates warnings or other messages in
   * plain english to inform the human user. These are informational only
   * and can be ignored if not wanted.
   *
   * @param file   may be a file handle returned from open() if this message
   *               pertains to a specific open file, or NULL if not related to
   *               a specific file.
   * @param format a printf() style format string. It does NOT include a
   *               trailing newline.
   * @see open()
   */
  void (*message)(struct mspack_file *file,
		  char *format,
		  ...);

  /**
   * Allocates memory.
   *
   * @param sys      a self-referential pointer to the mspack_system
   *                 structure whose alloc() method is being called.
   * @param bytes    the number of bytes to allocate
   * @result a pointer to the requested number of bytes, or NULL if
   *         not enough memory is available
   * @see free()
   */
  void * (*alloc)(struct mspack_system *sys,
		  size_t bytes);
  
  /**
   * Frees memory.
   * 
   * @param ptr the memory to be freed.
   * @see alloc()
   */
  void (*free)(void *ptr);

  /**
   * Copies from one region of memory to another.
   * 
   * The regions of memory are guaranteed not to overlap, are usually less
   * than 256 bytes, and may not be aligned. Please note that the source
   * parameter comes before the destination parameter, unlike the standard
   * C function memcpy().
   *
   * @param src   the region of memory to copy from
   * @param dest  the region of memory to copy to
   * @param bytes the size of the memory region, in bytes
   */
  void (*copy)(void *src,
	       void *dest,
	       size_t bytes);

  /**
   * A null pointer to mark the end of mspack_system. It must equal NULL.
   *
   * Should the mspack_system structure extend in the future, this NULL
   * will be seen, rather than have an invalid method pointer called.
   */
  void *null_ptr;
};

/** mspack_system::open() mode: open existing file for reading. */
#define MSPACK_SYS_OPEN_READ   (0)
/** mspack_system::open() mode: open new file for writing */
#define MSPACK_SYS_OPEN_WRITE  (1)
/** mspack_system::open() mode: open existing file for writing */
#define MSPACK_SYS_OPEN_UPDATE (2)
/** mspack_system::open() mode: open existing file for writing */
#define MSPACK_SYS_OPEN_APPEND (3)

/** mspack_system::seek() mode: seek relative to start of file */
#define MSPACK_SYS_SEEK_START  (0)
/** mspack_system::seek() mode: seek relative to current offset */
#define MSPACK_SYS_SEEK_CUR    (1)
/** mspack_system::seek() mode: seek relative to end of file */
#define MSPACK_SYS_SEEK_END    (2)

/** 
 * A structure which represents an open file handle. The contents of this
 * structure are determined by the implementation of the
 * mspack_system::open() method.
 */
struct mspack_file {
  int dummy;
};

/* --- error codes --------------------------------------------------------- */

/** Error code: no error */
#define MSPACK_ERR_OK          (0)
/** Error code: bad arguments to method */
#define MSPACK_ERR_ARGS        (1)
/** Error code: error opening file */
#define MSPACK_ERR_OPEN        (2)
/** Error code: error reading file */
#define MSPACK_ERR_READ        (3)
/** Error code: error writing file */
#define MSPACK_ERR_WRITE       (4)
/** Error code: seek error */
#define MSPACK_ERR_SEEK        (5)
/** Error code: out of memory */
#define MSPACK_ERR_NOMEMORY    (6)
/** Error code: bad "magic id" in file */
#define MSPACK_ERR_SIGNATURE   (7)
/** Error code: bad or corrupt file format */
#define MSPACK_ERR_DATAFORMAT  (8)
/** Error code: bad checksum or CRC */
#define MSPACK_ERR_CHECKSUM    (9)
/** Error code: error during compression */
#define MSPACK_ERR_CRUNCH      (10)
/** Error code: error during decompression */
#define MSPACK_ERR_DECRUNCH    (11)

#ifdef __cplusplus
};
#endif

#endif
