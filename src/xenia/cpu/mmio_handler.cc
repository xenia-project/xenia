/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/mmio_handler.h>

#include <poly/poly.h>

namespace BE {
#include <beaengine/BeaEngine.h>
}  // namespace BE

namespace xe {
namespace cpu {

MMIOHandler* MMIOHandler::global_handler_ = nullptr;

// Implemented in the platform cc file.
std::unique_ptr<MMIOHandler> CreateMMIOHandler(uint8_t* mapping_base);

std::unique_ptr<MMIOHandler> MMIOHandler::Install(uint8_t* mapping_base) {
  // There can be only one handler at a time.
  assert_null(global_handler_);
  if (global_handler_) {
    return nullptr;
  }

  // Create the platform-specific handler.
  auto handler = CreateMMIOHandler(mapping_base);

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

bool MMIOHandler::RegisterRange(uint64_t address, uint64_t mask, uint64_t size,
                                void* context, MMIOReadCallback read_callback,
                                MMIOWriteCallback write_callback) {
  mapped_ranges_.push_back({
      reinterpret_cast<uint64_t>(mapping_base_) | address,
      0xFFFFFFFF00000000ull | mask, size, context, read_callback,
      write_callback,
  });
  return true;
}

bool MMIOHandler::CheckLoad(uint64_t address, uint64_t* out_value) {
  for (const auto& range : mapped_ranges_) {
    if (((address | (uint64_t)mapping_base_) & range.mask) == range.address) {
      *out_value = static_cast<uint32_t>(range.read(range.context, address));
      return true;
    }
  }
  return false;
}

bool MMIOHandler::CheckStore(uint64_t address, uint64_t value) {
  for (const auto& range : mapped_ranges_) {
    if (((address | (uint64_t)mapping_base_) & range.mask) == range.address) {
      range.write(range.context, address, value);
      return true;
    }
  }
  return false;
}

bool MMIOHandler::HandleAccessFault(void* thread_state,
                                    uint64_t fault_address) {
  // Access violations are pretty rare, so we can do a linear search here.
  const MMIORange* range = nullptr;
  for (const auto& test_range : mapped_ranges_) {
    if ((fault_address & test_range.mask) == test_range.address) {
      // Address is within the range of this mapping.
      range = &test_range;
      break;
    }
  }
  if (!range) {
    // Access is not found within any range, so fail and let the caller handle
    // it (likely by aborting).
    return false;
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
    uint64_t value = range->read(range->context, fault_address & 0xFFFFFFFF);
    uint32_t be_reg_index;
    if (!poly::bit_scan_forward(arg1_type & 0xFFFF, &be_reg_index)) {
      be_reg_index = 0;
    }
    uint64_t* reg_ptr = GetThreadStateRegPtr(thread_state, be_reg_index);
    switch (disasm.Argument1.ArgSize) {
      case 8:
        *reg_ptr = static_cast<uint8_t>(value);
        break;
      case 16:
        *reg_ptr = poly::byte_swap(static_cast<uint16_t>(value));
        break;
      case 32:
        *reg_ptr = poly::byte_swap(static_cast<uint32_t>(value));
        break;
      case 64:
        *reg_ptr = poly::byte_swap(static_cast<uint64_t>(value));
        break;
    }
  } else if (is_store) {
    // Store of a register value - read register, swap, write to range.
    uint64_t value;
    if ((arg2_type & BE::REGISTER_TYPE) == BE::REGISTER_TYPE) {
      uint32_t be_reg_index;
      if (!poly::bit_scan_forward(arg2_type & 0xFFFF, &be_reg_index)) {
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
        value = poly::byte_swap(static_cast<uint16_t>(value));
        break;
      case 32:
        value = poly::byte_swap(static_cast<uint32_t>(value));
        break;
      case 64:
        value = poly::byte_swap(static_cast<uint64_t>(value));
        break;
    }
    range->write(range->context, fault_address & 0xFFFFFFFF, value);
  } else {
    // Unknown MMIO instruction type.
    assert_always();
    return false;
  }

  // Advance RIP to the next instruction so that we resume properly.
  SetThreadStateRip(thread_state, rip + instr_length);

  return true;
}

}  // namespace cpu
}  // namespace xe
