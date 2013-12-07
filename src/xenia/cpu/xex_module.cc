/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/xex_module.h>

#include <alloy/runtime/tracing.h>

#include <xenia/cpu/xenon_runtime.h>
#include <xenia/export_resolver.h>

using namespace alloy;
using namespace alloy::runtime;
using namespace xe::cpu;


XexModule::XexModule(
    XenonRuntime* runtime) :
    runtime_(runtime),
    name_(0), path_(0), xex_(0),
    base_address_(0), low_address_(0), high_address_(0),
    Module(runtime->memory()) {
}

XexModule::~XexModule() {
  xe_xex2_release(xex_);
  xe_free(name_);
  xe_free(path_);
}

int XexModule::Load(const char* name, const char* path, xe_xex2_ref xex) {
  int result;

  xex_ = xe_xex2_retain(xex);
  const xe_xex2_header_t* header = xe_xex2_get_header(xex);

  // Scan and find the low/high addresses.
  // All code sections are continuous, so this should be easy.
  low_address_ = UINT_MAX;
  high_address_ = 0;
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      low_address_ = (uint32_t)MIN(low_address_, start_address);
      high_address_ = (uint32_t)MAX(high_address_, end_address);
    }
    i += section->info.page_count;
  }

  // Add all imports (variables/functions).
  for (size_t n = 0; n < header->import_library_count; n++) {
    result = SetupLibraryImports(&header->import_libraries[n]);
    if (result) {
      return result;
    }
  }

  // Find __savegprlr_* and __restgprlr_* and the others.
  // We can flag these for special handling (inlining/etc).
  result = FindSaveRest();
  if (result) {
    return result;
  }

  // Setup debug info.
  name_ = xestrdupa(name);
  path_ = xestrdupa(path);
  // TODO(benvanik): debug info
  // TODO(benvanik): load map file.

  return 0;
}

int XexModule::SetupLibraryImports(const xe_xex2_import_library_t* library) {
  ExportResolver* export_resolver = runtime_->export_resolver();

  xe_xex2_import_info_t* import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(
      xex_, library, &import_infos, &import_info_count)) {
    return 1;
  }

  uint8_t* membase = memory_->membase();

  char name[128];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t* info = &import_infos[n];

    KernelExport* kernel_export = export_resolver->GetExportByOrdinal(
        library->name, info->ordinal);

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

    VariableInfo* var_info;
    DeclareVariable(info->value_address, &var_info);
    //var->set_name(name);
    var_info->set_status(SymbolInfo::STATUS_DECLARED);
    DefineVariable(var_info);
    //var->kernel_export = kernel_export;
    var_info->set_status(SymbolInfo::STATUS_DEFINED);

    // Grab, if available.
    uint32_t* slot = (uint32_t*)(membase + info->value_address);
    if (kernel_export->type == KernelExport::Function) {
      // Not exactly sure what this should be...
      // TODO(benvanik): find out what import variables are.
      XELOGW("kernel import variable not defined");
    } else {
      if (kernel_export->is_implemented) {
        // Implemented - replace with pointer.
        *slot = XESWAP32BE(kernel_export->variable_ptr);
      } else {
        // Not implemented - write with a dummy value.
        *slot = XESWAP32BE(0xD000BEEF | (kernel_export->ordinal & 0xFFF) << 16);
        XELOGCPU("WARNING: imported a variable with no value: %s",
                 kernel_export->name);
      }
    }

    if (info->thunk_address) {
      if (kernel_export) {
        xesnprintfa(name, XECOUNT(name), "%s", kernel_export->name);
      } else {
        xesnprintfa(name, XECOUNT(name), "__kernel_%s_%.3X", library->name,
                    info->ordinal);
      }

      FunctionInfo* fn_info;
      DeclareFunction(info->thunk_address, &fn_info);
      fn_info->set_end_address(info->thunk_address + 16 - 4);
      //fn->type = FunctionSymbol::Kernel;
      //fn->kernel_export = kernel_export;
      //fn->set_name(name);
      fn_info->set_status(SymbolInfo::STATUS_DECLARED);

      DefineFunction(fn_info);
      Function* fn = new ExternFunction(
          info->thunk_address,
          (ExternFunction::Handler)kernel_export->function_data.shim,
          kernel_export->function_data.shim_data,
          NULL);
      fn_info->set_function(fn);
      fn_info->set_status(SymbolInfo::STATUS_DEFINED);
    }
  }

  xe_free(import_infos);
  return 0;
}

bool XexModule::ContainsAddress(uint64_t address) {
  return address >= low_address_ && address < high_address_;
}

int XexModule::FindSaveRest() {
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
    0xCE61CB7D, 0xF0FE6039, 0xCE61EB7D, 0x00FF6039, 0xCE610B7E, 0x10FF6039,
    0xCE612B7E, 0x20FF6039, 0xCE614B7E, 0x30FF6039, 0xCE616B7E, 0x40FF6039,
    0xCE618B7E, 0x50FF6039, 0xCE61AB7E, 0x60FF6039, 0xCE61CB7E, 0x70FF6039,
    0xCE61EB7E, 0x80FF6039, 0xCE610B7F, 0x90FF6039, 0xCE612B7F, 0xA0FF6039,
    0xCE614B7F, 0xB0FF6039, 0xCE616B7F, 0xC0FF6039, 0xCE618B7F, 0xD0FF6039,
    0xCE61AB7F, 0xE0FF6039, 0xCE61CB7F,
    0xF0FF6039, // __savevmx_31
    0xCE61EB7F,
    0x2000804E,

    0x00FC6039, // __savevmx_64
    0xCB610B10, 0x10FC6039, 0xCB612B10, 0x20FC6039, 0xCB614B10, 0x30FC6039,
    0xCB616B10, 0x40FC6039, 0xCB618B10, 0x50FC6039, 0xCB61AB10, 0x60FC6039,
    0xCB61CB10, 0x70FC6039, 0xCB61EB10, 0x80FC6039, 0xCB610B11, 0x90FC6039,
    0xCB612B11, 0xA0FC6039, 0xCB614B11, 0xB0FC6039, 0xCB616B11, 0xC0FC6039,
    0xCB618B11, 0xD0FC6039, 0xCB61AB11, 0xE0FC6039, 0xCB61CB11, 0xF0FC6039,
    0xCB61EB11, 0x00FD6039, 0xCB610B12, 0x10FD6039, 0xCB612B12, 0x20FD6039,
    0xCB614B12, 0x30FD6039, 0xCB616B12, 0x40FD6039, 0xCB618B12, 0x50FD6039,
    0xCB61AB12, 0x60FD6039, 0xCB61CB12, 0x70FD6039, 0xCB61EB12, 0x80FD6039,
    0xCB610B13, 0x90FD6039, 0xCB612B13, 0xA0FD6039, 0xCB614B13, 0xB0FD6039,
    0xCB616B13, 0xC0FD6039, 0xCB618B13, 0xD0FD6039, 0xCB61AB13, 0xE0FD6039,
    0xCB61CB13, 0xF0FD6039, 0xCB61EB13, 0x00FE6039, 0xCF610B10, 0x10FE6039,
    0xCF612B10, 0x20FE6039, 0xCF614B10, 0x30FE6039, 0xCF616B10, 0x40FE6039,
    0xCF618B10, 0x50FE6039, 0xCF61AB10, 0x60FE6039, 0xCF61CB10, 0x70FE6039,
    0xCF61EB10, 0x80FE6039, 0xCF610B11, 0x90FE6039, 0xCF612B11, 0xA0FE6039,
    0xCF614B11, 0xB0FE6039, 0xCF616B11, 0xC0FE6039, 0xCF618B11, 0xD0FE6039,
    0xCF61AB11, 0xE0FE6039, 0xCF61CB11, 0xF0FE6039, 0xCF61EB11, 0x00FF6039,
    0xCF610B12, 0x10FF6039, 0xCF612B12, 0x20FF6039, 0xCF614B12, 0x30FF6039,
    0xCF616B12, 0x40FF6039, 0xCF618B12, 0x50FF6039, 0xCF61AB12, 0x60FF6039,
    0xCF61CB12, 0x70FF6039, 0xCF61EB12, 0x80FF6039, 0xCF610B13, 0x90FF6039,
    0xCF612B13, 0xA0FF6039, 0xCF614B13, 0xB0FF6039, 0xCF616B13, 0xC0FF6039,
    0xCF618B13, 0xD0FF6039, 0xCF61AB13, 0xE0FF6039, 0xCF61CB13,
    0xF0FF6039, // __savevmx_127
    0xCF61EB13,
    0x2000804E,

    0xE0FE6039, // __restvmx_14
    0xCE60CB7D, 0xF0FE6039, 0xCE60EB7D, 0x00FF6039, 0xCE600B7E, 0x10FF6039,
    0xCE602B7E, 0x20FF6039, 0xCE604B7E, 0x30FF6039, 0xCE606B7E, 0x40FF6039,
    0xCE608B7E, 0x50FF6039, 0xCE60AB7E, 0x60FF6039, 0xCE60CB7E, 0x70FF6039,
    0xCE60EB7E, 0x80FF6039, 0xCE600B7F, 0x90FF6039, 0xCE602B7F, 0xA0FF6039,
    0xCE604B7F, 0xB0FF6039, 0xCE606B7F, 0xC0FF6039, 0xCE608B7F, 0xD0FF6039,
    0xCE60AB7F, 0xE0FF6039, 0xCE60CB7F, 0xF0FF6039, // __restvmx_31
    0xCE60EB7F,
    0x2000804E,

    0x00FC6039, // __restvmx_64
    0xCB600B10, 0x10FC6039, 0xCB602B10, 0x20FC6039, 0xCB604B10, 0x30FC6039,
    0xCB606B10, 0x40FC6039, 0xCB608B10, 0x50FC6039, 0xCB60AB10, 0x60FC6039,
    0xCB60CB10, 0x70FC6039, 0xCB60EB10, 0x80FC6039, 0xCB600B11, 0x90FC6039,
    0xCB602B11, 0xA0FC6039, 0xCB604B11, 0xB0FC6039, 0xCB606B11, 0xC0FC6039,
    0xCB608B11, 0xD0FC6039, 0xCB60AB11, 0xE0FC6039, 0xCB60CB11, 0xF0FC6039,
    0xCB60EB11, 0x00FD6039, 0xCB600B12, 0x10FD6039, 0xCB602B12, 0x20FD6039,
    0xCB604B12, 0x30FD6039, 0xCB606B12, 0x40FD6039, 0xCB608B12, 0x50FD6039,
    0xCB60AB12, 0x60FD6039, 0xCB60CB12, 0x70FD6039, 0xCB60EB12, 0x80FD6039,
    0xCB600B13, 0x90FD6039, 0xCB602B13, 0xA0FD6039, 0xCB604B13, 0xB0FD6039,
    0xCB606B13, 0xC0FD6039, 0xCB608B13, 0xD0FD6039, 0xCB60AB13, 0xE0FD6039,
    0xCB60CB13, 0xF0FD6039, 0xCB60EB13, 0x00FE6039, 0xCF600B10, 0x10FE6039,
    0xCF602B10, 0x20FE6039, 0xCF604B10, 0x30FE6039, 0xCF606B10, 0x40FE6039,
    0xCF608B10, 0x50FE6039, 0xCF60AB10, 0x60FE6039, 0xCF60CB10, 0x70FE6039,
    0xCF60EB10, 0x80FE6039, 0xCF600B11, 0x90FE6039, 0xCF602B11, 0xA0FE6039,
    0xCF604B11, 0xB0FE6039, 0xCF606B11, 0xC0FE6039, 0xCF608B11, 0xD0FE6039,
    0xCF60AB11, 0xE0FE6039, 0xCF60CB11, 0xF0FE6039, 0xCF60EB11, 0x00FF6039,
    0xCF600B12, 0x10FF6039, 0xCF602B12, 0x20FF6039, 0xCF604B12, 0x30FF6039,
    0xCF606B12, 0x40FF6039, 0xCF608B12, 0x50FF6039, 0xCF60AB12, 0x60FF6039,
    0xCF60CB12, 0x70FF6039, 0xCF60EB12, 0x80FF6039, 0xCF600B13, 0x90FF6039,
    0xCF602B13, 0xA0FF6039, 0xCF604B13, 0xB0FF6039, 0xCF606B13, 0xC0FF6039,
    0xCF608B13, 0xD0FF6039, 0xCF60AB13, 0xE0FF6039, 0xCF60CB13,
    0xF0FF6039, // __restvmx_127
    0xCF60EB13,
    0x2000804E,
  };

  // TODO(benvanik): these are almost always sequential, if present.
  //     It'd be smarter to search around the other ones to prevent
  //     3 full module scans.
  uint64_t gplr_start = 0;
  uint64_t fpr_start = 0;
  uint64_t vmx_start = 0;
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      if (!gplr_start) {
        gplr_start = memory_->SearchAligned(
            start_address, end_address,
            gprlr_code_values, XECOUNT(gprlr_code_values));
      }
      if (!fpr_start) {
        fpr_start = memory_->SearchAligned(
            start_address, end_address,
            fpr_code_values, XECOUNT(fpr_code_values));
      }
      if (!vmx_start) {
        vmx_start = memory_->SearchAligned(
            start_address, end_address,
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
    uint64_t address = gplr_start;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__savegprlr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 2 * 4);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveGprLr;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
    address = gplr_start + 20 * 4;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__restgprlr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 3 * 4);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestGprLr;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
  }
  if (fpr_start) {
    uint64_t address = fpr_start;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__savefpr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 1 * 4);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveFpr;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
    address = fpr_start + (18 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__restfpr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 1 * 4);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestFpr;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
  }
  if (vmx_start) {
    // vmx is:
    // 14-31 save
    // 64-127 save
    // 14-31 rest
    // 64-127 rest
    uint64_t address = vmx_start;
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__savevmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      xesnprintfa(name, XECOUNT(name), "__savevmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address = vmx_start + (18 * 2 * 4) + (1 * 4) + (64 * 2 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      xesnprintfa(name, XECOUNT(name), "__restvmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      xesnprintfa(name, XECOUNT(name), "__restvmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      // TODO(benvanik): set name
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
  }

  return 0;
}
