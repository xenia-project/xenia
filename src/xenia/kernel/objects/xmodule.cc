/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xmodule.h>

#include <xdb/protocol.h>

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

XModule::~XModule() {
  auto ev = xdb::protocol::ModuleUnloadEvent::Append(memory()->trace_base());
  if (ev) {
    ev->type = xdb::protocol::EventType::MODULE_UNLOAD;
    ev->module_id = handle();
  }

  kernel_state_->UnregisterModule(this);
}

void XModule::OnLoad() {
  auto ev = xdb::protocol::ModuleLoadEvent::Append(memory()->trace_base());
  if (ev) {
    ev->type = xdb::protocol::EventType::MODULE_LOAD;
    ev->module_id = handle();
  }

  kernel_state_->RegisterModule(this);
}

X_STATUS XModule::GetSection(const char* name, uint32_t* out_section_data,
                             uint32_t* out_section_size) {
  return X_STATUS_UNSUCCESSFUL;
}

}  // namespace kernel
}  // namespace xe
