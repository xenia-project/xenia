/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/server/mi/mi_command_processor.h"

#include "xenia/base/logging.h"
#include "xenia/base/string_buffer.h"
#include "xenia/debug/server/mi/mi_reader.h"
#include "xenia/debug/server/mi/mi_server.h"
#include "xenia/debug/server/mi/mi_writer.h"

namespace xe {
namespace debug {
namespace server {
namespace mi {

constexpr size_t kReceiveBufferSize = 1 * 1024 * 1024;
constexpr size_t kTransmitBufferSize = 1 * 1024 * 1024;

MICommandProcessor::MICommandProcessor(MIServer* server, Debugger* debugger)
    : server_(server), debugger_(debugger) {
  receive_buffer_.resize(kReceiveBufferSize);
  transmit_buffer_.resize(kTransmitBufferSize);
}

bool MICommandProcessor::ProcessBuffer(const uint8_t* buffer,
                                       size_t buffer_length) {
  // Grow and append the bytes to the receive buffer.
  while (receive_offset_ + buffer_length > receive_buffer_.capacity()) {
    receive_buffer_.resize(receive_buffer_.size() * 2);
  }
  std::memcpy(receive_buffer_.data() + receive_offset_, buffer, buffer_length);
  receive_offset_ += buffer_length;

  // While there are bytes pending in the buffer we scan through looking for end
  // markers. When we find one, we emit the packet and move on.
  size_t process_offset = 0;
  while (process_offset < receive_offset_) {
    // Look for an end marker.
    size_t end_offset = -1;
    size_t buffer_end_offset = -1;
    for (size_t i = process_offset; i < receive_offset_; ++i) {
      if (receive_buffer_[i] == '\r') {
        end_offset = i - 1;
        buffer_end_offset = i + 1;
        break;
      } else if (receive_buffer_[i] == '\n') {
        end_offset = i - 1;
        buffer_end_offset = i;
      }
    }
    if (end_offset == -1) {
      // No end marker found - break out for now.
      break;
    }

    // Emit packet.
    if (!ProcessPacket((const char*)(receive_buffer_.data() + process_offset),
                       end_offset - process_offset)) {
      // Failed to process line.
      std::string line(receive_buffer_.data() + process_offset,
                       receive_buffer_.data() + end_offset);
      XELOGE("Failed to process incoming MI line: %s", line.c_str());
      return false;
    }
    process_offset = buffer_end_offset + 1;
  }

  // If we have leftover unprocessed bytes move them to the front of the buffer.
  if (process_offset < receive_offset_) {
    size_t remaining_bytes = receive_offset_ - process_offset;
    std::memmove(receive_buffer_.data(),
                 receive_buffer_.data() + process_offset, remaining_bytes);
    receive_offset_ = remaining_bytes;
  } else {
    receive_offset_ = 0;
  }

  return true;
}

bool MICommandProcessor::ProcessPacket(const char* buffer,
                                       size_t buffer_length) {
  // Will likely have a request prefix, like '#####-....'.
  auto buffer_ptr = std::strchr(buffer, '-');
  if (!buffer_ptr) {
    XELOGE("Malformed MI command");
    return false;
  }
  std::string token;
  if (buffer_ptr != buffer) {
    token = std::string(buffer, buffer_ptr);
  }
  ++buffer_ptr;  // Skip leading '-'.

  std::string line((const char*)buffer_ptr,
                   buffer_length - (buffer_ptr - buffer) + 1);
  XELOGI("MI -> %s", line.c_str());

  auto command = line.substr(0, line.find(' '));
  auto args =
      command.size() == line.size() ? "" : line.substr(command.size() + 1);

  bool handled = false;
  if (command == "gdb-set") {
    auto key = args.substr(0, args.find(' '));
    auto value = args.substr(args.find(' ') + 1);
    if (key == "mi-async" || key == "target-async") {
      bool is_enabled = value == "1" || value == "on";
      SendResult(token, ResultClass::kDone);
      handled = true;
    } else if (key == "stop-on-solib-events") {
      bool is_enabled = value == "1" || value == "on";
      SendResult(token, ResultClass::kDone);
      handled = true;
    }
  } else if (command == "file-exec-and-symbols") {
    // args=foo
    SendResult(token, ResultClass::kDone);
    handled = true;
  } else if (command == "break-insert") {
    // args=main
    SendResult(token, ResultClass::kDone);
    handled = true;
  } else if (command == "exec-run") {
    exec_token_ = token;
    SendResult(token, ResultClass::kRunning);
    handled = true;

    MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
    writer.AppendRaw(exec_token_);
    writer.BeginAsyncNotifyRecord("library-loaded");
    writer.AppendString("id=123");
    writer.AppendString("target-name=\"foo.xex\"");
    writer.AppendString("host-name=\"foo.xex\"");
    writer.EndRecord();
    server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
  } else if (command == "exec-continue") {
    exec_token_ = token;
    SendResult(token, ResultClass::kRunning);
    handled = true;
  } else if (command == "exec-interrupt") {
    SendResult(token, ResultClass::kDone);
    handled = true;

    // later:
    MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
    writer.AppendRaw(exec_token_);
    writer.BeginAsyncExecRecord("stopped");
    writer.AppendString("reason=\"signal-received\"");
    writer.AppendString("signal-name=\"SIGINT\"");
    writer.AppendString("signal-meaning=\"Interrupt\"");
    writer.EndRecord();
    server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
  } else if (command == "exec-abort") {
    XELOGI("Debug client requested abort");
    SendResult(token, ResultClass::kDone);
    exit(1);
    handled = true;
  } else if (command == "gdb-exit") {
    // TODO(benvanik): die better?
    XELOGI("Debug client requested exit");
    exit(1);
    SendResult(token, ResultClass::kDone);
    handled = true;
  }
  if (!handled) {
    XELOGE("Unknown GDB packet: %s", buffer_ptr);
    SendErrorResult(token, "Unknown/unimplemented");
  }
  return true;
}

void MICommandProcessor::SendConsoleStreamOutput(const char* value) {
  MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
  writer.BeginConsoleStreamOutput();
  writer.AppendString(value);
  writer.EndRecord();
  server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
}

void MICommandProcessor::SendTargetStreamOutput(const char* value) {
  MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
  writer.BeginTargetStreamOutput();
  writer.AppendString(value);
  writer.EndRecord();
  server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
}

void MICommandProcessor::SendLogStreamOutput(const char* value) {
  MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
  writer.BeginLogStreamOutput();
  writer.AppendString(value);
  writer.EndRecord();
  server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
}

void MICommandProcessor::SendResult(const std::string& prefix,
                                    ResultClass result_class) {
  MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
  writer.AppendRaw(prefix);
  writer.BeginResultRecord(result_class);
  writer.EndRecord();
  server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
}

void MICommandProcessor::SendErrorResult(const std::string& prefix,
                                         const char* message,
                                         const char* code) {
  MIWriter writer(transmit_buffer_.data(), transmit_buffer_.size());
  writer.AppendRaw(prefix);
  writer.BeginResultRecord(ResultClass::kError);
  if (message) {
    writer.AppendResult("msg", message);
  }
  if (code) {
    writer.AppendResult("code", code);
  }
  writer.EndRecord();
  server_->client()->Send(transmit_buffer_.data(), writer.buffer_offset());
}

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe
