/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/socket.h>

#include <winsock2.h>


// TODO(benvanik): win32 calls


void xe_socket_init() {
  // TODO(benvanik): do WSA init
}

socket_t xe_socket_create_tcp() {
  return 0;
}

void xe_socket_close(socket_t socket) {
}

void xe_socket_set_keepalive(socket_t socket, bool value) {
}

void xe_socket_set_reuseaddr(socket_t socket, bool value) {
}

void xe_socket_set_nodelay(socket_t socket, bool value) {
}

void xe_socket_set_nonblock(socket_t socket, bool value) {
}

int xe_socket_bind(socket_t socket, uint32_t port) {
  return 0;
}

int xe_socket_listen(socket_t socket) {
  return 0;
}

int xe_socket_accept(socket_t socket, xe_socket_connection_t* out_client_info) {
  return 0;
}

int64_t xe_socket_send(socket_t socket, const uint8_t* data, size_t length,
                       int flags, int* out_error_code) {
  return 0;
}

int64_t xe_socket_recv(socket_t socket, uint8_t* data, size_t length, int flags,
                       int* out_error_code) {
  return 0;
}

struct xe_socket_loop {
  socket_t  socket;
};

xe_socket_loop_t* xe_socket_loop_create(socket_t socket) {
  xe_socket_loop_t* loop = (xe_socket_loop_t*)xe_calloc(
      sizeof(xe_socket_loop_t));

  loop->socket = socket;

  return loop;
}

void xe_socket_loop_destroy(xe_socket_loop_t* loop) {
  xe_free(loop);
}

int xe_socket_loop_poll(xe_socket_loop_t* loop,
                        bool check_read, bool check_write) {
  return 0;
}

void xe_socket_loop_set_queued_write(xe_socket_loop_t* loop) {
}

bool xe_socket_loop_check_queued_write(xe_socket_loop_t* loop) {
  return false;
}

bool xe_socket_loop_check_socket_recv(xe_socket_loop_t* loop) {
  return false;
}

bool xe_socket_loop_check_socket_send(xe_socket_loop_t* loop) {
  return false;
}
