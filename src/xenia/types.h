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

#include <cstdint>
#include <functional>

#include <poly/platform.h>


#define XE_EMPTY_MACRO          do { } while(0)

#define XEDECLARECLASS1(ns1, name) \
    namespace ns1 { class name; }
#define XEDECLARECLASS2(ns1, ns2, name) \
    namespace ns1 { namespace ns2 { \
      class name; \
    } }
#define XEDECLARECLASS3(ns1, ns2, ns3, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { \
      class name; \
    } } }
#define XEDECLARECLASS4(ns1, ns2, ns3, ns4, name) \
    namespace ns1 { namespace ns2 { namespace ns3 { namespace ns4 { \
      class name; \
    } } } }

#if XE_COMPILER_MSVC
// http://msdn.microsoft.com/en-us/library/z8y1yy88.aspx
#define XEFORCEINLINE           static __forceinline
#define XENOINLINE              __declspec(noinline)
#elif XE_COMPILER_GNUC
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

#if XE_COMPILER_MSVC
#define XEPACKEDSTRUCT(name, value) \
    __pragma(pack(push, 1)) struct name##_s value __pragma(pack(pop)); \
    typedef struct name##_s name;
#define XEPACKEDSTRUCTANONYMOUS(value) \
    __pragma(pack(push, 1)) struct value __pragma(pack(pop));
#define XEPACKEDUNION(name, value) \
    __pragma(pack(push, 1)) union name##_s value __pragma(pack(pop)); \
    typedef union name##_s name;
#elif XE_COMPILER_GNUC
#define XEPACKEDSTRUCT(name, value) \
    struct __attribute__((packed)) name
#define XEPACKEDSTRUCTANONYMOUS(value) \
    struct __attribute__((packed))
#define XEPACKEDUNION(name, value) \
    union __attribute__((packed)) name
#endif  // MSVC

#if XE_COMPILER_MSVC
// http://msdn.microsoft.com/en-us/library/83ythb65.aspx
#define XECACHEALIGN            __declspec(align(XE_ALIGNMENT))
#define XECACHEALIGN64          __declspec(align(64))
#elif XE_COMPILER_GNUC
// http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
#define XECACHEALIGN            __attribute__ ((aligned(XE_ALIGNMENT)))
#define XECACHEALIGN64          __attribute__ ((aligned(64)))
#else
#define XECACHEALIGN
#define XECACHEALIGN64
#endif  // MSVC
typedef XECACHEALIGN volatile void xe_aligned_void_t;

#if XE_COMPILER_MSVC
// http://msdn.microsoft.com/en-us/library/ms175773.aspx
#define XECOUNT(array)          _countof(array)
#elif XE_COMPILER_GNUC
#define XECOUNT(array)          (sizeof(array) / sizeof(__typeof__(array[0])))
#else
#define XECOUNT(array)          (sizeof(array) / sizeof(array[0]))
#endif  // MSVC

#define XEFAIL()                goto XECLEANUP
#define XEEXPECT(expr)          if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTTRUE(expr)      if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTFALSE(expr)     if ( (expr)         ) { goto XECLEANUP; }
#define XEEXPECTZERO(expr)      if ( (expr) != 0    ) { goto XECLEANUP; }
#define XEEXPECTNOTZERO(expr)   if ( (expr) == 0    ) { goto XECLEANUP; }
#define XEEXPECTNULL(expr)      if ( (expr) != NULL ) { goto XECLEANUP; }
#define XEEXPECTNOTNULL(expr)   if ( (expr) == NULL ) { goto XECLEANUP; }


#endif  // XENIA_TYPES_H_
