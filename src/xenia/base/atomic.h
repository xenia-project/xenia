/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_ATOMIC_H_
#define XENIA_BASE_ATOMIC_H_

#include <cstdint>

#include "xenia/base/platform.h"

namespace xe {

#if XE_PLATFORM_WIN32

inline int32_t atomic_inc(volatile int32_t* value) {
  return _InterlockedIncrement(reinterpret_cast<volatile long*>(value));
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return _InterlockedDecrement(reinterpret_cast<volatile long*>(value));
}
inline int32_t atomic_or(volatile int32_t* value, int32_t nv) {
  return _InterlockedOr(reinterpret_cast<volatile long*>(value), nv);
}

inline int32_t atomic_and(volatile int32_t* value, int32_t nv) {
  return _InterlockedAnd(reinterpret_cast<volatile long*>(value), nv);
}

inline int32_t atomic_xor(volatile int32_t* value, int32_t nv) {
  return _InterlockedXor(reinterpret_cast<volatile long*>(value), nv);
}

inline int32_t atomic_exchange(int32_t new_value, volatile int32_t* value) {
  return _InterlockedExchange(reinterpret_cast<volatile long*>(value),
                              new_value);
}
inline int64_t atomic_exchange(int64_t new_value, volatile int64_t* value) {
  return _InterlockedExchange64(reinterpret_cast<volatile long long*>(value),
                                new_value);
}

inline int32_t atomic_exchange_add(int32_t amount, volatile int32_t* value) {
  return _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(value),
                                 amount);
}
inline int64_t atomic_exchange_add(int64_t amount, volatile int64_t* value) {
  return _InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(value),
                                   amount);
}

inline bool atomic_cas(int32_t old_value, int32_t new_value,
                       volatile int32_t* value) {
  return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(value),
                                     new_value, old_value) == old_value;
}
inline bool atomic_cas(int64_t old_value, int64_t new_value,
                       volatile int64_t* value) {
  return _InterlockedCompareExchange64(
             reinterpret_cast<volatile long long*>(value), new_value,
             old_value) == old_value;
}

#elif XE_PLATFORM_LINUX || XE_PLATFORM_MAC

inline int32_t atomic_inc(volatile int32_t* value) {
  return __sync_add_and_fetch(value, 1);
}
inline int32_t atomic_dec(volatile int32_t* value) {
  return __sync_sub_and_fetch(value, 1);
}
inline int32_t atomic_or(volatile int32_t* value, int32_t nv) {
  return __sync_or_and_fetch(value, nv);
}
inline int32_t atomic_and(volatile int32_t* value, int32_t nv) {
  return __sync_and_and_fetch(value, nv);
}
inline int32_t atomic_xor(volatile int32_t* value, int32_t nv) {
  return __sync_xor_and_fetch(value, nv);
}

inline int32_t atomic_exchange(int32_t new_value, volatile int32_t* value) {
  return __sync_val_compare_and_swap(value, *value, new_value);
}
inline int64_t atomic_exchange(int64_t new_value, volatile int64_t* value) {
  return __sync_val_compare_and_swap(value, *value, new_value);
}

inline int32_t atomic_exchange_add(int32_t amount, volatile int32_t* value) {
  return __sync_fetch_and_add(value, amount);
}
inline int64_t atomic_exchange_add(int64_t amount, volatile int64_t* value) {
  return __sync_fetch_and_add(value, amount);
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

#endif  // XE_PLATFORM

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

inline uint32_t atomic_exchange_add(uint32_t amount, volatile uint32_t* value) {
  return static_cast<uint32_t>(
      atomic_exchange_add(static_cast<int32_t>(amount),
                          reinterpret_cast<volatile int32_t*>(value)));
}
inline uint64_t atomic_exchange_add(uint64_t amount, volatile uint64_t* value) {
  return static_cast<uint64_t>(
      atomic_exchange_add(static_cast<int64_t>(amount),
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

}  // namespace xe

#endif  // XENIA_BASE_ATOMIC_H_
