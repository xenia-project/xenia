/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

namespace xe {

// Based on VIXL Instruction::IsLoad and IsStore.
// https://github.com/Linaro/vixl/blob/d48909dd0ac62197edb75d26ed50927e4384a199/src/aarch64/instructions-aarch64.cc#L484
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
bool IsArm64LoadPrefetchStore(uint32_t instruction, bool& is_store_out) {
  if ((instruction & kArm64LoadStoreAnyMask) != kArm64LoadStoreAnyValue) {
    return false;
  }
  if ((instruction & kArm64LoadStorePairAnyMask) ==
      kArm64LoadStorePairAnyValue) {
    is_store_out = !(instruction & kArm64LoadStorePairLoadBit);
    return true;
  }
  switch (Arm64LoadStoreOp(instruction & kArm64LoadStoreMask)) {
    case Arm64LoadStoreOp::kLDRB_w:
    case Arm64LoadStoreOp::kLDRH_w:
    case Arm64LoadStoreOp::kLDR_w:
    case Arm64LoadStoreOp::kLDR_x:
    case Arm64LoadStoreOp::kLDRSB_x:
    case Arm64LoadStoreOp::kLDRSH_x:
    case Arm64LoadStoreOp::kLDRSW_x:
    case Arm64LoadStoreOp::kLDRSB_w:
    case Arm64LoadStoreOp::kLDRSH_w:
    case Arm64LoadStoreOp::kLDR_b:
    case Arm64LoadStoreOp::kLDR_h:
    case Arm64LoadStoreOp::kLDR_s:
    case Arm64LoadStoreOp::kLDR_d:
    case Arm64LoadStoreOp::kLDR_q:
    case Arm64LoadStoreOp::kPRFM:
      is_store_out = false;
      return true;
    case Arm64LoadStoreOp::kSTRB_w:
    case Arm64LoadStoreOp::kSTRH_w:
    case Arm64LoadStoreOp::kSTR_w:
    case Arm64LoadStoreOp::kSTR_x:
    case Arm64LoadStoreOp::kSTR_b:
    case Arm64LoadStoreOp::kSTR_h:
    case Arm64LoadStoreOp::kSTR_s:
    case Arm64LoadStoreOp::kSTR_d:
    case Arm64LoadStoreOp::kSTR_q:
      is_store_out = true;
      return true;
    default:
      return false;
  }
}

}  // namespace xe
