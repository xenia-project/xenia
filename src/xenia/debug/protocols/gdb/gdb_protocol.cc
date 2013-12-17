/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/gdb/gdb_protocol.h>

#include <xenia/debug/protocols/gdb/gdb_client.h>

#include <gflags/gflags.h>


DEFINE_int32(gdb_debug_port, 6201,
    "Remote debugging port for GDB TCP connections.");


using namespace xe;
using namespace xe::debug;
using namespace xe::debug::protocols::gdb;


GDBProtocol::GDBProtocol(DebugServer* debug_server) :
    Protocol(debug_server) {
  port_ = FLAGS_gdb_debug_port;
}

GDBProtocol::~GDBProtocol() {
  if (socket_id_) {
    xe_socket_close(socket_id_);
  }
}

int GDBProtocol::Setup() {
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

int GDBProtocol::WaitForClient() {
  if (!socket_id_) {
    return 1;
  }

  // Accept the first connection we get.
  xe_socket_connection_t client_info;
  if (xe_socket_accept(socket_id_, &client_info)) {
    return 1;
  }

  XELOGI("GDB debugger connected from %s", client_info.addr);

  // Create the client object.
  // Note that the client will delete itself when done.
  GDBClient* client = new GDBClient(debug_server_, client_info.socket);
  if (client->Setup()) {
    // Client failed to setup - abort.
    return 1;
  }

  return 0;
}
