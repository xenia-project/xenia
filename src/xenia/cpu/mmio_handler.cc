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
#include "xenia/base/memory.h"

namespace xe {
namespace cpu {

MMIOHandler* MMIOHandler::global_handler_ = nullptr;

std::unique_ptr<MMIOHandler> MMIOHandler::Install(
    uint8_t* virtual_membase, uint8_t* physical_membase, uint8_t* membase_end,
    AccessViolationCallback access_violation_callback,
    void* access_violation_callback_context) {
  // There can be only one handler at a time.
  assert_null(global_handler_);
  if (global_handler_) {
    return nullptr;
  }

  auto handler = std::unique_ptr<MMIOHandler>(new MMIOHandler(
      virtual_membase, physical_membase, membase_end, access_violation_callback,
      access_violation_callback_context));

  // Install the exception handler directed at the MMIOHandler.
  ExceptionHandler::Install(ExceptionCallbackThunk, handler.get());

  global_handler_ = handler.get();
  return handler;
}

MMIOHandler::MMIOHandler(uint8_t* virtual_membase, uint8_t* physical_membase,
                         uint8_t* membase_end,
                         AccessViolationCallback access_violation_callback,
                         void* access_violation_callback_context)
    : virtual_membase_(virtual_membase),
      physical_membase_(physical_membase),
      memory_end_(membase_end),
      access_violation_callback_(access_violation_callback),
      access_violation_callback_context_(access_violation_callback_context) {}

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

    // Recheck if the pages are still protected (race condition - another thread
    // clears the writewatch we just hit).
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

    // The address is not found within any range, so either a write watch or an
    // actual access violation.
    if (access_violation_callback_) {
      switch (ex->access_violation_operation()) {
        case Exception::AccessViolationOperation::kRead:
          return access_violation_callback_(access_violation_callback_context_,
                                            size_t(ex->fault_address()), false);
        case Exception::AccessViolationOperation::kWrite:
          return access_violation_callback_(access_violation_callback_context_,
                                            size_t(ex->fault_address()), true);
        default:
          // Data Execution Prevention or something else uninteresting.
          break;
      }
    }
    return false;
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
