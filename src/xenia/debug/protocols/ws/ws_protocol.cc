/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/ws/ws_protocol.h>

#include <xenia/debug/protocols/ws/ws_client.h>

#include <gflags/gflags.h>


using namespace xe;
using namespace xe::debug;
using namespace xe::debug::protocols::ws;


DEFINE_int32(ws_debug_port, 6200,
    "Remote debugging port for ws:// connections.");


WSProtocol::WSProtocol(DebugServer* debug_server) :
    Protocol(debug_server) {
  port_ = FLAGS_ws_debug_port;
}

WSProtocol::~WSProtocol() {
  if (socket_id_) {
    xe_socket_close(socket_id_);
  }
}

int WSProtocol::Setup() {
  if (port_ == 0 || port_ == -1) {
    return 0;
  }

  xe_socket_init();

  socket_id_ = xe_socket_create_tcp();
  if (socket_id_ == XE_INVALID_SOCKET) {
    return 1;
  }

  xe_socket_set_keepalive(socket_id_, true);
  xe_socket_set_reuseaddr(socket_id_, true);
  xe_socket_set_nodelay(socket_id_, true);

  if (xe_socket_bind(socket_id_, port_)) {
    XELOGE("Could not bind listen socket: %d", errno);
    return 1;
  }

  if (xe_socket_listen(socket_id_)) {
    xe_socket_close(socket_id_);
    return 1;
  }

  return 0;
}

int WSProtocol::WaitForClient() {
  if (!socket_id_) {
    return 1;
  }

  // Accept the first connection we get.
  xe_socket_connection_t client_info;
  if (xe_socket_accept(socket_id_, &client_info)) {
    return 1;
  }

  XELOGI("WS debugger connected from %s", client_info.addr);

  // Create the client object.
  // Note that the client will delete itself when done.
  WSClient* client = new WSClient(debug_server_, client_info.socket);
  if (client->Setup()) {
    // Client failed to setup - abort.
    return 1;
  }

  return 0;
}
