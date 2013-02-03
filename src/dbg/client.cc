/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/client.h>

#include <xenia/dbg/debugger.h>


using namespace xe;
using namespace xe::dbg;


Client::Client(Debugger* debugger) :
    debugger_(debugger) {
  debugger_->AddClient(this);
}

Client::~Client() {
  debugger_->RemoveClient(this);
}

void Client::OnMessage(const uint8_t* data, size_t length) {
  debugger_->Dispatch(this, data, length);
}

void Client::Write(uint8_t* buffer, size_t length) {
  uint8_t* buffers[] = {buffer};
  size_t lengths[] = {length};
  Write(buffers, lengths, 1);
}
