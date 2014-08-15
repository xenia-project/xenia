/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_XENON_MEMORY_H_
#define XENIA_CPU_XENON_MEMORY_H_

#include <memory>

#include <alloy/memory.h>

#include <xenia/core.h>
#include <xenia/cpu/mmio_handler.h>


typedef struct xe_ppc_state xe_ppc_state_t;

namespace xe {
namespace cpu {

class XenonMemoryHeap;

class XenonMemory : public alloy::Memory {
public:
  XenonMemory();
  virtual ~XenonMemory();

  int Initialize() override;

  uint64_t page_table() const override { return page_table_; }

  bool AddMappedRange(uint64_t address, uint64_t mask,
                      uint64_t size,
                      void* context,
                      MMIOReadCallback read_callback,
                      MMIOWriteCallback write_callback);

  uint8_t LoadI8(uint64_t address) override;
  uint16_t LoadI16(uint64_t address) override;
  uint32_t LoadI32(uint64_t address) override;
  uint64_t LoadI64(uint64_t address) override;
  void StoreI8(uint64_t address, uint8_t value) override;
  void StoreI16(uint64_t address, uint16_t value) override;
  void StoreI32(uint64_t address, uint32_t value) override;
  void StoreI64(uint64_t address, uint64_t value) override;

  uint64_t HeapAlloc(
      uint64_t base_address, size_t size, uint32_t flags,
      uint32_t alignment = 0x20) override;
  int HeapFree(uint64_t address, size_t size) override;

  size_t QueryInformation(uint64_t base_address, MEMORY_BASIC_INFORMATION mem_info) override;
  size_t QuerySize(uint64_t base_address) override;

  int Protect(uint64_t address, size_t size, uint32_t access) override;
  uint32_t QueryProtect(uint64_t address) override;

private:
  int MapViews(uint8_t* mapping_base);
  void UnmapViews();

private:
  HANDLE    mapping_;
  uint8_t*  mapping_base_;
  union {
    struct {
      uint8_t*  v00000000;
      uint8_t*  v40000000;
      uint8_t*  v80000000;
      uint8_t*  vA0000000;
      uint8_t*  vC0000000;
      uint8_t*  vE0000000;
    };
    uint8_t*    all_views[6];
  } views_;

  std::unique_ptr<MMIOHandler> mmio_handler_;

  XenonMemoryHeap* virtual_heap_;
  XenonMemoryHeap* physical_heap_;

  uint64_t page_table_;

  friend class XenonMemoryHeap;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_XENON_MEMORY_H_
