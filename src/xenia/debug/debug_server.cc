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
#include <xenia/debug/debug_target.h>
#include <xenia/debug/protocol.h>
#include <xenia/debug/protocols/gdb/gdb_protocol.h>
#include <xenia/debug/protocols/ws/ws_protocol.h>


using namespace xe;
using namespace xe::debug;


DEFINE_bool(wait_for_debugger, false,
    "Whether to wait for the debugger to attach before launching.");


DebugServer::DebugServer(Emulator* emulator) :
    emulator_(emulator), lock_(0) {
  lock_ = xe_mutex_alloc(10000);

  client_event_ = CreateEvent(NULL, FALSE, FALSE, NULL);

  protocols_.push_back(
      new protocols::gdb::GDBProtocol(this));
  protocols_.push_back(
      new protocols::ws::WSProtocol(this));
}

DebugServer::~DebugServer() {
  Shutdown();

  CloseHandle(client_event_);

  xe_free(lock_);
  lock_ = 0;
}

bool DebugServer::has_clients() {
  xe_mutex_lock(lock_);
  bool has_clients = clients_.size() > 0;
  xe_mutex_unlock(lock_);
  return has_clients;
}

int DebugServer::Startup() {
  return 0;
}

int DebugServer::BeforeEntry() {
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
    XELOGI("Waiting for debugger...");
    if (WaitForClient()) {
      return 1;
    }
    XELOGI("Debugger attached, continuing...");
  }

  return 0;
}

void DebugServer::Shutdown() {
  xe_mutex_lock(lock_);

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

  xe_mutex_unlock(lock_);
}

void DebugServer::AddTarget(const char* name, DebugTarget* target) {
  xe_mutex_lock(lock_);
  targets_[name] = target;
  xe_mutex_unlock(lock_);
}

void DebugServer::RemoveTarget(const char* name) {
  xe_mutex_lock(lock_);
  targets_[name] = NULL;
  xe_mutex_unlock(lock_);
}

DebugTarget* DebugServer::GetTarget(const char* name) {
  xe_mutex_lock(lock_);
  DebugTarget* target = targets_[name];
  xe_mutex_unlock(lock_);
  return target;
}

void DebugServer::BroadcastEvent(json_t* event_json) {
  // TODO(benvanik): avoid lock somehow?
  xe_mutex_lock(lock_);
  for (auto client : clients_) {
    client->SendEvent(event_json);
  }
  xe_mutex_unlock(lock_);
}

int DebugServer::WaitForClient() {
  while (!has_clients()) {
    WaitForSingleObject(client_event_, INFINITE);
  }
  return 0;
}

void DebugServer::AddClient(DebugClient* debug_client) {
  xe_mutex_lock(lock_);

  // Only one debugger at a time right now. Kill any old one.
  while (clients_.size()) {
    DebugClient* old_client = clients_.back();
    clients_.pop_back();
    old_client->Close();
  }

  clients_.push_back(debug_client);

  // Notify targets.
  for (auto it = targets_.begin(); it != targets_.end(); ++it) {
    it->second->OnDebugClientConnected(debug_client->client_id());
  }

  xe_mutex_unlock(lock_);
}

void DebugServer::ReadyClient(DebugClient* debug_client) {
  SetEvent(client_event_);
}

void DebugServer::RemoveClient(DebugClient* debug_client) {
  xe_mutex_lock(lock_);

  // Notify targets.
  for (auto it = targets_.begin(); it != targets_.end(); ++it) {
    it->second->OnDebugClientDisconnected(debug_client->client_id());
  }

  for (std::vector<DebugClient*>::iterator it = clients_.begin();
       it != clients_.end(); ++it) {
    if (*it == debug_client) {
      clients_.erase(it);
      break;
    }
  }

  xe_mutex_unlock(lock_);
}
