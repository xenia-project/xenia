/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/dbg/ws_listener.h>

#include <xenia/dbg/ws_client.h>


using namespace xe;
using namespace xe::dbg;


WsListener::WsListener(Debugger* debugger, xe_pal_ref pal, uint32_t port) :
    Listener(debugger, pal),
    port_(port) {

}

WsListener::~WsListener() {
  if (socket_id_) {
    xe_socket_close(socket_id_);
  }
}

int WsListener::Setup() {
  xe_socket_init();

  socket_id_ = xe_socket_create_tcp();
  if (socket_id_ == XE_INVALID_SOCKET) {
    return 1;
  }

  xe_socket_set_keepalive(socket_id_, true);
  xe_socket_set_reuseaddr(socket_id_, true);
  xe_socket_set_nodelay(socket_id_, true);

  if (xe_socket_bind(socket_id_, port_)) {
    XELOGE(XT("Could not bind listen socket: %d"), errno);
    return 1;
  }

  if (xe_socket_listen(socket_id_)) {
    xe_socket_close(socket_id_);
    return 1;
  }

  return 0;
}

int WsListener::WaitForClient() {
  // Accept the first connection we get.
  xe_socket_connection_t client_info;
  if (xe_socket_accept(socket_id_, &client_info)) {
    return 1;
  }

  XELOGI(XT("Debugger connected from %s"), client_info.addr);

  // Create the client object.
  // Note that the client will delete itself when done.
  WsClient* client = new WsClient(debugger_, client_info.socket);
  if (client->Setup()) {
    // Client failed to setup - abort.
    return 1;
  }

  return 0;
}
