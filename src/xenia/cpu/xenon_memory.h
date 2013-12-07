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

#include <alloy/memory.h>

#include <xenia/core.h>


namespace xe {
namespace cpu {

class XenonMemoryHeap;


class XenonMemory : public alloy::Memory {
public:
  XenonMemory();
  virtual ~XenonMemory();

  virtual int Initialize();

  virtual uint64_t HeapAlloc(
      uint64_t base_address, size_t size, uint32_t flags,
      uint32_t alignment = 0x20);
  virtual int HeapFree(uint64_t address, size_t size);

  virtual int Protect(uint64_t address, size_t size, uint32_t access);
  virtual uint32_t QueryProtect(uint64_t address);

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

  XenonMemoryHeap* virtual_heap_;
  XenonMemoryHeap* physical_heap_;

  friend class XenonMemoryHeap;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_XENON_MEMORY_H_
