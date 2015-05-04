/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_X64_X64_BACKEND_H_
#define XENIA_BACKEND_X64_X64_BACKEND_H_

#include "xenia/cpu/backend/backend.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64CodeCache;

#define XENIA_HAS_X64_BACKEND 1

typedef void* (*HostToGuestThunk)(void* target, void* arg0, void* arg1);
typedef void* (*GuestToHostThunk)(void* target, void* arg0, void* arg1);

class X64Backend : public Backend {
 public:
  X64Backend(Processor* processor);
  ~X64Backend() override;

  X64CodeCache* code_cache() const { return code_cache_; }
  HostToGuestThunk host_to_guest_thunk() const { return host_to_guest_thunk_; }
  GuestToHostThunk guest_to_host_thunk() const { return guest_to_host_thunk_; }

  int Initialize() override;

  std::unique_ptr<Assembler> CreateAssembler() override;

 private:
  X64CodeCache* code_cache_;
  HostToGuestThunk host_to_guest_thunk_;
  GuestToHostThunk guest_to_host_thunk_;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_X64_X64_BACKEND_H_
