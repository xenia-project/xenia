/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/core/socket.h>

#include <errno.h>
#include <mstcpip.h>
#include <winsock2.h>
#include <ws2tcpip.h>


// TODO(benvanik): win32 calls


void xe_socket_init() {
  WSADATA wsa_data;
  int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (result) {
    XELOGE("Winsock failed to initialize: %d", result);
    return;
  }
}

socket_t xe_socket_create_tcp() {
  socket_t socket_result = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_result < 1) {
    return XE_INVALID_SOCKET;
  }
  return socket_result;
}

void xe_socket_close(socket_t socket) {
  struct linger so_linger;
  so_linger.l_onoff = TRUE;
  so_linger.l_linger = 30;
  setsockopt(
      socket,
      SOL_SOCKET,
      SO_LINGER,
      (const char*)&so_linger,
      sizeof so_linger);
  shutdown(socket, SD_SEND);
  closesocket(socket);
}

void xe_socket_set_keepalive(socket_t socket, bool value) {
  struct tcp_keepalive alive;
  alive.onoff = TRUE;
  alive.keepalivetime = 7200000;
  alive.keepaliveinterval = 6000;
  DWORD bytes_returned;
  WSAIoctl(socket, SIO_KEEPALIVE_VALS, &alive, sizeof(alive),
           NULL, 0, &bytes_returned, NULL, NULL);
}

void xe_socket_set_reuseaddr(socket_t socket, bool value) {
  int opt_value = value ? 1 : 0;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
             (const char*)&opt_value, sizeof(opt_value));
}

void xe_socket_set_nodelay(socket_t socket, bool value) {
  int opt_value = value ? 1 : 0;
  setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
             (const char*)&opt_value, sizeof(opt_value));
}

void xe_socket_set_nonblock(socket_t socket, bool value) {
  u_long mode = value ? 1 : 0;
	ioctlsocket(socket, FIONBIO, &mode);
}

int xe_socket_bind(socket_t socket, uint32_t port) {
  struct sockaddr_in socket_addr;
  socket_addr.sin_family      = AF_INET;
  socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  socket_addr.sin_port        = htons(port);
  int r = bind(socket, (struct sockaddr*)&socket_addr, sizeof(socket_addr));
  if (r == SOCKET_ERROR) {
    return 1;
  }
  return 0;
}

int xe_socket_bind_loopback(socket_t socket) {
  struct sockaddr_in socket_addr;
  socket_addr.sin_family      = AF_INET;
  socket_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socket_addr.sin_port        = htons(0);
  int r = bind(socket, (struct sockaddr*)&socket_addr, sizeof(socket_addr));
  if (r == SOCKET_ERROR) {
    return 1;
  }
  return 0;
}

int xe_socket_listen(socket_t socket) {
  int r = listen(socket, 5);
  if (r == SOCKET_ERROR) {
    return 1;
  }
  return 0;
}

int xe_socket_accept(socket_t socket, xe_socket_connection_t* out_client_info) {
  struct sockaddr_in client_addr;
  int client_count = sizeof(client_addr);
  socket_t client_socket_id = accept(
      socket, (struct sockaddr*)&client_addr, &client_count);
  if (client_socket_id == INVALID_SOCKET) {
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
  int result = send(socket, (const char*)data, (int)length, flags);
  int error_code = WSAGetLastError();
  if (error_code == WSAEWOULDBLOCK) {
    *out_error_code = EWOULDBLOCK;
  } else {
    *out_error_code = error_code;
  }
  return result;
}

int64_t xe_socket_recv(socket_t socket, uint8_t* data, size_t length, int flags,
                       int* out_error_code) {
  int result = recv(socket, (char*)data, (int)length, flags);
  int error_code = WSAGetLastError();
  if (error_code == WSAEWOULDBLOCK) {
    *out_error_code = EWOULDBLOCK;
  } else {
    *out_error_code = error_code;
  }
  return result;
}

struct xe_socket_loop {
  socket_t  socket;

  socket_t  notify_rd_id;
  socket_t  notify_wr_id;

  WSAPOLLFD events[2];

  bool      pending_queued_write;
  bool      pending_recv;
  bool      pending_send;
};

namespace {
int Win32SocketPair(socket_t sockets[2]) {
  sockets[0] = sockets[1] = INVALID_SOCKET;

  int r;

  socket_t listener = xe_socket_create_tcp();
  xe_socket_set_reuseaddr(listener, true);
  r = xe_socket_bind_loopback(listener);
  r = xe_socket_listen(listener);

  sockaddr listener_name;
  int listener_name_len = sizeof(listener_name);
  r = getsockname(listener, &listener_name, &listener_name_len);

  socket_t client = xe_socket_create_tcp();
  r = connect(client, &listener_name, listener_name_len);

  socket_t server = accept(listener, &listener_name, &listener_name_len);

  sockaddr client_name;
  int client_name_len = sizeof(client_name);
  r = getsockname(client, &client_name, &client_name_len);
  const char *pc = (const char*)&client_name;
  const char *pn = (const char*)&listener_name;
  for (size_t n = 0; n < sizeof(client_name); n++) {
    if (pc[n] != pn[n]) {
      closesocket(listener);
      return 1;
    }
  }

  xe_socket_set_nonblock(client, true);

  sockets[0] = client;
  sockets[1] = server;

  closesocket(listener);
  return 0;
}
}

xe_socket_loop_t* xe_socket_loop_create(socket_t socket) {
  xe_socket_loop_t* loop = (xe_socket_loop_t*)xe_calloc(
      sizeof(xe_socket_loop_t));

  loop->socket = socket;

  socket_t notify_ids[2] = { 0, 0 };
  for (int retry = 0; retry < 5; retry++) {
    if (!Win32SocketPair(notify_ids)) {
      break;
    }
  }
  if (!notify_ids[0]) {
    xe_free(loop);
    return NULL;
  }
  loop->notify_rd_id = notify_ids[0];
  loop->notify_wr_id = notify_ids[1];

  loop->events[0].fd      = socket;
  loop->events[0].events  = POLLIN;
  loop->events[1].fd      = loop->notify_rd_id;
  loop->events[1].events  = POLLIN;

  return loop;
}

void xe_socket_loop_destroy(xe_socket_loop_t* loop) {
  closesocket(loop->notify_rd_id);
  closesocket(loop->notify_wr_id);
  xe_free(loop);
}

int xe_socket_loop_poll(xe_socket_loop_t* loop,
                        bool check_read, bool check_write) {
  // Prep events object.
  loop->events[0].events = 0;
  if (check_read) {
    loop->events[0].events |= POLLIN;
  }
  if (check_write) {
    loop->events[0].events |= POLLOUT;
  }

  // Poll.
  int r = WSAPoll(loop->events, XECOUNT(loop->events), -1);
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
    char dummy;
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
  char b = (char)0xFF;
  send(loop->notify_wr_id, &b, 1, 0);
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
