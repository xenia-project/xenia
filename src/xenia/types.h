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

#include <xenia/platform.h>


#define XE_EMPTY_MACRO          do { } while(0)

#define XEUNREFERENCED(expr)    (void)(expr)

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
#define XEASSUME(expr)          __analysis_assume(expr)
#else
#define XEASSUME(expr)
#endif  // MSVC

#if XE_COMPILER_MSVC
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

#if !defined(MIN)
#if XE_COMPILER_GNUC
#define MIN(A, B)               ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); (__a < __b) ? __a : __b; })
#define MAX(A, B)               ({ __typeof__(A) __a = (A); __typeof__(B) __b = (B); (__a < __b) ? __b : __a; })
//#elif XE_COMPILER_MSVC
// TODO(benvanik): experiment with decltype:
//     http://msdn.microsoft.com/en-us/library/dd537655.aspx
#else
// NOTE: this implementation will evaluate the arguments twice - may be worth
// writing an inline function instead.
#define MIN(A, B)               ( ((A) < (B)) ? (A) : (B) )
#define MAX(A, B)               ( ((A) < (B)) ? (B) : (A) )
#endif  // GNUC
#endif  // !MIN

XEFORCEINLINE size_t hash_combine(size_t seed) {
  return seed;
}
template <typename T, typename... Ts>
size_t hash_combine(size_t seed, const T& v, const Ts&... vs) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
  return hash_combine(seed, vs...);
}

#if XE_PLATFORM_WIN32
#define XESAFERELEASE(p)        if (p) { p->Release(); }
#endif  // WIN32

#define XEBITMASK(a, b) (((unsigned) -1 >> (31 - (b))) & ~((1U << (a)) - 1))
#define XESELECTBITS(value, a, b) ((value & XEBITMASK(a, b)) >> a)

#define XEROUNDUP(v, multiple)  ((v) + (multiple) - 1 - ((v) - 1) % (multiple))
static inline uint32_t XENEXTPOW2(uint32_t v) {
  v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++; return v;
}
#define XEALIGN(value, align) ((value + align - 1) & ~(align - 1))

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
