/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "dbg/ws_client.h"

#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <wslay/wslay.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>


using namespace xe;
using namespace xe::dbg;


WsClient::WsClient(int socket_id) :
    Client(),
    socket_id_(socket_id) {
  mutex_ = xe_mutex_alloc(1000);

  int notify_ids[2];
  socketpair(PF_LOCAL, SOCK_STREAM, 0, notify_ids);
  notify_rd_id_ = notify_ids[0];
  notify_wr_id_ = notify_ids[1];
}

WsClient::~WsClient() {
  xe_mutex_t* mutex = mutex_;
  xe_mutex_lock(mutex);
  mutex_ = NULL;
  shutdown(socket_id_, SHUT_WR);
  close(socket_id_);
  socket_id_ = 0;
  xe_mutex_unlock(mutex);
  xe_mutex_free(mutex);

  close(notify_rd_id_);
  close(notify_wr_id_);
}

int WsClient::socket_id() {
  return socket_id_;
}

int WsClient::Setup() {
  // Prep the socket.
  int opt_value;
  opt_value = 1;
  setsockopt(socket_id_, SOL_SOCKET, SO_KEEPALIVE,
             &opt_value, sizeof(opt_value));
  opt_value = 1;
  setsockopt(socket_id_, IPPROTO_TCP, TCP_NODELAY,
             &opt_value, sizeof(opt_value));

  // launch thread/etc
  EventThread();

  return 0;
}

namespace {

ssize_t WsClientSendCallback(wslay_event_context_ptr ctx,
                             const uint8_t* data, size_t len, int flags,
                             void* user_data) {
  WsClient* client = reinterpret_cast<WsClient*>(user_data);

  int sflags = 0;
#ifdef MSG_MORE
  if (flags & WSLAY_MSG_MORE) {
    sflags |= MSG_MORE;
  }
#endif  // MSG_MORE

  ssize_t r;
  while ((r = send(client->socket_id(), data, len, sflags)) == -1 &&
         errno == EINTR);
  if (r == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
    } else {
      wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
  }
  return r;
}

ssize_t WsClientRecvCallback(wslay_event_context_ptr ctx,
                             uint8_t* data, size_t len, int flags,
                             void* user_data) {
  WsClient* client = reinterpret_cast<WsClient*>(user_data);
  ssize_t r;
  while ((r = recv(client->socket_id(), data, len, 0)) == -1 &&
         errno == EINTR);
  if (r == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
    } else {
      wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
  } else if (r == 0) {
    wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    r = -1;
  }
  return r;
}

void WsClientOnMsgCallback(wslay_event_context_ptr ctx,
                           const struct wslay_event_on_msg_recv_arg* arg,
                           void* user_data) {
  if (wslay_is_ctrl_frame(arg->opcode)) {
    // Ignore control frames.
    return;
  }

  switch (arg->opcode) {
    case WSLAY_TEXT_FRAME:
      // arg->msg, arg->msg_length
      break;
    case WSLAY_BINARY_FRAME:
      // arg->msg, arg->msg_length
      break;
    default:
      // Unknown opcode - some frame stuff?
      break;
  }

  // TODO(benvanik): dispatch

  // WsClient* client = reinterpret_cast<WsClient*>(user_data);
  // char hello[] = "HELLO";
  // size_t len = sizeof(hello);
  // uint8_t* bs[] = {(uint8_t*)&hello};
  // size_t lens[] = {len};
  // client->Write((uint8_t**)bs, lens, 1);
  // printf("on msg %d, %ld\n", arg->opcode, arg->msg_length);
}

std::string EncodeBase64(const uint8_t* input, size_t length) {
  // Good god what a horrible API.
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* bmem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, bmem);
  BIO_write(b64, input, length);
  XEIGNORE(BIO_flush(b64));
  BUF_MEM* bptr;
  BIO_get_mem_ptr(b64, &bptr);
  std::string result(bptr->data, bptr->length);
  BIO_free_all(b64);
  // Strip the last character, which is a \n.
  result.erase(result.size() - 1);
  return result;
}

}

int WsClient::PerformHandshake() {
  std::string headers;
  char buffer[4096];
  ssize_t r;
  while (true) {
    while ((r = read(socket_id_, buffer, sizeof(buffer))) == -1 &&
           errno == EINTR);
    if (r == -1) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        if (!headers.size()) {
          // Nothing read yet - spin.
          continue;
        }
        break;
      } else {
        XELOGE(XT("HTTP header read failure"));
        return 1;
      }
    } else if (r == 0) {
      // EOF.
      XELOGE(XT("HTTP header EOF"));
      return 2;
    } else {
      headers.append(buffer, buffer + r);
      if (headers.size() > 8192) {
        XELOGE(XT("HTTP headers exceeded max buffer size"));
        return 3;
      }
    }
  }

  if (headers.find("\r\n\r\n") == std::string::npos) {
    XELOGE(XT("Incomplete HTTP headers: %s"), headers.c_str());
    return 1;
  }

  // Parse the headers to verify its a websocket request.
  std::string::size_type keyhdstart;
  if (headers.find("Upgrade: websocket\r\n") == std::string::npos ||
      headers.find("Connection: Upgrade\r\n") == std::string::npos ||
      (keyhdstart = headers.find("Sec-WebSocket-Key: ")) ==
          std::string::npos) {
    XELOGW(XT("HTTP connection does not contain websocket headers"));
    return 2;
  }
  keyhdstart += 19;
  std::string::size_type keyhdend = headers.find("\r\n", keyhdstart);
  std::string client_key = headers.substr(keyhdstart, keyhdend - keyhdstart);
  std::string accept_key = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t accept_sha[20];
  SHA1((uint8_t*)accept_key.c_str(), accept_key.size(), accept_sha);
  accept_key = EncodeBase64(accept_sha, sizeof(accept_sha));

  // Write the response to upgrade the connection.
  std::string response =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " + accept_key + "\r\n"
      "\r\n";
  size_t write_offset = 0;
  size_t write_length = response.size();
  while (true) {
    while ((r = write(socket_id_, response.c_str() + write_offset,
                      write_length)) == -1 && errno == EINTR);
    if (r == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        XELOGE(XT("HTTP response write failure"));
        return 4;
      }
    } else {
      write_offset += r;
      write_length -= r;
      if (!write_length) {
        break;
      }
    }
  }

  return 0;
}

void WsClient::EventThread() {
  int r;

  // Enable non-blocking IO on the socket.
  int flags;
  while ((flags = fcntl(socket_id_, F_GETFL, 0)) == -1 &&
         errno == EINTR);
  if (flags == -1) {
    return;
  }
  while ((r = fcntl(socket_id_, F_SETFL, flags | O_NONBLOCK)) == -1 &&
         errno == EINTR);
  if (r == -1) {
    return;
  }

  // First run the HTTP handshake.
  // This will fail if the connection is not for websockets.
  if (PerformHandshake()) {
    return;
  }

  // Prep callbacks.
  struct wslay_event_callbacks callbacks = {
    WsClientRecvCallback,
    WsClientSendCallback,
    NULL,
    NULL,
    NULL,
    NULL,
    WsClientOnMsgCallback,
  };

  // Prep the websocket server context.
  wslay_event_context_ptr ctx;
  wslay_event_context_server_init(&ctx, &callbacks, this);

  // Event for waiting on input.
  struct pollfd events[2];
  xe_zero_struct(&events, sizeof(events));
  events[0].fd      = socket_id_;
  events[0].events  = POLLIN;
  events[1].fd      = notify_rd_id_;
  events[1].events  = POLLIN;

  // Loop forever.
  while(wslay_event_want_read(ctx) || wslay_event_want_write(ctx)) {
    // Wait on the event.
    while ((r = poll(events, XECOUNT(events), -1)) == -1 && errno == EINTR);
    if (r == -1) {
      break;
    }

    // Handle any self-generated events to queue messages.
    if (events[1].revents) {
      uint8_t dummy;
      XEIGNORE(recv(notify_rd_id_, &dummy, 1, 0));
      xe_mutex_lock(mutex_);
      for (std::vector<struct wslay_event_msg>::iterator it =
           pending_messages_.begin(); it != pending_messages_.end(); it++) {
        struct wslay_event_msg* msg = &*it;
        wslay_event_queue_msg(ctx, msg);
      }
      pending_messages_.clear();
      xe_mutex_unlock(mutex_);
      events[1].revents = 0;
      events[1].events = POLL_IN;
    }

    // Handle websocket messages.
    struct pollfd* event = &events[0];
    if (((event->revents & POLLIN) && wslay_event_recv(ctx)) ||
        ((event->revents & POLLOUT) && wslay_event_send(ctx)) ||
        ((event->revents & (POLLERR | POLLHUP | POLLNVAL)))) {
      // Error handling the event.
      XELOGE(XT("Error handling WebSocket data"));
      break;
    }
    event->revents = 0;
    event->events = 0;
    if (wslay_event_want_read(ctx)) {
      event->events |= POLLIN;
    }
    if (wslay_event_want_write(ctx)) {
      event->events |= POLLOUT;
    }
  }

  wslay_event_context_free(ctx);
}

void WsClient::Write(uint8_t** buffers, size_t* lengths, size_t count) {
  if (!count) {
    return;
  }

  size_t combined_length;
  uint8_t* combined_buffer;
  if (count == 1) {
    // Single buffer, just copy.
    combined_length = lengths[0];
    combined_buffer = (uint8_t*)xe_malloc(lengths[0]);
    XEIGNORE(xe_copy_memory(combined_buffer, combined_length,
                            buffers[0], lengths[0]));
  } else {
    // Multiple buffers, merge.
    combined_length = 0;
    for (size_t n = 0; n < count; n++) {
      combined_length += lengths[n];
    }
    combined_buffer = (uint8_t*)xe_malloc(combined_length);
    for (size_t n = 0, offset = 0; n < count; n++) {
      XEIGNORE(xe_copy_memory(
          combined_buffer + offset, combined_length - offset,
          buffers[n], lengths[n]));
      offset += lengths[n];
    }
  }

  struct wslay_event_msg msg = {
    WSLAY_BINARY_FRAME,
    combined_buffer,
    combined_length,
  };

  xe_mutex_lock(mutex_);
  pending_messages_.push_back(msg);
  bool needs_signal = pending_messages_.size() == 1;
  xe_mutex_unlock(mutex_);

  if (needs_signal) {
    // Notify the poll().
    uint8_t b = 0xFF;
    write(notify_wr_id_, &b, 1);
  }
}
