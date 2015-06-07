/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/app.h"

#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

XApp::XApp(KernelState* kernel_state, uint32_t app_id)
    : kernel_state_(kernel_state),
      memory_(kernel_state->memory()),
      app_id_(app_id) {}

void XAppManager::RegisterApp(std::unique_ptr<XApp> app) {
  assert_zero(app_lookup_.count(app->app_id()));
  app_lookup_.insert({app->app_id(), app.get()});
  apps_.push_back(std::move(app));
}

X_RESULT XAppManager::DispatchMessageSync(uint32_t app_id, uint32_t message,
                                          uint32_t buffer_ptr,
                                          uint32_t buffer_length) {
  const auto& it = app_lookup_.find(app_id);
  if (it == app_lookup_.end()) {
    return X_STATUS_UNSUCCESSFUL;
  }
  return it->second->DispatchMessageSync(message, buffer_ptr, buffer_length);
}

X_RESULT XAppManager::DispatchMessageAsync(uint32_t app_id, uint32_t message,
                                           uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  const auto& it = app_lookup_.find(app_id);
  if (it == app_lookup_.end()) {
    return X_ERROR_NOT_FOUND;
  }
  return it->second->DispatchMessageSync(message, buffer_ptr, buffer_length);
}

}  // namespace kernel
}  // namespace xe
