/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_module.h"

#include "xenia/base/logging.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/raw_module.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {

KernelModule::KernelModule(KernelState* kernel_state,
                           const std::string_view path)
    : XModule(kernel_state, ModuleType::kKernelModule, true) {
  emulator_ = kernel_state->emulator();
  memory_ = emulator_->memory();
  export_resolver_ = kernel_state->emulator()->export_resolver();

  path_ = path;
  name_ = utf8::find_base_name_from_guest_path(path);

  // HACK: Allocates memory where xboxkrnl.exe would be!
  // TODO: Need to free this memory when necessary.
  auto heap = memory()->LookupHeap(0x80040000);
  if (heap->AllocRange(0x80040000, 0x801C0000, kTrampolineSize, 16,
                       kMemoryAllocationCommit,
                       kMemoryProtectRead | kMemoryProtectWrite, false,
                       &guest_trampoline_)) {
    guest_trampoline_size_ = kTrampolineSize;

    auto module = std::make_unique<cpu::RawModule>(emulator_->processor());
    guest_trampoline_module_ = module.get();

    module->set_name(name_ + "_trampoline");
    module->SetAddressRange(guest_trampoline_, guest_trampoline_size_);
    emulator_->processor()->AddModule(std::move(module));
  } else {
    XELOGW("KernelModule {} could not allocate trampoline for GetProcAddress!",
           path);
  }

  OnLoad();
}

KernelModule::~KernelModule() {}

uint32_t KernelModule::GenerateTrampoline(
    std::string name, cpu::GuestFunction::ExternHandler handler,
    cpu::Export* export_data) {
  // Generate the function.
  assert_true(guest_trampoline_next_ * 8 < guest_trampoline_size_);
  if (guest_trampoline_next_ * 8 >= guest_trampoline_size_) {
    assert_always();  // If you hit this, increase kTrampolineSize
    XELOGE("KernelModule::GenerateTrampoline trampoline exhausted");
    return 0;
  }

  uint32_t guest_addr = guest_trampoline_ + (guest_trampoline_next_++ * 8);

  auto mem = memory()->TranslateVirtual(guest_addr);
  xe::store_and_swap<uint32_t>(mem + 0x0, 0x44000042);  // sc
  xe::store_and_swap<uint32_t>(mem + 0x4, 0x4E800020);  // blr

  // Declare/define the extern function.
  cpu::Function* function;
  guest_trampoline_module_->DeclareFunction(guest_addr, &function);
  function->set_end_address(guest_addr + 8);
  function->set_name(std::string("__T_") + name);

  static_cast<cpu::GuestFunction*>(function)->SetupExtern(handler, export_data);

  function->set_status(cpu::Symbol::Status::kDeclared);

  return guest_addr;
}

uint32_t KernelModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  auto export_entry =
      export_resolver_->GetExportByOrdinal(name().c_str(), ordinal);
  if (!export_entry) {
    // Export (or its parent library) not found.
    return 0;
  }
  if (export_entry->get_type() == cpu::Export::Type::kVariable) {
    if (export_entry->variable_ptr) {
      return export_entry->variable_ptr;
    } else {
      XELOGW(
          "ERROR: var export referenced GetProcAddressByOrdinal({:04X}({})) is "
          "not implemented",
          ordinal, export_entry->name);
      return 0;
    }
  } else {
    if (export_entry->function_data.trampoline) {
      auto global_lock = global_critical_region_.Acquire();

      // See if the function has been generated already.
      auto item = guest_trampoline_map_.find(ordinal);
      if (item != guest_trampoline_map_.end()) {
        return item->second;
      }

      cpu::GuestFunction::ExternHandler handler = nullptr;
      if (export_entry->function_data.trampoline) {
        handler = (cpu::GuestFunction::ExternHandler)
                      export_entry->function_data.trampoline;
      }

      uint32_t guest_addr =
          GenerateTrampoline(export_entry->name, handler, export_entry);

      XELOGD("GetProcAddressByOrdinal(\"{}\", \"{}\") = {:08X}", name(),
             export_entry->name, guest_addr);

      // Register the function in our map.
      guest_trampoline_map_.emplace(ordinal, guest_addr);
      return guest_addr;
    } else {
      // Not implemented.
      XELOGW(
          "ERROR: fn export referenced GetProcAddressByOrdinal({:04X}({})) is "
          "not implemented",
          ordinal, export_entry->name);
      return 0;
    }
  }
}

uint32_t KernelModule::GetProcAddressByName(const std::string_view name) {
  // TODO: Does this even work for kernel modules?
  XELOGE("KernelModule::GetProcAddressByName not implemented");
  return 0;
}

}  // namespace kernel
}  // namespace xe
