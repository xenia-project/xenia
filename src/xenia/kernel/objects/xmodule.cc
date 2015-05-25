/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xmodule.h"

#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

XModule::XModule(KernelState* kernel_state, const std::string& path)
    : XObject(kernel_state, kTypeModule), path_(path) {
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
}

XModule::~XModule() { kernel_state_->UnregisterModule(this); }

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

X_STATUS XModule::GetSection(const char* name, uint32_t* out_section_data,
                             uint32_t* out_section_size) {
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace kernel
}  // namespace xe
