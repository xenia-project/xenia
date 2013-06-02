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
    xe_memory_ref memory, ExportResolver* export_resolver,
    SymbolTable* sym_table, xe_xex2_ref xex) :
    SymbolDatabase(memory, export_resolver, sym_table) {
  xex_ = xe_xex2_retain(xex);
}

XexSymbolDatabase::~XexSymbolDatabase() {
  xe_xex2_release(xex_);
}

int XexSymbolDatabase::Analyze() {
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);

  // Find __savegprlr_* and __restgprlr_* and the others.
  FindSaveRest();

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

int XexSymbolDatabase::FindSaveRest() {
  // Special stack save/restore functions.
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  static const uint32_t gprlr_code_values[] = {
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
  // __savefpr_14 to __savefpr_31
  // __restfpr_14 to __restfpr_31
  static const uint32_t fpr_code_values[] = {
    0x70FFCCD9, // __savefpr_14
    0x78FFECD9, // __savefpr_15
    0x80FF0CDA, // __savefpr_16
    0x88FF2CDA, // __savefpr_17
    0x90FF4CDA, // __savefpr_18
    0x98FF6CDA, // __savefpr_19
    0xA0FF8CDA, // __savefpr_20
    0xA8FFACDA, // __savefpr_21
    0xB0FFCCDA, // __savefpr_22
    0xB8FFECDA, // __savefpr_23
    0xC0FF0CDB, // __savefpr_24
    0xC8FF2CDB, // __savefpr_25
    0xD0FF4CDB, // __savefpr_26
    0xD8FF6CDB, // __savefpr_27
    0xE0FF8CDB, // __savefpr_28
    0xE8FFACDB, // __savefpr_29
    0xF0FFCCDB, // __savefpr_30
    0xF8FFECDB, // __savefpr_31
    0x2000804E,
    0x70FFCCC9, // __restfpr_14
    0x78FFECC9, // __restfpr_15
    0x80FF0CCA, // __restfpr_16
    0x88FF2CCA, // __restfpr_17
    0x90FF4CCA, // __restfpr_18
    0x98FF6CCA, // __restfpr_19
    0xA0FF8CCA, // __restfpr_20
    0xA8FFACCA, // __restfpr_21
    0xB0FFCCCA, // __restfpr_22
    0xB8FFECCA, // __restfpr_23
    0xC0FF0CCB, // __restfpr_24
    0xC8FF2CCB, // __restfpr_25
    0xD0FF4CCB, // __restfpr_26
    0xD8FF6CCB, // __restfpr_27
    0xE0FF8CCB, // __restfpr_28
    0xE8FFACCB, // __restfpr_29
    0xF0FFCCCB, // __restfpr_30
    0xF8FFECCB, // __restfpr_31
    0x2000804E,
  };
  // __savevmx_14 to __savevmx_31
  // __savevmx_64 to __savevmx_127
  // __restvmx_14 to __restvmx_31
  // __restvmx_64 to __restvmx_127
  static const uint32_t vmx_code_values[] = {
    0xE0FE6039, // __savevmx_14
    0xCE61CB7D,
    0xF0FE6039,
    0xCE61EB7D,
    0x00FF6039,
    0xCE610B7E,
    0x10FF6039,
    0xCE612B7E,
    0x20FF6039,
    0xCE614B7E,
    0x30FF6039,
    0xCE616B7E,
    0x40FF6039,
    0xCE618B7E,
    0x50FF6039,
    0xCE61AB7E,
    0x60FF6039,
    0xCE61CB7E,
    0x70FF6039,
    0xCE61EB7E,
    0x80FF6039,
    0xCE610B7F,
    0x90FF6039,
    0xCE612B7F,
    0xA0FF6039,
    0xCE614B7F,
    0xB0FF6039,
    0xCE616B7F,
    0xC0FF6039,
    0xCE618B7F,
    0xD0FF6039,
    0xCE61AB7F,
    0xE0FF6039,
    0xCE61CB7F,
    0xF0FF6039, // __savevmx_31
    0xCE61EB7F,
    0x2000804E,

    0x00FC6039, // __savevmx_64
    0xCB610B10,
    0x10FC6039,
    0xCB612B10,
    0x20FC6039,
    0xCB614B10,
    0x30FC6039,
    0xCB616B10,
    0x40FC6039,
    0xCB618B10,
    0x50FC6039,
    0xCB61AB10,
    0x60FC6039,
    0xCB61CB10,
    0x70FC6039,
    0xCB61EB10,
    0x80FC6039,
    0xCB610B11,
    0x90FC6039,
    0xCB612B11,
    0xA0FC6039,
    0xCB614B11,
    0xB0FC6039,
    0xCB616B11,
    0xC0FC6039,
    0xCB618B11,
    0xD0FC6039,
    0xCB61AB11,
    0xE0FC6039,
    0xCB61CB11,
    0xF0FC6039,
    0xCB61EB11,
    0x00FD6039,
    0xCB610B12,
    0x10FD6039,
    0xCB612B12,
    0x20FD6039,
    0xCB614B12,
    0x30FD6039,
    0xCB616B12,
    0x40FD6039,
    0xCB618B12,
    0x50FD6039,
    0xCB61AB12,
    0x60FD6039,
    0xCB61CB12,
    0x70FD6039,
    0xCB61EB12,
    0x80FD6039,
    0xCB610B13,
    0x90FD6039,
    0xCB612B13,
    0xA0FD6039,
    0xCB614B13,
    0xB0FD6039,
    0xCB616B13,
    0xC0FD6039,
    0xCB618B13,
    0xD0FD6039,
    0xCB61AB13,
    0xE0FD6039,
    0xCB61CB13,
    0xF0FD6039,
    0xCB61EB13,
    0x00FE6039,
    0xCF610B10,
    0x10FE6039,
    0xCF612B10,
    0x20FE6039,
    0xCF614B10,
    0x30FE6039,
    0xCF616B10,
    0x40FE6039,
    0xCF618B10,
    0x50FE6039,
    0xCF61AB10,
    0x60FE6039,
    0xCF61CB10,
    0x70FE6039,
    0xCF61EB10,
    0x80FE6039,
    0xCF610B11,
    0x90FE6039,
    0xCF612B11,
    0xA0FE6039,
    0xCF614B11,
    0xB0FE6039,
    0xCF616B11,
    0xC0FE6039,
    0xCF618B11,
    0xD0FE6039,
    0xCF61AB11,
    0xE0FE6039,
    0xCF61CB11,
    0xF0FE6039,
    0xCF61EB11,
    0x00FF6039,
    0xCF610B12,
    0x10FF6039,
    0xCF612B12,
    0x20FF6039,
    0xCF614B12,
    0x30FF6039,
    0xCF616B12,
    0x40FF6039,
    0xCF618B12,
    0x50FF6039,
    0xCF61AB12,
    0x60FF6039,
    0xCF61CB12,
    0x70FF6039,
    0xCF61EB12,
    0x80FF6039,
    0xCF610B13,
    0x90FF6039,
    0xCF612B13,
    0xA0FF6039,
    0xCF614B13,
    0xB0FF6039,
    0xCF616B13,
    0xC0FF6039,
    0xCF618B13,
    0xD0FF6039,
    0xCF61AB13,
    0xE0FF6039,
    0xCF61CB13,
    0xF0FF6039, // __savevmx_127
    0xCF61EB13,
    0x2000804E,

    0xE0FE6039, // __restvmx_14
    0xCE60CB7D,
    0xF0FE6039,
    0xCE60EB7D,
    0x00FF6039,
    0xCE600B7E,
    0x10FF6039,
    0xCE602B7E,
    0x20FF6039,
    0xCE604B7E,
    0x30FF6039,
    0xCE606B7E,
    0x40FF6039,
    0xCE608B7E,
    0x50FF6039,
    0xCE60AB7E,
    0x60FF6039,
    0xCE60CB7E,
    0x70FF6039,
    0xCE60EB7E,
    0x80FF6039,
    0xCE600B7F,
    0x90FF6039,
    0xCE602B7F,
    0xA0FF6039,
    0xCE604B7F,
    0xB0FF6039,
    0xCE606B7F,
    0xC0FF6039,
    0xCE608B7F,
    0xD0FF6039,
    0xCE60AB7F,
    0xE0FF6039,
    0xCE60CB7F,
    0xF0FF6039, // __restvmx_31
    0xCE60EB7F,
    0x2000804E,

    0x00FC6039, // __restvmx_64
    0xCB600B10,
    0x10FC6039,
    0xCB602B10,
    0x20FC6039,
    0xCB604B10,
    0x30FC6039,
    0xCB606B10,
    0x40FC6039,
    0xCB608B10,
    0x50FC6039,
    0xCB60AB10,
    0x60FC6039,
    0xCB60CB10,
    0x70FC6039,
    0xCB60EB10,
    0x80FC6039,
    0xCB600B11,
    0x90FC6039,
    0xCB602B11,
    0xA0FC6039,
    0xCB604B11,
    0xB0FC6039,
    0xCB606B11,
    0xC0FC6039,
    0xCB608B11,
    0xD0FC6039,
    0xCB60AB11,
    0xE0FC6039,
    0xCB60CB11,
    0xF0FC6039,
    0xCB60EB11,
    0x00FD6039,
    0xCB600B12,
    0x10FD6039,
    0xCB602B12,
    0x20FD6039,
    0xCB604B12,
    0x30FD6039,
    0xCB606B12,
    0x40FD6039,
    0xCB608B12,
    0x50FD6039,
    0xCB60AB12,
    0x60FD6039,
    0xCB60CB12,
    0x70FD6039,
    0xCB60EB12,
    0x80FD6039,
    0xCB600B13,
    0x90FD6039,
    0xCB602B13,
    0xA0FD6039,
    0xCB604B13,
    0xB0FD6039,
    0xCB606B13,
    0xC0FD6039,
    0xCB608B13,
    0xD0FD6039,
    0xCB60AB13,
    0xE0FD6039,
    0xCB60CB13,
    0xF0FD6039,
    0xCB60EB13,
    0x00FE6039,
    0xCF600B10,
    0x10FE6039,
    0xCF602B10,
    0x20FE6039,
    0xCF604B10,
    0x30FE6039,
    0xCF606B10,
    0x40FE6039,
    0xCF608B10,
    0x50FE6039,
    0xCF60AB10,
    0x60FE6039,
    0xCF60CB10,
    0x70FE6039,
    0xCF60EB10,
    0x80FE6039,
    0xCF600B11,
    0x90FE6039,
    0xCF602B11,
    0xA0FE6039,
    0xCF604B11,
    0xB0FE6039,
    0xCF606B11,
    0xC0FE6039,
    0xCF608B11,
    0xD0FE6039,
    0xCF60AB11,
    0xE0FE6039,
    0xCF60CB11,
    0xF0FE6039,
    0xCF60EB11,
    0x00FF6039,
    0xCF600B12,
    0x10FF6039,
    0xCF602B12,
    0x20FF6039,
    0xCF604B12,
    0x30FF6039,
    0xCF606B12,
    0x40FF6039,
    0xCF608B12,
    0x50FF6039,
    0xCF60AB12,
    0x60FF6039,
    0xCF60CB12,
    0x70FF6039,
    0xCF60EB12,
    0x80FF6039,
    0xCF600B13,
    0x90FF6039,
    0xCF602B13,
    0xA0FF6039,
    0xCF604B13,
    0xB0FF6039,
    0xCF606B13,
    0xC0FF6039,
    0xCF608B13,
    0xD0FF6039,
    0xCF60AB13,
    0xE0FF6039,
    0xCF60CB13,
    0xF0FF6039, // __restvmx_127
    0xCF60EB13,
    0x2000804E,
  };



  uint32_t gplr_start = 0;
  uint32_t fpr_start = 0;
  uint32_t vmx_start = 0;
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      if (!gplr_start) {
        gplr_start = xe_memory_search_aligned(
            memory_, start_address, end_address,
            gprlr_code_values, XECOUNT(gprlr_code_values));
      }
      if (!fpr_start) {
        fpr_start = xe_memory_search_aligned(
            memory_, start_address, end_address,
            fpr_code_values, XECOUNT(fpr_code_values));
      }
      if (!vmx_start) {
        vmx_start = xe_memory_search_aligned(
            memory_, start_address, end_address,
            vmx_code_values, XECOUNT(vmx_code_values));
      }
      if (gplr_start && fpr_start && vmx_start) {
        break;
      }
    }
    i += section->info.page_count;
  }

  // Add function stubs.
  char name[32];
  if (gplr_start) {
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
  }
  if (fpr_start) {
    uint32_t address = fpr_start;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__savefpr_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->end_address = fn->start_address + (31 - n) * 4 + 1 * 4;
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagSaveFpr;
      address += 4;
    }
    address = fpr_start + (18 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__restfpr_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->end_address = fn->start_address + (31 - n) * 4 + 1 * 4;
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagRestFpr;
      address += 4;
    }
  }
  if (vmx_start) {
    // vmx is:
    // 14-31 save
    // 64-127 save
    // 14-31 rest
    // 64-127 rest
    uint32_t address = vmx_start;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__savevmx_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagSaveVmx;
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      xesnprintfa(name, XECOUNT(name), "__savevmx_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagSaveVmx;
      address += 2 * 4;
    }
    address = vmx_start + (18 * 2 * 4) + (1 * 4) + (64 * 2 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__restvmx_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagRestVmx;
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      xesnprintfa(name, XECOUNT(name), "__restvmx_%d", n);
      FunctionSymbol* fn = GetOrInsertFunction(address);
      fn->set_name(name);
      fn->type = FunctionSymbol::User;
      fn->flags |= FunctionSymbol::kFlagSaveVmx;
      address += 2 * 4;
    }
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
    if (method_info->address != 0) {
      FunctionSymbol* fn = GetOrInsertFunction(method_info->address);
      fn->end_address = method_info->address + method_info->total_length - 4;
      fn->type = FunctionSymbol::User;
    }
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
