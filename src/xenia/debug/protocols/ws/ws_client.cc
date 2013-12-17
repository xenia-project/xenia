/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/ws/ws_client.h>

#include <xenia/debug/debug_server.h>
#include <xenia/debug/protocols/ws/simple_sha1.h>

#if XE_PLATFORM(WIN32)
// Required for wslay.
typedef SSIZE_T ssize_t;
#endif  // WIN32

#include <wslay/wslay.h>


using namespace xe;
using namespace xe::debug;
using namespace xe::debug::protocols::ws;


WSClient::WSClient(DebugServer* debug_server, int socket_id) :
    DebugClient(debug_server),
    thread_(NULL),
    socket_id_(socket_id) {
  mutex_ = xe_mutex_alloc(1000);

  loop_ = xe_socket_loop_create(socket_id);
}

WSClient::~WSClient() {
  xe_mutex_t* mutex = mutex_;
  xe_mutex_lock(mutex);

  mutex_ = NULL;

  xe_socket_close(socket_id_);
  socket_id_ = 0;

  xe_socket_loop_destroy(loop_);
  loop_ = NULL;

  xe_mutex_unlock(mutex);
  xe_mutex_free(mutex);

  xe_thread_release(thread_);
}

int WSClient::socket_id() {
  return socket_id_;
}

int WSClient::Setup() {
  // Prep the socket.
  xe_socket_set_keepalive(socket_id_, true);
  xe_socket_set_nodelay(socket_id_, true);

  thread_ = xe_thread_create("WS Debugger Client", StartCallback, this);
  return xe_thread_start(thread_);
}

void WSClient::StartCallback(void* param) {
  WSClient* client = reinterpret_cast<WSClient*>(param);
  client->EventThread();
}

namespace {

int64_t WSClientSendCallback(wslay_event_context_ptr ctx,
                             const uint8_t* data, size_t len, int flags,
                             void* user_data) {
  WSClient* client = reinterpret_cast<WSClient*>(user_data);

  int error_code = 0;
  int64_t r;
  while ((r = xe_socket_send(client->socket_id(), data, len, 0,
                             &error_code)) == -1 && error_code == EINTR);
  if (r == -1) {
    if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
      wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
    } else {
      wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
    }
  }
  return r;
}

int64_t WSClientRecvCallback(wslay_event_context_ptr ctx,
                             uint8_t* data, size_t len, int flags,
                             void* user_data) {
  WSClient* client = reinterpret_cast<WSClient*>(user_data);

  int error_code = 0;
  int64_t r;
  while ((r = xe_socket_recv(client->socket_id(), data, len, 0,
                             &error_code)) == -1 && error_code == EINTR);
  if (r == -1) {
    if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
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

void WSClientOnMsgCallback(wslay_event_context_ptr ctx,
                           const struct wslay_event_on_msg_recv_arg* arg,
                           void* user_data) {
  if (wslay_is_ctrl_frame(arg->opcode)) {
    // Ignore control frames.
    return;
  }

  WSClient* client = reinterpret_cast<WSClient*>(user_data);
  switch (arg->opcode) {
    case WSLAY_TEXT_FRAME:
      XELOGW("Text frame ignored; use binary messages");
      break;
    case WSLAY_BINARY_FRAME:
      //client->OnMessage(arg->msg, arg->msg_length);
      break;
    default:
      // Unknown opcode - some frame stuff?
      break;
  }
}

std::string EncodeBase64(const uint8_t* input, size_t length) {
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  size_t remaining = length;
  size_t n = 0;
  while (remaining) {
    result.push_back(b64[input[n] >> 2]);
    result.push_back(b64[((input[n] & 0x03) << 4) |
                         ((input[n + 1] & 0xf0) >> 4)]);
    remaining--;
    if (remaining) {
      result.push_back(b64[((input[n + 1] & 0x0f) << 2) |
                           ((input[n + 2] & 0xc0) >> 6)]);
      remaining--;
    } else {
      result.push_back('=');
    }
    if (remaining) {
      result.push_back(b64[input[n + 2] & 0x3f]);
      remaining--;
    } else {
      result.push_back('=');
    }
    n += 3;
  }
  return result;
}

}

int WSClient::PerformHandshake() {
  std::string headers;
  uint8_t buffer[4096];
  int error_code = 0;
  int64_t r;
  while (true) {
    while ((r = xe_socket_recv(socket_id_, buffer, sizeof(buffer), 0,
                               &error_code)) == -1 && error_code == EINTR);
    if (r == -1) {
      if (error_code == EWOULDBLOCK || error_code == EAGAIN) {
        if (!headers.size()) {
          // Nothing read yet - spin.
          continue;
        }
        break;
      } else {
        XELOGE("HTTP header read failure");
        return 1;
      }
    } else if (r == 0) {
      // EOF.
      XELOGE("HTTP header EOF");
      return 2;
    } else {
      headers.append(buffer, buffer + r);
      if (headers.size() > 8192) {
        XELOGE("HTTP headers exceeded max buffer size");
        return 3;
      }
    }
  }

  if (headers.find("\r\n\r\n") == std::string::npos) {
    XELOGE("Incomplete HTTP headers: %s", headers.c_str());
    return 1;
  }

  // Parse the headers to verify its a websocket request.
  std::string::size_type keyhdstart;
  if (headers.find("Upgrade: websocket\r\n") == std::string::npos ||
      headers.find("Connection: Upgrade\r\n") == std::string::npos ||
      (keyhdstart = headers.find("Sec-WebSocket-Key: ")) ==
          std::string::npos) {
    XELOGW("HTTP connection does not contain websocket headers");
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
    while ((r = xe_socket_send(socket_id_,
                               (uint8_t*)response.c_str() + write_offset,
                               write_length, 0, &error_code)) == -1 &&
           error_code == EINTR);
    if (r == -1) {
      if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
        break;
      } else {
        XELOGE("HTTP response write failure");
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

void WSClient::EventThread() {
  // Enable non-blocking IO on the socket.
  xe_socket_set_nonblock(socket_id_, true);

  // First run the HTTP handshake.
  // This will fail if the connection is not for websockets.
  if (PerformHandshake()) {
    return;
  }

  // Prep callbacks.
  struct wslay_event_callbacks callbacks = {
    (wslay_event_recv_callback)WSClientRecvCallback,
    (wslay_event_send_callback)WSClientSendCallback,
    NULL,
    NULL,
    NULL,
    NULL,
    WSClientOnMsgCallback,
  };

  // Prep the websocket server context.
  wslay_event_context_ptr ctx;
  wslay_event_context_server_init(&ctx, &callbacks, this);

  // Loop forever.
  while (wslay_event_want_read(ctx) || wslay_event_want_write(ctx)) {
    // Wait on the event.
    if (xe_socket_loop_poll(loop_,
                            !!wslay_event_want_read(ctx),
                            !!wslay_event_want_write(ctx))) {
      break;
    }

    // Handle any self-generated events to queue messages.
    if (xe_socket_loop_check_queued_write(loop_)) {
      xe_mutex_lock(mutex_);
      for (std::vector<struct wslay_event_msg>::iterator it =
           pending_messages_.begin(); it != pending_messages_.end(); it++) {
        struct wslay_event_msg* msg = &*it;
        wslay_event_queue_msg(ctx, msg);
      }
      pending_messages_.clear();
      xe_mutex_unlock(mutex_);
    }

    // Handle websocket messages.
    if ((xe_socket_loop_check_socket_recv(loop_) && wslay_event_recv(ctx)) ||
        (xe_socket_loop_check_socket_send(loop_) && wslay_event_send(ctx))) {
      // Error handling the event.
      XELOGE("Error handling WebSocket data");
      break;
    }
  }

  wslay_event_context_free(ctx);
}

void WSClient::Write(uint8_t** buffers, size_t* lengths, size_t count) {
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
    xe_socket_loop_set_queued_write(loop_);
  }
}
