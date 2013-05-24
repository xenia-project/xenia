/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_TYPES_H_
#define XENIA_TYPES_H_

#include <xenia/platform.h>
#include <xenia/platform_includes.h>

namespace xe {
// TODO(benvanik): support other compilers/etc
using std::auto_ptr;
using std::tr1::shared_ptr;
}  // namespace xe


#define XE_EMPTY_MACRO          do { } while(0)

#define XEUNREFERENCED(expr)    (void)(expr)

#if XE_COMPILER(MSVC)
#define XEASSUME(expr)          __analysis_assume(expr)
#else
#define XEASSUME(expr)
#endif  // MSVC

#if XE_COMPILER(MSVC)
#define XECDECL                 __cdecl
#else
#define XECDECL
#endif  // MSVC

#if defined(__cplusplus)
#define XEEXTERNC               extern "C"
#define XEEXTERNC_BEGIN         extern "C" {
#define XEEXTERNC_END           }
#else
#define XEEXTERNC               extern
#define XEEXTERNC_BEGIN
#define XEEXTERNC_END
#endif  // __cplusplus

#if XE_COMPILER(MSVC)
// http://msdn.microsoft.com/en-us/library/z8y1yy88.aspx
#define XEFORCEINLINE           static __forceinline
#define XENOINLINE              __declspec(noinline)
#elif XE_COMPILER(GNUC)
// http://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html
#if (__GNUC__ >= 4)
#define XEFORCEINLINE           static __inline__ __attribute__((always_inline))
#else
#define XEFORCEINLINE           static __inline__
#endif
#define XENOINLINE
#else
#define XEFORCEINLINE
#define XENOINLINE
#endif  // MSVC

#if XE_COMPILER(MSVC)
// http://msdn.microsoft.com/en-us/library/83ythb65.aspx
#define XECACHEALIGN            __declspec(align(XE_ALIGNMENT))
#define XECACHEALIGN64          __declspec(align(64))
#elif XE_COMPILER(GNUC)
// http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
#define XECACHEALIGN            __attribute__ ((aligned(XE_ALIGNMENT)))
#define XECACHEALIGN64          __attribute__ ((aligned(64)))
#else
#define XECACHEALIGN
#define XECACHEALIGN64
#endif  // MSVC
typedef XECACHEALIGN volatile void xe_aligned_void_t;

#if XE_COMPILER(MSVC)
// http://msdn.microsoft.com/en-us/library/ms175773.aspx
#define XECOUNT(array)          _countof(array)
#elif XE_COMPILER(GNUC)
#define XECOUNT(array)          (sizeof(array) / sizeof(__typeof__(array[0])))
#else
#define XECOUNT(array)          (sizeof(array) / sizeof(array[0]))
#endif  // MSVC

#if !defined(MIN)
#if XE_COMPILER(GNUC)
#define MIN(A, B)               ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); (__a < __b) ? __a : __b; })
#define MAX(A, B)               ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); (__a < __b) ? __b : __a; })
//#elif XE_COMPILER(MSVC)
// TODO(benvanik): experiment with decltype:
//     http://msdn.microsoft.com/en-us/library/dd537655.aspx
#else
// NOTE: this implementation will evaluate the arguments twice - may be worth
// writing an inline function instead.
#define MIN(A, B)               ( ((A) < (B)) ? (A) : (B) )
#define MAX(A, B)               ( ((A) < (B)) ? (B) : (A) )
#endif  // GNUC
#endif  // !MIN

#define XEBITMASK(a, b) (((unsigned) -1 >> (31 - (b))) & ~((1U << (a)) - 1))
#define XESELECTBITS(value, a, b) ((value & XEBITMASK(a, b)) >> a)

#define XESUCCEED()             goto XECLEANUP
#define XEFAIL()                goto XECLEANUP
#define XEEXPECT(expr)          if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTTRUE(expr)      if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTFALSE(expr)     if ( (expr)         ) { goto XECLEANUP; }
#define XEEXPECTZERO(expr)      if ( (expr) != 0    ) { goto XECLEANUP; }
#define XEEXPECTNOTZERO(expr)   if ( (expr) == 0    ) { goto XECLEANUP; }
#define XEEXPECTNULL(expr)      if ( (expr) != NULL ) { goto XECLEANUP; }
#define XEEXPECTNOTNULL(expr)   if ( (expr) == NULL ) { goto XECLEANUP; }
#define XEIGNORE(expr)          do { (void)(expr); } while(0)


#endif  // XENIA_TYPES_H_
