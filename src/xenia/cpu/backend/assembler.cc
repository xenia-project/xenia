/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/assembler.h"

namespace xe {
namespace cpu {
namespace backend {

Assembler::Assembler(Backend* backend) : backend_(backend) {}

Assembler::~Assembler() { Reset(); }

int Assembler::Initialize() { return 0; }

void Assembler::Reset() {}

}  // namespace backend
}  // namespace cpu
}  // namespace xe
