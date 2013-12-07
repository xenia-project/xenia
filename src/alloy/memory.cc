/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/memory.h>

using namespace alloy;


Memory::Memory() :
    membase_(0) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  system_page_size_ = si.dwPageSize;
}

Memory::~Memory() {
}

int Memory::Initialize() {
  return 0;
}

void Memory::Zero(uint64_t address, size_t size) {
  uint8_t* p = membase_ + address;
  XEIGNORE(xe_zero_memory(p, size, 0, size));
}

void Memory::Fill(uint64_t address, size_t size, uint8_t value) {
  uint8_t* p = membase_ + address;
  memset(p, value, size);
}

void Memory::Copy(uint64_t dest, uint64_t src, size_t size) {
  uint8_t* pdest = membase_ + dest;
  uint8_t* psrc = membase_ + src;
  XEIGNORE(xe_copy_memory(pdest, size, psrc, size));
}

uint64_t Memory::SearchAligned(
    uint64_t start, uint64_t end,
    const uint32_t* values, size_t value_count) {
  XEASSERT(start <= end);
  const uint32_t *p = (const uint32_t*)(membase_ + start);
  const uint32_t *pe = (const uint32_t*)(membase_ + end);
  while (p != pe) {
    if (*p == values[0]) {
      const uint32_t *pc = p + 1;
      size_t matched = 1;
      for (size_t n = 1; n < value_count; n++, pc++) {
        if (*pc != values[n]) {
          break;
        }
        matched++;
      }
      if (matched == value_count) {
        return (uint64_t)((uint8_t*)p - membase_);
      }
    }
    p++;
  }
  return 0;
}
