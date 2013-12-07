/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_TRACING_H_
#define ALLOY_RUNTIME_TRACING_H_

#include <alloy/tracing/tracing.h>
#include <alloy/tracing/event_type.h>


namespace alloy {
namespace runtime {

const uint32_t ALLOY_RUNTIME = alloy::tracing::EventType::ALLOY_RUNTIME;


class EventType {
public:
  enum {
    ALLOY_RUNTIME_INIT                  = ALLOY_RUNTIME | (1),
    ALLOY_RUNTIME_DEINIT                = ALLOY_RUNTIME | (2),

    ALLOY_RUNTIME_THREAD                = ALLOY_RUNTIME | (1 << 25),
    ALLOY_RUNTIME_THREAD_INIT           = ALLOY_RUNTIME_THREAD | (1),
    ALLOY_RUNTIME_THREAD_DEINIT         = ALLOY_RUNTIME_THREAD | (2),

    ALLOY_RUNTIME_MEMORY                = ALLOY_RUNTIME | (2 << 25),
    ALLOY_RUNTIME_MEMORY_INIT           = ALLOY_RUNTIME_MEMORY | (1),
    ALLOY_RUNTIME_MEMORY_DEINIT         = ALLOY_RUNTIME_MEMORY | (2),
    ALLOY_RUNTIME_MEMORY_HEAP           = ALLOY_RUNTIME_MEMORY | (1000),
    ALLOY_RUNTIME_MEMORY_HEAP_INIT      = ALLOY_RUNTIME_MEMORY_HEAP | (1),
    ALLOY_RUNTIME_MEMORY_HEAP_DEINIT    = ALLOY_RUNTIME_MEMORY | (2),
    ALLOY_RUNTIME_MEMORY_HEAP_ALLOC     = ALLOY_RUNTIME_MEMORY | (3),
    ALLOY_RUNTIME_MEMORY_HEAP_FREE      = ALLOY_RUNTIME_MEMORY | (4),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_DEINIT;
  } Deinit;

  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_THREAD_INIT;
  } ThreadInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_THREAD_DEINIT;
  } ThreadDeinit;

  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_INIT;
    // map of memory, etc?
  } MemoryInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_DEINIT;
  } MemoryDeinit;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_HEAP_INIT;
    uint32_t  heap_id;
    uint64_t  low_address;
    uint64_t  high_address;
    uint32_t  is_physical;
  } MemoryHeapInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_HEAP_DEINIT;
    uint32_t  heap_id;
  } MemoryHeapDeinit;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_HEAP_ALLOC;
    uint32_t  heap_id;
    uint32_t  flags;
    uint64_t  address;
    size_t    size;
  } MemoryHeapAlloc;
  typedef struct {
    static const uint32_t event_type = ALLOY_RUNTIME_MEMORY_HEAP_FREE;
    uint32_t  heap_id;
    uint64_t  address;
  } MemoryHeapFree;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_TRACING_H_
