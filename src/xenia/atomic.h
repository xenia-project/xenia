/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_ATOMIC_H_
#define XENIA_ATOMIC_H_

#include <xenia/platform.h>
#include <xenia/platform_includes.h>


// These functions are modeled off of the Apple OSAtomic routines
// http://developer.apple.com/library/mac/#documentation/DriversKernelHardware/Reference/libkern_ref/OSAtomic_h/

#if XE_LIKE_OSX
#include <libkern/OSAtomic.h>

#define xe_atomic_inc_32(value) \
    OSAtomicIncrement32Barrier(value)
#define xe_atomic_dec_32(value) \
    OSAtomicDecrement32Barrier(value)
#define xe_atomic_add_32(amount, value) \
    ((void)OSAtomicAdd32Barrier(amount, value))
#define xe_atomic_sub_32(amount, value) \
    ((void)OSAtomicAdd32Barrier(-amount, value))
#define xe_atomic_exchange_32(newValue, value) \
    TODOTODO
#define xe_atomic_cas_32(oldValue, newValue, value) \
    OSAtomicCompareAndSwap32Barrier(oldValue, newValue, value)

typedef OSQueueHead xe_atomic_stack_t;
#define xe_atomic_stack_init(stack) \
    *(stack) = (OSQueueHead)OS_ATOMIC_QUEUE_INIT
#define xe_atomic_stack_enqueue(stack, item, offset) \
    OSAtomicEnqueue((OSQueueHead*)stack, item, offset)
#define xe_atomic_stack_dequeue(stack, offset) \
    OSAtomicDequeue((OSQueueHead*)stack, offset)

#elif XE_LIKE_WIN32

#define xe_atomic_inc_32(value) \
    InterlockedIncrement((volatile LONG*)value)
#define xe_atomic_dec_32(value) \
    InterlockedDecrement((volatile LONG*)value)
#define xe_atomic_add_32(amount, value) \
    ((void)InterlockedExchangeAdd((volatile LONG*)value, amount))
#define xe_atomic_sub_32(amount, value) \
    ((void)InterlockedExchangeSubtract((volatile unsigned*)value, amount))
#define xe_atomic_exchange_32(newValue, value) \
    InterlockedExchange((volatile LONG*)value, newValue)
#define xe_atomic_exchange_64(newValue, value) \
    InterlockedExchange64((volatile LONGLONG*)value, newValue)
#define xe_atomic_cas_32(oldValue, newValue, value) \
    (InterlockedCompareExchange((volatile LONG*)value, newValue, oldValue) == oldValue)

typedef SLIST_HEADER xe_atomic_stack_t;
#define xe_atomic_stack_init(stack) \
    InitializeSListHead((PSLIST_HEADER)stack)
#define xe_atomic_stack_enqueue(stack, item, offset) \
    XEIGNORE(InterlockedPushEntrySList((PSLIST_HEADER)stack, (PSLIST_ENTRY)((byte*)item + offset)))
XEFORCEINLINE void* xe_atomic_stack_dequeue(xe_atomic_stack_t* stack,
                                            const size_t offset) {
  void* ptr = (void*)InterlockedPopEntrySList((PSLIST_HEADER)stack);
  if (ptr) {
    return (void*)(((uint8_t*)ptr) - offset);
  } else {
    return NULL;
  }
}

#elif XE_LIKE_POSIX

#define xe_atomic_inc_32(value) \
    __sync_add_and_fetch(value, 1)
#define xe_atomic_dec_32(value) \
    __sync_sub_and_fetch(value, 1)
#define xe_atomic_add_32(amount, value) \
    __sync_fetch_and_add(value, amount)
#define xe_atomic_sub_32(amount, value) \
    __sync_fetch_and_sub(value, amount)
#define xe_atomic_exchange_32(newValue, value) \
    TODOTODO
#define xe_atomic_cas_32(oldValue, newValue, value) \
    __sync_bool_compare_and_swap(value, oldValue, newValue)

#else

#error No atomic primitives defined for this platform/cpu combination.

#endif  // OSX


#endif  // XENIA_ATOMIC_H_
