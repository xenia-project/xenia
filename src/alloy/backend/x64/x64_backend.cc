/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_backend.h>

#include <alloy/backend/x64/tracing.h>
#include <alloy/backend/x64/x64_assembler.h>
#include <alloy/backend/x64/x64_code_cache.h>
#include <alloy/backend/x64/lowering/lowering_table.h>
#include <alloy/backend/x64/lowering/lowering_sequences.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;
using namespace alloy::runtime;


X64Backend::X64Backend(Runtime* runtime) :
    code_cache_(0), lowering_table_(0),
    Backend(runtime) {
}

X64Backend::~X64Backend() {
  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
  delete lowering_table_;
  delete code_cache_;
}

int X64Backend::Initialize() {
  int result = Backend::Initialize();
  if (result) {
    return result;
  }

  code_cache_ = new X64CodeCache();
  result = code_cache_->Initialize();
  if (result) {
    return result;
  }

  lowering_table_ = new LoweringTable(this);
  RegisterSequences(lowering_table_);

  alloy::tracing::WriteEvent(EventType::Init({
  }));

  return result;
}

Assembler* X64Backend::CreateAssembler() {
  return new X64Assembler(this);
}
