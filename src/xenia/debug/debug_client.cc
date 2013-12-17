/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/debug_client.h>

#include <xenia/debug/debug_server.h>


using namespace xe;
using namespace xe::debug;


DebugClient::DebugClient(DebugServer* debug_server) :
    debug_server_(debug_server) {
  debug_server_->AddClient(this);
}

DebugClient::~DebugClient() {
  debug_server_->RemoveClient(this);
}
