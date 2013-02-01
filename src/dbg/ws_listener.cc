/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "dbg/ws_listener.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "dbg/ws_client.h"


using namespace xe;
using namespace xe::dbg;


WsListener::WsListener(xe_pal_ref pal, uint32_t port) :
    Listener(pal),
    port_(port) {

}

WsListener::~WsListener() {
  if (socket_id_) {
    close(socket_id_);
  }
}

int WsListener::Setup() {
  socket_id_ = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_id_ < 1) {
    return 1;
  }

  int opt_value;
  opt_value = 1;
  setsockopt(socket_id_, SOL_SOCKET, SO_KEEPALIVE,
             &opt_value, sizeof(opt_value));
  opt_value = 0;
  setsockopt(socket_id_, IPPROTO_TCP, TCP_NODELAY,
             &opt_value, sizeof(opt_value));

  struct sockaddr_in socket_addr;
  socket_addr.sin_family      = AF_INET;
  socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  socket_addr.sin_port        = htons(port_);
  if (bind(socket_id_, (struct sockaddr*)&socket_addr,
           sizeof(socket_addr)) < 0) {
    return 1;
  }

  if (listen(socket_id_, 5) < 0) {
    close(socket_id_);
    return 1;
  }

  return 0;
}

int WsListener::WaitForClient() {
  // Accept the first connection we get.
  struct sockaddr_in client_addr;
  socklen_t client_count = sizeof(client_addr);
  int client_socket_id = accept(socket_id_, (struct sockaddr*)&client_addr,
                                &client_count);
  if (client_socket_id < 0) {
    return 1;
  }

  int client_ip = client_addr.sin_addr.s_addr;
  char client_ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_ip, client_ip_str, XECOUNT(client_ip_str));
  XELOGI(XT("Debugger connected from %s"), client_ip_str);

  //WsClient* client = new WsClient(client_socket_id);

  // TODO(benvanik): add to list for cleanup

  return 0;
}
