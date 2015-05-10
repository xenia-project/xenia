/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_FUNCTION_DATA_H_
#define XENIA_DEBUG_FUNCTION_DATA_H_

#include <cstdint>

#include "xenia/base/memory.h"

namespace xe {
namespace debug {

class FunctionData {
 public:
  struct Header {
    // Format is used by tooling, changes must be made across all targets.
    // + 0   4b   (data size)
    // + 4   4b   start_address
    // + 8   4b   end_address
    // +12   4b   type (user, external, etc)
    // +16   4b   source_map_entry_count
    //
    // +20   4b   source_disasm_length
    // +20   4b   raw_hir_disasm_length
    // +20   4b   hir_disasm_length
    // +20   4b   machine_code_disasm_length
    // +20   12b* source_map_entries
    uint32_t data_size;
    uint32_t start_address;
    uint32_t end_address;
    uint32_t type;
    /*
    source_map_count
    source_map[] : {ppc_address, hir_offset, code_offset}
    raw_hir_disasm_ (len + chars)
    hir_disasm_ (len + chars)
    machine_code_ (len + bytes) (without tracing? regen?)
    */
  };

  FunctionData() : header_(nullptr) {}

  void Reset(uint8_t* trace_data, size_t trace_data_size,
             uint32_t start_address, uint32_t end_address) {
    header_ = reinterpret_cast<Header*>(trace_data);
    header_->data_size = uint32_t(trace_data_size);
    header_->start_address = start_address;
    header_->end_address = end_address;
    header_->type = 0;
    // Clear any remaining.
    std::memset(trace_data + sizeof(Header), 0,
                trace_data_size - sizeof(Header));
  }

  bool is_valid() const { return header_ != nullptr; }

  uint32_t start_address() const { return header_->start_address; }
  uint32_t end_address() const { return header_->end_address; }
  uint32_t instruction_count() const {
    return (header_->end_address - header_->start_address) / 4 + 1;
  }

  Header* header() const { return header_; }

  static size_t SizeOfHeader() { return sizeof(Header); }

 private:
  Header* header_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_FUNCTION_DATA_H_
