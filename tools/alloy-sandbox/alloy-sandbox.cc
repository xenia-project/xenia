/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>
#include <alloy/alloy.h>

#include <alloy/runtime/raw_module.h>
#include <xenia/cpu/xenon_memory.h>
#include <xenia/cpu/xenon_runtime.h>

#include <gflags/gflags.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::runtime;
using namespace xe;
using namespace xe::cpu;


int alloy_sandbox(int argc, xechar_t** argv) {
  int result_code = 1;

  XenonMemory* memory = new XenonMemory();

  ExportResolver* export_resolver = new ExportResolver();
  XenonRuntime* runtime = new XenonRuntime(memory, export_resolver);

  Backend* backend = 0;
  // Backend* backend = new alloy::backend::interpreter::InterpreterBackend(
  //     runtime);
  // Backend* backend = new alloy::backend::x64::X64Backend(
  //     runtime);
  runtime->Initialize(backend);

  RawModule* module = new RawModule(runtime->memory());
  module->LoadFile(0x82000000, "test\\codegen\\instr_add.bin");
  runtime->AddModule(module);

  XenonThreadState* thread_state = new XenonThreadState(
      runtime, 100, 64 * 1024, 0);

  Function* fn;
  runtime->ResolveFunction(0x82000000, &fn);
  fn->Call(thread_state);

  delete thread_state;

  delete runtime;
  delete memory;

  return result_code;
}
// ehhh
#include <xenia/platform.cc>
XE_MAIN_THUNK(alloy_sandbox, "alloy-sandbox");
