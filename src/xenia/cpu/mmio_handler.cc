/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/mmio_handler.h"

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace BE {
#include <beaengine/BeaEngine.h>
}  // namespace BE

namespace xe {
namespace cpu {

MMIOHandler* MMIOHandler::global_handler_ = nullptr;

// Implemented in the platform cc file.
std::unique_ptr<MMIOHandler> CreateMMIOHandler(uint8_t* virtual_membase,
                                               uint8_t* physical_membase);

std::unique_ptr<MMIOHandler> MMIOHandler::Install(uint8_t* virtual_membase,
                                                  uint8_t* physical_membase) {
  // There can be only one handler at a time.
  assert_null(global_handler_);
  if (global_handler_) {
    return nullptr;
  }

  // Create the platform-specific handler.
  auto handler = CreateMMIOHandler(virtual_membase, physical_membase);

  // Platform-specific initialization for the handler.
  if (!handler->Initialize()) {
    return nullptr;
  }

  global_handler_ = handler.get();
  return handler;
}

MMIOHandler::~MMIOHandler() {
  assert_true(global_handler_ == this);
  global_handler_ = nullptr;
}

bool MMIOHandler::RegisterRange(uint32_t virtual_address, uint32_t mask,
                                uint32_t size, void* context,
                                MMIOReadCallback read_callback,
                                MMIOWriteCallback write_callback) {
  mapped_ranges_.push_back({
      virtual_address, mask, size, context, read_callback, write_callback,
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

bool MMIOHandler::CheckLoad(uint32_t virtual_address, uint64_t* out_value) {
  for (const auto& range : mapped_ranges_) {
    if ((virtual_address & range.mask) == range.address) {
      *out_value = static_cast<uint32_t>(
          range.read(nullptr, range.callback_context, virtual_address));
      return true;
    }
  }
  return false;
}

bool MMIOHandler::CheckStore(uint32_t virtual_address, uint64_t value) {
  for (const auto& range : mapped_ranges_) {
    if ((virtual_address & range.mask) == range.address) {
      range.write(nullptr, range.callback_context, virtual_address, value);
      return true;
    }
  }
  return false;
}

uintptr_t MMIOHandler::AddPhysicalWriteWatch(uint32_t guest_address,
                                             size_t length,
                                             WriteWatchCallback callback,
                                             void* callback_context,
                                             void* callback_data) {
  uint32_t base_address = guest_address;
  assert_true(base_address < 0x1FFFFFFF);

  // Can only protect sizes matching system page size.
  // This means we need to round up, which will cause spurious access
  // violations and invalidations.
  // TODO(benvanik): only invalidate if actually within the region?
  length = xe::round_up(length + (base_address % xe::memory::page_size()),
                        xe::memory::page_size());
  base_address = base_address - (base_address % xe::memory::page_size());

  // Add to table. The slot reservation may evict a previous watch, which
  // could include our target, so we do it first.
  auto entry = new WriteWatchEntry();
  entry->address = base_address;
  entry->length = uint32_t(length);
  entry->callback = callback;
  entry->callback_context = callback_context;
  entry->callback_data = callback_data;
  write_watch_mutex_.lock();
  write_watches_.push_back(entry);
  write_watch_mutex_.unlock();

  // Make the desired range read only under all address spaces.
  xe::memory::Protect(physical_membase_ + entry->address, entry->length,
                      xe::memory::PageAccess::kReadOnly, nullptr);
  xe::memory::Protect(virtual_membase_ + 0xA0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadOnly,
                      nullptr);
  xe::memory::Protect(virtual_membase_ + 0xC0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadOnly,
                      nullptr);
  xe::memory::Protect(virtual_membase_ + 0xE0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadOnly,
                      nullptr);

  return reinterpret_cast<uintptr_t>(entry);
}

void MMIOHandler::ClearWriteWatch(WriteWatchEntry* entry) {
  xe::memory::Protect(physical_membase_ + entry->address, entry->length,
                      xe::memory::PageAccess::kReadWrite, nullptr);
  xe::memory::Protect(virtual_membase_ + 0xA0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadWrite,
                      nullptr);
  xe::memory::Protect(virtual_membase_ + 0xC0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadWrite,
                      nullptr);
  xe::memory::Protect(virtual_membase_ + 0xE0000000 + entry->address,
                      entry->length, xe::memory::PageAccess::kReadWrite,
                      nullptr);
}

void MMIOHandler::CancelWriteWatch(uintptr_t watch_handle) {
  auto entry = reinterpret_cast<WriteWatchEntry*>(watch_handle);

  // Allow access to the range again.
  ClearWriteWatch(entry);

  // Remove from table.
  write_watch_mutex_.lock();
  auto it = std::find(write_watches_.begin(), write_watches_.end(), entry);
  if (it != write_watches_.end()) {
    write_watches_.erase(it);
  }
  write_watch_mutex_.unlock();

  delete entry;
}

bool MMIOHandler::CheckWriteWatch(void* thread_state, uint64_t fault_address) {
  uint32_t physical_address = uint32_t(fault_address);
  if (physical_address > 0x1FFFFFFF) {
    physical_address &= 0x1FFFFFFF;
  }
  std::list<WriteWatchEntry*> pending_invalidates;
  write_watch_mutex_.lock();
  for (auto it = write_watches_.begin(); it != write_watches_.end();) {
    auto entry = *it;
    if (entry->address <= physical_address &&
        entry->address + entry->length > physical_address) {
      // Hit!
      pending_invalidates.push_back(entry);
      // TODO(benvanik): outside of lock?
      ClearWriteWatch(entry);
      auto erase_it = it;
      ++it;
      write_watches_.erase(erase_it);
      continue;
    }
    ++it;
  }
  write_watch_mutex_.unlock();
  if (pending_invalidates.empty()) {
    // Rethrow access violation - range was not being watched.
    return false;
  }
  while (!pending_invalidates.empty()) {
    auto entry = pending_invalidates.back();
    pending_invalidates.pop_back();
    entry->callback(entry->callback_context, entry->callback_data,
                    physical_address);
    delete entry;
  }
  // Range was watched, so lets eat this access violation.
  return true;
}

bool MMIOHandler::HandleAccessFault(void* thread_state,
                                    uint64_t fault_address) {
  if (fault_address < uint64_t(virtual_membase_)) {
    // Quick kill anything below our mapping base.
    return false;
  }

  // Access violations are pretty rare, so we can do a linear search here.
  // Only check if in the virtual range, as we only support virtual ranges.
  const MMIORange* range = nullptr;
  if (fault_address < uint64_t(physical_membase_)) {
    for (const auto& test_range : mapped_ranges_) {
      if ((uint32_t(fault_address) & test_range.mask) == test_range.address) {
        // Address is within the range of this mapping.
        range = &test_range;
        break;
      }
    }
  }
  if (!range) {
    // Access is not found within any range, so fail and let the caller handle
    // it (likely by aborting).
    return CheckWriteWatch(thread_state, fault_address);
  }

  // TODO(benvanik): replace with simple check of mov (that's all
  //     we care about).
  auto rip = GetThreadStateRip(thread_state);
  BE::DISASM disasm = {0};
  disasm.Archi = 64;
  disasm.Options = BE::MasmSyntax + BE::PrefixedNumeral;
  disasm.EIP = static_cast<BE::UIntPtr>(rip);
  size_t instr_length = BE::Disasm(&disasm);
  if (instr_length == BE::UNKNOWN_OPCODE) {
    // Failed to decode instruction. Either it's an unhandled mov case or
    // not a mov.
    assert_always();
    return false;
  }

  int32_t arg1_type = disasm.Argument1.ArgType;
  int32_t arg2_type = disasm.Argument2.ArgType;
  bool is_load = (arg1_type & BE::REGISTER_TYPE) == BE::REGISTER_TYPE &&
                 (arg1_type & BE::GENERAL_REG) == BE::GENERAL_REG &&
                 (disasm.Argument1.AccessMode & BE::WRITE) == BE::WRITE;
  bool is_store = (arg1_type & BE::MEMORY_TYPE) == BE::MEMORY_TYPE &&
                  (((arg2_type & BE::REGISTER_TYPE) == BE::REGISTER_TYPE &&
                    (arg2_type & BE::GENERAL_REG) == BE::GENERAL_REG) ||
                   (arg2_type & BE::CONSTANT_TYPE) == BE::CONSTANT_TYPE) &&
                  (disasm.Argument1.AccessMode & BE::WRITE) == BE::WRITE;
  if (is_load) {
    // Load of a memory value - read from range, swap, and store in the
    // register.
    uint64_t value = range->read(nullptr, range->callback_context,
                                 fault_address & 0xFFFFFFFF);
    uint32_t be_reg_index;
    if (!xe::bit_scan_forward(arg1_type & 0xFFFF, &be_reg_index)) {
      be_reg_index = 0;
    }
    uint64_t* reg_ptr = GetThreadStateRegPtr(thread_state, be_reg_index);
    switch (disasm.Argument1.ArgSize) {
      case 8:
        *reg_ptr = static_cast<uint8_t>(value);
        break;
      case 16:
        *reg_ptr = xe::byte_swap(static_cast<uint16_t>(value));
        break;
      case 32:
        *reg_ptr = xe::byte_swap(static_cast<uint32_t>(value));
        break;
      case 64:
        *reg_ptr = xe::byte_swap(static_cast<uint64_t>(value));
        break;
    }
  } else if (is_store) {
    // Store of a register value - read register, swap, write to range.
    uint64_t value = 0;
    if ((arg2_type & BE::REGISTER_TYPE) == BE::REGISTER_TYPE) {
      uint32_t be_reg_index;
      if (!xe::bit_scan_forward(arg2_type & 0xFFFF, &be_reg_index)) {
        be_reg_index = 0;
      }
      uint64_t* reg_ptr = GetThreadStateRegPtr(thread_state, be_reg_index);
      value = *reg_ptr;
    } else if ((arg2_type & BE::CONSTANT_TYPE) == BE::CONSTANT_TYPE) {
      value = disasm.Instruction.Immediat;
    } else {
      // Unknown destination type in mov.
      assert_always();
    }
    switch (disasm.Argument2.ArgSize) {
      case 8:
        value = static_cast<uint8_t>(value);
        break;
      case 16:
        value = xe::byte_swap(static_cast<uint16_t>(value));
        break;
      case 32:
        value = xe::byte_swap(static_cast<uint32_t>(value));
        break;
      case 64:
        value = xe::byte_swap(static_cast<uint64_t>(value));
        break;
    }
    range->write(nullptr, range->callback_context, fault_address & 0xFFFFFFFF,
                 value);
  } else {
    assert_always("Unknown MMIO instruction type");
    return false;
  }

  // Advance RIP to the next instruction so that we resume properly.
  SetThreadStateRip(thread_state, rip + instr_length);

  return true;
}

}  // namespace cpu
}  // namespace xe
