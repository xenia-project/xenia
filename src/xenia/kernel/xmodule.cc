/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xmodule.h"

#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

XModule::XModule(KernelState* kernel_state, ModuleType module_type,
                 const std::string& path)
    : XObject(kernel_state, kTypeModule),
      module_type_(module_type),
      path_(path),
      processor_module_(nullptr),
      hmodule_ptr_(0) {
  auto last_slash = path.find_last_of('/');
  if (last_slash == path.npos) {
    last_slash = path.find_last_of('\\');
  }
  if (last_slash == path.npos) {
    name_ = path_;
  } else {
    name_ = path_.substr(last_slash + 1);
  }
  auto dot = name_.find_last_of('.');
  if (dot != name_.npos) {
    name_ = name_.substr(0, dot);
  }

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

bool XModule::Matches(const std::string& name) const {
  if (strcasecmp(xe::find_name_from_path(path_).c_str(), name.c_str()) == 0) {
    return true;
  }
  if (strcasecmp(name_.c_str(), name.c_str()) == 0) {
    return true;
  }
  if (strcasecmp(path_.c_str(), name.c_str()) == 0) {
    return true;
  }
  return false;
}

void XModule::OnLoad() { kernel_state_->RegisterModule(this); }

void XModule::OnUnload() { kernel_state_->UnregisterModule(this); }

X_STATUS XModule::GetSection(const char* name, uint32_t* out_section_data,
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

}  // namespace kernel
}  // namespace xe
