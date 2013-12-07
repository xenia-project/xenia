/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/compiler.h>

#include <alloy/compiler/pass.h>
#include <alloy/compiler/tracing.h>

using namespace alloy;
using namespace alloy::compiler;
using namespace alloy::hir;


Compiler::Compiler() {
  alloy::tracing::WriteEvent(EventType::Init({
  }));
}

Compiler::~Compiler() {
  Reset();
  
  for (PassList::iterator it = passes_.begin(); it != passes_.end(); ++it) {
    Pass* pass = *it;
    delete pass;
  }

  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

void Compiler::AddPass(Pass* pass) {
  passes_.push_back(pass);
}

void Compiler::Reset() {
}

int Compiler::Compile(FunctionBuilder* builder) {
  // TODO(benvanik): sophisticated stuff. Run passes in parallel, run until they
  //                 stop changing things, etc.
  for (PassList::iterator it = passes_.begin(); it != passes_.end(); ++it) {
    Pass* pass = *it;
    //
  }

  return 0;
}