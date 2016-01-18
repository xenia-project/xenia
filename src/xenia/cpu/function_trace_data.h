/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FUNCTION_TRACE_DATA_H_
#define XENIA_CPU_FUNCTION_TRACE_DATA_H_

#include <cstdint>
#include <cstring>

#include "xenia/base/memory.h"

namespace xe {
namespace cpu {

class FunctionTraceData {
 public:
  static const int kFunctionCallerHistoryCount = 4;

  struct Header {
    // Format is used by tooling, changes must be made across all targets.
    // + 0   4b  (data size)
    // + 4   4b  start_address
    // + 8   4b  end_address
    // +12   4b  type (user, external, etc)
    // +16   8b  function_thread_use  // bitmask of thread id
    // +24   8b  function_call_count
    // +32   4b+ function_caller_history[4]
    // +48   8b+ instruction_execute_count[instruction count]
    uint32_t data_size;
    uint32_t start_address;
    uint32_t end_address;
    uint32_t type;
    uint64_t function_thread_use;
    uint64_t function_call_count;
    uint32_t function_caller_history[kFunctionCallerHistoryCount];
    // uint64_t instruction_execute_count[];
  };

  FunctionTraceData() : header_(nullptr) {}

  void Reset(uint8_t* trace_data, size_t trace_data_size,
             uint32_t start_address, uint32_t end_address) {
    header_ = reinterpret_cast<Header*>(trace_data);
    header_->data_size = uint32_t(trace_data_size);
    header_->start_address = start_address;
    header_->end_address = end_address;
    header_->type = 0;
    header_->function_thread_use = 0;
    header_->function_call_count = 0;
    for (int i = 0; i < kFunctionCallerHistoryCount; ++i) {
      header_->function_caller_history[i] = 0;
    }
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

  uint8_t* instruction_execute_counts() const {
    return reinterpret_cast<uint8_t*>(header_) + sizeof(Header);
  }

  static size_t SizeOfHeader() { return sizeof(Header); }

  static size_t SizeOfInstructionCounts(uint32_t start_address,
                                        uint32_t end_address) {
    uint32_t instruction_count = (end_address - start_address) / 4 + 1;
    return instruction_count * 8;
  }

 private:
  Header* header_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FUNCTION_TRACE_DATA_H_
