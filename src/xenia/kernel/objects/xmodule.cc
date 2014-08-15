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


using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


XModule::XModule(KernelState* kernel_state, const char* path) :
    XObject(kernel_state, kTypeModule) {
  XEIGNORE(xestrcpya(path_, XECOUNT(path_), path));
  const char* slash = xestrrchra(path, '/');
  if (!slash) {
    slash = xestrrchra(path, '\\');
  }
  if (slash) {
    XEIGNORE(xestrcpya(name_, XECOUNT(name_), slash + 1));
  }
  char* dot = xestrrchra(name_, '.');
  if (dot) {
    *dot = 0;
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

X_STATUS XModule::GetSection(
    const char* name,
    uint32_t* out_section_data, uint32_t* out_section_size) {
  return X_STATUS_UNSUCCESSFUL;
}
