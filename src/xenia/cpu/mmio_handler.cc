/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/mmio_handler.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace xe {
namespace cpu {

MMIOHandler* MMIOHandler::global_handler_ = nullptr;

std::unique_ptr<MMIOHandler> MMIOHandler::Install(uint8_t* virtual_membase,
                                                  uint8_t* physical_membase,
                                                  uint8_t* membase_end) {
  // There can be only one handler at a time.
  assert_null(global_handler_);
  if (global_handler_) {
    return nullptr;
  }

  auto handler = std::unique_ptr<MMIOHandler>(
      new MMIOHandler(virtual_membase, physical_membase, membase_end));

  // Install the exception handler directed at the MMIOHandler.
  ExceptionHandler::Install(ExceptionCallbackThunk, handler.get());

  global_handler_ = handler.get();
  return handler;
}

MMIOHandler::MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
                         uint8_t* membase_end)
    : virtual_membase_(virtual_membase),
      physical_membase_(physical_membase),
      memory_end_(membase_end) {
  system_page_size_log2_ = xe::log2_ceil(uint32_t(xe::memory::page_size()));

  uint32_t physical_page_count = (512 * 1024 * 1024) >> system_page_size_log2_;
  physical_write_watched_pages_.resize(physical_page_count >> 4);
  assert_true(physical_write_watched_pages_.size() != 0);
  std::memset(physical_write_watched_pages_.data(), 0,
              physical_write_watched_pages_.size() * sizeof(uint64_t));
}

MMIOHandler::~MMIOHandler() {
  ExceptionHandler::Uninstall(ExceptionCallbackThunk, this);

  assert_true(global_handler_ == this);
  global_handler_ = nullptr;
}

bool MMIOHandler::RegisterRange(uint32_t virtual_address, uint32_t mask,
                                uint32_t size, void* context,
                                MMIOReadCallback read_callback,
                                MMIOWriteCallback write_callback) {
  mapped_ranges_.push_back({
      virtual_address,
      mask,
      size,
      context,
      read_callback,
      write_callback,
  });
  return true;
}

MMIORange* MMIOHandler::LookupRange(uint32_t virtual_address) {
  for (auto& range : mapped_ranges_) {
    if ((virtual_address & range.mask) == range.address) {
      return &range;
    }
  }
  return nullptr;
}

bool MMIOHandler::CheckLoad(uint32_t virtual_address, uint32_t* out_value) {
  for (const auto& range : mapped_ranges_) {
    if ((virtual_address & range.mask) == range.address) {
      *out_value = static_cast<uint32_t>(
          range.read(nullptr, range.callback_context, virtual_address));
      return true;
    }
  }
  return false;
}

bool MMIOHandler::CheckStore(uint32_t virtual_address, uint32_t value) {
  for (const auto& range : mapped_ranges_) {
    if ((virtual_address & range.mask) == range.address) {
      range.write(nullptr, range.callback_context, virtual_address, value);
      return true;
    }
  }
  return false;
}

uintptr_t MMIOHandler::AddPhysicalAccessWatch(uint32_t guest_address,
                                              size_t length, WatchType type,
                                              AccessWatchCallback callback,
                                              void* callback_context,
                                              void* callback_data) {
  uint32_t base_address = guest_address & 0x1FFFFFFF;

  // Can only protect sizes matching system page size.
  // This means we need to round up, which will cause spurious access
  // violations and invalidations.
  // TODO(benvanik): only invalidate if actually within the region?
  length = xe::round_up(length + (base_address % xe::memory::page_size()),
                        xe::memory::page_size());
  base_address = base_address - (base_address % xe::memory::page_size());

  auto lock = global_critical_region_.Acquire();

  // Fire any access watches that overlap this region.
  for (auto it = access_watches_.begin(); it != access_watches_.end();) {
    // Case 1: 2222222|222|11111111
    // Case 2: 1111111|222|22222222
    // Case 3: 1111111|222|11111111 (fragmentation)
    // Case 4: 2222222|222|22222222 (complete overlap)
    bool hit = false;
    auto entry = *it;

    if (base_address <= (*it)->address &&
        base_address + length > (*it)->address) {
      hit = true;
    } else if ((*it)->address <= base_address &&
               (*it)->address + (*it)->length > base_address) {
      hit = true;
    } else if ((*it)->address <= base_address &&
               (*it)->address + (*it)->length > base_address + length) {
      hit = true;
    } else if ((*it)->address >= base_address &&
               (*it)->address + (*it)->length < base_address + length) {
      hit = true;
    }

    if (hit) {
      FireAccessWatch(*it);
      it = access_watches_.erase(it);
      delete entry;
      continue;
    }

    ++it;
  }

  // Add to table. The slot reservation may evict a previous watch, which
  // could include our target, so we do it first.
  auto entry = new AccessWatchEntry();
  entry->address = base_address;
  entry->length = uint32_t(length);
  entry->type = type;
  entry->callback = callback;
  entry->callback_context = callback_context;
  entry->callback_data = callback_data;
  access_watches_.push_back(entry);

  auto page_access = memory::PageAccess::kNoAccess;
  switch (type) {
    case kWatchWrite:
      page_access = memory::PageAccess::kReadOnly;
      break;
    case kWatchReadWrite:
      page_access = memory::PageAccess::kNoAccess;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }

  // Protect the range under all address spaces
  memory::Protect(physical_membase_ + entry->address, entry->length,
                  page_access, nullptr);
  memory::Protect(virtual_membase_ + 0xA0000000 + entry->address, entry->length,
                  page_access, nullptr);
  memory::Protect(virtual_membase_ + 0xC0000000 + entry->address, entry->length,
                  page_access, nullptr);
  memory::Protect(virtual_membase_ + 0xE0000000 + entry->address, entry->length,
                  page_access, nullptr);

  return reinterpret_cast<uintptr_t>(entry);
}

void MMIOHandler::FireAccessWatch(AccessWatchEntry* entry) {
  ClearAccessWatch(entry);
  entry->callback(entry->callback_context, entry->callback_data,
                  entry->address);
}

void MMIOHandler::ClearAccessWatch(AccessWatchEntry* entry) {
  memory::Protect(physical_membase_ + entry->address, entry->length,
                  xe::memory::PageAccess::kReadWrite, nullptr);
  memory::Protect(virtual_membase_ + 0xA0000000 + entry->address, entry->length,
                  xe::memory::PageAccess::kReadWrite, nullptr);
  memory::Protect(virtual_membase_ + 0xC0000000 + entry->address, entry->length,
                  xe::memory::PageAccess::kReadWrite, nullptr);
  memory::Protect(virtual_membase_ + 0xE0000000 + entry->address, entry->length,
                  xe::memory::PageAccess::kReadWrite, nullptr);
}

void MMIOHandler::CancelAccessWatch(uintptr_t watch_handle) {
  auto entry = reinterpret_cast<AccessWatchEntry*>(watch_handle);
  auto lock = global_critical_region_.Acquire();

  // Allow access to the range again.
  ClearAccessWatch(entry);

  // Remove from table.
  auto it = std::find(access_watches_.begin(), access_watches_.end(), entry);
  assert_false(it == access_watches_.end());

  if (it != access_watches_.end()) {
    access_watches_.erase(it);
  }

  delete entry;
}

void* MMIOHandler::RegisterPhysicalWriteWatch(
    PhysicalWriteWatchCallback callback, void* callback_context) {
  PhysicalWriteWatchEntry* entry = new PhysicalWriteWatchEntry;
  entry->callback = callback;
  entry->callback_context = callback_context;

  auto lock = global_critical_region_.Acquire();
  physical_write_watches_.push_back(entry);

  return entry;
}

void MMIOHandler::UnregisterPhysicalWriteWatch(void* watch_handle) {
  auto entry = reinterpret_cast<PhysicalWriteWatchEntry*>(watch_handle);

  {
    auto lock = global_critical_region_.Acquire();
    auto it = std::find(physical_write_watches_.begin(),
                        physical_write_watches_.end(), entry);
    assert_false(it == physical_write_watches_.end());
    if (it != physical_write_watches_.end()) {
      physical_write_watches_.erase(it);
    }
  }

  delete entry;
}

void MMIOHandler::ProtectAndWatchPhysicalMemory(
    uint32_t physical_address_and_heap, uint32_t length) {
  // Bits to set in 16-bit blocks to mark that the pages are protected.
  uint64_t block_heap_mask;
  if (physical_address_and_heap >= 0xE0000000) {
    block_heap_mask = 0x4444444444444444ull;
  } else if (physical_address_and_heap >= 0xC0000000) {
    block_heap_mask = 0x2222222222222222ull;
  } else if (physical_address_and_heap >= 0xA0000000) {
    block_heap_mask = 0x1111111111111111ull;
  } else {
    assert_always();
    return;
  }

  uint32_t heap_relative_address = physical_address_and_heap & 0x1FFFFFFF;
  length = std::min(length, 0x20000000u - heap_relative_address);
  if (length == 0) {
    return;
  }

  uint32_t page_first = heap_relative_address >> system_page_size_log2_;
  uint32_t page_last =
      (heap_relative_address + length - 1) >> system_page_size_log2_;
  uint32_t block_first = page_first >> 4;
  uint32_t block_last = page_last >> 4;

  auto lock = global_critical_region_.Acquire();

  // Set the bits indicating that the pages are watched and access violations
  // there are intentional.
  for (uint32_t i = block_first; i <= block_last; ++i) {
    uint64_t block_set_bits = block_heap_mask;
    if (i == block_first) {
      block_set_bits &= ~((1ull << ((page_first & 15) * 4)) - 1);
    }
    if (i == block_last && (page_last & 15) != 15) {
      block_set_bits &= (1ull << (((page_last & 15) + 1) * 4)) - 1;
    }
    physical_write_watched_pages_[i] |= block_set_bits;
  }

  // Protect only in one range (due to difficulties synchronizing protection
  // levels between those ranges).
  memory::Protect(virtual_membase_ + (physical_address_and_heap & ~0x1FFFFFFF) +
                      (page_first << system_page_size_log2_),
                  (page_last - page_first + 1) << system_page_size_log2_,
                  memory::PageAccess::kReadOnly, nullptr);
}

void MMIOHandler::InvalidateRange(uint32_t physical_address_and_heap,
                                  uint32_t length, bool unprotect) {
  uint32_t heap_relative_address = physical_address_and_heap & 0x1FFFFFFF;
  length = std::min(length, 0x20000000u - heap_relative_address);
  if (length == 0) {
    return;
  }

  auto lock = global_critical_region_.Acquire();

  // Trigger the new (per-page) watches and unwatch the pages.
  if (physical_address_and_heap >= 0xA0000000) {
    uint32_t heap_address = physical_address_and_heap & ~0x1FFFFFFF;
    uint64_t heap_bit;
    if (heap_address >= 0xE0000000) {
      heap_bit = 1 << 2;
    } else if (heap_address >= 0xC0000000) {
      heap_bit = 1 << 1;
    } else {
      heap_bit = 1 << 0;
    }
    uint32_t page_first = heap_relative_address >> system_page_size_log2_;
    uint32_t page_last =
        (heap_relative_address + length - 1) >> system_page_size_log2_;
    uint32_t range_start = UINT32_MAX;
    for (uint32_t i = page_first; i <= page_last; ++i) {
      uint64_t page_heap_bit = heap_bit << ((i & 15) * 4);
      if (physical_write_watched_pages_[i >> 4] & page_heap_bit) {
        if (range_start == UINT32_MAX) {
          range_start = i;
        }
        physical_write_watched_pages_[i >> 4] &= ~page_heap_bit;
      } else {
        if (range_start != UINT32_MAX) {
          for (auto it = physical_write_watches_.begin();
               it != physical_write_watches_.end(); ++it) {
            auto entry = *it;
            entry->callback(entry->callback_context, range_start, i - 1);
          }
          if (unprotect) {
            memory::Protect(virtual_membase_ + heap_address +
                                (range_start << system_page_size_log2_),
                            (i - range_start) << system_page_size_log2_,
                            xe::memory::PageAccess::kReadWrite, nullptr);
          }
          range_start = UINT32_MAX;
        }
      }
    }
    if (range_start != UINT32_MAX) {
      for (auto it = physical_write_watches_.begin();
           it != physical_write_watches_.end(); ++it) {
        auto entry = *it;
        entry->callback(entry->callback_context, range_start, page_last);
        if (unprotect) {
          memory::Protect(virtual_membase_ + heap_address +
                              (range_start << system_page_size_log2_),
                          (page_last - range_start + 1)
                              << system_page_size_log2_,
                          xe::memory::PageAccess::kReadWrite, nullptr);
        }
      }
    }
  }

  // Trigger the legacy (per-range) watches.
  for (auto it = access_watches_.begin(); it != access_watches_.end();) {
    auto entry = *it;
    if ((entry->address <= heap_relative_address &&
         entry->address + entry->length > heap_relative_address) ||
        (entry->address >= heap_relative_address &&
         entry->address < heap_relative_address + length)) {
      // This watch lies within the range. End it.
      FireAccessWatch(entry);
      it = access_watches_.erase(it);
      delete entry;
      continue;
    }

    ++it;
  }
}

bool MMIOHandler::IsRangeWatched(uint32_t physical_address, size_t length) {
  auto lock = global_critical_region_.Acquire();

  for (auto it = access_watches_.begin(); it != access_watches_.end(); ++it) {
    auto entry = *it;
    if ((entry->address <= physical_address &&
         entry->address + entry->length > physical_address + length)) {
      // This range lies entirely within this watch.
      return true;
    }

    // TODO(DrChat): Check if the range is partially covered, and subtract the
    // covered portion if it is.
    if ((entry->address <= physical_address &&
         entry->address + entry->length > physical_address)) {
      // The beginning of range lies partially within this watch.
    } else if ((entry->address < physical_address + length &&
                entry->address + entry->length > physical_address + length)) {
      // The ending of this range lies partially within this watch.
    }
  }

  return false;
}

bool MMIOHandler::CheckAccessWatch(uint32_t physical_address,
                                   uint32_t heap_address) {
  bool hit = false;

  // Trigger new (per-page) access watches.
  if (heap_address >= 0xA0000000) {
    uint32_t page_index = physical_address >> system_page_size_log2_;
    // Check the watch only for the virtual memory mapping it was triggered in,
    // because as guest protection levels may be different for different
    // mappings of the physical memory, it's difficult to synchronize protection
    // between the mappings.
    uint64_t heap_bit;
    if (heap_address >= 0xE0000000) {
      heap_bit = 1 << 2;
    } else if (heap_address >= 0xC0000000) {
      heap_bit = 1 << 1;
    } else {
      heap_bit = 1 << 0;
    }
    heap_bit <<= (page_index & 15) * 4;
    if (physical_write_watched_pages_[page_index >> 4] & heap_bit) {
      hit = true;
      memory::Protect(virtual_membase_ + heap_address +
                          (page_index << system_page_size_log2_),
                      size_t(1) << system_page_size_log2_,
                      xe::memory::PageAccess::kReadWrite, nullptr);
      physical_write_watched_pages_[page_index >> 4] &= ~heap_bit;
      for (auto it = physical_write_watches_.begin();
           it != physical_write_watches_.end(); ++it) {
        auto entry = *it;
        entry->callback(entry->callback_context, page_index, page_index);
      }
    }
  }

  // Trigger legacy (per-range) access watches.
  auto lock = global_critical_region_.Acquire();
  for (auto it = access_watches_.begin(); it != access_watches_.end();) {
    auto entry = *it;
    if (entry->address <= physical_address &&
        entry->address + entry->length > physical_address) {
      // Hit! Remove the watch.
      hit = true;
      FireAccessWatch(entry);
      it = access_watches_.erase(it);
      delete entry;
      continue;
    }
    ++it;
  }

  if (!hit) {
    // Rethrow access violation - range was not being watched.
    return false;
  }

  // Range was watched, so lets eat this access violation.
  return true;
}

struct DecodedMov {
  size_t length;
  // Inidicates this is a load (or conversely a store).
  bool is_load;
  // Indicates the memory must be swapped.
  bool byte_swap;
  // Source (for store) or target (for load) register.
  // AX  CX  DX  BX  SP  BP  SI  DI   // REX.R=0
  // R8  R9  R10 R11 R12 R13 R14 R15  // REX.R=1
  uint32_t value_reg;
  // [base + (index * scale) + displacement]
  bool mem_has_base;
  uint8_t mem_base_reg;
  bool mem_has_index;
  uint8_t mem_index_reg;
  uint8_t mem_scale;
  int32_t mem_displacement;
  bool is_constant;
  int32_t constant;
};

bool TryDecodeMov(const uint8_t* p, DecodedMov* mov) {
  uint8_t i = 0;  // Current byte decode index.
  uint8_t rex = 0;
  if ((p[i] & 0xF0) == 0x40) {
    rex = p[0];
    ++i;
  }
  if (p[i] == 0x0F && p[i + 1] == 0x38 && p[i + 2] == 0xF1) {
    // MOVBE m32, r32 (store)
    // https://web.archive.org/web/20170629091435/https://www.tptp.cc/mirrors/siyobik.info/instruction/MOVBE.html
    // 44 0f 38 f1 a4 02 00     movbe  DWORD PTR [rdx+rax*1+0x0],r12d
    // 42 0f 38 f1 8c 22 00     movbe  DWORD PTR [rdx+r12*1+0x0],ecx
    // 0f 38 f1 8c 02 00 00     movbe  DWORD PTR [rdx + rax * 1 + 0x0], ecx
    mov->is_load = false;
    mov->byte_swap = true;
    i += 3;
  } else if (p[i] == 0x0F && p[i + 1] == 0x38 && p[i + 2] == 0xF0) {
    // MOVBE r32, m32 (load)
    // https://web.archive.org/web/20170629091435/https://www.tptp.cc/mirrors/siyobik.info/instruction/MOVBE.html
    // 44 0f 38 f0 a4 02 00     movbe  r12d,DWORD PTR [rdx+rax*1+0x0]
    // 42 0f 38 f0 8c 22 00     movbe  ecx,DWORD PTR [rdx+r12*1+0x0]
    // 46 0f 38 f0 a4 22 00     movbe  r12d,DWORD PTR [rdx+r12*1+0x0]
    // 0f 38 f0 8c 02 00 00     movbe  ecx,DWORD PTR [rdx+rax*1+0x0]
    // 0F 38 F0 1C 02           movbe  ebx,dword ptr [rdx+rax]
    mov->is_load = true;
    mov->byte_swap = true;
    i += 3;
  } else if (p[i] == 0x89) {
    // MOV m32, r32 (store)
    // https://web.archive.org/web/20170629072136/https://www.tptp.cc/mirrors/siyobik.info/instruction/MOV.html
    // 44 89 24 02              mov  DWORD PTR[rdx + rax * 1], r12d
    // 42 89 0c 22              mov  DWORD PTR[rdx + r12 * 1], ecx
    // 89 0c 02                 mov  DWORD PTR[rdx + rax * 1], ecx
    mov->is_load = false;
    mov->byte_swap = false;
    ++i;
  } else if (p[i] == 0x8B) {
    // MOV r32, m32 (load)
    // https://web.archive.org/web/20170629072136/https://www.tptp.cc/mirrors/siyobik.info/instruction/MOV.html
    // 44 8b 24 02              mov  r12d, DWORD PTR[rdx + rax * 1]
    // 42 8b 0c 22              mov  ecx, DWORD PTR[rdx + r12 * 1]
    // 46 8b 24 22              mov  r12d, DWORD PTR[rdx + r12 * 1]
    // 8b 0c 02                 mov  ecx, DWORD PTR[rdx + rax * 1]
    mov->is_load = true;
    mov->byte_swap = false;
    ++i;
  } else if (p[i] == 0xC7) {
    // MOV m32, simm32
    // https://web.archive.org/web/20161017042413/https://www.asmpedia.org/index.php?title=MOV
    // C7 04 02 02 00 00 00     mov  dword ptr [rdx+rax],2
    mov->is_load = false;
    mov->byte_swap = false;
    mov->is_constant = true;
    ++i;
  } else {
    return false;
  }

  uint8_t rex_b = rex & 0b0001;
  uint8_t rex_x = rex & 0b0010;
  uint8_t rex_r = rex & 0b0100;
  uint8_t rex_w = rex & 0b1000;

  // http://www.sandpile.org/x86/opc_rm.htm
  // http://www.sandpile.org/x86/opc_sib.htm
  uint8_t modrm = p[i++];
  uint8_t mod = (modrm & 0b11000000) >> 6;
  uint8_t reg = (modrm & 0b00111000) >> 3;
  uint8_t rm = (modrm & 0b00000111);
  mov->value_reg = reg + (rex_r ? 8 : 0);
  mov->mem_has_base = false;
  mov->mem_base_reg = 0;
  mov->mem_has_index = false;
  mov->mem_index_reg = 0;
  mov->mem_scale = 1;
  mov->mem_displacement = 0;
  bool has_sib = false;
  switch (rm) {
    case 0b100:  // SIB
      has_sib = true;
      break;
    case 0b101:
      if (mod == 0b00) {
        // RIP-relative not supported.
        return false;
      }
      mov->mem_has_base = true;
      mov->mem_base_reg = rm + (rex_b ? 8 : 0);
      break;
    default:
      mov->mem_has_base = true;
      mov->mem_base_reg = rm + (rex_b ? 8 : 0);
      break;
  }
  if (has_sib) {
    uint8_t sib = p[i++];
    mov->mem_scale = 1 << ((sib & 0b11000000) >> 8);
    uint8_t sib_index = (sib & 0b00111000) >> 3;
    uint8_t sib_base = (sib & 0b00000111);
    switch (sib_index) {
      case 0b100:
        // No index.
        break;
      default:
        mov->mem_has_index = true;
        mov->mem_index_reg = sib_index + (rex_x ? 8 : 0);
        break;
    }
    switch (sib_base) {
      case 0b101:
        // Alternate rbp-relative addressing not supported.
        assert_zero(mod);
        return false;
      default:
        mov->mem_has_base = true;
        mov->mem_base_reg = sib_base + (rex_b ? 8 : 0);
        break;
    }
  }
  switch (mod) {
    case 0b00: {
      mov->mem_displacement += 0;
    } break;
    case 0b01: {
      mov->mem_displacement += int8_t(p[i++]);
    } break;
    case 0b10: {
      mov->mem_displacement += xe::load<int32_t>(p + i);
      i += 4;
    } break;
  }
  if (mov->is_constant) {
    mov->constant = xe::load<int32_t>(p + i);
    i += 4;
  }
  mov->length = i;
  return true;
}

bool MMIOHandler::ExceptionCallbackThunk(Exception* ex, void* data) {
  return reinterpret_cast<MMIOHandler*>(data)->ExceptionCallback(ex);
}

bool MMIOHandler::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kAccessViolation) {
    return false;
  }
  if (ex->fault_address() < uint64_t(virtual_membase_) ||
      ex->fault_address() > uint64_t(memory_end_)) {
    // Quick kill anything outside our mapping.
    return false;
  }

  // Access violations are pretty rare, so we can do a linear search here.
  // Only check if in the virtual range, as we only support virtual ranges.
  const MMIORange* range = nullptr;
  if (ex->fault_address() < uint64_t(physical_membase_)) {
    for (const auto& test_range : mapped_ranges_) {
      if ((static_cast<uint32_t>(ex->fault_address()) & test_range.mask) ==
          test_range.address) {
        // Address is within the range of this mapping.
        range = &test_range;
        break;
      }
    }
  }
  if (!range) {
    auto fault_address = reinterpret_cast<uint8_t*>(ex->fault_address());
    uint32_t guest_address, guest_heap_address;
    if (fault_address >= virtual_membase_ &&
        fault_address < physical_membase_) {
      // Faulting on a virtual address.
      guest_address = static_cast<uint32_t>(ex->fault_address()) & 0x1FFFFFFF;
      guest_heap_address =
          static_cast<uint32_t>(ex->fault_address()) & ~0x1FFFFFFF;
    } else {
      // Faulting on a physical address.
      guest_address = static_cast<uint32_t>(ex->fault_address());
      guest_heap_address = 0;
    }

    // HACK: Recheck if the pages are still protected (race condition - another
    // thread clears the writewatch we just hit)
    // Do this under the lock so we don't introduce another race condition.
    auto lock = global_critical_region_.Acquire();
    memory::PageAccess cur_access;
    size_t page_length = memory::page_size();
    memory::QueryProtect((void*)fault_address, page_length, cur_access);
    if (cur_access != memory::PageAccess::kReadOnly &&
        cur_access != memory::PageAccess::kNoAccess) {
      // Another thread has cleared this write watch. Abort.
      return true;
    }

    // Access is not found within any range, so fail and let the caller handle
    // it (likely by aborting).
    // TODO(Triang3l): Don't call for the host physical memory view when legacy
    // watches are removed.
    return CheckAccessWatch(guest_address, guest_heap_address);
  }

  auto rip = ex->pc();
  auto p = reinterpret_cast<const uint8_t*>(rip);
  DecodedMov mov = {0};
  bool decoded = TryDecodeMov(p, &mov);
  if (!decoded) {
    XELOGE("Unable to decode MMIO mov at %p", p);
    assert_always("Unknown MMIO instruction type");
    return false;
  }

  if (mov.is_load) {
    // Load of a memory value - read from range, swap, and store in the
    // register.
    uint32_t value = range->read(nullptr, range->callback_context,
                                 static_cast<uint32_t>(ex->fault_address()));
    uint64_t* reg_ptr = &ex->thread_context()->int_registers[mov.value_reg];
    if (!mov.byte_swap) {
      // We swap only if it's not a movbe, as otherwise we are swapping twice.
      value = xe::byte_swap(value);
    }
    *reg_ptr = value;
  } else {
    // Store of a register value - read register, swap, write to range.
    int32_t value;
    if (mov.is_constant) {
      value = uint32_t(mov.constant);
    } else {
      uint64_t* reg_ptr = &ex->thread_context()->int_registers[mov.value_reg];
      value = static_cast<uint32_t>(*reg_ptr);
      if (!mov.byte_swap) {
        // We swap only if it's not a movbe, as otherwise we are swapping twice.
        value = xe::byte_swap(static_cast<uint32_t>(value));
      }
    }
    range->write(nullptr, range->callback_context,
                 static_cast<uint32_t>(ex->fault_address()), value);
  }

  // Advance RIP to the next instruction so that we resume properly.
  ex->set_resume_pc(rip + mov.length);

  return true;
}

}  // namespace cpu
}  // namespace xe
