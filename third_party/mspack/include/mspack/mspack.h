/* libmspack -- a library for working with Microsoft compression formats.
 * (C) 2003-2016 Stuart Caie <kyzer@cabextract.org.uk>
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
 * \section formats Formats supported
 *
 * The following file formats are supported:
 * - SZDD files, which use LZSS compression
 * - KWAJ files, which use LZSS, LZSS+Huffman or deflate compression
 * - .HLP (MS Help) files, which use LZSS compression
 * - .CAB (MS Cabinet) files, which use deflate, LZX or Quantum compression
 * - .CHM (HTML Help) files, which use LZX compression
 * - .LIT (MS EBook) files, which use LZX compression and DES encryption
 * - .LZX (Exchange Offline Addressbook) files, which use LZX compression
 *
 * To determine the capabilities of the library, and the binary
 * compatibility version of any particular compressor or decompressor, use
 * the mspack_version() function. The UNIX library interface version is
 * defined as the highest-versioned library component.
 *
 * \section starting Getting started
 *
 * The macro MSPACK_SYS_SELFTEST() should be used to ensure the library can
 * be used. In particular, it checks if the caller is using 32-bit file I/O
 * when the library is compiled for 64-bit file I/O and vice versa.
 *
 * If compiled normally, the library includes basic file I/O and memory
 * management functionality using the standard C library. This can be
 * customised and replaced entirely by creating a mspack_system structure.
 *
 * A compressor or decompressor for the required format must be
 * instantiated before it can be used. Each construction function takes
 * one parameter, which is either a pointer to a custom mspack_system
 * structure, or NULL to use the default. The instantiation returned, if
 * not NULL, contains function pointers (methods) to work with the given
 * file format.
 * 
 * For compression:
 * - mspack_create_cab_compressor() creates a mscab_compressor
 * - mspack_create_chm_compressor() creates a mschm_compressor
 * - mspack_create_lit_compressor() creates a mslit_compressor
 * - mspack_create_hlp_compressor() creates a mshlp_compressor
 * - mspack_create_szdd_compressor() creates a msszdd_compressor
 * - mspack_create_kwaj_compressor() creates a mskwaj_compressor
 * - mspack_create_oab_compressor() creates a msoab_compressor
 *
 * For decompression:
 * - mspack_create_cab_decompressor() creates a mscab_decompressor
 * - mspack_create_chm_decompressor() creates a mschm_decompressor
 * - mspack_create_lit_decompressor() creates a mslit_decompressor
 * - mspack_create_hlp_decompressor() creates a mshlp_decompressor
 * - mspack_create_szdd_decompressor() creates a msszdd_decompressor
 * - mspack_create_kwaj_decompressor() creates a mskwaj_decompressor
 * - mspack_create_oab_decompressor() creates a msoab_decompressor
 *
 * Once finished working with a format, each kind of
 * compressor/decompressor has its own specific destructor:
 * - mspack_destroy_cab_compressor()
 * - mspack_destroy_cab_decompressor()
 * - mspack_destroy_chm_compressor()
 * - mspack_destroy_chm_decompressor()
 * - mspack_destroy_lit_compressor()
 * - mspack_destroy_lit_decompressor()
 * - mspack_destroy_hlp_compressor()
 * - mspack_destroy_hlp_decompressor()
 * - mspack_destroy_szdd_compressor()
 * - mspack_destroy_szdd_decompressor()
 * - mspack_destroy_kwaj_compressor()
 * - mspack_destroy_kwaj_decompressor()
 * - mspack_destroy_oab_compressor()
 * - mspack_destroy_oab_decompressor()
 *
 * Destroying a compressor or decompressor does not destroy any objects,
 * structures or handles that have been created using that compressor or
 * decompressor. Ensure that everything created or opened is destroyed or
 * closed before compressor/decompressor is itself destroyed.
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
 *
 * \section threading Multi-threading
 *
 * libmspack methods are reentrant and multithreading-safe when each
 * thread has its own compressor or decompressor.

 * You should not call multiple methods simultaneously on a single
 * compressor or decompressor instance.
 *
 * If this may happen, you can either use one compressor or
 * decompressor per thread, or you can use your preferred lock,
 * semaphore or mutex library to ensure no more than one method on a
 * compressor/decompressor is called simultaneously. libmspack will
 * not do this locking for you.
 *
 * Example of incorrect behaviour:
 * - thread 1 calls mspack_create_cab_decompressor()
 * - thread 1 calls open()
 * - thread 1 calls extract() for one file
 * - thread 2 simultaneously calls extract() for another file
 *
 * Correct behaviour:
 * - thread 1 calls mspack_create_cab_decompressor()
 * - thread 2 calls mspack_create_cab_decompressor()
 * - thread 1 calls its own open() / extract()
 * - thread 2 simultaneously calls its own open() / extract()
 *
 * Also correct behaviour:
 * - thread 1 calls mspack_create_cab_decompressor()
 * - thread 1 locks a mutex for with the decompressor before
 *   calling any methods on it, and unlocks the mutex after each
 *   method returns.
 * - thread 1 can share the results of open() with thread 2, and both
 *   can call extract(), provided they both guard against simultaneous
 *   use of extract(), and any other methods, with the mutex
 */

#ifndef LIB_MSPACK_H
#define LIB_MSPACK_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdlib.h>

/**
 * System self-test function, to ensure both library and calling program
 * can use one another.
 *
 * A result of MSPACK_ERR_OK means the library and caller are
 * compatible. Any other result indicates that the library and caller are
 * not compatible and should not be used. In particular, a value of
 * MSPACK_ERR_SEEK means the library and caller use different off_t
 * datatypes.
 *
 * It should be used like so:
 *
 * @code
 * int selftest_result;
 * MSPACK_SYS_SELFTEST(selftest_result);
 * if (selftest_result != MSPACK_ERR_OK) {
 *   fprintf(stderr, "incompatible with this build of libmspack\n");
 *   exit(0);
 * }
 * @endcode
 *
 * @param  result   an int variable to store the result of the self-test
 */
#define MSPACK_SYS_SELFTEST(result)  do { \
  (result) = mspack_sys_selftest_internal(sizeof(off_t)); \
} while (0)

/** Part of the MSPACK_SYS_SELFTEST() macro, must not be used directly. */
extern int mspack_sys_selftest_internal(int);

/**
 * Enquire about the binary compatibility version of a specific interface in
 * the library. Currently, the following interfaces are defined:
 *
 * - #MSPACK_VER_LIBRARY: the overall library
 * - #MSPACK_VER_SYSTEM: the mspack_system interface
 * - #MSPACK_VER_MSCABD: the mscab_decompressor interface
 * - #MSPACK_VER_MSCABC: the mscab_compressor interface
 * - #MSPACK_VER_MSCHMD: the mschm_decompressor interface
 * - #MSPACK_VER_MSCHMC: the mschm_compressor interface
 * - #MSPACK_VER_MSLITD: the mslit_decompressor interface
 * - #MSPACK_VER_MSLITC: the mslit_compressor interface
 * - #MSPACK_VER_MSHLPD: the mshlp_decompressor interface
 * - #MSPACK_VER_MSHLPC: the mshlp_compressor interface
 * - #MSPACK_VER_MSSZDDD: the msszdd_decompressor interface
 * - #MSPACK_VER_MSSZDDC: the msszdd_compressor interface
 * - #MSPACK_VER_MSKWAJD: the mskwaj_decompressor interface
 * - #MSPACK_VER_MSKWAJC: the mskwaj_compressor interface
 * - #MSPACK_VER_MSOABD: the msoab_decompressor interface
 * - #MSPACK_VER_MSOABC: the msoab_compressor interface
 *
 * The result of the function should be interpreted as follows:
 * - -1: this interface is completely unknown to the library
 * - 0: this interface is known, but non-functioning
 * - 1: this interface has all basic functionality
 * - 2, 3, ...: this interface has additional functionality, clearly marked
 *   in the documentation as "version 2", "version 3" and so on.
 *
 * @param entity the interface to request current version of
 * @return the version of the requested interface
 */
extern int mspack_version(int entity);

/** Pass to mspack_version() to get the overall library version */
#define MSPACK_VER_LIBRARY   (0)
/** Pass to mspack_version() to get the mspack_system version */
#define MSPACK_VER_SYSTEM    (1)
/** Pass to mspack_version() to get the mscab_decompressor version */
#define MSPACK_VER_MSCABD    (2)
/** Pass to mspack_version() to get the mscab_compressor version */
#define MSPACK_VER_MSCABC    (3)
/** Pass to mspack_version() to get the mschm_decompressor version */
#define MSPACK_VER_MSCHMD    (4)
/** Pass to mspack_version() to get the mschm_compressor version */
#define MSPACK_VER_MSCHMC    (5)
/** Pass to mspack_version() to get the mslit_decompressor version */
#define MSPACK_VER_MSLITD    (6)
/** Pass to mspack_version() to get the mslit_compressor version */
#define MSPACK_VER_MSLITC    (7)
/** Pass to mspack_version() to get the mshlp_decompressor version */
#define MSPACK_VER_MSHLPD    (8)
/** Pass to mspack_version() to get the mshlp_compressor version */
#define MSPACK_VER_MSHLPC    (9)
/** Pass to mspack_version() to get the msszdd_decompressor version */
#define MSPACK_VER_MSSZDDD   (10)
/** Pass to mspack_version() to get the msszdd_compressor version */
#define MSPACK_VER_MSSZDDC   (11)
/** Pass to mspack_version() to get the mskwaj_decompressor version */
#define MSPACK_VER_MSKWAJD   (12)
/** Pass to mspack_version() to get the mskwaj_compressor version */
#define MSPACK_VER_MSKWAJC   (13)
/** Pass to mspack_version() to get the msoab_decompressor version */
#define MSPACK_VER_MSOABD    (14)
/** Pass to mspack_version() to get the msoab_compressor version */
#define MSPACK_VER_MSOABC    (15)

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
   * @param self     a self-referential pointer to the mspack_system
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
   *         for other mspack_system methods to operate. Returning the NULL
   *         pointer indicates an error condition.
   * @see close(), read(), write(), seek(), tell(), message()
   */
  struct mspack_file * (*open)(struct mspack_system *self,
                               const char *filename,
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
   *         than zero to indicate an error. The library does not "retry"
   *         reads and assumes short reads are due to EOF, so you should
   *         avoid returning short reads because of transient errors.
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
                  const char *format,
                  ...);

  /**
   * Allocates memory.
   *
   * @param self     a self-referential pointer to the mspack_system
   *                 structure whose alloc() method is being called.
   * @param bytes    the number of bytes to allocate
   * @result a pointer to the requested number of bytes, or NULL if
   *         not enough memory is available
   * @see free()
   */
  void * (*alloc)(struct mspack_system *self,
                  size_t bytes);
  
  /**
   * Frees memory.
   * 
   * @param ptr the memory to be freed. NULL is accepted and ignored.
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

/* --- functions available in library -------------------------------------- */

/** Creates a new CAB compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mscab_compressor or NULL
 */
extern struct mscab_compressor *
  mspack_create_cab_compressor(struct mspack_system *sys);

/** Creates a new CAB decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mscab_decompressor or NULL
 */
extern struct mscab_decompressor *
  mspack_create_cab_decompressor(struct mspack_system *sys);

/** Destroys an existing CAB compressor.
 * @param self the #mscab_compressor to destroy
 */
extern void mspack_destroy_cab_compressor(struct mscab_compressor *self);

/** Destroys an existing CAB decompressor.
 * @param self the #mscab_decompressor to destroy
 */
extern void mspack_destroy_cab_decompressor(struct mscab_decompressor *self);


/** Creates a new CHM compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mschm_compressor or NULL
 */
extern struct mschm_compressor *
  mspack_create_chm_compressor(struct mspack_system *sys);

/** Creates a new CHM decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mschm_decompressor or NULL
 */
extern struct mschm_decompressor *
  mspack_create_chm_decompressor(struct mspack_system *sys);

/** Destroys an existing CHM compressor.
 * @param self the #mschm_compressor to destroy
 */
extern void mspack_destroy_chm_compressor(struct mschm_compressor *self);

/** Destroys an existing CHM decompressor.
 * @param self the #mschm_decompressor to destroy
 */
extern void mspack_destroy_chm_decompressor(struct mschm_decompressor *self);


/** Creates a new LIT compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mslit_compressor or NULL
 */
extern struct mslit_compressor *
  mspack_create_lit_compressor(struct mspack_system *sys);

/** Creates a new LIT decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mslit_decompressor or NULL
 */
extern struct mslit_decompressor *
  mspack_create_lit_decompressor(struct mspack_system *sys);

/** Destroys an existing LIT compressor.
 * @param self the #mslit_compressor to destroy
 */
extern void mspack_destroy_lit_compressor(struct mslit_compressor *self);

/** Destroys an existing LIT decompressor.
 * @param self the #mslit_decompressor to destroy
 */
extern void mspack_destroy_lit_decompressor(struct mslit_decompressor *self);


/** Creates a new HLP compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mshlp_compressor or NULL
 */
extern struct mshlp_compressor *
  mspack_create_hlp_compressor(struct mspack_system *sys);

/** Creates a new HLP decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mshlp_decompressor or NULL
 */
extern struct mshlp_decompressor *
  mspack_create_hlp_decompressor(struct mspack_system *sys);

/** Destroys an existing hlp compressor.
 * @param self the #mshlp_compressor to destroy
 */
extern void mspack_destroy_hlp_compressor(struct mshlp_compressor *self);

/** Destroys an existing hlp decompressor.
 * @param self the #mshlp_decompressor to destroy
 */
extern void mspack_destroy_hlp_decompressor(struct mshlp_decompressor *self);


/** Creates a new SZDD compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #msszdd_compressor or NULL
 */
extern struct msszdd_compressor *
  mspack_create_szdd_compressor(struct mspack_system *sys);

/** Creates a new SZDD decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #msszdd_decompressor or NULL
 */
extern struct msszdd_decompressor *
  mspack_create_szdd_decompressor(struct mspack_system *sys);

/** Destroys an existing SZDD compressor.
 * @param self the #msszdd_compressor to destroy
 */
extern void mspack_destroy_szdd_compressor(struct msszdd_compressor *self);

/** Destroys an existing SZDD decompressor.
 * @param self the #msszdd_decompressor to destroy
 */
extern void mspack_destroy_szdd_decompressor(struct msszdd_decompressor *self);


/** Creates a new KWAJ compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mskwaj_compressor or NULL
 */
extern struct mskwaj_compressor *
  mspack_create_kwaj_compressor(struct mspack_system *sys);

/** Creates a new KWAJ decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #mskwaj_decompressor or NULL
 */
extern struct mskwaj_decompressor *
  mspack_create_kwaj_decompressor(struct mspack_system *sys);

/** Destroys an existing KWAJ compressor.
 * @param self the #mskwaj_compressor to destroy
 */
extern void mspack_destroy_kwaj_compressor(struct mskwaj_compressor *self);

/** Destroys an existing KWAJ decompressor.
 * @param self the #mskwaj_decompressor to destroy
 */
extern void mspack_destroy_kwaj_decompressor(struct mskwaj_decompressor *self);


/** Creates a new OAB compressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #msoab_compressor or NULL
 */
extern struct msoab_compressor *
  mspack_create_oab_compressor(struct mspack_system *sys);

/** Creates a new OAB decompressor.
 * @param sys a custom mspack_system structure, or NULL to use the default
 * @return a #msoab_decompressor or NULL
 */
extern struct msoab_decompressor *
  mspack_create_oab_decompressor(struct mspack_system *sys);

/** Destroys an existing OAB compressor.
 * @param self the #msoab_compressor to destroy
 */
extern void mspack_destroy_oab_compressor(struct msoab_compressor *self);

/** Destroys an existing OAB decompressor.
 * @param self the #msoab_decompressor to destroy
 */
extern void mspack_destroy_oab_decompressor(struct msoab_decompressor *self);


/* --- support for .CAB (MS Cabinet) file format --------------------------- */

/**
 * A structure which represents a single cabinet file.
 *
 * All fields are READ ONLY.
 *
 * If this cabinet is part of a merged cabinet set, the #files and #folders
 * fields are common to all cabinets in the set, and will be identical.
 *
 * @see mscab_decompressor::open(), mscab_decompressor::close(),
 *      mscab_decompressor::search()
 */
struct mscabd_cabinet {
  /**
   * The next cabinet in a chained list, if this cabinet was opened with
   * mscab_decompressor::search(). May be NULL to mark the end of the
   * list.
   */
  struct mscabd_cabinet *next;

  /**
   * The filename of the cabinet. More correctly, the filename of the
   * physical file that the cabinet resides in. This is given by the
   * library user and may be in any format.
   */
  const char *filename;
  
  /** The file offset of cabinet within the physical file it resides in. */
  off_t base_offset;

  /** The length of the cabinet file in bytes. */
  unsigned int length;

  /** The previous cabinet in a cabinet set, or NULL. */
  struct mscabd_cabinet *prevcab;

  /** The next cabinet in a cabinet set, or NULL. */
  struct mscabd_cabinet *nextcab;

  /** The filename of the previous cabinet in a cabinet set, or NULL. */
  char *prevname;

  /** The filename of the next cabinet in a cabinet set, or NULL. */
  char *nextname;

  /** The name of the disk containing the previous cabinet in a cabinet
   * set, or NULL.
   */
  char *previnfo;

  /** The name of the disk containing the next cabinet in a cabinet set,
   * or NULL.
   */
  char *nextinfo;

  /** A list of all files in the cabinet or cabinet set. */
  struct mscabd_file *files;

  /** A list of all folders in the cabinet or cabinet set. */
  struct mscabd_folder *folders;

  /** 
   * The set ID of the cabinet. All cabinets in the same set should have
   * the same set ID.
   */
  unsigned short set_id;

  /**
   * The index number of the cabinet within the set. Numbering should
   * start from 0 for the first cabinet in the set, and increment by 1 for
   * each following cabinet.
   */
  unsigned short set_index;

  /**
   * The number of bytes reserved in the header area of the cabinet.
   *
   * If this is non-zero and flags has MSCAB_HDR_RESV set, this data can
   * be read by the calling application. It is of the given length,
   * located at offset (base_offset + MSCAB_HDR_RESV_OFFSET) in the
   * cabinet file.
   *
   * @see flags
   */
  unsigned short header_resv;

  /**
   * Header flags.
   *
   * - MSCAB_HDR_PREVCAB indicates the cabinet is part of a cabinet set, and
   *                     has a predecessor cabinet.
   * - MSCAB_HDR_NEXTCAB indicates the cabinet is part of a cabinet set, and
   *                     has a successor cabinet.
   * - MSCAB_HDR_RESV indicates the cabinet has reserved header space.
   *
   * @see prevname, previnfo, nextname, nextinfo, header_resv
   */
  int flags;
};

/** Offset from start of cabinet to the reserved header data (if present). */
#define MSCAB_HDR_RESV_OFFSET (0x28)

/** Cabinet header flag: cabinet has a predecessor */
#define MSCAB_HDR_PREVCAB (0x01)
/** Cabinet header flag: cabinet has a successor */
#define MSCAB_HDR_NEXTCAB (0x02)
/** Cabinet header flag: cabinet has reserved header space */
#define MSCAB_HDR_RESV    (0x04)

/**
 * A structure which represents a single folder in a cabinet or cabinet set.
 *
 * All fields are READ ONLY.
 *
 * A folder is a single compressed stream of data. When uncompressed, it
 * holds the data of one or more files. A folder may be split across more
 * than one cabinet.
 */
struct mscabd_folder {
  /**
   * A pointer to the next folder in this cabinet or cabinet set, or NULL
   * if this is the final folder.
   */
  struct mscabd_folder *next;

  /** 
   * The compression format used by this folder.
   *
   * The macro MSCABD_COMP_METHOD() should be used on this field to get
   * the algorithm used. The macro MSCABD_COMP_LEVEL() should be used to get
   * the "compression level".
   *
   * @see MSCABD_COMP_METHOD(), MSCABD_COMP_LEVEL()
   */
  int comp_type;

  /**
   * The total number of data blocks used by this folder. This includes
   * data blocks present in other files, if this folder spans more than
   * one cabinet.
   */
  unsigned int num_blocks;
};

/**
 * Returns the compression method used by a folder.
 *
 * @param comp_type a mscabd_folder::comp_type value
 * @return one of #MSCAB_COMP_NONE, #MSCAB_COMP_MSZIP, #MSCAB_COMP_QUANTUM
 *         or #MSCAB_COMP_LZX
 */
#define MSCABD_COMP_METHOD(comp_type) ((comp_type) & 0x0F)
/**
 * Returns the compression level used by a folder.
 *
 * @param comp_type a mscabd_folder::comp_type value
 * @return the compression level. This is only defined by LZX and Quantum
 *         compression
 */
#define MSCABD_COMP_LEVEL(comp_type) (((comp_type) >> 8) & 0x1F)

/** Compression mode: no compression. */
#define MSCAB_COMP_NONE       (0)
/** Compression mode: MSZIP (deflate) compression. */
#define MSCAB_COMP_MSZIP      (1)
/** Compression mode: Quantum compression */
#define MSCAB_COMP_QUANTUM    (2)
/** Compression mode: LZX compression */
#define MSCAB_COMP_LZX        (3)

/**
 * A structure which represents a single file in a cabinet or cabinet set.
 *
 * All fields are READ ONLY.
 */
struct mscabd_file {
  /**
   * The next file in the cabinet or cabinet set, or NULL if this is the
   * final file.
   */
  struct mscabd_file *next;

  /**
   * The filename of the file.
   *
   * A null terminated string of up to 255 bytes in length, it may be in
   * either ISO-8859-1 or UTF8 format, depending on the file attributes.
   *
   * @see attribs
   */
  char *filename;

  /** The uncompressed length of the file, in bytes. */
  unsigned int length;

  /**
   * File attributes.
   *
   * The following attributes are defined:
   * - #MSCAB_ATTRIB_RDONLY indicates the file is write protected.
   * - #MSCAB_ATTRIB_HIDDEN indicates the file is hidden.
   * - #MSCAB_ATTRIB_SYSTEM indicates the file is a operating system file.
   * - #MSCAB_ATTRIB_ARCH indicates the file is "archived".
   * - #MSCAB_ATTRIB_EXEC indicates the file is an executable program.
   * - #MSCAB_ATTRIB_UTF_NAME indicates the filename is in UTF8 format rather
   *   than ISO-8859-1.
   */
  int attribs;

  /** File's last modified time, hour field. */
  char time_h;
  /** File's last modified time, minute field. */
  char time_m;
  /** File's last modified time, second field. */
  char time_s;

  /** File's last modified date, day field. */
  char date_d;
  /** File's last modified date, month field. */
  char date_m;
  /** File's last modified date, year field. */
  int date_y;

  /** A pointer to the folder that contains this file. */
  struct mscabd_folder *folder;

  /** The uncompressed offset of this file in its folder. */
  unsigned int offset;
};

/** mscabd_file::attribs attribute: file is read-only. */
#define MSCAB_ATTRIB_RDONLY   (0x01)
/** mscabd_file::attribs attribute: file is hidden. */
#define MSCAB_ATTRIB_HIDDEN   (0x02)
/** mscabd_file::attribs attribute: file is an operating system file. */
#define MSCAB_ATTRIB_SYSTEM   (0x04)
/** mscabd_file::attribs attribute: file is "archived". */
#define MSCAB_ATTRIB_ARCH     (0x20)
/** mscabd_file::attribs attribute: file is an executable program. */
#define MSCAB_ATTRIB_EXEC     (0x40)
/** mscabd_file::attribs attribute: filename is UTF8, not ISO-8859-1. */
#define MSCAB_ATTRIB_UTF_NAME (0x80)

/** mscab_decompressor::set_param() parameter: search buffer size. */
#define MSCABD_PARAM_SEARCHBUF (0)
/** mscab_decompressor::set_param() parameter: repair MS-ZIP streams? */
#define MSCABD_PARAM_FIXMSZIP  (1)
/** mscab_decompressor::set_param() parameter: size of decompression buffer */
#define MSCABD_PARAM_DECOMPBUF (2)
/** mscab_decompressor::set_param() parameter: salvage data from bad cabinets?
 * If enabled, open() will skip file with bad folder indices or filenames
 * rather than reject the whole cabinet, and extract() will limit rather than
 * reject files with invalid offsets and lengths, and bad data block checksums
 * will be ignored. Available only in CAB decoder version 2 and above.
 */
#define MSCABD_PARAM_SALVAGE   (3)

/** TODO */
struct mscab_compressor {
  int dummy; 
};

/**
 * A decompressor for .CAB (Microsoft Cabinet) files
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_cab_decompressor(), mspack_destroy_cab_decompressor()
 */
struct mscab_decompressor {
  /**
   * Opens a cabinet file and reads its contents.
   *
   * If the file opened is a valid cabinet file, all headers will be read
   * and a mscabd_cabinet structure will be returned, with a full list of
   * folders and files.
   *
   * In the case of an error occuring, NULL is returned and the error code
   * is available from last_error().
   *
   * The filename pointer should be considered "in use" until close() is
   * called on the cabinet.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  filename the filename of the cabinet file. This is passed
   *                  directly to mspack_system::open().
   * @return a pointer to a mscabd_cabinet structure, or NULL on failure
   * @see close(), search(), last_error()
   */
  struct mscabd_cabinet * (*open) (struct mscab_decompressor *self,
                                   const char *filename);

  /**
   * Closes a previously opened cabinet or cabinet set.
   *
   * This closes a cabinet, all cabinets associated with it via the
   * mscabd_cabinet::next, mscabd_cabinet::prevcab and
   * mscabd_cabinet::nextcab pointers, and all folders and files. All
   * memory used by these entities is freed.
   *
   * The cabinet pointer is now invalid and cannot be used again. All
   * mscabd_folder and mscabd_file pointers from that cabinet or cabinet
   * set are also now invalid, and cannot be used again.
   *
   * If the cabinet pointer given was created using search(), it MUST be
   * the cabinet pointer returned by search() and not one of the later
   * cabinet pointers further along the mscabd_cabinet::next chain.

   * If extra cabinets have been added using append() or prepend(), these
   * will all be freed, even if the cabinet pointer given is not the first
   * cabinet in the set. Do NOT close() more than one cabinet in the set.
   *
   * The mscabd_cabinet::filename is not freed by the library, as it is
   * not allocated by the library. The caller should free this itself if
   * necessary, before it is lost forever.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  cab      the cabinet to close
   * @see open(), search(), append(), prepend()
   */
  void (*close)(struct mscab_decompressor *self,
                struct mscabd_cabinet *cab);

  /**
   * Searches a regular file for embedded cabinets.
   *
   * This opens a normal file with the given filename and will search the
   * entire file for embedded cabinet files
   *
   * If any cabinets are found, the equivalent of open() is called on each
   * potential cabinet file at the offset it was found. All successfully
   * open()ed cabinets are kept in a list.
   *
   * The first cabinet found will be returned directly as the result of
   * this method. Any further cabinets found will be chained in a list
   * using the mscabd_cabinet::next field.
   *
   * In the case of an error occuring anywhere other than the simulated
   * open(), NULL is returned and the error code is available from
   * last_error().
   *
   * If no error occurs, but no cabinets can be found in the file, NULL is
   * returned and last_error() returns MSPACK_ERR_OK.
   *
   * The filename pointer should be considered in use until close() is
   * called on the cabinet.
   *
   * close() should only be called on the result of search(), not on any
   * subsequent cabinets in the mscabd_cabinet::next chain.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  filename the filename of the file to search for cabinets. This
   *                  is passed directly to mspack_system::open().
   * @return a pointer to a mscabd_cabinet structure, or NULL
   * @see close(), open(), last_error()
   */
  struct mscabd_cabinet * (*search) (struct mscab_decompressor *self,
                                     const char *filename);

  /**
   * Appends one mscabd_cabinet to another, forming or extending a cabinet
   * set.
   *
   * This will attempt to append one cabinet to another such that
   * <tt>(cab->nextcab == nextcab) && (nextcab->prevcab == cab)</tt> and
   * any folders split between the two cabinets are merged.
   *
   * The cabinets MUST be part of a cabinet set -- a cabinet set is a
   * cabinet that spans more than one physical cabinet file on disk -- and
   * must be appropriately matched.
   *
   * It can be determined if a cabinet has further parts to load by
   * examining the mscabd_cabinet::flags field:
   *
   * - if <tt>(flags & MSCAB_HDR_PREVCAB)</tt> is non-zero, there is a
   *   predecessor cabinet to open() and prepend(). Its MS-DOS
   *   case-insensitive filename is mscabd_cabinet::prevname
   * - if <tt>(flags & MSCAB_HDR_NEXTCAB)</tt> is non-zero, there is a
   *   successor cabinet to open() and append(). Its MS-DOS case-insensitive
   *   filename is mscabd_cabinet::nextname
   *
   * If the cabinets do not match, an error code will be returned. Neither
   * cabinet has been altered, and both should be closed seperately.
   *
   * Files and folders in a cabinet set are a single entity. All cabinets
   * in a set use the same file list, which is updated as cabinets in the
   * set are added. All pointers to mscabd_folder and mscabd_file
   * structures in either cabinet must be discarded and re-obtained after
   * merging.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  cab      the cabinet which will be appended to,
   *                  predecessor of nextcab
   * @param  nextcab  the cabinet which will be appended,
   *                  successor of cab
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see prepend(), open(), close()
   */
  int (*append) (struct mscab_decompressor *self,
                 struct mscabd_cabinet *cab,
                 struct mscabd_cabinet *nextcab);

  /**
   * Prepends one mscabd_cabinet to another, forming or extending a
   * cabinet set.
   *
   * This will attempt to prepend one cabinet to another, such that
   * <tt>(cab->prevcab == prevcab) && (prevcab->nextcab == cab)</tt>. In
   * all other respects, it is identical to append(). See append() for the
   * full documentation.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  cab      the cabinet which will be prepended to,
   *                  successor of prevcab
   * @param  prevcab  the cabinet which will be prepended,
   *                  predecessor of cab
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see append(), open(), close()
   */
  int (*prepend) (struct mscab_decompressor *self,
                  struct mscabd_cabinet *cab,
                  struct mscabd_cabinet *prevcab);

  /**
   * Extracts a file from a cabinet or cabinet set.
   *
   * This extracts a compressed file in a cabinet and writes it to the given
   * filename.
   *
   * The MS-DOS filename of the file, mscabd_file::filename, is NOT USED
   * by extract(). The caller must examine this MS-DOS filename, copy and
   * change it as necessary, create directories as necessary, and provide
   * the correct filename as a parameter, which will be passed unchanged
   * to the decompressor's mspack_system::open()
   *
   * If the file belongs to a split folder in a multi-part cabinet set,
   * and not enough parts of the cabinet set have been loaded and appended
   * or prepended, an error will be returned immediately.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  file     the file to be decompressed
   * @param  filename the filename of the file being written to
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*extract)(struct mscab_decompressor *self,
                 struct mscabd_file *file,
                 const char *filename);

  /**
   * Sets a CAB decompression engine parameter.
   *
   * The following parameters are defined:
   * - #MSCABD_PARAM_SEARCHBUF: How many bytes should be allocated as a
   *   buffer when using search()? The minimum value is 4.  The default
   *   value is 32768.
   * - #MSCABD_PARAM_FIXMSZIP: If non-zero, extract() will ignore bad
   *   checksums and recover from decompression errors in MS-ZIP
   *   compressed folders. The default value is 0 (don't recover).
   * - #MSCABD_PARAM_DECOMPBUF: How many bytes should be used as an input
   *   bit buffer by decompressors? The minimum value is 4. The default
   *   value is 4096.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @param  param    the parameter to set
   * @param  value    the value to set the parameter to
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS if there
   *         is a problem with either parameter or value.
   * @see search(), extract()
   */
  int (*set_param)(struct mscab_decompressor *self,
                   int param,
                   int value);

  /**
   * Returns the error code set by the most recently called method.
   *
   * This is useful for open() and search(), which do not return an error
   * code directly.
   *
   * @param  self     a self-referential pointer to the mscab_decompressor
   *                  instance being called
   * @return the most recent error code
   * @see open(), search()
   */
  int (*last_error)(struct mscab_decompressor *self);
};

/* --- support for .CHM (HTMLHelp) file format ----------------------------- */

/**
 * A structure which represents a file to be placed in a CHM helpfile.
 *
 * A contiguous array of these structures should be passed to
 * mschm_compressor::generate(). The array list is terminated with an
 * entry whose mschmc_file::section field is set to #MSCHMC_ENDLIST, the
 * other fields in this entry are ignored.
 */
struct mschmc_file {
  /** One of #MSCHMC_ENDLIST, #MSCHMC_UNCOMP or #MSCHMC_MSCOMP. */
  int section;

  /** The filename of the source file that will be added to the CHM. This
   * is passed directly to mspack_system::open(). */
  const char *filename;

  /** The full path and filename of the file within the CHM helpfile, a
   * UTF-1 encoded null-terminated string. */
  char *chm_filename;

  /** The length of the file, in bytes. This will be adhered to strictly
   * and a read error will be issued if this many bytes cannot be read
   * from the real file at CHM generation time. */
  off_t length;
};

/**
 * A structure which represents a section of a CHM helpfile.
 *
 * All fields are READ ONLY.
 *
 * Not used directly, but used as a generic base type for
 * mschmd_sec_uncompressed and mschmd_sec_mscompressed.
 */
struct mschmd_section {
  /** A pointer to the CHM helpfile that contains this section. */
  struct mschmd_header *chm;

  /**
   * The section ID. Either 0 for the uncompressed section
   * mschmd_sec_uncompressed, or 1 for the LZX compressed section
   * mschmd_sec_mscompressed. No other section IDs are known.
   */
  unsigned int id;
};

/**
 * A structure which represents the uncompressed section of a CHM helpfile.
 * 
 * All fields are READ ONLY.
 */
struct mschmd_sec_uncompressed {
  /** Generic section data. */
  struct mschmd_section base;

  /** The file offset of where this section begins in the CHM helpfile. */
  off_t offset;
};

/**
 * A structure which represents the LZX compressed section of a CHM helpfile. 
 * 
 * All fields are READ ONLY.
 */
struct mschmd_sec_mscompressed {
  /** Generic section data. */
  struct mschmd_section base;

  /** A pointer to the meta-file which represents all LZX compressed data. */
  struct mschmd_file *content;

  /** A pointer to the file which contains the LZX control data. */
  struct mschmd_file *control;

  /** A pointer to the file which contains the LZX reset table. */
  struct mschmd_file *rtable;

  /** A pointer to the file which contains the LZX span information.
   * Available only in CHM decoder version 2 and above.
   */
  struct mschmd_file *spaninfo;
};

/**
 * A structure which represents a CHM helpfile.
 * 
 * All fields are READ ONLY.
 */
struct mschmd_header {
  /** The version of the CHM file format used in this file. */
  unsigned int version;

  /**
   * The "timestamp" of the CHM helpfile. 
   *
   * It is the lower 32 bits of a 64-bit value representing the number of
   * centiseconds since 1601-01-01 00:00:00 UTC, plus 42. It is not useful
   * as a timestamp, but it is useful as a semi-unique ID.
   */
  unsigned int timestamp;
      
  /**
   * The default Language and Country ID (LCID) of the user who ran the
   * HTMLHelp Compiler. This is not the language of the CHM file itself.
   */
  unsigned int language;

  /**
   * The filename of the CHM helpfile. This is given by the library user
   * and may be in any format.
   */
  const char *filename;

  /** The length of the CHM helpfile, in bytes. */
  off_t length;

  /** A list of all non-system files in the CHM helpfile. */
  struct mschmd_file *files;

  /**
   * A list of all system files in the CHM helpfile.
   *
   * System files are files which begin with "::". They are meta-files
   * generated by the CHM creation process.
   */
  struct mschmd_file *sysfiles;

  /** The section 0 (uncompressed) data in this CHM helpfile. */
  struct mschmd_sec_uncompressed sec0;

  /** The section 1 (MSCompressed) data in this CHM helpfile. */
  struct mschmd_sec_mscompressed sec1;

  /** The file offset of the first PMGL/PMGI directory chunk. */
  off_t dir_offset;

  /** The number of PMGL/PMGI directory chunks in this CHM helpfile. */
  unsigned int num_chunks;

  /** The size of each PMGL/PMGI chunk, in bytes. */
  unsigned int chunk_size;

  /** The "density" of the quick-reference section in PMGL/PMGI chunks. */
  unsigned int density;

  /** The depth of the index tree.
   *
   * - if 1, there are no PMGI chunks, only PMGL chunks.
   * - if 2, there is 1 PMGI chunk. All chunk indices point to PMGL chunks.
   * - if 3, the root PMGI chunk points to secondary PMGI chunks, which in
   *         turn point to PMGL chunks.
   * - and so on...
   */
  unsigned int depth;

  /**
   * The number of the root PMGI chunk.
   *
   * If there is no index in the CHM helpfile, this will be 0xFFFFFFFF.
   */
  unsigned int index_root;

  /**
   * The number of the first PMGL chunk. Usually zero.
   * Available only in CHM decoder version 2 and above.
   */
  unsigned int first_pmgl;

  /**
   * The number of the last PMGL chunk. Usually num_chunks-1.
   * Available only in CHM decoder version 2 and above.
   */
  unsigned int last_pmgl;

  /**
   * A cache of loaded chunks, filled in by mschm_decoder::fast_find().
   * Available only in CHM decoder version 2 and above.
   */
  unsigned char **chunk_cache;
};

/**
 * A structure which represents a file stored in a CHM helpfile.
 * 
 * All fields are READ ONLY.
 */
struct mschmd_file {
  /**
   * A pointer to the next file in the list, or NULL if this is the final
   * file.
   */
  struct mschmd_file *next;

  /**
   * A pointer to the section that this file is located in. Indirectly,
   * it also points to the CHM helpfile the file is located in.
   */
  struct mschmd_section *section;

  /** The offset within the section data that this file is located at. */
  off_t offset;

  /** The length of this file, in bytes */
  off_t length;

  /** The filename of this file -- a null terminated string in UTF-8. */
  char *filename;
};

/** mschmc_file::section value: end of CHM file list */
#define MSCHMC_ENDLIST   (0)
/** mschmc_file::section value: this file is in the Uncompressed section */
#define MSCHMC_UNCOMP    (1)
/** mschmc_file::section value: this file is in the MSCompressed section */
#define MSCHMC_MSCOMP    (2)
 
/** mschm_compressor::set_param() parameter: "timestamp" header */
#define MSCHMC_PARAM_TIMESTAMP  (0)
/** mschm_compressor::set_param() parameter: "language" header */
#define MSCHMC_PARAM_LANGUAGE   (1)
/** mschm_compressor::set_param() parameter: LZX window size */
#define MSCHMC_PARAM_LZXWINDOW  (2)
/** mschm_compressor::set_param() parameter: intra-chunk quickref density */
#define MSCHMC_PARAM_DENSITY    (3)
/** mschm_compressor::set_param() parameter: whether to create indices */
#define MSCHMC_PARAM_INDEX      (4)

/**
 * A compressor for .CHM (Microsoft HTMLHelp) files.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_chm_compressor(), mspack_destroy_chm_compressor()
 */
struct mschm_compressor {
  /**
   * Generates a CHM help file.
   *
   * The help file will contain up to two sections, an Uncompressed
   * section and potentially an MSCompressed (LZX compressed)
   * section.
   *
   * While the contents listing of a CHM file is always in lexical order,
   * the file list passed in will be taken as the correct order for files
   * within the sections.  It is in your interest to place similar files
   * together for better compression.
   *
   * There are two modes of generation, to use a temporary file or not to
   * use one. See use_temporary_file() for the behaviour of generate() in
   * these two different modes.
   *
   * @param  self        a self-referential pointer to the mschm_compressor
   *                     instance being called
   * @param  file_list   an array of mschmc_file structures, terminated
   *                     with an entry whose mschmc_file::section field is
   *                     #MSCHMC_ENDLIST. The order of the list is
   *                     preserved within each section. The length of any
   *                     mschmc_file::chm_filename string cannot exceed
   *                     roughly 4096 bytes. Each source file must be able
   *                     to supply as many bytes as given in the
   *                     mschmc_file::length field.
   * @param  output_file the file to write the generated CHM helpfile to.
   *                     This is passed directly to mspack_system::open()
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see use_temporary_file() set_param()
   */
  int (*generate)(struct mschm_compressor *self,
                  struct mschmc_file file_list[],
                  const char *output_file);

  /**
   * Specifies whether a temporary file is used during CHM generation.
   *
   * The CHM file format includes data about the compressed section (such
   * as its overall size) that is stored in the output CHM file prior to
   * the compressed section itself. This unavoidably requires that the
   * compressed section has to be generated, before these details can be
   * set. There are several ways this can be handled. Firstly, the
   * compressed section could be generated entirely in memory before
   * writing any of the output CHM file. This approach is not used in
   * libmspack, as the compressed section can exceed the addressable
   * memory space on most architectures.
   *
   * libmspack has two options, either to write these unknowable sections
   * with blank data, generate the compressed section, then re-open the
   * output file for update once the compressed section has been
   * completed, or to write the compressed section to a temporary file,
   * then write the entire output file at once, performing a simple
   * file-to-file copy for the compressed section.
   *
   * The simple solution of buffering the entire compressed section in
   * memory can still be used, if desired. As the temporary file's
   * filename is passed directly to mspack_system::open(), it is possible
   * for a custom mspack_system implementation to hold this file in memory,
   * without writing to a disk.
   *
   * If a temporary file is set, generate() performs the following
   * sequence of events: the temporary file is opened for writing, the
   * compression algorithm writes to the temporary file, the temporary
   * file is closed.  Then the output file is opened for writing and the
   * temporary file is re-opened for reading. The output file is written
   * and the temporary file is read from. Both files are then closed. The
   * temporary file itself is not deleted. If that is desired, the
   * temporary file should be deleted after the completion of generate(),
   * if it exists.
   *
   * If a temporary file is set not to be used, generate() performs the
   * following sequence of events: the output file is opened for writing,
   * then it is written and closed. The output file is then re-opened for
   * update, the appropriate sections are seek()ed to and re-written, then
   * the output file is closed.
   *
   * @param  self          a self-referential pointer to the
   *                       mschm_compressor instance being called
   * @param  use_temp_file non-zero if the temporary file should be used,
   *                       zero if the temporary file should not be used.
   * @param  temp_file     a file to temporarily write compressed data to,
   *                       before opening it for reading and copying the
   *                       contents to the output file. This is passed
   *                       directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see generate()
   */
  int (*use_temporary_file)(struct mschm_compressor *self,
                            int use_temp_file,
                            const char *temp_file);
  /**
   * Sets a CHM compression engine parameter.
   *
   * The following parameters are defined:

   * - #MSCHMC_PARAM_TIMESTAMP: Sets the "timestamp" of the CHM file
   *   generated. This is not a timestamp, see mschmd_header::timestamp
   *   for a description. If this timestamp is 0, generate() will use its
   *   own algorithm for making a unique ID, based on the lengths and
   *   names of files in the CHM itself. Defaults to 0, any value between
   *   0 and (2^32)-1 is valid.
   * - #MSCHMC_PARAM_LANGUAGE: Sets the "language" of the CHM file
   *   generated.  This is not the language used in the CHM file, but the
   *   language setting of the user who ran the HTMLHelp compiler. It
   *   defaults to 0x0409. The valid range is between 0x0000 and 0x7F7F.
   * - #MSCHMC_PARAM_LZXWINDOW: Sets the size of the LZX history window,
   *   which is also the interval at which the compressed data stream can be
   *   randomly accessed. The value is not a size in bytes, but a power of
   *   two. The default value is 16 (which makes the window 2^16 bytes, or
   *   64 kilobytes), the valid range is from 15 (32 kilobytes) to 21 (2
   *   megabytes).
   * - #MSCHMC_PARAM_DENSITY: Sets the "density" of quick reference
   *   entries stored at the end of directory listing chunk. Each chunk is
   *   4096 bytes in size, and contains as many file entries as there is
   *   room for. At the other end of the chunk, a list of "quick reference"
   *   pointers is included. The offset of every 'N'th file entry is given a
   *   quick reference, where N = (2^density) + 1. The default density is
   *   2. The smallest density is 0 (N=2), the maximum is 10 (N=1025). As
   *   each file entry requires at least 5 bytes, the maximum number of
   *   entries in a single chunk is roughly 800, so the maximum value 10
   *   can be used to indicate there are no quickrefs at all.
   * - #MSCHMC_PARAM_INDEX: Sets whether or not to include quick lookup
   *   index chunk(s), in addition to normal directory listing chunks. A
   *   value of zero means no index chunks will be created, a non-zero value
   *   means index chunks will be created. The default is zero, "don't
   *   create an index".
   *
   * @param  self     a self-referential pointer to the mschm_compressor
   *                  instance being called
   * @param  param    the parameter to set
   * @param  value    the value to set the parameter to
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS if there
   *         is a problem with either parameter or value.
   * @see generate()
   */
  int (*set_param)(struct mschm_compressor *self,
                   int param,
                   unsigned int value);

  /**
   * Returns the error code set by the most recently called method.
   *
   * @param  self     a self-referential pointer to the mschm_compressor
   *                  instance being called
   * @return the most recent error code
   * @see set_param(), generate()
   */
  int (*last_error)(struct mschm_compressor *self);
};

/**
 * A decompressor for .CHM (Microsoft HTMLHelp) files
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_chm_decompressor(), mspack_destroy_chm_decompressor()
 */
struct mschm_decompressor {
  /**
   * Opens a CHM helpfile and reads its contents.
   *
   * If the file opened is a valid CHM helpfile, all headers will be read
   * and a mschmd_header structure will be returned, with a full list of
   * files.
   *
   * In the case of an error occuring, NULL is returned and the error code
   * is available from last_error().
   *
   * The filename pointer should be considered "in use" until close() is
   * called on the CHM helpfile.
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @param  filename the filename of the CHM helpfile. This is passed
   *                  directly to mspack_system::open().
   * @return a pointer to a mschmd_header structure, or NULL on failure
   * @see close()
   */
  struct mschmd_header *(*open)(struct mschm_decompressor *self,
                                const char *filename);

  /**
   * Closes a previously opened CHM helpfile.
   *
   * This closes a CHM helpfile, frees the mschmd_header and all
   * mschmd_file structures associated with it (if any). This works on
   * both helpfiles opened with open() and helpfiles opened with
   * fast_open().
   *
   * The CHM header pointer is now invalid and cannot be used again. All
   * mschmd_file pointers referencing that CHM are also now invalid, and
   * cannot be used again.
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @param  chm      the CHM helpfile to close
   * @see open(), fast_open()
   */
  void (*close)(struct mschm_decompressor *self,
                struct mschmd_header *chm);

  /**
   * Extracts a file from a CHM helpfile.
   *
   * This extracts a file from a CHM helpfile and writes it to the given
   * filename. The filename of the file, mscabd_file::filename, is not
   * used by extract(), but can be used by the caller as a guide for
   * constructing an appropriate filename.
   *
   * This method works both with files found in the mschmd_header::files
   * and mschmd_header::sysfiles list and mschmd_file structures generated
   * on the fly by fast_find().
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @param  file     the file to be decompressed
   * @param  filename the filename of the file being written to
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*extract)(struct mschm_decompressor *self,
                 struct mschmd_file *file,
                 const char *filename);

  /**
   * Returns the error code set by the most recently called method.
   *
   * This is useful for open() and fast_open(), which do not return an
   * error code directly.
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @return the most recent error code
   * @see open(), extract()
   */
  int (*last_error)(struct mschm_decompressor *self);

  /**
   * Opens a CHM helpfile quickly.
   *
   * If the file opened is a valid CHM helpfile, only essential headers
   * will be read. A mschmd_header structure will be still be returned, as
   * with open(), but the mschmd_header::files field will be NULL. No
   * files details will be automatically read.  The fast_find() method
   * must be used to obtain file details.
   *
   * In the case of an error occuring, NULL is returned and the error code
   * is available from last_error().
   *
   * The filename pointer should be considered "in use" until close() is
   * called on the CHM helpfile.
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @param  filename the filename of the CHM helpfile. This is passed
   *                  directly to mspack_system::open().
   * @return a pointer to a mschmd_header structure, or NULL on failure
   * @see open(), close(), fast_find(), extract()
   */
  struct mschmd_header *(*fast_open)(struct mschm_decompressor *self,
                                     const char *filename);

  /**
   * Finds file details quickly.
   *
   * Instead of reading all CHM helpfile headers and building a list of
   * files, fast_open() and fast_find() are intended for finding file
   * details only when they are needed. The CHM file format includes an
   * on-disk file index to allow this.
   *
   * Given a case-sensitive filename, fast_find() will search the on-disk
   * index for that file.
   *
   * If the file was found, the caller-provided mschmd_file structure will
   * be filled out like so:
   * - section: the correct value for the found file
   * - offset: the correct value for the found file
   * - length: the correct value for the found file
   * - all other structure elements: NULL or 0
   *
   * If the file was not found, MSPACK_ERR_OK will still be returned as the
   * result, but the caller-provided structure will be filled out like so:
   * - section: NULL
   * - offset: 0
   * - length: 0
   * - all other structure elements: NULL or 0
   *
   * This method is intended to be used in conjunction with CHM helpfiles
   * opened with fast_open(), but it also works with helpfiles opened
   * using the regular open().
   *
   * @param  self     a self-referential pointer to the mschm_decompressor
   *                  instance being called
   * @param  chm      the CHM helpfile to search for the file
   * @param  filename the filename of the file to search for
   * @param  f_ptr    a pointer to a caller-provded mschmd_file structure
   * @param  f_size   <tt>sizeof(struct mschmd_file)</tt>
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see open(), close(), fast_find(), extract()
   */
  int (*fast_find)(struct mschm_decompressor *self,
                   struct mschmd_header *chm,
                   const char *filename,
                   struct mschmd_file *f_ptr,
                   int f_size);
};

/* --- support for .LIT (EBook) file format -------------------------------- */

/** TODO */
struct mslit_compressor {
  int dummy; 
};

/** TODO */
struct mslit_decompressor {
  int dummy; 
};


/* --- support for .HLP (MS Help) file format ------------------------------ */

/** TODO */
struct mshlp_compressor {
  int dummy; 
};

/** TODO */
struct mshlp_decompressor {
  int dummy; 
};


/* --- support for SZDD file format ---------------------------------------- */

/** msszdd_compressor::set_param() parameter: the missing character */
#define MSSZDDC_PARAM_MISSINGCHAR (0)

/** msszddd_header::format value - a regular SZDD file */
#define MSSZDD_FMT_NORMAL (0)

/** msszddd_header::format value - a special QBasic SZDD file */
#define MSSZDD_FMT_QBASIC (1)

/**
 * A structure which represents an SZDD compressed file.
 *
 * All fields are READ ONLY.
 */
struct msszddd_header {
  /** The file format; either #MSSZDD_FMT_NORMAL or #MSSZDD_FMT_QBASIC */
  int format;

  /** The amount of data in the SZDD file once uncompressed. */
  off_t length;

  /**
   * The last character in the filename, traditionally replaced with an
   * underscore to show the file is compressed. The null character is used
   * to show that this character has not been stored (e.g. because the
   * filename is not known). Generally, only characters that may appear in
   * an MS-DOS filename (except ".") are valid.
   */
  char missing_char;
};

/**
 * A compressor for the SZDD file format.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_szdd_compressor(), mspack_destroy_szdd_compressor()
 */
struct msszdd_compressor {
  /**
   * Reads an input file and creates a compressed output file in the
   * SZDD compressed file format. The SZDD compression format is quick
   * but gives poor compression. It is possible for the compressed output
   * file to be larger than the input file.
   *
   * Conventionally, SZDD compressed files have the final character in
   * their filename replaced with an underscore, to show they are
   * compressed.  The missing character is stored in the compressed file
   * itself. This is due to the restricted filename conventions of MS-DOS,
   * most operating systems, such as UNIX, simply append another file
   * extension to the existing filename. As mspack does not deal with
   * filenames, this is left up to you. If you wish to set the missing
   * character stored in the file header, use set_param() with the
   * #MSSZDDC_PARAM_MISSINGCHAR parameter.
   *
   * "Stream" compression (where the length of the input data is not
   * known) is not possible. The length of the input data is stored in the
   * header of the SZDD file and must therefore be known before any data
   * is compressed. Due to technical limitations of the file format, the
   * maximum size of uncompressed file that will be accepted is 2147483647
   * bytes.
   *
   * @param  self    a self-referential pointer to the msszdd_compressor
   *                 instance being called
   * @param  input   the name of the file to compressed. This is passed
   *                 passed directly to mspack_system::open()
   * @param  output  the name of the file to write compressed data to.
   *                 This is passed directly to mspack_system::open().
   * @param  length  the length of the uncompressed file, or -1 to indicate
   *                 that this should be determined automatically by using
   *                 mspack_system::seek() on the input file.
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see set_param()
   */
  int (*compress)(struct msszdd_compressor *self,
                  const char *input,
                  const char *output,
                  off_t length);

  /**
   * Sets an SZDD compression engine parameter.
   *
   * The following parameters are defined:

   * - #MSSZDDC_PARAM_CHARACTER: the "missing character", the last character
   *   in the uncompressed file's filename, which is traditionally replaced
   *   with an underscore to show the file is compressed. Traditionally,
   *   this can only be a character that is a valid part of an MS-DOS,
   *   filename, but libmspack permits any character between 0x00 and 0xFF
   *   to be stored. 0x00 is the default, and it represents "no character
   *   stored".
   *
   * @param  self     a self-referential pointer to the msszdd_compressor
   *                  instance being called
   * @param  param    the parameter to set
   * @param  value    the value to set the parameter to
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS if there
   *         is a problem with either parameter or value.
   * @see compress()
   */
  int (*set_param)(struct msszdd_compressor *self,
                   int param,
                   unsigned int value);

  /**
   * Returns the error code set by the most recently called method.
   *
   * @param  self     a self-referential pointer to the msszdd_compressor
   *                  instance being called
   * @return the most recent error code
   * @see compress()
   */
  int (*last_error)(struct mschm_decompressor *self);
};

/**
 * A decompressor for SZDD compressed files.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_szdd_decompressor(), mspack_destroy_szdd_decompressor()
 */
struct msszdd_decompressor {
  /**
   * Opens a SZDD file and reads the header.
   *
   * If the file opened is a valid SZDD file, all headers will be read and
   * a msszddd_header structure will be returned.
   *
   * In the case of an error occuring, NULL is returned and the error code
   * is available from last_error().
   *
   * The filename pointer should be considered "in use" until close() is
   * called on the SZDD file.
   *
   * @param  self     a self-referential pointer to the msszdd_decompressor
   *                  instance being called
   * @param  filename the filename of the SZDD compressed file. This is
   *                  passed directly to mspack_system::open().
   * @return a pointer to a msszddd_header structure, or NULL on failure
   * @see close()
   */
  struct msszddd_header *(*open)(struct msszdd_decompressor *self,
                                 const char *filename);

  /**
   * Closes a previously opened SZDD file.
   *
   * This closes a SZDD file and frees the msszddd_header associated with
   * it.
   *
   * The SZDD header pointer is now invalid and cannot be used again.
   *
   * @param  self     a self-referential pointer to the msszdd_decompressor
   *                  instance being called
   * @param  szdd     the SZDD file to close
   * @see open()
   */
  void (*close)(struct msszdd_decompressor *self,
                struct msszddd_header *szdd);

  /**
   * Extracts the compressed data from a SZDD file.
   *
   * This decompresses the compressed SZDD data stream and writes it to
   * an output file.
   *
   * @param  self     a self-referential pointer to the msszdd_decompressor
   *                  instance being called
   * @param  szdd     the SZDD file to extract data from
   * @param  filename the filename to write the decompressed data to. This
   *                  is passed directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*extract)(struct msszdd_decompressor *self,
                 struct msszddd_header *szdd,
                 const char *filename);

  /**
   * Decompresses an SZDD file to an output file in one step.
   *
   * This opens an SZDD file as input, reads the header, then decompresses
   * the compressed data immediately to an output file, finally closing
   * both the input and output file. It is more convenient to use than
   * open() then extract() then close(), if you do not need to know the
   * SZDD output size or missing character.
   *
   * @param  self     a self-referential pointer to the msszdd_decompressor
   *                  instance being called
   * @param  input    the filename of the input SZDD file. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename to write the decompressed data to. This
   *                  is passed directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*decompress)(struct msszdd_decompressor *self,
                    const char *input,
                    const char *output);

  /**
   * Returns the error code set by the most recently called method.
   *
   * This is useful for open() which does not return an
   * error code directly.
   *
   * @param  self     a self-referential pointer to the msszdd_decompressor
   *                  instance being called
   * @return the most recent error code
   * @see open(), extract(), decompress()
   */
  int (*last_error)(struct msszdd_decompressor *self);
};

/* --- support for KWAJ file format ---------------------------------------- */

/** mskwaj_compressor::set_param() parameter: compression type */
#define MSKWAJC_PARAM_COMP_TYPE  (0)

/** mskwaj_compressor::set_param() parameter: include the length of the
 * uncompressed file in the header?
 */
#define MSKWAJC_PARAM_INCLUDE_LENGTH (1)

/** KWAJ compression type: no compression. */
#define MSKWAJ_COMP_NONE (0)
/** KWAJ compression type: no compression, 0xFF XOR "encryption". */
#define MSKWAJ_COMP_XOR (1)
/** KWAJ compression type: LZSS (same method as SZDD) */
#define MSKWAJ_COMP_SZDD (2)
/** KWAJ compression type: LZ+Huffman compression */
#define MSKWAJ_COMP_LZH (3)
/** KWAJ compression type: MSZIP */
#define MSKWAJ_COMP_MSZIP (4)

/** KWAJ optional header flag: decompressed file length is included */
#define MSKWAJ_HDR_HASLENGTH (0x01)

/** KWAJ optional header flag: unknown 2-byte structure is included */
#define MSKWAJ_HDR_HASUNKNOWN1 (0x02)

/** KWAJ optional header flag: unknown multi-sized structure is included */
#define MSKWAJ_HDR_HASUNKNOWN2 (0x04)

/** KWAJ optional header flag: file name (no extension) is included */
#define MSKWAJ_HDR_HASFILENAME (0x08)

/** KWAJ optional header flag: file extension is included */
#define MSKWAJ_HDR_HASFILEEXT (0x10)

/** KWAJ optional header flag: extra text is included */
#define MSKWAJ_HDR_HASEXTRATEXT (0x20)

/**
 * A structure which represents an KWAJ compressed file.
 *
 * All fields are READ ONLY.
 */
struct mskwajd_header {
  /** The compression type; should be one of #MSKWAJ_COMP_NONE,
   * #MSKWAJ_COMP_XOR, #MSKWAJ_COMP_SZDD or #MSKWAJ_COMP_LZH
   */
  unsigned short comp_type;

  /** The offset in the file where the compressed data stream begins */
  off_t data_offset;

  /** Flags indicating which optional headers were included. */
  int headers;

  /** The amount of uncompressed data in the file, or 0 if not present. */
  off_t length;

  /** output filename, or NULL if not present */
  char *filename;

  /** extra uncompressed data (usually text) in the header.
   * This data can contain nulls so use extra_length to get the size.
   */
  char *extra;

  /** length of extra uncompressed data in the header */
  unsigned short extra_length;
};

/**
 * A compressor for the KWAJ file format.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_kwaj_compressor(), mspack_destroy_kwaj_compressor()
 */
struct mskwaj_compressor {
  /**
   * Reads an input file and creates a compressed output file in the
   * KWAJ compressed file format. The KWAJ compression format is quick
   * but gives poor compression. It is possible for the compressed output
   * file to be larger than the input file.
   *
   * @param  self    a self-referential pointer to the mskwaj_compressor
   *                 instance being called
   * @param  input   the name of the file to compressed. This is passed
   *                 passed directly to mspack_system::open()
   * @param  output  the name of the file to write compressed data to.
   *                 This is passed directly to mspack_system::open().
   * @param  length  the length of the uncompressed file, or -1 to indicate
   *                 that this should be determined automatically by using
   *                 mspack_system::seek() on the input file.
   * @return an error code, or MSPACK_ERR_OK if successful
   * @see set_param()
   */
  int (*compress)(struct mskwaj_compressor *self,
                  const char *input,
                  const char *output,
                  off_t length);

  /**
   * Sets an KWAJ compression engine parameter.
   *
   * The following parameters are defined:
   *
   * - #MSKWAJC_PARAM_COMP_TYPE: the compression method to use. Must
   *   be one of #MSKWAJC_COMP_NONE, #MSKWAJC_COMP_XOR, #MSKWAJ_COMP_SZDD
   *   or #MSKWAJ_COMP_LZH. The default is #MSKWAJ_COMP_LZH.
   *
   * - #MSKWAJC_PARAM_INCLUDE_LENGTH: a boolean; should the compressed
   *   output file should include the uncompressed length of the input
   *   file in the header? This adds 4 bytes to the size of the output
   *   file. A value of zero says "no", non-zero says "yes". The default
   *   is "no".
   *
   * @param  self     a self-referential pointer to the mskwaj_compressor
   *                  instance being called
   * @param  param    the parameter to set
   * @param  value    the value to set the parameter to
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS if there
   *         is a problem with either parameter or value.
   * @see generate()
   */
  int (*set_param)(struct mskwaj_compressor *self,
                   int param,
                   unsigned int value);


  /**
   * Sets the original filename of the file before compression,
   * which will be stored in the header of the output file.
   *
   * The filename should be a null-terminated string, it must be an
   * MS-DOS "8.3" type filename (up to 8 bytes for the filename, then
   * optionally a "." and up to 3 bytes for a filename extension).
   *
   * If NULL is passed as the filename, no filename is included in the
   * header. This is the default.
   *
   * @param  self     a self-referential pointer to the mskwaj_compressor
   *                  instance being called
   * @param  filename the original filename to use
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS if the
   *         filename is too long
   */
  int (*set_filename)(struct mskwaj_compressor *self,
                      const char *filename);

  /**
   * Sets arbitrary data that will be stored in the header of the
   * output file, uncompressed. It can be up to roughly 64 kilobytes,
   * as the overall size of the header must not exceed 65535 bytes.
   * The data can contain null bytes if desired.
   *
   * If NULL is passed as the data pointer, or zero is passed as the
   * length, no extra data is included in the header. This is the
   * default.
   *
   * @param  self     a self-referential pointer to the mskwaj_compressor
   *                  instance being called
   * @param  data     a pointer to the data to be stored in the header
   * @param  bytes    the length of the data in bytes
   * @return MSPACK_ERR_OK if all is OK, or MSPACK_ERR_ARGS extra data
   *         is too long
   */
  int (*set_extra_data)(struct mskwaj_compressor *self,
                        void *data,
                        size_t bytes);

  /**
   * Returns the error code set by the most recently called method.
   *
   * @param  self     a self-referential pointer to the mskwaj_compressor
   *                  instance being called
   * @return the most recent error code
   * @see compress()
   */
  int (*last_error)(struct mschm_decompressor *self);
};

/**
 * A decompressor for KWAJ compressed files.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_kwaj_decompressor(), mspack_destroy_kwaj_decompressor()
 */
struct mskwaj_decompressor {
  /**
   * Opens a KWAJ file and reads the header.
   *
   * If the file opened is a valid KWAJ file, all headers will be read and
   * a mskwajd_header structure will be returned.
   *
   * In the case of an error occuring, NULL is returned and the error code
   * is available from last_error().
   *
   * The filename pointer should be considered "in use" until close() is
   * called on the KWAJ file.
   *
   * @param  self     a self-referential pointer to the mskwaj_decompressor
   *                  instance being called
   * @param  filename the filename of the KWAJ compressed file. This is
   *                  passed directly to mspack_system::open().
   * @return a pointer to a mskwajd_header structure, or NULL on failure
   * @see close()
   */
  struct mskwajd_header *(*open)(struct mskwaj_decompressor *self,
                                 const char *filename);

  /**
   * Closes a previously opened KWAJ file.
   *
   * This closes a KWAJ file and frees the mskwajd_header associated
   * with it. The KWAJ header pointer is now invalid and cannot be
   * used again.
   *
   * @param  self     a self-referential pointer to the mskwaj_decompressor
   *                  instance being called
   * @param  kwaj     the KWAJ file to close
   * @see open()
   */
  void (*close)(struct mskwaj_decompressor *self,
                struct mskwajd_header *kwaj);

  /**
   * Extracts the compressed data from a KWAJ file.
   *
   * This decompresses the compressed KWAJ data stream and writes it to
   * an output file.
   *
   * @param  self     a self-referential pointer to the mskwaj_decompressor
   *                  instance being called
   * @param  kwaj     the KWAJ file to extract data from
   * @param  filename the filename to write the decompressed data to. This
   *                  is passed directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*extract)(struct mskwaj_decompressor *self,
                 struct mskwajd_header *kwaj,
                 const char *filename);

  /**
   * Decompresses an KWAJ file to an output file in one step.
   *
   * This opens an KWAJ file as input, reads the header, then decompresses
   * the compressed data immediately to an output file, finally closing
   * both the input and output file. It is more convenient to use than
   * open() then extract() then close(), if you do not need to know the
   * KWAJ output size or output filename.
   *
   * @param  self     a self-referential pointer to the mskwaj_decompressor
   *                  instance being called
   * @param  input    the filename of the input KWAJ file. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename to write the decompressed data to. This
   *                  is passed directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*decompress)(struct mskwaj_decompressor *self,
                    const char *input,
                    const char *output);

  /**
   * Returns the error code set by the most recently called method.
   *
   * This is useful for open() which does not return an
   * error code directly.
   *
   * @param  self     a self-referential pointer to the mskwaj_decompressor
   *                  instance being called
   * @return the most recent error code
   * @see open(), search()
   */
  int (*last_error)(struct mskwaj_decompressor *self);
};

/* --- support for .LZX (Offline Address Book) file format ----------------- */

/**
 * A compressor for the Offline Address Book (OAB) format.
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_oab_compressor(), mspack_destroy_oab_compressor()
 */
struct msoab_compressor {
  /**
   * Compress a full OAB file.
   *
   * The input file will be read and the compressed contents written to the
   * output file.
   *
   * @param  self     a self-referential pointer to the msoab_decompressor
   *                  instance being called
   * @param  input    the filename of the input file. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename of the output file. This is passed
   *                  directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*compress) (struct msoab_compressor *self,
                   const char *input,
                   const char *output);

  /**
   * Generate a compressed incremental OAB patch file.
   *
   * The two uncompressed files "input" and "base" will be read, and an
   * incremental patch to generate "input" from "base" will be written to
   * the output file.
   *
   * @param  self     a self-referential pointer to the msoab_compressor
   *                  instance being called
   * @param  input    the filename of the input file containing the new
   *                  version of its contents. This is passed directly
   *                  to mspack_system::open().
   * @param  base     the filename of the original base file containing
   *                  the old version of its contents, against which the
   *                  incremental patch shall generated. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename of the output file. This is passed
   *                  directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*compress_incremental) (struct msoab_compressor *self,
                               const char *input,
                               const char *base,
                               const char *output);
};

/**
 * A decompressor for .LZX (Offline Address Book) files
 *
 * All fields are READ ONLY.
 *
 * @see mspack_create_oab_decompressor(), mspack_destroy_oab_decompressor()
 */
struct msoab_decompressor {
  /**
   * Decompresses a full Offline Address Book file.
   *
   * If the input file is a valid compressed Offline Address Book file, 
   * it will be read and the decompressed contents will be written to
   * the output file.
   *
   * @param  self     a self-referential pointer to the msoab_decompressor
   *                  instance being called
   * @param  input    the filename of the input file. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename of the output file. This is passed
   *                  directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*decompress) (struct msoab_decompressor *self,
                     const char *input,
                     const char *output);

  /**
   * Decompresses an Offline Address Book with an incremental patch file.
   *
   * This requires both a full UNCOMPRESSED Offline Address Book file to
   * act as the "base", and a compressed incremental patch file as input.
   * If the input file is valid, it will be decompressed with reference to
   * the base file, and the decompressed contents will be written to the
   * output file.
   *
   * There is no way to tell what the right base file is for the given
   * incremental patch, but if you get it wrong, this will usually result
   * in incorrect data being decompressed, which will then fail a checksum
   * test.
   *
   * @param  self     a self-referential pointer to the msoab_decompressor
   *                  instance being called
   * @param  input    the filename of the input file. This is passed
   *                  directly to mspack_system::open().
   * @param  base     the filename of the base file to which the
   *                  incremental patch shall be applied. This is passed
   *                  directly to mspack_system::open().
   * @param  output   the filename of the output file. This is passed
   *                  directly to mspack_system::open().
   * @return an error code, or MSPACK_ERR_OK if successful
   */
  int (*decompress_incremental) (struct msoab_decompressor *self,
                                 const char *input,
                                 const char *base,
                                 const char *output);
};

#ifdef __cplusplus
}
#endif

#endif
