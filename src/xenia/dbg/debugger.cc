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
#include <xenia/dbg/ws_listener.h>


using namespace xe;
using namespace xe::dbg;


DEFINE_bool(wait_for_debugger, false,
    "Whether to wait for the debugger to attach before launching.");
DEFINE_int32(remote_debug_port, 6200,
    "Websocket port to listen for debugger connections on.");


Debugger::Debugger(xe_pal_ref pal) {
  pal_ = xe_pal_retain(pal);

  listener_ = auto_ptr<Listener>(new WsListener(
      this, pal_, FLAGS_remote_debug_port));
}

Debugger::~Debugger() {
  std::vector<Client*> clients(clients_.begin(), clients_.end());
  clients_.clear();
  for (std::vector<Client*>::iterator it = clients.begin();
       it != clients.end(); ++it) {
    delete *it;
  }

  for (std::map<uint32_t, ContentSource*>::iterator it =
       content_sources_.begin(); it != content_sources_.end(); ++it) {
    delete it->second;
  }
  content_sources_.clear();

  xe_pal_release(pal_);
}

xe_pal_ref Debugger::pal() {
  return xe_pal_retain(pal_);
}

void Debugger::RegisterContentSource(ContentSource* content_source) {
  content_sources_.insert(std::pair<uint32_t, ContentSource*>(
      content_source->source_id(), content_source));
}

int Debugger::Startup() {
  // Start listener.
  // This may launch a thread and such.
  if (listener_->Setup()) {
    return 1;
  }

  // If desired, wait until the first client connects.
  if (FLAGS_wait_for_debugger) {
    XELOGI("Waiting for debugger on port %d...", FLAGS_remote_debug_port);
    if (listener_->WaitForClient()) {
      return 1;
    }
    XELOGI("Debugger attached, continuing...");
  }

  return 0;
}

void Debugger::Broadcast(uint32_t source_id,
                         const uint8_t* data, size_t length) {
  uint32_t header[] = {
    0x00000001,
    source_id,
    0,
    (uint32_t)length,
  };
  uint8_t* buffers[] = {
    (uint8_t*)header,
    (uint8_t*)data,
  };
  size_t lengths[] = {
    sizeof(header),
    length,
  };
  for (std::vector<Client*>::iterator it = clients_.begin();
       it != clients_.end(); ++it) {
    Client* client = *it;
    client->Write(buffers, lengths, XECOUNT(buffers));
  }
}

void Debugger::AddClient(Client* client) {
  clients_.push_back(client);
}

void Debugger::RemoveClient(Client* client) {
  for (std::vector<Client*>::iterator it = clients_.begin();
       it != clients_.end(); ++it) {
    if (*it == client) {
      clients_.erase(it);
      return;
    }
  }
}

int Debugger::Dispatch(Client* client, const uint8_t* data, size_t length) {
  uint32_t type       = XEGETUINT32LE(data + 0);
  uint32_t source_id  = XEGETUINT32LE(data + 4);
  uint32_t request_id = XEGETUINT32LE(data + 8);
  uint32_t size       = XEGETUINT32LE(data + 12);

  std::map<uint32_t, ContentSource*>::iterator it =
      content_sources_.find(source_id);
  if (it == content_sources_.end()) {
    XELOGW("Content source %d not found, ignoring message", source_id);
    return 1;
  }
  return it->second->Dispatch(client, type, request_id, data + 16, size);
}
