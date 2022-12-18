/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xmodule.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"

namespace xe {
namespace kernel {

XModule::XModule(KernelState* kernel_state, ModuleType module_type,
                 bool host_object)
    : XObject(kernel_state, kObjectType, host_object),
      module_type_(module_type),
      processor_module_(nullptr),
      hmodule_ptr_(0) {
  // Loader data (HMODULE)
  hmodule_ptr_ = memory()->SystemHeapAlloc(sizeof(X_LDR_DATA_TABLE_ENTRY));

  // Hijack the checksum field to store our kernel object handle.
  auto ldr_data =
      memory()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule_ptr_);
  ldr_data->checksum = handle();
}

XModule::~XModule() {
  kernel_state_->UnregisterModule(this);

  // Destroy the loader data.
  memory()->SystemHeapFree(hmodule_ptr_);
}

bool XModule::Matches(const std::string_view name) const {
  return xe::utf8::equal_case(xe::utf8::find_name_from_guest_path(path()),
                              name) ||
         xe::utf8::equal_case(this->name(), name) ||
         xe::utf8::equal_case(path(), name);
}  // namespace kernel

void XModule::OnLoad() { kernel_state_->RegisterModule(this); }

void XModule::OnUnload() {
  kernel_state_->processor()->RemoveModule(this->name());
  kernel_state_->UnregisterModule(this);
}

X_STATUS XModule::GetSection(const std::string_view name,
                             uint32_t* out_section_data,
                             uint32_t* out_section_size) {
  return X_STATUS_UNSUCCESSFUL;
}

object_ref<XModule> XModule::GetFromHModule(KernelState* kernel_state,
                                            void* hmodule) {
  // Grab the object from our stashed kernel handle
  return kernel_state->object_table()->LookupObject<XModule>(
      GetHandleFromHModule(hmodule));
}

uint32_t XModule::GetHandleFromHModule(void* hmodule) {
  auto ldr_data = reinterpret_cast<X_LDR_DATA_TABLE_ENTRY*>(hmodule);
  return ldr_data->checksum;
}

bool XModule::Save(ByteStream* stream) {
  XELOGD("XModule {:08X} ({})", handle(), path());

  stream->Write(kModuleSaveSignature);

  stream->Write(path());
  stream->Write(hmodule_ptr_);

  if (!SaveObject(stream)) {
    return false;
  }

  return true;
}

object_ref<XModule> XModule::Restore(KernelState* kernel_state,
                                     ByteStream* stream) {
  if (stream->Read<uint32_t>() != kModuleSaveSignature) {
    return nullptr;
  }

  auto path = stream->Read<std::string>();
  auto hmodule_ptr = stream->Read<uint32_t>();

  // Can only save user modules at the moment, so just redirect.
  // TODO: Find a way to call RestoreObject here before UserModule::Restore.
  auto module = UserModule::Restore(kernel_state, stream, path);
  if (!module) {
    return nullptr;
  }

  XELOGD("XModule {:08X} ({})", module->handle(), module->path());

  module->hmodule_ptr_ = hmodule_ptr;
  return module;
}

}  // namespace kernel
}  // namespace xe
