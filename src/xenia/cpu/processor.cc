/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/processor.h>

#include <jansson.h>

#include <xenia/emulator.h>
#include <xenia/cpu/xenon_memory.h>
#include <xenia/cpu/xenon_runtime.h>
#include <xenia/cpu/xex_module.h>


using namespace alloy;
using namespace alloy::backend;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;
using namespace xe;
using namespace xe::cpu;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    //ppc::RegisterDisasmCategoryAltivec();
    //ppc::RegisterDisasmCategoryALU();
    //ppc::RegisterDisasmCategoryControl();
    //ppc::RegisterDisasmCategoryFPU();
    //ppc::RegisterDisasmCategoryMemory();

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


Processor::Processor(Emulator* emulator) :
    emulator_(emulator), export_resolver_(emulator->export_resolver()),
    runtime_(0), memory_(emulator->memory()),
    interrupt_thread_lock_(NULL), interrupt_thread_state_(NULL),
    interrupt_thread_block_(0),
    DebugTarget(emulator->debug_server()) {
  InitializeIfNeeded();

  emulator_->debug_server()->AddTarget("cpu", this);
}

Processor::~Processor() {
  emulator_->debug_server()->RemoveTarget("cpu");

  if (interrupt_thread_block_) {
    memory_->HeapFree(interrupt_thread_block_, 2048);
    delete interrupt_thread_state_;
    xe_mutex_free(interrupt_thread_lock_);
  }

  delete runtime_;
}

int Processor::Setup() {
  XEASSERTNULL(runtime_);

  runtime_ = new XenonRuntime(memory_, export_resolver_);
  if (!runtime_) {
    return 1;
  }

  Backend* backend = 0;
  // Backend* backend = new alloy::backend::ivm::IVMBackend(
  //     runtime);
  // Backend* backend = new alloy::backend::x64::X64Backend(
  //     runtime);
  int result = runtime_->Initialize(backend);
  if (result) {
    return result;
  }

  interrupt_thread_lock_ = xe_mutex_alloc(10000);
  interrupt_thread_state_ = new XenonThreadState(
      runtime_, 0, 16 * 1024, 0);
  interrupt_thread_block_ = memory_->HeapAlloc(
      0, 2048, MEMORY_FLAG_ZERO);
  interrupt_thread_state_->context()->r[13] = interrupt_thread_block_;

  return 0;
}

void Processor::AddRegisterAccessCallbacks(
    xe::cpu::RegisterAccessCallbacks callbacks) {
  runtime_->AddRegisterAccessCallbacks(callbacks);
}

int Processor::Execute(XenonThreadState* thread_state, uint64_t address) {
  // Attempt to get the function.
  Function* fn;
  if (runtime_->ResolveFunction(address, &fn)) {
    // Symbol not found in any module.
    XELOGCPU("Execute(%.8X): failed to find function", address);
    return 1;
  }

  PPCContext* context = thread_state->context();

  // This could be set to anything to give us a unique identifier to track
  // re-entrancy/etc.
  uint32_t lr = 0xBEBEBEBE;

  // Setup registers.
  context->lr = lr;

  // Execute the function.
  fn->Call(thread_state, lr);
  return 0;
}

uint64_t Processor::Execute(
    XenonThreadState* thread_state, uint64_t address, uint64_t arg0) {
  PPCContext* context = thread_state->context();
  context->r[3] = arg0;
  if (Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::Execute(
    XenonThreadState* thread_state, uint64_t address, uint64_t arg0,
    uint64_t arg1) {
  PPCContext* context = thread_state->context();
  context->r[3] = arg0;
  context->r[4] = arg1;
  if (Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::ExecuteInterrupt(
    uint32_t cpu, uint64_t address, uint64_t arg0, uint64_t arg1) {
  // Acquire lock on interrupt thread (we can only dispatch one at a time).
  xe_mutex_lock(interrupt_thread_lock_);

  // Set 0x10C(r13) to the current CPU ID.
  uint8_t* p = memory_->membase();
  XESETUINT8BE(p + interrupt_thread_block_ + 0x10C, cpu);

  // Execute interrupt.
  uint64_t result = Execute(interrupt_thread_state_, address, arg0, arg1);

  xe_mutex_unlock(interrupt_thread_lock_);
  return result;
}

json_t* Processor::OnDebugRequest(
    const char* command, json_t* request, bool& succeeded) {
  succeeded = true;
  if (xestrcmpa(command, "get_module_list") == 0) {
    json_t* list = json_array();
    Runtime::ModuleList modules = runtime_->GetModules();
    for (Runtime::ModuleList::iterator it = modules.begin();
         it != modules.end(); ++it) {
      XexModule* module = (XexModule*)(*it);
      json_t* module_json = json_object();
      json_t* module_name_json = json_string(module->name());
      json_object_set_new(module_json, "name", module_name_json);
      json_array_append_new(list, module_json);
    }
    return list;
  /*} else if (xestrcmpa(command, "get_module") == 0) {
    return json_null();*/
  } else if (xestrcmpa(command, "get_function_list") == 0) {
    json_t* module_name_json = json_object_get(request, "module");
    if (!module_name_json || !json_is_string(module_name_json)) {
      succeeded = false;
      return json_string("Module name not specified");
    }
    const char* module_name = json_string_value(module_name_json);
    XexModule* module = (XexModule*)runtime_->GetModule(module_name);
    if (!module) {
      succeeded = false;
      return json_string("Module not found");
    }
    json_t* list = json_array();
    module->ForEachFunction([&](FunctionInfo* info) {
      json_t* fn_json = json_object();
      // TODO(benvanik): get name
      char name_buffer[32];
      xesnprintfa(name_buffer, XECOUNT(name_buffer), "sub_%.8X",
                  info->address());
      json_t* name_json = json_string(name_buffer);
      json_object_set_new(fn_json, "name", name_json);
      json_t* address_json = json_integer(info->address());
      json_object_set_new(fn_json, "address", address_json);
      json_t* link_status_json = json_integer(info->status());
      json_object_set_new(fn_json, "linkStatus", link_status_json);
      json_array_append_new(list, fn_json);
    });
    return list;
  } else if (xestrcmpa(command, "get_function") == 0) {
    json_t* address_json = json_object_get(request, "address");
    if (!address_json || !json_is_number(address_json)) {
      succeeded = false;
      return json_string("Function address not specified");
    }
    uint64_t address = (uint64_t)json_number_value(address_json);

    FunctionInfo* info;
    if (runtime_->LookupFunctionInfo(address, &info)) {
      succeeded = false;
      return json_string("Function not found");
    }

    // Demand a new function with all debug info retained.
    // If we ever wanted absolute x64 addresses/etc we could
    // use the x64 from the function in the symbol table.
    Function* fn;
    if (runtime_->frontend()->DefineFunction(info, &fn)) {
      succeeded = false;
      return json_string("Unable to resolve function");
    }
    DebugInfo* debug_info = fn->debug_info();
    if (!debug_info) {
      succeeded = false;
      return json_string("No debug info present for function");
    }

    json_t* fn_json = json_object();
    // TODO(benvanik): get name
    char name_buffer[32];
    xesnprintfa(name_buffer, XECOUNT(name_buffer), "sub_%.8X",
                info->address());
    json_t* name_json = json_string(name_buffer);
    json_object_set_new(fn_json, "name", name_json);
    json_t* start_address_json = json_integer(info->address());
    json_object_set_new(fn_json, "startAddress", start_address_json);
    json_t* end_address_json = json_integer(info->end_address());
    json_object_set_new(fn_json, "endAddress", end_address_json);
    json_t* link_status_json = json_integer(info->status());
    json_object_set_new(fn_json, "linkStatus", link_status_json);

    json_t* disasm_json = json_object();
    json_t* disasm_str_json;
    disasm_str_json = json_string(debug_info->source_disasm());
    json_object_set_new(disasm_json, "source", disasm_str_json);
    disasm_str_json = json_string(debug_info->raw_hir_disasm());
    json_object_set_new(disasm_json, "rawHir", disasm_str_json);
    disasm_str_json = json_string(debug_info->hir_disasm());
    json_object_set_new(disasm_json, "hir", disasm_str_json);
    disasm_str_json = json_string(debug_info->raw_lir_disasm());
    json_object_set_new(disasm_json, "rawLir", disasm_str_json);
    disasm_str_json = json_string(debug_info->lir_disasm());
    json_object_set_new(disasm_json, "lir", disasm_str_json);
    disasm_str_json = json_string(debug_info->machine_code_disasm());
    json_object_set_new(disasm_json, "machineCode", disasm_str_json);
    json_object_set_new(fn_json, "disasm", disasm_json);

    delete fn;

    return fn_json;
  } else {
    succeeded = false;
    return json_string("Unknown command");
  }
}
