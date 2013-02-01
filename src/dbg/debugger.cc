/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/debugger.h>

#include <gflags/gflags.h>

#include <xenia/dbg/content_source.h>

#include "dbg/ws_listener.h"


using namespace xe;
using namespace xe::dbg;


DEFINE_bool(wait_for_debugger, false,
    "Whether to wait for the debugger to attach before launching.");
DEFINE_int32(remote_debug_port, 6200,
    "Websocket port to listen for debugger connections on.");


Debugger::Debugger(xe_pal_ref pal) {
  pal_ = xe_pal_retain(pal);

  listener_ = auto_ptr<Listener>(new WsListener(
                                                pal_, FLAGS_remote_debug_port));
}

Debugger::~Debugger() {
  for (std::map<char*, ContentSource*>::iterator it = content_sources_.begin();
       it != content_sources_.end(); ++it) {
    xe_free(it->first);
    delete it->second;
  }
  content_sources_.clear();

  xe_pal_release(pal_);
}

void Debugger::RegisterContentSource(std::string& name,
                                     ContentSource* content_source) {
  content_sources_.insert(std::pair<char*, ContentSource*>(
      xestrdupa(name.c_str()), content_source));
}

int Debugger::Startup() {
  // Start listener.
  // This may launch a thread and such.
  if (listener_->Setup()) {
    return 1;
  }

  // If desired, wait until the first client connects.
  if (FLAGS_wait_for_debugger) {
    XELOGI(XT("Waiting for debugger on port %d..."), FLAGS_remote_debug_port);
    if (listener_->WaitForClient()) {
      return 1;
    }
    XELOGI(XT("Debugger attached, continuing..."));
  }

  return 0;
}

int Debugger::DispatchRequest(Client* client, const char* source_name,
                              const uint8_t* data, size_t length) {
  std::map<char*, ContentSource*>::iterator it =
      content_sources_.find((char*)source_name);
  if (it == content_sources_.end()) {
    return 1;
  }
  return it->second->DispatchRequest(client, data, length);
}
