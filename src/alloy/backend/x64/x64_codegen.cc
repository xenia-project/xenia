/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_codegen.h>

#include <alloy/backend/x64/x64_backend.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::runtime;


X64Codegen::X64Codegen(X64Backend* backend) :
    backend_(backend) {
}

X64Codegen::~X64Codegen() {
}

int X64Codegen::Initialize() {
  return 0;
}

void X64Codegen::Reset() {
}
