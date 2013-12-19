/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/gdb/gdb_client.h>

#include <xenia/debug/debug_server.h>
#include <xenia/debug/protocols/gdb/message.h>


using namespace xe;
using namespace xe::debug;
using namespace xe::debug::protocols::gdb;


GDBClient::GDBClient(DebugServer* debug_server, socket_t socket_id) :
    DebugClient(debug_server),
    thread_(NULL),
    socket_id_(socket_id),
    current_reader_(0), send_queue_stalled_(false) {
  mutex_ = xe_mutex_alloc(1000);

  loop_ = xe_socket_loop_create(socket_id);
}

GDBClient::~GDBClient() {
  xe_mutex_t* mutex = mutex_;
  xe_mutex_lock(mutex);

  mutex_ = NULL;

  delete current_reader_;
  for (auto it = message_reader_pool_.begin();
       it != message_reader_pool_.end(); ++it) {
    delete *it;
  }
  for (auto it = message_writer_pool_.begin();
       it != message_writer_pool_.end(); ++it) {
    delete *it;
  }
  for (auto it = send_queue_.begin(); it != send_queue_.end(); ++it) {
    delete *it;
  }

  xe_socket_close(socket_id_);
  socket_id_ = 0;

  xe_socket_loop_destroy(loop_);
  loop_ = NULL;

  xe_mutex_unlock(mutex);
  xe_mutex_free(mutex);

  xe_thread_release(thread_);
}

int GDBClient::Setup() {
  // Prep the socket.
  xe_socket_set_keepalive(socket_id_, true);
  xe_socket_set_nodelay(socket_id_, true);

  thread_ = xe_thread_create("GDB Debugger Client", StartCallback, this);
  return xe_thread_start(thread_);
}

void GDBClient::Close() {
  xe_socket_close(socket_id_);
  socket_id_ = 0;
}

void GDBClient::StartCallback(void* param) {
  GDBClient* client = reinterpret_cast<GDBClient*>(param);
  client->EventThread();
}

int GDBClient::PerformHandshake() {
  return 0;
}

void GDBClient::EventThread() {
  // Enable non-blocking IO on the socket.
  xe_socket_set_nonblock(socket_id_, true);

  // First run the HTTP handshake.
  // This will fail if the connection is not for websockets.
  if (PerformHandshake()) {
    return;
  }

  // Loop forever.
  while (true) {
    // Wait on the event.
    if (xe_socket_loop_poll(loop_, true, send_queue_stalled_)) {
      break;
    }

    // Handle any self-generated events to queue messages.
    xe_mutex_lock(mutex_);
    for (auto it = pending_messages_.begin();
          it != pending_messages_.end(); it++) {
      MessageWriter* message = *it;
      send_queue_.push_back(message);
    }
    pending_messages_.clear();
    xe_mutex_unlock(mutex_);

    // Handle websocket messages.
    if (xe_socket_loop_check_socket_recv(loop_) && CheckReceive()) {
      // Error handling the event.
      XELOGE("Error handling WebSocket data");
      break;
    }
    if (!send_queue_stalled_ || xe_socket_loop_check_socket_send(loop_)) {
      if (PumpSendQueue()) {
        // Error handling the event.
        XELOGE("Error handling WebSocket data");
        break;
      }
    }
  }
}

int GDBClient::CheckReceive() {
  uint8_t buffer[4096];
  while (true) {
    int error_code = 0;
    int64_t r = xe_socket_recv(
        socket_id_, buffer, sizeof(buffer), 0, &error_code);
    if (r == -1) {
      if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
        return 0;
      } else {
        return 1;
      }
    } else if (r == 0) {
      return 1;
    } else {
      // Append current reader until #.
      int64_t pos = 0;
      if (current_reader_) {
        for (; pos < r; pos++) {
          if (buffer[pos] == '#') {
            pos++;
            current_reader_->Append(buffer, pos);
            MessageReader* message = current_reader_;
            current_reader_ = NULL;
            DispatchMessage(message);
            break;
          }
        }
      }

      // Loop reading all messages in the buffer.
      for (; pos < r; pos++) {
        if (buffer[pos] == '$') {
          if (message_reader_pool_.size()) {
            current_reader_ = message_reader_pool_.back();
            message_reader_pool_.pop_back();
            current_reader_->Reset();
          } else {
            current_reader_ = new MessageReader();
          }
          size_t start_pos = pos;

          // Scan until next #.
          bool found_end = false;
          for (; pos < r; pos++) {
            if (buffer[pos] == '#') {
              pos++;
              current_reader_->Append(buffer + start_pos, pos - start_pos);
              MessageReader* message = current_reader_;
              current_reader_ = NULL;
              DispatchMessage(message);
              found_end = true;
              break;
            }
          }
          if (!found_end) {
            // Ran out of bytes before message ended, keep around.
            current_reader_->Append(buffer + start_pos, r - start_pos);
          }
          break;
        }
      }
    }
  }

  return 0;
}

void GDBClient::DispatchMessage(MessageReader* message) {
  // $[message]#
  const char* str = message->GetString();
  str++; // skip $

  printf("GDB: %s", str);

  bool handled = false;
  switch (str[0]) {
  case '!':
    // Enable extended mode.
    Send("OK");
    handled = true;
    break;
  case 'v':
    // Verbose packets.
    if (strstr(str, "vRun") == str) {
      Send("S05");
      handled = true;
    } else if (strstr(str, "vCont") == str) {
      Send("S05");
      handled = true;
    }
    break;
  case 'q':
    // Query packets.
    switch (str[1]) {
    case 'C':
      // Get current thread ID.
      Send("QC01");
      handled = true;
      break;
    case 'R':
      // Command.
      Send("OK");
      handled = true;
      break;
    default:
      if (strstr(str, "qfThreadInfo") == str) {
        // Start of thread list request.
        Send("m01");
        handled = true;
      } else if (strstr(str, "qsThreadInfo") == str) {
        // Continuation of thread list.
        Send("l"); // l = last
        handled = true;
      }
      break;
    }
    break;
    
#if 0
  case 'H':
    // Set current thread.
    break;
#endif

  case 'g':
    // Read all registers.
    HandleReadRegisters(str + 1);
    handled = true;
    break;
  case 'G':
    // Write all registers.
    HandleWriteRegisters(str + 1);
    handled = true;
    break;
  case 'p':
    // Read register.
    HandleReadRegister(str + 1);
    handled = true;
    break;
  case 'P':
    // Write register.
    HandleWriteRegister(str + 1);
    handled = true;
    break;

  case 'm':
    // Read memory.
    HandleReadMemory(str + 1);
    handled = true;
  case 'M':
    // Write memory.
    HandleWriteMemory(str + 1);
    handled = true;
    break;

  case 'Z':
    // Insert breakpoint.
    HandleAddBreakpoint(str + 1);
    handled = true;
    break;
  case 'z':
    // Remove breakpoint.
    HandleRemoveBreakpoint(str + 1);
    handled = true;
    break;

  case '?':
    // Query halt reason.
    Send("S05");
    handled = true;
    break;
  case 'c':
    // Continue.
    // Deprecated: vCont should be used instead.
    // NOTE: reply is sent on halt, not right now.
    Send("S05");
    handled = true;
    break;
  case 's':
    // Single step.
    // NOTE: reply is sent on halt, not right now.
    Send("S05");
    handled = true;
    break;
  }

  if (!handled) {
    // Unknown packet type. We should ACK just to keep IDA happy.
    XELOGW("Unknown GDB packet type: %c", str[0]);
    Send("");
  }
}

int GDBClient::PumpSendQueue() {
  send_queue_stalled_ = false;
  for (auto it = send_queue_.begin(); it != send_queue_.end(); it++) {
    MessageWriter* message = *it;
    int error_code = 0;
    int64_t r;
    const uint8_t* data = message->buffer() + message->offset();
    size_t bytes_remaining = message->length();
    while (bytes_remaining) {
      r = xe_socket_send(
          socket_id_, data, bytes_remaining, 0, &error_code);
      if (r == -1) {
        if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
          // Message did not finish sending.
          send_queue_stalled_ = true;
          return 0;
        } else {
          return 1;
        }
      } else {
        bytes_remaining -= r;
        data += r;
        message->set_offset(message->offset() + r);
      }
    }
    if (!bytes_remaining) {
      xe_mutex_lock(mutex_);
      message_writer_pool_.push_back(message);
      xe_mutex_unlock(mutex_);
    }
  }
  return 0;
}

MessageWriter* GDBClient::BeginSend() {
  MessageWriter* message = NULL;
  xe_mutex_lock(mutex_);
  if (message_writer_pool_.size()) {
    message = message_writer_pool_.back();
    message_writer_pool_.pop_back();
  }
  xe_mutex_unlock(mutex_);
  if (message) {
    message->Reset();
  } else {
    message = new MessageWriter();
  }
  return message;
}

void GDBClient::EndSend(MessageWriter* message) {
  message->Finalize();

  xe_mutex_lock(mutex_);
  pending_messages_.push_back(message);
  bool needs_signal = pending_messages_.size() == 1;
  xe_mutex_unlock(mutex_);

  if (needs_signal) {
    // Notify the poll().
    xe_socket_loop_set_queued_write(loop_);
  }
}

void GDBClient::Send(const char* format, ...) {
  auto message = BeginSend();
  va_list args;
  va_start(args, format);
  message->AppendVarargs(format, args);
  va_end(args);
  EndSend(message);
}

void GDBClient::HandleReadRegisters(const char* str) {
  auto message = BeginSend();
  for (int32_t n = 0; n < 32; n++) {
    // gpr
    message->Append("%08X", n);
  }
  for (int64_t n = 0; n < 32; n++) {
    // fpr
    message->Append("%016llX", n);
  }
  message->Append("%08X", 0x8202FB40); // pc
  message->Append("%08X", 65); // msr
  message->Append("%08X", 66); // cr
  message->Append("%08X", 67); // lr
  message->Append("%08X", 68); // ctr
  message->Append("%08X", 69); // xer
  message->Append("%08X", 70); // fpscr
  EndSend(message);
}

void GDBClient::HandleWriteRegisters(const char* str) {
  Send("OK");
}

void GDBClient::HandleReadRegister(const char* str) {
  // p...HH
  // HH = hex digit indicating register #
  auto message = BeginSend();
  message->Append("%.8X", 0x8202FB40);
  EndSend(message);
}

void GDBClient::HandleWriteRegister(const char* str) {
  Send("OK");
}

void GDBClient::HandleReadMemory(const char* str) {
  // m...ADDR,SIZE
  uint32_t addr;
  uint32_t size;
  scanf("%X,%X", &addr, &size);
  auto message = BeginSend();
  for (size_t n = 0; n < size; n++) {
    message->Append("00");
  }
  EndSend(message);
}

void GDBClient::HandleWriteMemory(const char* str) {
  Send("OK");
}

void GDBClient::HandleAddBreakpoint(const char* str) {
  Send("OK");
}

void GDBClient::HandleRemoveBreakpoint(const char* str) {
  Send("OK");
}
