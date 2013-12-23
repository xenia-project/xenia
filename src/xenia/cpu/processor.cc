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

#include <alloy/runtime/debugger.h>
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

  debug_client_states_lock_ = xe_mutex_alloc();

  emulator_->debug_server()->AddTarget("cpu", this);
}

Processor::~Processor() {
  emulator_->debug_server()->RemoveTarget("cpu");

  xe_mutex_lock(debug_client_states_lock_);
  for (auto it = debug_client_states_.begin();
       it != debug_client_states_.end(); ++it) {
    DebugClientState* client_state = it->second;
    delete client_state;
  }
  debug_client_states_.clear();
  xe_mutex_unlock(debug_client_states_lock_);
  xe_mutex_free(debug_client_states_lock_);

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

  // Setup debugger events.
  auto debugger = runtime_->debugger();
  auto debug_server = emulator_->debug_server();
  debugger->breakpoint_hit.AddListener(
      [debug_server](BreakpointHitEvent& e) {
    const char* breakpoint_id = e.breakpoint()->id();
    if (!breakpoint_id) {
      // This is not a breakpoint we know about. Ignore.
      return;
    }

    json_t* event_json = json_object();
    json_t* type_json = json_string("breakpoint");
    json_object_set_new(event_json, "type", type_json);

    json_t* thread_id_json = json_integer(e.thread_state()->thread_id());
    json_object_set_new(event_json, "threadId", thread_id_json);
    json_t* breakpoint_id_json = json_string(breakpoint_id);
    json_object_set_new(event_json, "breakpointId", breakpoint_id_json);

    debug_server->BroadcastEvent(event_json);

    json_decref(event_json);
  });

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

void Processor::OnDebugClientConnected(uint32_t client_id) {
  DebugClientState* client_state = new DebugClientState(runtime_);
  xe_mutex_lock(debug_client_states_lock_);
  debug_client_states_[client_id] = client_state;
  xe_mutex_unlock(debug_client_states_lock_);
}

void Processor::OnDebugClientDisconnected(uint32_t client_id) {
  DebugClientState* client_state = debug_client_states_[client_id];
  xe_mutex_lock(debug_client_states_lock_);
  debug_client_states_.erase(client_id);
  xe_mutex_unlock(debug_client_states_lock_);
  delete client_state;
}

json_t* Processor::OnDebugRequest(
    uint32_t client_id, const char* command, json_t* request,
    bool& succeeded) {
  xe_mutex_lock(debug_client_states_lock_);
  DebugClientState* client_state = debug_client_states_[client_id];
  xe_mutex_unlock(debug_client_states_lock_);
  XEASSERTNOTNULL(client_state);

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
      const char* name = info->name();
      char name_buffer[32];
      if (!name) {
        xesnprintfa(name_buffer, XECOUNT(name_buffer), "sub_%.8X",
                    info->address());
        name = name_buffer;
      }
      json_t* name_json = json_string(name);
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
    if (runtime_->frontend()->DefineFunction(info, true, &fn)) {
      succeeded = false;
      return json_string("Unable to resolve function");
    }
    DebugInfo* debug_info = fn->debug_info();
    if (!debug_info) {
      succeeded = false;
      return json_string("No debug info present for function");
    }

    json_t* fn_json = json_object();
    const char* name = info->name();
    char name_buffer[32];
    if (!name) {
      xesnprintfa(name_buffer, XECOUNT(name_buffer), "sub_%.8X",
                  info->address());
      name = name_buffer;
    }
    json_t* name_json = json_string(name);
    json_object_set_new(fn_json, "name", name_json);
    json_t* start_address_json = json_integer(info->address());
    json_object_set_new(fn_json, "startAddress", start_address_json);
    json_t* end_address_json = json_integer(info->end_address());
    json_object_set_new(fn_json, "endAddress", end_address_json);
    json_t* link_status_json = json_integer(info->status());
    json_object_set_new(fn_json, "linkStatus", link_status_json);

    json_t* disasm_json = json_object();
    json_t* disasm_str_json;
    disasm_str_json = json_loads(debug_info->source_json(), 0, NULL);
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
  } else if (xestrcmpa(command, "add_breakpoints") == 0) {
    // breakpoints: [{}]
    json_t* breakpoints_json = json_object_get(request, "breakpoints");
    if (!breakpoints_json || !json_is_array(breakpoints_json)) {
      succeeded = false;
      return json_string("Breakpoints not specified");
    }
    if (!json_array_size(breakpoints_json)) {
      // No-op;
      return json_null();
    }
    for (size_t n = 0; n < json_array_size(breakpoints_json); n++) {
      json_t* breakpoint_json = json_array_get(breakpoints_json, n);
      if (!breakpoint_json || !json_is_object(breakpoint_json)) {
        succeeded = false;
        return json_string("Invalid breakpoint type");
      }

      json_t* breakpoint_id_json = json_object_get(breakpoint_json, "id");
      json_t* type_json = json_object_get(breakpoint_json, "type");
      json_t* address_json = json_object_get(breakpoint_json, "address");
      if (!breakpoint_id_json || !json_is_string(breakpoint_id_json) ||
          !type_json || !json_is_string(type_json) ||
          !address_json || !json_is_number(address_json)) {
        succeeded = false;
        return json_string("Invalid breakpoint members");
      }
      const char* breakpoint_id = json_string_value(breakpoint_id_json);
      const char* type_str = json_string_value(type_json);
      uint64_t address = (uint64_t)json_number_value(address_json);
      Breakpoint::Type type;
      if (xestrcmpa(type_str, "temp") == 0) {
        type = Breakpoint::TEMP_TYPE;
      } else if (xestrcmpa(type_str, "code") == 0) {
        type = Breakpoint::CODE_TYPE;
      } else {
        succeeded = false;
        return json_string("Unknown breakpoint type");
      }

      Breakpoint* breakpoint = new Breakpoint(
          type, address);
      breakpoint->set_id(breakpoint_id);
      if (client_state->AddBreakpoint(breakpoint_id, breakpoint)) {
        succeeded = false;
        return json_string("Error adding breakpoint");
      }
    }
    return json_null();
  } else if (xestrcmpa(command, "remove_breakpoints") == 0) {
    // breakpointIds: ['id']
    json_t* breakpoint_ids_json = json_object_get(request, "breakpointIds");
    if (!breakpoint_ids_json || !json_is_array(breakpoint_ids_json)) {
      succeeded = false;
      return json_string("Breakpoint IDs not specified");
    }
    if (!json_array_size(breakpoint_ids_json)) {
      // No-op;
      return json_null();
    }
    for (size_t n = 0; n < json_array_size(breakpoint_ids_json); n++) {
      json_t* breakpoint_id_json = json_array_get(breakpoint_ids_json, n);
      if (!breakpoint_id_json || !json_is_string(breakpoint_id_json)) {
        succeeded = false;
        return json_string("Invalid breakpoint ID type");
      }
      const char* breakpoint_id = json_string_value(breakpoint_id_json);
      if (client_state->RemoveBreakpoint(breakpoint_id)) {
        succeeded = false;
        return json_string("Unable to remove breakpoints");
      }
    }
    return json_null();
  } else if (xestrcmpa(command, "remove_all_breakpoints") == 0) {
    if (client_state->RemoveAllBreakpoints()) {
      succeeded = false;
      return json_string("Unable to remove breakpoints");
    }
    return json_null();
  } else {
    succeeded = false;
    return json_string("Unknown command");
  }
}

Processor::DebugClientState::DebugClientState(XenonRuntime* runtime) :
    runtime_(runtime) {
  breakpoints_lock_ = xe_mutex_alloc(10000);
}

Processor::DebugClientState::~DebugClientState() {
  RemoveAllBreakpoints();
  xe_mutex_free(breakpoints_lock_);
}

int Processor::DebugClientState::AddBreakpoint(
    const char* breakpoint_id, Breakpoint* breakpoint) {
  xe_mutex_lock(breakpoints_lock_);
  breakpoints_[breakpoint_id] = breakpoint;
  int result = runtime_->debugger()->AddBreakpoint(breakpoint);
  xe_mutex_unlock(breakpoints_lock_);
  return result;
}

int Processor::DebugClientState::RemoveBreakpoint(const char* breakpoint_id) {
  xe_mutex_lock(breakpoints_lock_);
  Breakpoint* breakpoint = breakpoints_[breakpoint_id];
  if (breakpoint) {
    breakpoints_.erase(breakpoint_id);
    runtime_->debugger()->RemoveBreakpoint(breakpoint);
    delete breakpoint;
  }
  xe_mutex_unlock(breakpoints_lock_);
  return 0;
}

int Processor::DebugClientState::RemoveAllBreakpoints() {
  xe_mutex_lock(breakpoints_lock_);
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    Breakpoint* breakpoint = it->second;
    runtime_->debugger()->RemoveBreakpoint(breakpoint);
    delete breakpoint;
  }
  breakpoints_.clear();
  xe_mutex_unlock(breakpoints_lock_);
  return 0;
}
