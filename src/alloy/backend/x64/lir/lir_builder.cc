/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lir/lir_builder.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lir;


LIRBuilder::LIRBuilder(X64Backend* backend) :
    backend_(backend) {
}

LIRBuilder::~LIRBuilder() {
}

void LIRBuilder::Reset() {
}

int LIRBuilder::Finalize() {
  return 0;
}

void LIRBuilder::Dump(StringBuffer* str) {
}
