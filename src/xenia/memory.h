/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_MEMORY_H_
#define XENIA_MEMORY_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "xenia/memory.h"

#include "xenia/common.h"
#include "xenia/cpu/mmio_handler.h"

typedef struct xe_ppc_state xe_ppc_state_t;

namespace xe {

class MemoryHeap;

// TODO(benvanik): move to heap.
enum {
  MEMORY_FLAG_64KB_PAGES = (1 << 1),
  MEMORY_FLAG_ZERO = (1 << 2),
  MEMORY_FLAG_PHYSICAL = (1 << 3),
};

// TODO(benvanik): move to heap.
// Equivalent to the Win32 MEMORY_BASIC_INFORMATION struct.
struct AllocationInfo {
  uint64_t base_address;
  uint64_t allocation_base;
  uint32_t allocation_protect;  // TBD
  size_t region_size;
  uint32_t state;    // TBD
  uint32_t protect;  // TBD
  uint32_t type;     // TBD
};

class Memory {
 public:
  Memory();
  ~Memory();

  int Initialize();

  inline uint8_t* membase() const { return membase_; }
  inline uint8_t* Translate(uint64_t guest_address) const {
    return membase_ + guest_address;
  };
  inline uint64_t* reserve_address() { return &reserve_address_; }
  inline uint64_t* reserve_value() { return &reserve_value_; }

  uint64_t trace_base() const { return trace_base_; }
  void set_trace_base(uint64_t value) { trace_base_ = value; }

  // TODO(benvanik): make poly memory utils for these.
  void Zero(uint64_t address, size_t size);
  void Fill(uint64_t address, size_t size, uint8_t value);
  void Copy(uint64_t dest, uint64_t src, size_t size);
  uint64_t SearchAligned(uint64_t start, uint64_t end, const uint32_t* values,
                         size_t value_count);

  bool AddMappedRange(uint64_t address, uint64_t mask, uint64_t size,
                      void* context, cpu::MMIOReadCallback read_callback,
                      cpu::MMIOWriteCallback write_callback);

  uintptr_t AddWriteWatch(uint32_t guest_address, size_t length,
                          cpu::WriteWatchCallback callback,
                          void* callback_context, void* callback_data);
  void CancelWriteWatch(uintptr_t watch_handle);

  uint64_t HeapAlloc(uint64_t base_address, size_t size, uint32_t flags,
                     uint32_t alignment = 0x20);
  int HeapFree(uint64_t address, size_t size);

  bool QueryInformation(uint64_t base_address, AllocationInfo* mem_info);
  size_t QuerySize(uint64_t base_address);

  int Protect(uint64_t address, size_t size, uint32_t access);
  uint32_t QueryProtect(uint64_t address);

 private:
  int MapViews(uint8_t* mapping_base);
  void UnmapViews();

 private:
  size_t system_page_size_;
  uint8_t* membase_;
  uint64_t reserve_address_;
  uint64_t reserve_value_;
  uint64_t trace_base_;

  HANDLE mapping_;
  uint8_t* mapping_base_;
  union {
    struct {
      uint8_t* v00000000;
      uint8_t* v40000000;
      uint8_t* v7F000000;
      uint8_t* v7F100000;
      uint8_t* v80000000;
      uint8_t* v90000000;
      uint8_t* vA0000000;
      uint8_t* vC0000000;
      uint8_t* vE0000000;
    };
    uint8_t* all_views[9];
  } views_;

  std::unique_ptr<cpu::MMIOHandler> mmio_handler_;

  MemoryHeap* virtual_heap_;
  MemoryHeap* physical_heap_;

  friend class MemoryHeap;
};

}  // namespace xe

#endif  // XENIA_MEMORY_H_
