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

namespace xe {

enum SystemHeapFlag : uint32_t {
  kSystemHeapVirtual = 1 << 0,
  kSystemHeapPhysical = 1 << 1,

  kSystemHeapDefault = kSystemHeapVirtual,
};
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
  uint32_t base_address;
  uint32_t allocation_base;
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

  inline uint8_t* virtual_membase() const { return virtual_membase_; }
  inline uint8_t* TranslateVirtual(uint32_t guest_address) const {
    return virtual_membase_ + guest_address;
  };
  template <typename T>
  inline T TranslateVirtual(uint32_t guest_address) const {
    return reinterpret_cast<T>(virtual_membase_ + guest_address);
  };

  inline uint8_t* physical_membase() const { return physical_membase_; }
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return physical_membase_ + (guest_address & 0x1FFFFFFF);
  }
  template <typename T>
  inline T TranslatePhysical(uint32_t guest_address) const {
    return reinterpret_cast<T>(physical_membase_ +
                               (guest_address & 0x1FFFFFFF));
  }

  inline uint64_t* reserve_address() { return &reserve_address_; }
  inline uint64_t* reserve_value() { return &reserve_value_; }

  uint64_t trace_base() const { return trace_base_; }
  void set_trace_base(uint64_t value) { trace_base_ = value; }

  // TODO(benvanik): make poly memory utils for these.
  void Zero(uint32_t address, uint32_t size);
  void Fill(uint32_t address, uint32_t size, uint8_t value);
  void Copy(uint32_t dest, uint32_t src, uint32_t size);
  uint32_t SearchAligned(uint32_t start, uint32_t end, const uint32_t* values,
                         size_t value_count);

  bool AddMappedRange(uint32_t address, uint32_t mask, uint32_t size,
                      void* context, cpu::MMIOReadCallback read_callback,
                      cpu::MMIOWriteCallback write_callback);

  uintptr_t AddWriteWatch(uint32_t guest_address, uint32_t length,
                          cpu::WriteWatchCallback callback,
                          void* callback_context, void* callback_data);
  void CancelWriteWatch(uintptr_t watch_handle);

  uint32_t SystemHeapAlloc(uint32_t size, uint32_t alignment = 0x20,
                           uint32_t system_heap_flags = kSystemHeapDefault);
  void SystemHeapFree(uint32_t address);
  uint32_t HeapAlloc(uint32_t base_address, uint32_t size, uint32_t flags,
                     uint32_t alignment = 0x20);
  int HeapFree(uint32_t address, uint32_t size);

  bool QueryInformation(uint32_t base_address, AllocationInfo* mem_info);
  uint32_t QuerySize(uint32_t base_address);

  int Protect(uint32_t address, uint32_t size, uint32_t access);
  uint32_t QueryProtect(uint32_t address);

 private:
  int MapViews(uint8_t* mapping_base);
  void UnmapViews();

 private:
  uint32_t system_page_size_;
  uint8_t* virtual_membase_;
  uint8_t* physical_membase_;
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
