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
#include <xenia/export_resolver.h>
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
  interrupt_thread_state_->set_name("Interrupt");
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

  // Whenever we support multiple clients we will need to respect pause
  // settings. For now, resume until running.
  runtime_->debugger()->ResumeAllThreads(true);
}

json_t* json_object_set_string_new(
    json_t* object, const char* key, const char* value) {
  json_t* value_json = json_string(value);
  json_object_set_new(object, key, value_json);
  return value_json;
}

json_t* json_object_set_string_format_new(
    json_t* object, const char* key, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  xevsnprintfa(buffer, XECOUNT(buffer), format, args);
  va_end(args);
  json_t* value_json = json_string(buffer);
  json_object_set_new(object, key, value_json);
  return value_json;
}

json_t* json_object_set_integer_new(
    json_t* object, const char* key, json_int_t value) {
  json_t* value_json = json_integer(value);
  json_object_set_new(object, key, value_json);
  return value_json;
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
    for (auto it = modules.begin(); it != modules.end(); ++it) {
      XexModule* module = (XexModule*)(*it);
      json_t* module_json = json_object();
      json_object_set_string_new(module_json, "name", module->name());
      json_array_append_new(list, module_json);
    }
    return list;
  } else if (xestrcmpa(command, "get_module") == 0) {
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
    return DumpModule(module, succeeded);
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
    json_t* since_json = json_object_get(request, "since");
    if (since_json && !json_is_number(since_json)) {
      succeeded = false;
      return json_string("Version since is an invalid type");
    }
    size_t since = since_json ?
        (size_t)json_number_value(since_json) : 0;
    json_t* list = json_array();
    size_t version = 0;
    module->ForEachFunction(since, version, [&](FunctionInfo* info) {
      json_t* fn_json = json_object();
      const char* name = info->name();
      char name_buffer[32];
      if (!name) {
        xesnprintfa(name_buffer, XECOUNT(name_buffer), "sub_%.8X",
                    info->address());
        name = name_buffer;
      }
      json_object_set_string_new(fn_json, "name", name);
      json_object_set_integer_new(fn_json, "address", info->address());
      json_object_set_integer_new(fn_json, "linkStatus", info->status());
      json_array_append_new(list, fn_json);
    });
    json_t* result = json_object();
    json_object_set_integer_new(result, "version", version);
    json_object_set_new(result, "list", list);
    return result;
  } else if (xestrcmpa(command, "get_function") == 0) {
    json_t* address_json = json_object_get(request, "address");
    if (!address_json || !json_is_number(address_json)) {
      succeeded = false;
      return json_string("Function address not specified");
    }
    uint64_t address = (uint64_t)json_number_value(address_json);
    return DumpFunction(address, succeeded);
  } else if (xestrcmpa(command, "get_thread_states") == 0) {
    json_t* result = json_object();
    runtime_->debugger()->ForEachThread([&](ThreadState* thread_state) {
      json_t* state_json = DumpThreadState((XenonThreadState*)thread_state);
      char threadIdString[32];
      xesnprintfa(
          threadIdString, XECOUNT(threadIdString),
          "%d", thread_state->thread_id());
      json_object_set_new(result, threadIdString, state_json);
    });
    return result;
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
  } else if (xestrcmpa(command, "continue") == 0) {
    if (runtime_->debugger()->ResumeAllThreads()) {
      succeeded = false;
      return json_string("Unable to resume threads");
    }
    return json_null();
  } else if (xestrcmpa(command, "break") == 0) {
    if (runtime_->debugger()->SuspendAllThreads()) {
      succeeded = false;
      return json_string("Unable to suspend threads");
    }
    return json_null();
  } else if (xestrcmpa(command, "step") == 0) {
    // threadId
    return json_null();
  } else {
    succeeded = false;
    return json_string("Unknown command");
  }
}

json_t* Processor::DumpModule(XexModule* module, bool& succeeded) {
  auto xex = module->xex();
  auto header = xe_xex2_get_header(xex);

  auto export_resolver = runtime_->export_resolver();

  json_t* module_json = json_object();

  json_object_set_integer_new(
      module_json, "moduleFlags", header->module_flags);
  json_object_set_integer_new(
      module_json, "systemFlags", header->system_flags);
  json_object_set_integer_new(
      module_json, "exeAddress", header->exe_address);
  json_object_set_integer_new(
      module_json, "exeEntryPoint", header->exe_entry_point);
  json_object_set_integer_new(
      module_json, "exeStackSize", header->exe_stack_size);
  json_object_set_integer_new(
      module_json, "exeHeapSize", header->exe_heap_size);

  json_t* exec_info_json = json_object();
  json_object_set_integer_new(
      exec_info_json, "mediaId", header->execution_info.media_id);
  json_object_set_string_format_new(
      exec_info_json, "version", "%d.%d.%d.%d",
      header->execution_info.version.major,
      header->execution_info.version.minor,
      header->execution_info.version.build,
      header->execution_info.version.qfe);
  json_object_set_string_format_new(
      exec_info_json, "baseVersion", "%d.%d.%d.%d",
      header->execution_info.base_version.major,
      header->execution_info.base_version.minor,
      header->execution_info.base_version.build,
      header->execution_info.base_version.qfe);
  json_object_set_integer_new(
      exec_info_json, "titleId", header->execution_info.title_id);
  json_object_set_integer_new(
      exec_info_json, "platform", header->execution_info.platform);
  json_object_set_integer_new(
      exec_info_json, "executableTable", header->execution_info.executable_table);
  json_object_set_integer_new(
      exec_info_json, "discNumber", header->execution_info.disc_number);
  json_object_set_integer_new(
      exec_info_json, "discCount", header->execution_info.disc_count);
  json_object_set_integer_new(
      exec_info_json, "savegameId", header->execution_info.savegame_id);
  json_object_set_new(module_json, "executionInfo", exec_info_json);

  json_t* loader_info_json = json_object();
  json_object_set_integer_new(
      loader_info_json, "imageFlags", header->loader_info.image_flags);
  json_object_set_integer_new(
      loader_info_json, "gameRegions", header->loader_info.game_regions);
  json_object_set_integer_new(
      loader_info_json, "mediaFlags", header->loader_info.media_flags);
  json_object_set_new(module_json, "loaderInfo", loader_info_json);

  json_t* tls_info_json = json_object();
  json_object_set_integer_new(
      tls_info_json, "slotCount", header->tls_info.slot_count);
  json_object_set_integer_new(
      tls_info_json, "dataSize", header->tls_info.data_size);
  json_object_set_integer_new(
      tls_info_json, "rawDataAddress", header->tls_info.raw_data_address);
  json_object_set_integer_new(
      tls_info_json, "rawDataSize", header->tls_info.raw_data_size);
  json_object_set_new(module_json, "tlsInfo", tls_info_json);

  json_t* headers_json = json_array();
  for (size_t n = 0; n < header->header_count; n++) {
    auto opt_header = &header->headers[n];
    json_t* header_entry_json = json_object();
    json_object_set_integer_new(
        header_entry_json, "key", opt_header->key);
    json_object_set_integer_new(
        header_entry_json, "length", opt_header->length);
    json_object_set_integer_new(
        header_entry_json, "value", opt_header->value);
    json_array_append_new(headers_json, header_entry_json);
  }
  json_object_set_new(module_json, "headers", headers_json);

  json_t* resource_infos_json = json_array();
  for (size_t n = 0; n < header->resource_info_count; n++) {
    auto& res = header->resource_infos[n];
    json_t* resource_info_json = json_object();
    json_object_set_string_new(
        resource_info_json, "name", res.name);
    json_object_set_integer_new(
        resource_info_json, "address", res.address);
    json_object_set_integer_new(
        resource_info_json, "size", res.size);
    json_array_append_new(resource_infos_json, resource_info_json);
  }
  json_object_set_new(module_json, "resourceInfos", resource_infos_json);

  json_t* sections_json = json_array();
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const char* type = "unknown";
    switch (section->info.type) {
    case XEX_SECTION_CODE:
      type = "code";
      break;
    case XEX_SECTION_DATA:
      type = "rwdata";
      break;
    case XEX_SECTION_READONLY_DATA:
      type = "rodata";
      break;
    }
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    json_t* section_entry_json = json_object();
    json_object_set_string_new(
        section_entry_json, "type", type);
    json_object_set_integer_new(
        section_entry_json, "pageCount", section->info.page_count);
    json_object_set_integer_new(
        section_entry_json, "startAddress", start_address);
    json_object_set_integer_new(
        section_entry_json, "endAddress", end_address);
    json_object_set_integer_new(
        section_entry_json, "totalLength",
        section->info.page_count * xe_xex2_section_length);
    json_array_append_new(sections_json, section_entry_json);
    i += section->info.page_count;
  }
  json_object_set_new(module_json, "sections", sections_json);

  json_t* static_libraries_json = json_array();
  for (size_t n = 0; n < header->static_library_count; n++) {
    const xe_xex2_static_library_t *library = &header->static_libraries[n];
    json_t* static_library_entry_json = json_object();
    json_object_set_string_new(
        static_library_entry_json, "name", library->name);
    json_object_set_string_format_new(
        static_library_entry_json, "version", "%d.%d.%d.%d",
        library->major, library->minor, library->build, library->qfe);
    json_array_append_new(static_libraries_json, static_library_entry_json);
  }
  json_object_set_new(module_json, "staticLibraries", static_libraries_json);

  json_t* library_imports_json = json_array();
  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t* library = &header->import_libraries[n];
    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    if (xe_xex2_get_import_infos(
        xex, library, &import_infos, &import_info_count)) {
      continue;
    }

    json_t* import_library_json = json_object();
    json_object_set_string_new(
        import_library_json, "name", library->name);
    json_object_set_string_format_new(
        import_library_json, "version", "%d.%d.%d.%d",
        library->version.major, library->version.minor,
        library->version.build, library->version.qfe);
    json_object_set_string_format_new(
        import_library_json, "minVersion", "%d.%d.%d.%d",
        library->min_version.major, library->min_version.minor,
        library->min_version.build, library->min_version.qfe);

    json_t* imports_json = json_array();
    for (size_t m = 0; m < import_info_count; m++) {
      auto info = &import_infos[m];
      auto kernel_export = export_resolver->GetExportByOrdinal(
          library->name, info->ordinal);
      json_t* import_json = json_object();
      if (kernel_export) {
        json_object_set_string_new(
            import_json, "name", kernel_export->name);
        json_object_set_new(
            import_json, "implemented",
            kernel_export->is_implemented ? json_true() : json_false());
      } else {
        json_object_set_new(import_json, "name", json_null());
        json_object_set_new(import_json, "implemented", json_false());
      }
      json_object_set_integer_new(
          import_json, "ordinal", info->ordinal);
      json_object_set_integer_new(
          import_json, "valueAddress", info->value_address);
      if (kernel_export && kernel_export->type == KernelExport::Variable) {
        json_object_set_string_new(
            import_json, "type", "variable");
      } else if (kernel_export) {
        json_object_set_string_new(
            import_json, "type", "function");
        json_object_set_integer_new(
            import_json, "thunkAddress", info->thunk_address);
      } else {
        json_object_set_string_new(
            import_json, "type", "unknown");
      }
      json_array_append_new(imports_json, import_json);
    }
    json_object_set_new(import_library_json, "imports", imports_json);

    json_array_append_new(library_imports_json, import_library_json);
  }
  json_object_set_new(module_json, "libraryImports", library_imports_json);

  // TODO(benvanik): exports?

  return module_json;
}

json_t* Processor::DumpFunction(uint64_t address, bool& succeeded) {
  FunctionInfo* info;
  if (runtime_->LookupFunctionInfo(address, &info)) {
    succeeded = false;
    return json_string("Function not found");
  }

  // Demand a new function with all debug info retained.
  // If we ever wanted absolute x64 addresses/etc we could
  // use the x64 from the function in the symbol table.
  Function* fn;
  if (runtime_->frontend()->DefineFunction(
      info, DEBUG_INFO_ALL_DISASM, &fn)) {
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
  disasm_str_json = json_loads(debug_info->source_disasm(), 0, NULL);
  json_object_set_new(disasm_json, "source", disasm_str_json);
  disasm_str_json = json_string(debug_info->raw_hir_disasm());
  json_object_set_new(disasm_json, "rawHir", disasm_str_json);
  disasm_str_json = json_string(debug_info->hir_disasm());
  json_object_set_new(disasm_json, "hir", disasm_str_json);
  disasm_str_json = json_string(debug_info->machine_code_disasm());
  json_object_set_new(disasm_json, "machineCode", disasm_str_json);
  json_object_set_new(fn_json, "disasm", disasm_json);

  delete fn;

  succeeded = true;
  return fn_json;
}

json_t* Processor::DumpThreadState(XenonThreadState* thread_state) {
  json_t* result = json_object();

  json_object_set_integer_new(result, "id", thread_state->thread_id());
  json_object_set_string_new(result, "name", thread_state->name());
  json_object_set_integer_new(
      result, "stackAddress", thread_state->stack_address());
  json_object_set_integer_new(
      result, "stackSize", thread_state->stack_size());
  json_object_set_integer_new(
      result, "threadStateAddress", thread_state->thread_state_address());

  char value[32];

  json_t* context_json = json_object();
  auto context = thread_state->context();

  json_object_set_new(
      context_json, "pc", json_integer(0));
  json_object_set_new(
      context_json, "lr", json_integer(context->lr));

  json_object_set_new(
      context_json, "ctr", json_integer(context->ctr));
  xesnprintfa(value, XECOUNT(value), "%.16llX", context->ctr);
  json_object_set_new(
      context_json, "ctrh", json_string(value));
  xesnprintfa(value, XECOUNT(value), "%lld", context->ctr);
  json_object_set_new(
      context_json, "ctrs", json_string(value));

  // xer
  // cr*
  // fpscr

  json_t* r_json = json_array();
  for (size_t n = 0; n < 32; n++) {
    json_array_append_new(r_json, json_integer(context->r[n]));
  }
  json_object_set_new(context_json, "r", r_json);
  json_t* rh_json = json_array();
  for (size_t n = 0; n < 32; n++) {
    xesnprintfa(value, XECOUNT(value), "%.16llX", context->r[n]);
    json_array_append_new(rh_json, json_string(value));
  }
  json_object_set_new(context_json, "rh", rh_json);
  json_t* rs_json = json_array();
  for (size_t n = 0; n < 32; n++) {
    xesnprintfa(value, XECOUNT(value), "%lld", context->r[n]);
    json_array_append_new(rs_json, json_string(value));
  }
  json_object_set_new(context_json, "rs", rs_json);

  json_t* f_json = json_array();
  for (size_t n = 0; n < 32; n++) {
    json_array_append_new(f_json, json_real(context->f[n]));
  }
  json_object_set_new(context_json, "f", f_json);
  json_t* fh_json = json_array();
  for (size_t n = 0; n < 32; n++) {
    union {
      double f;
      uint64_t i;
    } fi = { context->f[n] };
    xesnprintfa(value, XECOUNT(value), "%.16llX", fi.i);
    json_array_append_new(fh_json, json_string(value));
  }
  json_object_set_new(context_json, "fh", fh_json);

  json_t* v_json = json_array();
  for (size_t n = 0; n < 128; n++) {
    auto& v = context->v[n];
    json_t* vec4_json = json_array();
    json_array_append_new(vec4_json, json_integer(v.ix));
    json_array_append_new(vec4_json, json_integer(v.iy));
    json_array_append_new(vec4_json, json_integer(v.iz));
    json_array_append_new(vec4_json, json_integer(v.iw));
    json_array_append_new(v_json, vec4_json);
  }
  json_object_set_new(context_json, "v", v_json);

  json_object_set_new(result, "context", context_json);

  // TODO(benvanik): callstack

  return result;
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
