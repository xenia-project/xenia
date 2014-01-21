// File: crn_atomics.h
#ifndef CRN_ATOMICS_H
#define CRN_ATOMICS_H

#ifdef WIN32
#pragma once
#endif

#ifdef WIN32
#include "crn_winhdr.h"
#endif

#if defined(__GNUC__) && CRNLIB_PLATFORM_PC
extern __inline__ __attribute__((__always_inline__,__gnu_inline__)) void crnlib_yield_processor()
{
   __asm__ __volatile__("pause");
}
#else
CRNLIB_FORCE_INLINE void crnlib_yield_processor()
{
#if CRNLIB_USE_MSVC_INTRINSICS
   #if CRNLIB_PLATFORM_PC_X64
      _mm_pause();
   #else
      YieldProcessor();
   #endif
#else
   // No implementation
#endif
}
#endif

#if CRNLIB_USE_WIN32_ATOMIC_FUNCTIONS
   extern "C" __int64 _InterlockedCompareExchange64(__int64 volatile * Destination, __int64 Exchange, __int64 Comperand);
   #if defined(_MSC_VER)
      #pragma intrinsic(_InterlockedCompareExchange64)
   #endif
#endif // CRNLIB_USE_WIN32_ATOMIC_FUNCTIONS

namespace crnlib
{
#if CRNLIB_USE_WIN32_ATOMIC_FUNCTIONS
   typedef LONG atomic32_t;
   typedef LONGLONG atomic64_t;

   // Returns the original value.
   inline atomic32_t atomic_compare_exchange32(atomic32_t volatile *pDest, atomic32_t exchange, atomic32_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedCompareExchange(pDest, exchange, comparand);
   }

   // Returns the original value.
   inline atomic64_t atomic_compare_exchange64(atomic64_t volatile *pDest, atomic64_t exchange, atomic64_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 7) == 0);
      return _InterlockedCompareExchange64(pDest, exchange, comparand);
   }

   // Returns the resulting incremented value.
   inline atomic32_t atomic_increment32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedIncrement(pDest);
   }

   // Returns the resulting decremented value.
   inline atomic32_t atomic_decrement32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedDecrement(pDest);
   }

   // Returns the original value.
   inline atomic32_t atomic_exchange32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedExchange(pDest, val);
   }

   // Returns the resulting value.
   inline atomic32_t atomic_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedExchangeAdd(pDest, val) + val;
   }

   // Returns the original value.
   inline atomic32_t atomic_exchange_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return InterlockedExchangeAdd(pDest, val);
   }
#elif CRNLIB_USE_GCC_ATOMIC_BUILTINS
   typedef long atomic32_t;
   typedef long long atomic64_t;

   // Returns the original value.
   inline atomic32_t atomic_compare_exchange32(atomic32_t volatile *pDest, atomic32_t exchange, atomic32_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_val_compare_and_swap(pDest, comparand, exchange);
   }

   // Returns the original value.
   inline atomic64_t atomic_compare_exchange64(atomic64_t volatile *pDest, atomic64_t exchange, atomic64_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 7) == 0);
      return __sync_val_compare_and_swap(pDest, comparand, exchange);
   }

   // Returns the resulting incremented value.
   inline atomic32_t atomic_increment32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_add_and_fetch(pDest, 1);
   }

   // Returns the resulting decremented value.
   inline atomic32_t atomic_decrement32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_sub_and_fetch(pDest, 1);
   }

   // Returns the original value.
   inline atomic32_t atomic_exchange32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_lock_test_and_set(pDest, val);
   }

   // Returns the resulting value.
   inline atomic32_t atomic_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_add_and_fetch(pDest, val);
   }

   // Returns the original value.
   inline atomic32_t atomic_exchange_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return __sync_fetch_and_add(pDest, val);
   }
#else
   #define CRNLIB_NO_ATOMICS 1

   // Atomic ops not supported - but try to do something reasonable. Assumes no threading at all.
   typedef long atomic32_t;
   typedef long long atomic64_t;

   inline atomic32_t atomic_compare_exchange32(atomic32_t volatile *pDest, atomic32_t exchange, atomic32_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      atomic32_t cur = *pDest;
      if (cur == comparand)
         *pDest = exchange;
      return cur;
   }

   inline atomic64_t atomic_compare_exchange64(atomic64_t volatile *pDest, atomic64_t exchange, atomic64_t comparand)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 7) == 0);
      atomic64_t cur = *pDest;
      if (cur == comparand)
         *pDest = exchange;
      return cur;
   }

   inline atomic32_t atomic_increment32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return (*pDest += 1);
   }

   inline atomic32_t atomic_decrement32(atomic32_t volatile *pDest)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return (*pDest -= 1);
   }

   inline atomic32_t atomic_exchange32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      atomic32_t cur = *pDest;
      *pDest = val;
      return cur;
   }

   inline atomic32_t atomic_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      return (*pDest += val);
   }

   inline atomic32_t atomic_exchange_add32(atomic32_t volatile *pDest, atomic32_t val)
   {
      CRNLIB_ASSERT((reinterpret_cast<ptr_bits_t>(pDest) & 3) == 0);
      atomic32_t cur = *pDest;
      *pDest += val;
      return cur;
   }
#endif

} // namespace crnlib

#endif // CRN_ATOMICS_H
