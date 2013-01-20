/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb.h>

#include <list>
#include <map>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


SymbolDatabase::SymbolDatabase(xe_memory_ref memory, UserModule* module) {
  memory_ = xe_memory_retain(memory);
  module_ = module;
}

SymbolDatabase::~SymbolDatabase() {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    delete it->second;
  }

  xe_memory_release(memory_);
}

int SymbolDatabase::Analyze() {
  // Iteratively run passes over the db.
  // This uses a queue to do a breadth-first search of all accessible
  // functions. Callbacks and such likely won't be hit.

  const xe_xex2_header_t *header = module_->xex_header();

  // Find __savegprlr_* and __restgprlr_*.
  FindGplr();

  // Add each import thunk.
  for (size_t n = 0; n < header->import_library_count; n++) {
    AddImports(&header->import_libraries[n]);
  }

  // Add each export root.
  // TODO(benvanik): exports.
  //   - insert fn or variable
  //   - queue fn

  // Add method hints, if available.
  // Not all XEXs have these.
  AddMethodHints();

  // Queue entry point of the application.
  FunctionSymbol* fn = GetOrInsertFunction(header->exe_entry_point);
  fn->name = strdup("<entry>");

  // Keep pumping the queue until there's nothing left to do.
  FlushQueue();

  // Do a pass over the functions to fill holes.
  FillHoles();
  FlushQueue();

  return 0;
}

FunctionSymbol* SymbolDatabase::GetOrInsertFunction(uint32_t address) {
  FunctionSymbol* fn = GetFunction(address);
  if (fn) {
    return fn;
  }

  printf("add fn %.8X\n", address);
  fn = new FunctionSymbol();
  fn->start_address = address;
  function_count_++;
  symbols_.insert(SymbolMap::value_type(address, fn));
  scan_queue_.push_back(fn);
  return fn;
}

VariableSymbol* SymbolDatabase::GetOrInsertVariable(uint32_t address) {
  VariableSymbol* var = GetVariable(address);
  if (var) {
    return var;
  }

  printf("add var %.8X\n", address);
  var = new VariableSymbol();
  var->address = address;
  variable_count_++;
  symbols_.insert(SymbolMap::value_type(address, var));
  return var;
}

FunctionSymbol* SymbolDatabase::GetFunction(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Function) {
    return static_cast<FunctionSymbol*>(i->second);
  }
  return NULL;
}

VariableSymbol* SymbolDatabase::GetVariable(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Variable) {
    return static_cast<VariableSymbol*>(i->second);
  }
  return NULL;
}

int SymbolDatabase::GetAllFunctions(vector<FunctionSymbol*>& functions) {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    if (it->second->symbol_type == Symbol::Function) {
      functions.push_back(static_cast<FunctionSymbol*>(it->second));
    }
  }
  return 0;
}

void SymbolDatabase::Dump() {
  uint32_t previous = 0;
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    switch (it->second->symbol_type) {
      case Symbol::Function:
        {
          FunctionSymbol* fn = static_cast<FunctionSymbol*>(it->second);
          if (previous && (int)(fn->start_address - previous) > 0) {
            printf("%.8X-%.8X (%5d) h\n", previous, fn->start_address,
                   fn->start_address - previous);
          }
          printf("%.8X-%.8X (%5d) f %s\n", fn->start_address,
                 fn->end_address + 4,
                 fn->end_address - fn->start_address + 4,
                 fn->name ? fn->name : "<unknown>");
          previous = fn->end_address + 4;
        }
        break;
      case Symbol::Variable:
        {
          VariableSymbol* var = static_cast<VariableSymbol*>(it->second);
          printf("%.8X v %s\n", var->address,
                 var->name ? var->name : "<unknown>");
        }
        break;
    }
  }
}

int SymbolDatabase::FindGplr() {
  // Special stack save/restore functions.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  static const uint32_t code_values[] = {
    0x68FFC1F9, // __savegprlr_14
    0x70FFE1F9, // __savegprlr_15
    0x78FF01FA, // __savegprlr_16
    0x80FF21FA, // __savegprlr_17
    0x88FF41FA, // __savegprlr_18
    0x90FF61FA, // __savegprlr_19
    0x98FF81FA, // __savegprlr_20
    0xA0FFA1FA, // __savegprlr_21
    0xA8FFC1FA, // __savegprlr_22
    0xB0FFE1FA, // __savegprlr_23
    0xB8FF01FB, // __savegprlr_24
    0xC0FF21FB, // __savegprlr_25
    0xC8FF41FB, // __savegprlr_26
    0xD0FF61FB, // __savegprlr_27
    0xD8FF81FB, // __savegprlr_28
    0xE0FFA1FB, // __savegprlr_29
    0xE8FFC1FB, // __savegprlr_30
    0xF0FFE1FB, // __savegprlr_31
    0xF8FF8191,
    0x2000804E,
    0x68FFC1E9, // __restgprlr_14
    0x70FFE1E9, // __restgprlr_15
    0x78FF01EA, // __restgprlr_16
    0x80FF21EA, // __restgprlr_17
    0x88FF41EA, // __restgprlr_18
    0x90FF61EA, // __restgprlr_19
    0x98FF81EA, // __restgprlr_20
    0xA0FFA1EA, // __restgprlr_21
    0xA8FFC1EA, // __restgprlr_22
    0xB0FFE1EA, // __restgprlr_23
    0xB8FF01EB, // __restgprlr_24
    0xC0FF21EB, // __restgprlr_25
    0xC8FF41EB, // __restgprlr_26
    0xD0FF61EB, // __restgprlr_27
    0xD8FF81EB, // __restgprlr_28
    0xE0FFA1EB, // __restgprlr_29
    0xE8FFC1EB, // __restgprlr_30
    0xF0FFE1EB, // __restgprlr_31
    0xF8FF8181,
    0xA603887D,
    0x2000804E,
  };

  uint32_t gplr_start = 0;
  const xe_xex2_header_t* header = module_->xex_header();
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      gplr_start = xe_memory_search_aligned(
          memory_, start_address, end_address,
          code_values, XECOUNT(code_values));
      if (gplr_start) {
        break;
      }
    }
    i += section->info.page_count;
  }
  if (!gplr_start) {
    return 0;
  }

  // Add function stubs.
  char name[32];
  uint32_t address = gplr_start;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__savegprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 2 * 4;
    fn->name = xestrdup(name);
    fn->type = FunctionSymbol::User;
    address += 4;
  }
  address = gplr_start + 20 * 4;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__restgprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 3 * 4;
    fn->name = xestrdup(name);
    fn->type = FunctionSymbol::User;
    address += 4;
  }

  return 0;
}

int SymbolDatabase::AddImports(const xe_xex2_import_library_t* library) {
  xe_xex2_ref xex = module_->xex();
  xe_xex2_import_info_t* import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(xex, library, &import_infos,
                               &import_info_count)) {
    xe_xex2_release(xex);
    return 1;
  }

  char name[64];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t* info = &import_infos[n];
    VariableSymbol* var = GetOrInsertVariable(info->value_address);
    // TODO(benvanik): use kernel name
    xesnprintf(name, XECOUNT(name), "__var_%s_%.3X", library->name,
               info->ordinal);
    var->name = strdup(name);
    if (info->thunk_address) {
      FunctionSymbol* fn = GetOrInsertFunction(info->thunk_address);
      // TODO(benvanik): use kernel name
      xesnprintf(name, XECOUNT(name), "__thunk_%s_%.3X", library->name,
                 info->ordinal);
      fn->end_address = fn->start_address + 16 - 4;
      fn->name = strdup(name);
      fn->type = FunctionSymbol::Kernel;
    }
  }

  xe_xex2_release(xex);
  return 0;
}

int SymbolDatabase::AddMethodHints() {
  PEMethodInfo* method_infos;
  size_t method_info_count;
  if (module_->GetMethodHints(&method_infos, &method_info_count)) {
    return 1;
  }

  for (size_t n = 0; n < method_info_count; n++) {
    PEMethodInfo* method_info = &method_infos[n];
    FunctionSymbol* fn = GetOrInsertFunction(method_info->address);
    fn->end_address = method_info->address + method_info->total_length - 4;
    fn->type = FunctionSymbol::User;
    // TODO(benvanik): something with prolog_length?
  }

  return 0;
}

int SymbolDatabase::AnalyzeFunction(FunctionSymbol* fn) {
  // Ignore functions already analyzed.
  if (fn->type != FunctionSymbol::Unknown) {
    return 0;
  }

  // TODO(benvanik): analysis.
  // Search forward from start address to find the end address.
  // Use branch tracking to figure that out.
  return 0;
}

int SymbolDatabase::FlushQueue() {
  while (scan_queue_.size()) {
    FunctionSymbol* fn = scan_queue_.front();
    scan_queue_.pop_front();
    if (!AnalyzeFunction(fn)) {
      return 1;
    }
  }
  return 0;
}

int SymbolDatabase::FillHoles() {
  // TODO(benvanik): scan all holes
  // If 4b, check if 0x00000000 and ignore (alignment padding)
  // If 8b, check if first value is within .text and ignore (EH entry)
  // Else, add to scan queue as function?
  return 0;
}
