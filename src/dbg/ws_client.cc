/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "dbg/ws_client.h"


using namespace xe;
using namespace xe::dbg;


WsClient::WsClient(int socket_id) :
    Client(),
    socket_id_(socket_id) {
}

WsClient::~WsClient() {
  close(socket_id_);
}

void WsClient::Write(const uint8_t** buffers, const size_t* lengths,
                     size_t count) {
  //
}
