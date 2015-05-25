/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/xex_module.h"

#include <algorithm>

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/cpu-private.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xmodule.h"

namespace xe {
namespace cpu {

using namespace xe::cpu;
using namespace xe::kernel;

using PPCContext = xe::cpu::frontend::PPCContext;

void UndefinedImport(PPCContext* ppc_state, void* arg0, void* arg1) {
  XELOGE("call to undefined import");
}

XexModule::XexModule(Processor* processor, KernelState* state)
    : Module(processor),
      processor_(processor),
      kernel_state_(state),
      xex_(nullptr),
      base_address_(0),
      low_address_(0),
      high_address_(0) {}

XexModule::~XexModule() { xe_xex2_dealloc(xex_); }

bool XexModule::Load(const std::string& name, const std::string& path,
                     xe_xex2_ref xex) {
  xex_ = xex;
  const xe_xex2_header_t* header = xe_xex2_get_header(xex);

  // Scan and find the low/high addresses.
  // All code sections are continuous, so this should be easy.
  low_address_ = UINT_MAX;
  high_address_ = 0;
  for (uint32_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const uint32_t start_address =
        header->exe_address + (i * section->page_size);
    const uint32_t end_address =
        start_address + (section->info.page_count * section->page_size);
    if (section->info.type == XEX_SECTION_CODE) {
      low_address_ = std::min(low_address_, start_address);
      high_address_ = std::max(high_address_, end_address);
    }
    i += section->info.page_count;
  }

  // Notify backend that we have an executable range.
  processor_->backend()->CommitExecutableRange(low_address_, high_address_);

  // Add all imports (variables/functions).
  for (size_t n = 0; n < header->import_library_count; n++) {
    if (!SetupLibraryImports(&header->import_libraries[n])) {
      return false;
    }
  }

  // Find __savegprlr_* and __restgprlr_* and the others.
  // We can flag these for special handling (inlining/etc).
  if (!FindSaveRest()) {
    return false;
  }

  // Setup debug info.
  name_ = std::string(name);
  path_ = std::string(path);
  // TODO(benvanik): debug info

  // Load a specified module map and diff.
  if (FLAGS_load_module_map.size()) {
    if (!ReadMap(FLAGS_load_module_map.c_str())) {
      return false;
    }
  }

  return true;
}

bool XexModule::SetupLibraryImports(const xe_xex2_import_library_t* library) {
  ExportResolver* export_resolver = processor_->export_resolver();

  xe_xex2_import_info_t* import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(xex_, library, &import_infos,
                               &import_info_count)) {
    return false;
  }

  char name[128];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t* info = &import_infos[n];

    // Strip off the extension (for the symbol name)
    std::string libname = library->name;
    auto dot = libname.find_last_of('.');
    if (dot != libname.npos) {
      libname = libname.substr(0, dot);
    }

    KernelExport* kernel_export = NULL;  // kernel export info
    uint32_t user_export_addr = 0;       // user export address

    if (kernel_state_->IsKernelModule(library->name)) {
      kernel_export =
          export_resolver->GetExportByOrdinal(library->name, info->ordinal);
    } else {
      auto module = kernel_state_->GetModule(library->name);
      if (module) {
        user_export_addr = module->GetProcAddressByOrdinal(info->ordinal);
      }
    }

    if (kernel_export) {
      if (info->thunk_address) {
        snprintf(name, xe::countof(name), "__imp_%s", kernel_export->name);
      } else {
        snprintf(name, xe::countof(name), "%s", kernel_export->name);
      }
    } else {
      snprintf(name, xe::countof(name), "__imp_%s_%.3X", libname,
               info->ordinal);
    }

    VariableInfo* var_info;
    DeclareVariable(info->value_address, &var_info);
    // var->set_name(name);
    var_info->set_status(SymbolInfo::STATUS_DECLARED);
    DefineVariable(var_info);
    // var->kernel_export = kernel_export;
    var_info->set_status(SymbolInfo::STATUS_DEFINED);

    // Grab, if available.
    auto slot = memory_->TranslateVirtual<uint32_t*>(info->value_address);
    if (kernel_export) {
      if (kernel_export->type == KernelExport::Function) {
        // Not exactly sure what this should be...
        if (info->thunk_address) {
          *slot = xe::byte_swap(info->thunk_address);
        } else {
          // TODO(benvanik): find out what import variables are.
          XELOGW("kernel import variable not defined %.8X %s",
                 info->value_address, kernel_export->name);
          *slot = xe::byte_swap(0xF00DF00D);
        }
      } else {
        if (kernel_export->is_implemented) {
          // Implemented - replace with pointer.
          xe::store_and_swap<uint32_t>(slot, kernel_export->variable_ptr);
        } else {
          // Not implemented - write with a dummy value.
          xe::store_and_swap<uint32_t>(
              slot, 0xD000BEEF | (kernel_export->ordinal & 0xFFF) << 16);
          XELOGCPU("WARNING: imported a variable with no value: %s",
                   kernel_export->name);
        }
      }
    } else if (user_export_addr) {
      xe::store_and_swap<uint32_t>(slot, user_export_addr);
    } else {
      // No module found.
      XELOGE("kernel import not found: %s", name);
      if (info->thunk_address) {
        *slot = xe::byte_swap(info->thunk_address);
      } else {
        *slot = xe::byte_swap(0xF00DF00D);
      }
    }

    if (info->thunk_address) {
      if (kernel_export) {
        snprintf(name, xe::countof(name), "%s", kernel_export->name);
      } else if (user_export_addr) {
        snprintf(name, xe::countof(name), "__%s_%.3X", libname, info->ordinal);
      } else {
        snprintf(name, xe::countof(name), "__kernel_%s_%.3X", libname,
                 info->ordinal);
      }

      if (user_export_addr) {
        // Rewrite PPC code to set r11 to the target address
        // So we'll have:
        //    lis r11, user_export_addr
        //    ori r11, r11, user_export_addr
        //    mtspr CTR, r11
        //    bctr
        uint16_t hi_addr = (user_export_addr >> 16) & 0xFFFF;
        uint16_t low_addr = user_export_addr & 0xFFFF;

        uint8_t* p = memory()->TranslateVirtual(info->thunk_address);
        xe::store_and_swap<uint32_t>(p + 0x0, 0x3D600000 | hi_addr);
        xe::store_and_swap<uint32_t>(p + 0x4, 0x616B0000 | low_addr);
      } else {
        // On load we have something like this in memory:
        //     li r3, 0
        //     li r4, 0x1F5
        //     mtspr CTR, r11
        //     bctr
        // Real consoles rewrite this with some code that sets r11.
        // If we did that we'd still have to put a thunk somewhere and do the
        // dynamic lookup. Instead, we rewrite it to use syscalls, as they
        // aren't used on the 360. CPU backends can either take the syscall
        // or do something smarter.
        //     sc
        //     blr
        //     nop
        //     nop
        uint8_t* p = memory()->TranslateVirtual(info->thunk_address);
        xe::store_and_swap<uint32_t>(p + 0x0, 0x44000002);
        xe::store_and_swap<uint32_t>(p + 0x4, 0x4E800020);
        xe::store_and_swap<uint32_t>(p + 0x8, 0x60000000);
        xe::store_and_swap<uint32_t>(p + 0xC, 0x60000000);

        FunctionInfo::ExternHandler handler = 0;
        void* handler_data = 0;
        if (kernel_export) {
          handler =
              (FunctionInfo::ExternHandler)kernel_export->function_data.shim;
          handler_data = kernel_export->function_data.shim_data;
        } else {
          handler = (FunctionInfo::ExternHandler)UndefinedImport;
          handler_data = this;
        }

        FunctionInfo* fn_info;
        DeclareFunction(info->thunk_address, &fn_info);
        fn_info->set_end_address(info->thunk_address + 16 - 4);
        fn_info->set_name(name);
        fn_info->SetupExtern(handler, handler_data, NULL);
        fn_info->set_status(SymbolInfo::STATUS_DECLARED);
      }
    }
  }

  return true;
}

bool XexModule::ContainsAddress(uint32_t address) {
  return address >= low_address_ && address < high_address_;
}

bool XexModule::FindSaveRest() {
  // Special stack save/restore functions.
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  static const uint32_t gprlr_code_values[] = {
      0x68FFC1F9,  // __savegprlr_14
      0x70FFE1F9,  // __savegprlr_15
      0x78FF01FA,  // __savegprlr_16
      0x80FF21FA,  // __savegprlr_17
      0x88FF41FA,  // __savegprlr_18
      0x90FF61FA,  // __savegprlr_19
      0x98FF81FA,  // __savegprlr_20
      0xA0FFA1FA,  // __savegprlr_21
      0xA8FFC1FA,  // __savegprlr_22
      0xB0FFE1FA,  // __savegprlr_23
      0xB8FF01FB,  // __savegprlr_24
      0xC0FF21FB,  // __savegprlr_25
      0xC8FF41FB,  // __savegprlr_26
      0xD0FF61FB,  // __savegprlr_27
      0xD8FF81FB,  // __savegprlr_28
      0xE0FFA1FB,  // __savegprlr_29
      0xE8FFC1FB,  // __savegprlr_30
      0xF0FFE1FB,  // __savegprlr_31
      0xF8FF8191, 0x2000804E,
      0x68FFC1E9,  // __restgprlr_14
      0x70FFE1E9,  // __restgprlr_15
      0x78FF01EA,  // __restgprlr_16
      0x80FF21EA,  // __restgprlr_17
      0x88FF41EA,  // __restgprlr_18
      0x90FF61EA,  // __restgprlr_19
      0x98FF81EA,  // __restgprlr_20
      0xA0FFA1EA,  // __restgprlr_21
      0xA8FFC1EA,  // __restgprlr_22
      0xB0FFE1EA,  // __restgprlr_23
      0xB8FF01EB,  // __restgprlr_24
      0xC0FF21EB,  // __restgprlr_25
      0xC8FF41EB,  // __restgprlr_26
      0xD0FF61EB,  // __restgprlr_27
      0xD8FF81EB,  // __restgprlr_28
      0xE0FFA1EB,  // __restgprlr_29
      0xE8FFC1EB,  // __restgprlr_30
      0xF0FFE1EB,  // __restgprlr_31
      0xF8FF8181, 0xA603887D, 0x2000804E,
  };
  // __savefpr_14 to __savefpr_31
  // __restfpr_14 to __restfpr_31
  static const uint32_t fpr_code_values[] = {
      0x70FFCCD9,  // __savefpr_14
      0x78FFECD9,  // __savefpr_15
      0x80FF0CDA,  // __savefpr_16
      0x88FF2CDA,  // __savefpr_17
      0x90FF4CDA,  // __savefpr_18
      0x98FF6CDA,  // __savefpr_19
      0xA0FF8CDA,  // __savefpr_20
      0xA8FFACDA,  // __savefpr_21
      0xB0FFCCDA,  // __savefpr_22
      0xB8FFECDA,  // __savefpr_23
      0xC0FF0CDB,  // __savefpr_24
      0xC8FF2CDB,  // __savefpr_25
      0xD0FF4CDB,  // __savefpr_26
      0xD8FF6CDB,  // __savefpr_27
      0xE0FF8CDB,  // __savefpr_28
      0xE8FFACDB,  // __savefpr_29
      0xF0FFCCDB,  // __savefpr_30
      0xF8FFECDB,  // __savefpr_31
      0x2000804E,
      0x70FFCCC9,  // __restfpr_14
      0x78FFECC9,  // __restfpr_15
      0x80FF0CCA,  // __restfpr_16
      0x88FF2CCA,  // __restfpr_17
      0x90FF4CCA,  // __restfpr_18
      0x98FF6CCA,  // __restfpr_19
      0xA0FF8CCA,  // __restfpr_20
      0xA8FFACCA,  // __restfpr_21
      0xB0FFCCCA,  // __restfpr_22
      0xB8FFECCA,  // __restfpr_23
      0xC0FF0CCB,  // __restfpr_24
      0xC8FF2CCB,  // __restfpr_25
      0xD0FF4CCB,  // __restfpr_26
      0xD8FF6CCB,  // __restfpr_27
      0xE0FF8CCB,  // __restfpr_28
      0xE8FFACCB,  // __restfpr_29
      0xF0FFCCCB,  // __restfpr_30
      0xF8FFECCB,  // __restfpr_31
      0x2000804E,
  };
  // __savevmx_14 to __savevmx_31
  // __savevmx_64 to __savevmx_127
  // __restvmx_14 to __restvmx_31
  // __restvmx_64 to __restvmx_127
  static const uint32_t vmx_code_values[] = {
      0xE0FE6039,  // __savevmx_14
      0xCE61CB7D, 0xF0FE6039, 0xCE61EB7D, 0x00FF6039, 0xCE610B7E, 0x10FF6039,
      0xCE612B7E, 0x20FF6039, 0xCE614B7E, 0x30FF6039, 0xCE616B7E, 0x40FF6039,
      0xCE618B7E, 0x50FF6039, 0xCE61AB7E, 0x60FF6039, 0xCE61CB7E, 0x70FF6039,
      0xCE61EB7E, 0x80FF6039, 0xCE610B7F, 0x90FF6039, 0xCE612B7F, 0xA0FF6039,
      0xCE614B7F, 0xB0FF6039, 0xCE616B7F, 0xC0FF6039, 0xCE618B7F, 0xD0FF6039,
      0xCE61AB7F, 0xE0FF6039, 0xCE61CB7F, 0xF0FF6039,  // __savevmx_31
      0xCE61EB7F, 0x2000804E,

      0x00FC6039,  // __savevmx_64
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
      0xF0FF6039,  // __savevmx_127
      0xCF61EB13, 0x2000804E,

      0xE0FE6039,  // __restvmx_14
      0xCE60CB7D, 0xF0FE6039, 0xCE60EB7D, 0x00FF6039, 0xCE600B7E, 0x10FF6039,
      0xCE602B7E, 0x20FF6039, 0xCE604B7E, 0x30FF6039, 0xCE606B7E, 0x40FF6039,
      0xCE608B7E, 0x50FF6039, 0xCE60AB7E, 0x60FF6039, 0xCE60CB7E, 0x70FF6039,
      0xCE60EB7E, 0x80FF6039, 0xCE600B7F, 0x90FF6039, 0xCE602B7F, 0xA0FF6039,
      0xCE604B7F, 0xB0FF6039, 0xCE606B7F, 0xC0FF6039, 0xCE608B7F, 0xD0FF6039,
      0xCE60AB7F, 0xE0FF6039, 0xCE60CB7F, 0xF0FF6039,  // __restvmx_31
      0xCE60EB7F, 0x2000804E,

      0x00FC6039,  // __restvmx_64
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
      0xF0FF6039,  // __restvmx_127
      0xCF60EB13, 0x2000804E,
  };

  // TODO(benvanik): these are almost always sequential, if present.
  //     It'd be smarter to search around the other ones to prevent
  //     3 full module scans.
  uint32_t gplr_start = 0;
  uint32_t fpr_start = 0;
  uint32_t vmx_start = 0;
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);
  for (uint32_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const uint32_t start_address =
        header->exe_address + (i * section->page_size);
    const uint32_t end_address =
        start_address + (section->info.page_count * section->page_size);
    if (section->info.type == XEX_SECTION_CODE) {
      if (!gplr_start) {
        gplr_start = memory_->SearchAligned(start_address, end_address,
                                            gprlr_code_values,
                                            xe::countof(gprlr_code_values));
      }
      if (!fpr_start) {
        fpr_start =
            memory_->SearchAligned(start_address, end_address, fpr_code_values,
                                   xe::countof(fpr_code_values));
      }
      if (!vmx_start) {
        vmx_start =
            memory_->SearchAligned(start_address, end_address, vmx_code_values,
                                   xe::countof(vmx_code_values));
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
      snprintf(name, xe::countof(name), "__savegprlr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 2 * 4);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveGprLr;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_PROLOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
    address = gplr_start + 20 * 4;
    for (int n = 14; n <= 31; n++) {
      snprintf(name, xe::countof(name), "__restgprlr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 3 * 4);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestGprLr;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_EPILOG_RETURN);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
  }
  if (fpr_start) {
    uint32_t address = fpr_start;
    for (int n = 14; n <= 31; n++) {
      snprintf(name, xe::countof(name), "__savefpr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 1 * 4);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveFpr;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_PROLOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 4;
    }
    address = fpr_start + (18 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      snprintf(name, xe::countof(name), "__restfpr_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_end_address(address + (31 - n) * 4 + 1 * 4);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestFpr;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_EPILOG);
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
    uint32_t address = vmx_start;
    for (int n = 14; n <= 31; n++) {
      snprintf(name, xe::countof(name), "__savevmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_PROLOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      snprintf(name, xe::countof(name), "__savevmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_PROLOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address = vmx_start + (18 * 2 * 4) + (1 * 4) + (64 * 2 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      snprintf(name, xe::countof(name), "__restvmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_EPILOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      snprintf(name, xe::countof(name), "__restvmx_%d", n);
      FunctionInfo* symbol_info;
      DeclareFunction(address, &symbol_info);
      symbol_info->set_name(name);
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      symbol_info->set_behavior(FunctionInfo::BEHAVIOR_EPILOG);
      symbol_info->set_status(SymbolInfo::STATUS_DECLARED);
      address += 2 * 4;
    }
  }

  return true;
}

}  // namespace cpu
}  // namespace xe
