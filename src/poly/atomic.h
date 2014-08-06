/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_ATOMIC_H_
#define POLY_ATOMIC_H_

#include <cstdint>

#include <poly/config.h>
#include <poly/platform.h>

#if XE_LIKE_OSX
#include <libkern/OSAtomic.h>
#endif  // XE_LIKE_OSX

namespace poly {

// These functions are modeled off of the Apple OSAtomic routines
// http://developer.apple.com/library/mac/#documentation/DriversKernelHardware/Reference/libkern_ref/OSAtomic_h/

#if XE_LIKE_OSX

inline int32_t atomic_inc(volatile int32_t* value) {
  return OSAtomicIncrement32Barrier(reinterpret_cast<volatile int32_t*>(value));
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return OSAtomicDecrement32Barrier(reinterpret_cast<volatile int32_t*>(value));
}

inline int32_t atomic_exchange(int32_t new_value, volatile int32_t* value) {
  return OSAtomicCompareAndSwap32Barrier(*value, new_value, value);
}
inline int64_t atomic_exchange(int64_t new_value, volatile int64_t* value) {
  return OSAtomicCompareAndSwap64Barrier(*value, new_value, value);
}

inline bool atomic_cas(int32_t old_value, int32_t new_value,
                       volatile int32_t* value) {
  return OSAtomicCompareAndSwap32Barrier(
      old_value, new_value, reinterpret_cast<volatile int32_t*>(value));
}
inline bool atomic_cas(int64_t old_value, int64_t new_value,
                       volatile int32_t* value) {
  return OSAtomicCompareAndSwap64Barrier(
      old_value, new_value, reinterpret_cast<volatile int64_t*>(value));
}

#elif XE_LIKE_WIN32

inline int32_t atomic_inc(volatile int32_t* value) {
  return InterlockedIncrement(reinterpret_cast<volatile LONG*>(value));
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return InterlockedDecrement(reinterpret_cast<volatile LONG*>(value));
}

inline int32_t atomic_exchange(int32_t new_value, volatile int32_t* value) {
  return InterlockedExchange(reinterpret_cast<volatile LONG*>(value),
                             new_value);
}
inline int64_t atomic_exchange(int64_t new_value, volatile int64_t* value) {
  return InterlockedExchange64(reinterpret_cast<volatile LONGLONG*>(value),
                               new_value);
}

inline bool atomic_cas(int32_t old_value, int32_t new_value,
                       volatile int32_t* value) {
  return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(value),
                                    new_value, old_value) == old_value;
}
inline bool atomic_cas(int64_t old_value, int64_t new_value,
                       volatile int64_t* value) {
  return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(value),
                                      new_value, old_value) == old_value;
}

#elif XE_LIKE_POSIX

inline int32_t atomic_inc(volatile int32_t* value) {
  return __sync_add_and_fetch(value, 1);
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return __sync_sub_and_fetch(value, 1);
}

inline int32_t atomic_exchange(int32_t new_value, volatile int32_t* value) {
  return __sync_val_compare_and_swap(*value, value, new_value);
}
inline int64_t atomic_exchange(int64_t new_value, volatile int64_t* value) {
  return __sync_val_compare_and_swap(*value, value, new_value);
}

inline bool atomic_cas(int32_t old_value, int32_t new_value,
                       volatile int32_t* value) {
  return __sync_bool_compare_and_swap(
      reinterpret_cast<volatile int32_t*>(value), old_value, new_value);
}
inline bool atomic_cas(int64_t old_value, int64_t new_value,
                       volatile int64_t* value) {
  return __sync_bool_compare_and_swap(
      reinterpret_cast<volatile int64_t*>(value), old_value, new_value);
}

#else

#error No atomic primitives defined for this platform/cpu combination.

#endif  // OSX

inline uint32_t atomic_inc(volatile uint32_t* value) {
  return static_cast<uint32_t>(
      atomic_inc(reinterpret_cast<volatile int32_t*>(value)));
}
inline uint32_t atomic_dec(volatile uint32_t* value) {
  return static_cast<uint32_t>(
      atomic_dec(reinterpret_cast<volatile int32_t*>(value)));
}

inline uint32_t atomic_exchange(uint32_t new_value, volatile uint32_t* value) {
  return static_cast<uint32_t>(
      atomic_exchange(static_cast<int32_t>(new_value),
                      reinterpret_cast<volatile int32_t*>(value)));
}
inline uint64_t atomic_exchange(uint64_t new_value, volatile uint64_t* value) {
  return static_cast<uint64_t>(
      atomic_exchange(static_cast<int64_t>(new_value),
                      reinterpret_cast<volatile int64_t*>(value)));
}

inline bool atomic_cas(uint32_t old_value, uint32_t new_value,
                       volatile uint32_t* value) {
  return atomic_cas(static_cast<int32_t>(old_value),
                    static_cast<int32_t>(new_value),
                    reinterpret_cast<volatile int32_t*>(value));
}
inline bool atomic_cas(uint64_t old_value, uint64_t new_value,
                       volatile uint64_t* value) {
  return atomic_cas(static_cast<int64_t>(old_value),
                    static_cast<int64_t>(new_value),
                    reinterpret_cast<volatile int64_t*>(value));
}

}  // namespace poly

#endif  // POLY_ATOMIC_H_
