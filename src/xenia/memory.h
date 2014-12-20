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

#include <memory>

#include <alloy/memory.h>

#include <xenia/common.h>
#include <xenia/cpu/mmio_handler.h>

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

class Memory : public alloy::Memory {
 public:
  Memory();
  ~Memory() override;

  int Initialize() override;

  bool AddMappedRange(uint64_t address, uint64_t mask, uint64_t size,
                      void* context, cpu::MMIOReadCallback read_callback,
                      cpu::MMIOWriteCallback write_callback);

  uint8_t LoadI8(uint64_t address) override;
  uint16_t LoadI16(uint64_t address) override;
  uint32_t LoadI32(uint64_t address) override;
  uint64_t LoadI64(uint64_t address) override;
  void StoreI8(uint64_t address, uint8_t value) override;
  void StoreI16(uint64_t address, uint16_t value) override;
  void StoreI32(uint64_t address, uint32_t value) override;
  void StoreI64(uint64_t address, uint64_t value) override;

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
