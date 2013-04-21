/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/exec_module.h>

#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/sdb.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


ExecModule::ExecModule(
    xe_memory_ref memory, shared_ptr<ExportResolver> export_resolver,
    FunctionTable* fn_table,
    const char* module_name, const char* module_path) {
  memory_ = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
  fn_table_ = fn_table;
  module_name_ = xestrdupa(module_name);
  module_path_ = xestrdupa(module_path);
}

ExecModule::~ExecModule() {
  xe_free(module_path_);
  xe_free(module_name_);
  xe_memory_release(memory_);
}

SymbolDatabase* ExecModule::sdb() {
  return sdb_.get();
}

int ExecModule::PrepareRawBinary(uint32_t start_address, uint32_t end_address) {
  sdb_ = shared_ptr<sdb::SymbolDatabase>(
      new sdb::RawSymbolDatabase(memory_, export_resolver_.get(),
                                 start_address, end_address));

  code_addr_low_ = start_address;
  code_addr_high_ = end_address;

  return Prepare();
}

int ExecModule::PrepareXexModule(xe_xex2_ref xex) {
  sdb_ = shared_ptr<sdb::SymbolDatabase>(
      new sdb::XexSymbolDatabase(memory_, export_resolver_.get(), xex));

  code_addr_low_ = 0;
  code_addr_high_ = 0;
  const xe_xex2_header_t* header = xe_xex2_get_header(xex);
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      code_addr_low_ = MIN(code_addr_low_, start_address);
      code_addr_high_ = MAX(code_addr_high_, end_address);
    }
    i += section->info.page_count;
  }

  int result_code = Prepare();
  if (result_code) {
    return result_code;
  }

  return 0;
}

int ExecModule::Prepare() {
  int result_code = 1;
  char file_name[XE_MAX_PATH];

  // Analyze the module and add its symbols to the symbol database.
  // This always happens, even if we have a cached copy of the library, as
  // we may end up needing this information later.
  // TODO(benvanik): see how much memory this is using - it may be worth
  //     dropping and keeping around a smaller structure for future lookups
  //     instead.
  XEEXPECTZERO(sdb_->Analyze());

  // Load a specified module map and diff.
  if (FLAGS_load_module_map.size()) {
    sdb_->ReadMap(FLAGS_load_module_map.c_str());
  }

  // Dump the symbol database.
  if (FLAGS_dump_module_map) {
    xesnprintfa(file_name, XECOUNT(file_name),
                "%s%s.map", FLAGS_dump_path.c_str(), module_name_);
    sdb_->WriteMap(file_name);
  }

  // If recompiling, setup a recompiler and run.
  // Note that this will just load the library if it's present and valid.
  // TODO(benvanik): recompiler logic.

  // Initialize the module.
  XEEXPECTZERO(Init());

  result_code = 0;
XECLEANUP:
  return result_code;
}

int ExecModule::Init() {
  // Setup all kernel variables.
  std::vector<VariableSymbol*> variables;
  if (sdb_->GetAllVariables(variables)) {
    return 1;
  }
  uint8_t* mem = xe_memory_addr(memory_, 0);
  for (std::vector<VariableSymbol*>::iterator it = variables.begin();
       it != variables.end(); ++it) {
    VariableSymbol* var = *it;
    if (!var->kernel_export) {
      continue;
    }
    KernelExport* kernel_export = var->kernel_export;

    // Grab, if available.
    uint32_t* slot = (uint32_t*)(mem + var->address);
    if (kernel_export->type == KernelExport::Function) {
      // Not exactly sure what this should be...
      // TODO(benvanik): find out what import variables are.
    } else {
      if (kernel_export->is_implemented) {
        // Implemented - replace with pointer.
        *slot = XESWAP32BE(kernel_export->variable_ptr);
      } else {
        // Not implemented - write with a dummy value.
        *slot = XESWAP32BE(0xDEADBEEF);
        XELOGCPU("WARNING: imported a variable with no value: %s",
                 kernel_export->name);
      }
    }
  }

  return 0;
}

FunctionSymbol* ExecModule::FindFunctionSymbol(uint32_t address) {
  return sdb_->GetFunction(address);
}

void ExecModule::Dump() {
  sdb_->Dump(stdout);
}
