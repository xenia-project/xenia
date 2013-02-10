/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/socket.h>

#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>


void xe_socket_init() {
  // No-op.
}

socket_t xe_socket_create_tcp() {
  socket_t socket_result = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_result < 1) {
    return XE_INVALID_SOCKET;
  }
  return socket_result;
}

void xe_socket_close(socket_t socket) {
  shutdown(socket, SHUT_WR);
  close(socket);
}

void xe_socket_set_keepalive(socket_t socket, bool value) {
  int opt_value = value ? 1 : 0;
  setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE,
             &opt_value, sizeof(opt_value));
}

void xe_socket_set_reuseaddr(socket_t socket, bool value) {
  int opt_value = value ? 1 : 0;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
             &opt_value, sizeof(opt_value));
}

void xe_socket_set_nodelay(socket_t socket, bool value) {
  int opt_value = value ? 1 : 0;
  setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
             &opt_value, sizeof(opt_value));
}

void xe_socket_set_nonblock(socket_t socket, bool value) {
  int flags;
  while ((flags = fcntl(socket, F_GETFL, 0)) == -1 && errno == EINTR);
  if (flags == -1) {
    return;
  }
  int r;
  while ((r = fcntl(socket, F_SETFL, flags | O_NONBLOCK)) == -1 &&
         errno == EINTR);
  if (r == -1) {
    return;
  }
}

int xe_socket_bind(socket_t socket, uint32_t port) {
  struct sockaddr_in socket_addr;
  socket_addr.sin_family      = AF_INET;
  socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  socket_addr.sin_port        = htons(port);
  int r = bind(socket, (struct sockaddr*)&socket_addr, sizeof(socket_addr));
  if (r < 0) {
    return 1;
  }
  return 0;
}

int xe_socket_listen(socket_t socket) {
  int r = listen(socket, 5);
  if (r < 0) {
    return 1;
  }
  return 0;
}

int xe_socket_accept(socket_t socket, xe_socket_connection_t* out_client_info) {
  struct sockaddr_in client_addr;
  socklen_t client_count = sizeof(client_addr);
  int client_socket_id = accept(socket, (struct sockaddr*)&client_addr,
                                &client_count);
  if (client_socket_id < 0) {
    return 1;
  }

  out_client_info->socket = client_socket_id;

  int client_ip = client_addr.sin_addr.s_addr;
  inet_ntop(AF_INET, &client_ip,
      out_client_info->addr, XECOUNT(out_client_info->addr));

  return 0;
}

int64_t xe_socket_send(socket_t socket, const uint8_t* data, size_t length,
                       int flags, int* out_error_code) {
  ssize_t result = send(socket, data, length, flags);
  *out_error_code = errno;
  return result;
}

int64_t xe_socket_recv(socket_t socket, uint8_t* data, size_t length, int flags,
                       int* out_error_code) {
  ssize_t result = recv(socket, data, length, flags);
  *out_error_code = errno;
  return result;
}

struct xe_socket_loop {
  socket_t  socket;

  int       notify_rd_id;
  int       notify_wr_id;

  struct pollfd events[2];

  bool      pending_queued_write;
  bool      pending_recv;
  bool      pending_send;
};

xe_socket_loop_t* xe_socket_loop_create(socket_t socket) {
  xe_socket_loop_t* loop = (xe_socket_loop_t*)xe_calloc(
      sizeof(xe_socket_loop_t));

  loop->socket = socket;

  int notify_ids[2];
  socketpair(PF_LOCAL, SOCK_STREAM, 0, notify_ids);
  loop->notify_rd_id = notify_ids[0];
  loop->notify_wr_id = notify_ids[1];

  loop->events[0].fd      = socket;
  loop->events[0].events  = POLLIN;
  loop->events[1].fd      = loop->notify_rd_id;
  loop->events[1].events  = POLLIN;

  return loop;
}

void xe_socket_loop_destroy(xe_socket_loop_t* loop) {
  close(loop->notify_rd_id);
  close(loop->notify_wr_id);
  xe_free(loop);
}

int xe_socket_loop_poll(xe_socket_loop_t* loop,
                        bool check_read, bool check_write) {
  // Prep events object.
  if (check_read) {
    loop->events[0].events |= POLLIN;
  }
  if (check_write) {
    loop->events[0].events |= POLLOUT;
  }

  // Poll.
  int r;
  while ((r = poll(loop->events, XECOUNT(loop->events), -1)) == -1 &&
      errno == EINTR);
  if (r == -1) {
    return 1;
  }

  // If we failed, die.
  if (loop->events[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
    return 2;
  }

  // Check queued write.
  loop->pending_queued_write = loop->events[1].revents != 0;
  if (loop->pending_queued_write) {
    uint8_t dummy;
    XEIGNORE(recv(loop->notify_rd_id, &dummy, 1, 0));
  }
  loop->events[1].revents = 0;
  loop->events[1].events = POLLIN;

  // Check send/recv.
  loop->pending_recv = (loop->events[0].revents & POLLIN) != 0;
  loop->pending_send = (loop->events[0].revents & POLLOUT) != 0;
  loop->events[0].revents = 0;
  loop->events[0].events = 0;

  return 0;
}

void xe_socket_loop_set_queued_write(xe_socket_loop_t* loop) {
  uint8_t b = 0xFF;
  write(loop->notify_wr_id, &b, 1);
}

bool xe_socket_loop_check_queued_write(xe_socket_loop_t* loop) {
  return loop->pending_queued_write;
}

bool xe_socket_loop_check_socket_recv(xe_socket_loop_t* loop) {
  return loop->pending_recv;
}

bool xe_socket_loop_check_socket_send(xe_socket_loop_t* loop) {
  return loop->pending_send;
}
