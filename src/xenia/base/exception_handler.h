/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_EXCEPTION_HANDLER_H_
#define XENIA_BASE_EXCEPTION_HANDLER_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/host_thread_context.h"

namespace xe {

// AArch64 load and store decoding based on VIXL.
// https://github.com/Linaro/vixl/blob/ae5957cd66517b3f31dbf37e9bf39db6594abfe3/src/aarch64/constants-aarch64.h
//
// Copyright 2015, VIXL authors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

constexpr uint32_t kArm64LoadStoreAnyMask = UINT32_C(0x0A000000);
constexpr uint32_t kArm64LoadStoreAnyValue = UINT32_C(0x08000000);
constexpr uint32_t kArm64LoadStorePairAnyMask = UINT32_C(0x3A000000);
constexpr uint32_t kArm64LoadStorePairAnyValue = UINT32_C(0x28000000);
constexpr uint32_t kArm64LoadStorePairLoadBit = UINT32_C(1) << 22;
constexpr uint32_t kArm64LoadStoreMask = UINT32_C(0xC4C00000);

enum class Arm64LoadStoreOp : uint32_t {
  kSTRB_w = UINT32_C(0x00000000),
  kSTRH_w = UINT32_C(0x40000000),
  kSTR_w = UINT32_C(0x80000000),
  kSTR_x = UINT32_C(0xC0000000),
  kLDRB_w = UINT32_C(0x00400000),
  kLDRH_w = UINT32_C(0x40400000),
  kLDR_w = UINT32_C(0x80400000),
  kLDR_x = UINT32_C(0xC0400000),
  kLDRSB_x = UINT32_C(0x00800000),
  kLDRSH_x = UINT32_C(0x40800000),
  kLDRSW_x = UINT32_C(0x80800000),
  kLDRSB_w = UINT32_C(0x00C00000),
  kLDRSH_w = UINT32_C(0x40C00000),
  kSTR_b = UINT32_C(0x04000000),
  kSTR_h = UINT32_C(0x44000000),
  kSTR_s = UINT32_C(0x84000000),
  kSTR_d = UINT32_C(0xC4000000),
  kSTR_q = UINT32_C(0x04800000),
  kLDR_b = UINT32_C(0x04400000),
  kLDR_h = UINT32_C(0x44400000),
  kLDR_s = UINT32_C(0x84400000),
  kLDR_d = UINT32_C(0xC4400000),
  kLDR_q = UINT32_C(0x04C00000),
  kPRFM = UINT32_C(0xC0800000),
};

bool IsArm64LoadPrefetchStore(uint32_t instruction, bool& is_store_out);

class Exception {
 public:
  enum class Code {
    kInvalidException = 0,
    kAccessViolation,
    kIllegalInstruction,
  };

  enum class AccessViolationOperation {
    kUnknown,
    kRead,
    kWrite,
  };

  void InitializeAccessViolation(HostThreadContext* thread_context,
                                 uint64_t fault_address,
                                 AccessViolationOperation operation) {
    code_ = Code::kAccessViolation;
    thread_context_ = thread_context;
    fault_address_ = fault_address;
    access_violation_operation_ = operation;
  }
  void InitializeIllegalInstruction(HostThreadContext* thread_context) {
    code_ = Code::kIllegalInstruction;
    thread_context_ = thread_context;
  }

  Code code() const { return code_; }

  // Returns the platform-specific thread context info.
  HostThreadContext* thread_context() const { return thread_context_; }

  // Returns the program counter where the exception occurred.
  uint64_t pc() const {
#if XE_ARCH_AMD64
    return thread_context_->rip;
#elif XE_ARCH_ARM64
    return thread_context_->pc;
#else
    assert_always();
    return 0;
#endif  // XE_ARCH
  }

  // Sets the program counter where execution will resume.
  void set_resume_pc(uint64_t pc) {
#if XE_ARCH_AMD64
    thread_context_->rip = pc;
#elif XE_ARCH_ARM64
    thread_context_->pc = pc;
#else
    assert_always();
#endif  // XE_ARCH
  }

  // In case of AV, address that was read from/written to.
  uint64_t fault_address() const { return fault_address_; }

  // In case of AV, what kind of operation caused it.
  AccessViolationOperation access_violation_operation() const {
    return access_violation_operation_;
  }

 private:
  Code code_ = Code::kInvalidException;
  HostThreadContext* thread_context_ = nullptr;
  uint64_t fault_address_ = 0;
  AccessViolationOperation access_violation_operation_ =
      AccessViolationOperation::kUnknown;
};

class ExceptionHandler {
 public:
  typedef bool (*Handler)(Exception* ex, void* data);

  // Installs an exception handler.
  // Handlers are called in the order they are installed.
  static void Install(Handler fn, void* data);

  // Uninstalls a previously-installed exception handler.
  static void Uninstall(Handler fn, void* data);
};

}  // namespace xe

#endif  // XENIA_BASE_EXCEPTION_HANDLER_H_
