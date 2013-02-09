/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb/xex_symbol_database.h>

#include <xenia/cpu/ppc/instr.h>


using namespace std;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


namespace {


// IMAGE_CE_RUNTIME_FUNCTION_ENTRY
// http://msdn.microsoft.com/en-us/library/ms879748.aspx
typedef struct IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY_t {
  uint32_t      FuncStart;              // Virtual address
  union {
    struct {
      uint32_t  PrologLen       : 8;    // # of prolog instructions (size = x4)
      uint32_t  FuncLen         : 22;   // # of instructions total (size = x4)
      uint32_t  ThirtyTwoBit    : 1;    // Always 1
      uint32_t  ExceptionFlag   : 1;    // 1 if PDATA_EH in .text -- unknown if used
    } Flags;
    uint32_t    FlagsValue;             // To make byte swapping easier
  };
} IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY;


class PEMethodInfo {
public:
  uint32_t    address;
  uint32_t    total_length;   // in bytes
  uint32_t    prolog_length;  // in bytes
};


}


XexSymbolDatabase::XexSymbolDatabase(
    xe_memory_ref memory, ExportResolver* export_resolver, xe_xex2_ref xex) :
    SymbolDatabase(memory, export_resolver) {
  xex_ = xe_xex2_retain(xex);
}

XexSymbolDatabase::~XexSymbolDatabase() {
  xe_xex2_release(xex_);
}

int XexSymbolDatabase::Analyze() {
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);

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

  return SymbolDatabase::Analyze();
}

int XexSymbolDatabase::FindGplr() {
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
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
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
    xesnprintfa(name, XECOUNT(name), "__savegprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 2 * 4;
    fn->set_name(name);
    fn->type = FunctionSymbol::User;
    fn->flags |= FunctionSymbol::kFlagSaveGprLr;
    address += 4;
  }
  address = gplr_start + 20 * 4;
  for (int n = 14; n <= 31; n++) {
    xesnprintfa(name, XECOUNT(name), "__restgprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 3 * 4;
    fn->set_name(name);
    fn->type = FunctionSymbol::User;
    fn->flags |= FunctionSymbol::kFlagRestGprLr;
    address += 4;
  }

  return 0;
}

int XexSymbolDatabase::AddImports(const xe_xex2_import_library_t* library) {
  xe_xex2_import_info_t* import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(xex_, library, &import_infos,
                               &import_info_count)) {
    return 1;
  }

  char name[128];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t* info = &import_infos[n];

    KernelExport* kernel_export = export_resolver_->GetExportByOrdinal(
        library->name, info->ordinal);

    VariableSymbol* var = GetOrInsertVariable(info->value_address);
    if (kernel_export) {
      if (info->thunk_address) {
        xesnprintfa(name, XECOUNT(name), "__imp_%s", kernel_export->name);
      } else {
        xesnprintfa(name, XECOUNT(name), "%s", kernel_export->name);
      }
    } else {
      xesnprintfa(name, XECOUNT(name), "__imp_%s_%.3X", library->name,
                  info->ordinal);
    }
    var->set_name(name);
    var->kernel_export = kernel_export;
    if (info->thunk_address) {
      FunctionSymbol* fn = GetOrInsertFunction(info->thunk_address);
      fn->end_address = fn->start_address + 16 - 4;
      fn->type = FunctionSymbol::Kernel;
      fn->kernel_export = kernel_export;
      if (kernel_export) {
        xesnprintfa(name, XECOUNT(name), "%s", kernel_export->name);
      } else {
        xesnprintfa(name, XECOUNT(name), "__kernel_%s_%.3X", library->name,
                    info->ordinal);
      }
      fn->set_name(name);
    }
  }

  xe_free(import_infos);
  return 0;
}

int XexSymbolDatabase::AddMethodHints() {
  uint8_t* mem = xe_memory_addr(memory_, 0);

  const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY* entry = NULL;

  // Find pdata, which contains the exception handling entries.
  const PESection* pdata = xe_xex2_get_pe_section(xex_, ".pdata");
  if (!pdata) {
    // No exception data to go on.
    return 0;
  }

  // Resolve.
  const uint8_t* p = mem + pdata->address;

  // Entry count = pdata size / sizeof(entry).
  size_t entry_count = pdata->size / sizeof(IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY);
  if (!entry_count) {
    // Empty?
    return 0;
  }

  // Allocate output.
  PEMethodInfo* method_infos = (PEMethodInfo*)xe_calloc(
      entry_count * sizeof(PEMethodInfo));
  if (!method_infos) {
    return 0;
  }

  // Parse entries.
  // NOTE: entries are in memory as big endian, so pull them out and swap the
  // values before using them.
  entry = (const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY*)p;
  IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY temp_entry;
  for (size_t n = 0; n < entry_count; n++, entry++) {
    PEMethodInfo* method_info   = &method_infos[n];
    method_info->address        = XESWAP32BE(entry->FuncStart);

    // The bitfield needs to be swapped by hand.
    temp_entry.FlagsValue       = XESWAP32BE(entry->FlagsValue);
    method_info->total_length   = temp_entry.Flags.FuncLen * 4;
    method_info->prolog_length  = temp_entry.Flags.PrologLen * 4;
  }

  for (size_t n = 0; n < entry_count; n++) {
    PEMethodInfo* method_info = &method_infos[n];
    FunctionSymbol* fn = GetOrInsertFunction(method_info->address);
    fn->end_address = method_info->address + method_info->total_length - 4;
    fn->type = FunctionSymbol::User;
    // TODO(benvanik): something with prolog_length?
  }

  xe_free(method_infos);
  return 0;
}

uint32_t XexSymbolDatabase::GetEntryPoint() {
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
  return header->exe_entry_point;
};

bool XexSymbolDatabase::IsValueInTextRange(uint32_t value) {
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (value >= start_address && value < end_address) {
      return section->info.type == XEX_SECTION_CODE;
    }
    i += section->info.page_count;
  }
  return false;
}
