/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/debug_server.h>

#include <gflags/gflags.h>

#include <xenia/emulator.h>
#include <xenia/debug/debug_client.h>
#include <xenia/debug/protocol.h>
#include <xenia/debug/protocols/gdb/gdb_protocol.h>
#include <xenia/debug/protocols/ws/ws_protocol.h>


using namespace xe;
using namespace xe::debug;


DEFINE_bool(wait_for_debugger, false,
    "Whether to wait for the debugger to attach before launching.");


DebugServer::DebugServer(Emulator* emulator) :
    emulator_(emulator) {
  //protocols_.push_back(
  //    new protocols::gdb::GDBProtocol(this, gdb_port));
  protocols_.push_back(
      new protocols::ws::WSProtocol(this));
}

DebugServer::~DebugServer() {
  Shutdown();
}

int DebugServer::Startup() {
  // HACK(benvanik): say we are ok even if we have no listener.
  if (!protocols_.size()) {
    return 0;
  }

  // Start listeners.
  // This may launch threads and such.
  for (std::vector<Protocol*>::iterator it = protocols_.begin();
       it != protocols_.end(); ++it) {
    Protocol* protocol = *it;
    if (protocol->Setup()) {
      return 1;
    }
  }

  // If desired, wait until the first client connects.
  if (FLAGS_wait_for_debugger) {
    //XELOGI("Waiting for debugger on port %d...", FLAGS_remote_debug_port);
    //if (listener_->WaitForClient()) {
    //  return 1;
    //}
    XELOGI("Debugger attached, continuing...");
  }

  return 0;
}

void DebugServer::Shutdown() {
  std::vector<DebugClient*> clients(clients_.begin(), clients_.end());
  clients_.clear();
  for (std::vector<DebugClient*>::iterator it = clients.begin();
       it != clients.end(); ++it) {
    delete *it;
  }

  std::vector<Protocol*> protocols(protocols_.begin(), protocols_.end());
  protocols_.clear();
  for (std::vector<Protocol*>::iterator it = protocols.begin();
       it != protocols.end(); ++it) {
    delete *it;
  }
}

void DebugServer::AddClient(DebugClient* debug_client) {
  clients_.push_back(debug_client);
}

void DebugServer::RemoveClient(DebugClient* debug_client) {
  for (std::vector<DebugClient*>::iterator it = clients_.begin();
       it != clients_.end(); ++it) {
    if (*it == debug_client) {
      clients_.erase(it);
      return;
    }
  }
}
