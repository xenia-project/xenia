/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FUNCTION_H_
#define XENIA_CPU_FUNCTION_H_

#include <memory>
#include <mutex>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/cpu/debug_info.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/debug/breakpoint.h"

namespace xe {
namespace cpu {

class FunctionInfo;

struct SourceMapEntry {
  uint32_t source_offset;  // Original source address/offset.
  uint32_t hir_offset;     // Block ordinal (16b) | Instr ordinal (16b)
  uint32_t code_offset;    // Offset from emitted code start.
};

class Function {
 public:
  Function(FunctionInfo* symbol_info);
  virtual ~Function();

  uint32_t address() const { return address_; }
  FunctionInfo* symbol_info() const { return symbol_info_; }

  virtual uint8_t* machine_code() const = 0;
  virtual size_t machine_code_length() const = 0;

  DebugInfo* debug_info() const { return debug_info_.get(); }
  void set_debug_info(std::unique_ptr<DebugInfo> debug_info) {
    debug_info_ = std::move(debug_info);
  }
  std::vector<SourceMapEntry>& source_map() { return source_map_; }

  const SourceMapEntry* LookupSourceOffset(uint32_t offset) const;
  const SourceMapEntry* LookupHIROffset(uint32_t offset) const;
  const SourceMapEntry* LookupCodeOffset(uint32_t offset) const;

  bool AddBreakpoint(debug::Breakpoint* breakpoint);
  bool RemoveBreakpoint(debug::Breakpoint* breakpoint);

  bool Call(ThreadState* thread_state, uint32_t return_address);

 protected:
  debug::Breakpoint* FindBreakpoint(uint32_t address);
  virtual bool AddBreakpointImpl(debug::Breakpoint* breakpoint) { return 0; }
  virtual bool RemoveBreakpointImpl(debug::Breakpoint* breakpoint) { return 0; }
  virtual bool CallImpl(ThreadState* thread_state, uint32_t return_address) = 0;

 protected:
  uint32_t address_;
  FunctionInfo* symbol_info_;
  std::unique_ptr<DebugInfo> debug_info_;
  std::vector<SourceMapEntry> source_map_;

  // TODO(benvanik): move elsewhere? DebugData?
  xe::mutex lock_;
  std::vector<debug::Breakpoint*> breakpoints_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FUNCTION_H_
