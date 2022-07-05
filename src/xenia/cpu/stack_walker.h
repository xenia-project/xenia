/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_STACK_WALKER_H_
#define XENIA_CPU_STACK_WALKER_H_

#include <memory>
#include <string>

#include "xenia/base/host_thread_context.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {
class CodeCache;
}  // namespace backend
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {

struct StackFrame {
  enum class Type {
    // Host frame, likely in kernel or emulator code.
    kHost,
    // Guest frame, somewhere in PPC code.
    kGuest,
  };
  Type type;

  // Always valid, indicating the address in a backend-defined range.
  uint64_t host_pc;
  // Only valid for kGuest frames, indicating the PPC address.
  uint32_t guest_pc;

  union {
    // Contains symbol information for kHost frames.
    struct {
      // TODO(benvanik): better name, displacement, etc.
      uint64_t address;
      char name[256];
    } host_symbol;
    // Contains symbol information for kGuest frames.
    struct {
      Function* function;
    } guest_symbol;
  };
};

class StackWalker {
 public:
  // Creates a stack walker. Only one should exist within a process.
  // May fail if another process has mucked with ours (like RenderDoc).
  static std::unique_ptr<StackWalker> Create(backend::CodeCache* code_cache);

  // Dumps all thread stacks to the log.
  void Dump();

  // Captures up to the given number of stack frames from the current thread.
  // Use ResolveStackTrace to populate additional information.
  // Returns the number of frames captured, or 0 if an error occurred.
  // Optionally provides a hash value for the stack that can be used for
  // deduping.
  virtual size_t CaptureStackTrace(uint64_t* frame_host_pcs,
                                   size_t frame_offset, size_t frame_count,
                                   uint64_t* out_stack_hash = nullptr) = 0;

  // Captures up to the given number of stack frames from the given thread,
  // referenced by native thread handle. The thread must be suspended.
  // This does not populate any information other than host_pc.
  // Use ResolveStackTrace to populate additional information.
  // Returns the number of frames captured, or 0 if an error occurred.
  // Optionally provides a hash value for the stack that can be used for
  // deduping.
  virtual size_t CaptureStackTrace(void* thread_handle,
                                   uint64_t* frame_host_pcs,
                                   size_t frame_offset, size_t frame_count,
                                   const HostThreadContext* in_host_context,
                                   HostThreadContext* out_host_context,
                                   uint64_t* out_stack_hash = nullptr) = 0;

  // Resolves symbol information for the given stack frames.
  // Each frame provided must have host_pc set, and all other fields will be
  // populated.
  virtual bool ResolveStack(uint64_t* frame_host_pcs, StackFrame* frames,
                            size_t frame_count) = 0;

  virtual ~StackWalker() = default;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_STACK_WALKER_H_
