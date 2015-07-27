/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/server/gdb/gdb_command_processor.h"

#include "xenia/base/logging.h"
#include "xenia/base/string_buffer.h"
#include "xenia/debug/server/gdb/gdb_server.h"

namespace xe {
namespace debug {
namespace server {
namespace gdb {

constexpr size_t kReceiveBufferSize = 1 * 1024 * 1024;
constexpr size_t kTransmitBufferSize = 1 * 1024 * 1024;

GdbCommandProcessor::GdbCommandProcessor(GdbServer* server, Debugger* debugger)
    : server_(server), debugger_(debugger) {
  receive_buffer_.resize(kReceiveBufferSize);
  transmit_buffer_.resize(kTransmitBufferSize);
}

bool GdbCommandProcessor::ProcessBuffer(const uint8_t* buffer,
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
      if (receive_buffer_[i] == '#') {
        end_offset = i - 1;
        buffer_end_offset = i + 2;
        break;
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
      XELOGE("Failed to process incoming GDB line: %s", line.c_str());
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

bool GdbCommandProcessor::ProcessPacket(const char* buffer,
                                        size_t buffer_length) {
  // There may be any number of leading +'s or -'s.
  size_t offset = 0;
  for (; offset < buffer_length; ++offset) {
    if (buffer[offset] == '$') {
      // Command start.
      ++offset;
      break;
    } else if (buffer[offset] == '+') {
      // Ack.
    } else if (buffer[offset] == '-') {
      // No good - means transmission error.
      XELOGE("GDB client remorted transmission error");
      return false;
    }
  }

  std::string line((const char*)(buffer + offset), buffer_length - 1);
  XELOGI("GDB -> %s", line.c_str());

  // Immediately send ACK.
  if (!no_ack_mode_) {
    server_->client()->Send("+", 1);
  }

  const char* buffer_ptr = buffer + offset;
  bool handled = false;
  switch (buffer[offset]) {
    case '!':
      // Enable extended mode.
      SendResponse("OK");
      handled = true;
      break;
    case 'v':
      // Verbose packets.
      if (std::strstr(buffer_ptr, "vRun") == buffer_ptr) {
        SendResponse("S05");
        handled = true;
      } else if (std::strstr(buffer_ptr, "vCont") == buffer_ptr) {
        SendResponse("S05");
        handled = true;
      }
      break;
    case 'q':
      // Query packets.
      switch (buffer_ptr[1]) {
        case 'C':
          // Get current thread ID.
          SendResponse("QC01");
          handled = true;
          break;
        case 'R':
          // Command.
          SendResponse("OK");
          handled = true;
          break;
        default:
          if (std::strstr(buffer_ptr, "qSupported") == buffer_ptr) {
            // Get/set feature support.
            handled = Process_qSupported(line);
          } else if (std::strstr(buffer_ptr, "qAttached") == buffer_ptr) {
            // Check attach mode; 0 = new process, 1 = existing process.
            SendResponse("0");
            handled = true;
          } else if (std::strstr(buffer_ptr, "qfThreadInfo") == buffer_ptr) {
            // Start of thread list request.
            SendResponse("m01");
            handled = true;
          } else if (std::strstr(buffer_ptr, "qsThreadInfo") == buffer_ptr) {
            // Continuation of thread list request.
            SendResponse("l");  // l = last.
            handled = true;
          }
          break;
      }
      break;
    case 'Q':
      // Set packets.
      switch (buffer_ptr[1]) {
        default:
          if (std::strstr(buffer_ptr, "QStartNoAckMode") == buffer_ptr) {
            no_ack_mode_ = true;
            SendResponse("OK");
            handled = true;
          }
          break;
      }
      break;
    case 'H':
      // Set thread for subsequent operations.
      SendResponse("OK");
      handled = true;
      break;
    case 'g':
      // Read all registers.
      handled = ProcessReadRegisters(line);
      break;
    case 'G':
      // Write all registers.
      handled = true;
      break;
    case 'p':
      // Read register.
      handled = true;
      break;
    case 'P':
      // Write register.
      handled = true;
      break;
    case 'm':
      // Read memory.
      handled = true;
      break;
    case 'M':
      // Write memory.
      handled = true;
      break;
    case 'Z':
      // Insert breakpoint.
      handled = true;
      break;
    case 'z':
      // Remove breakpoint.
      handled = true;
      break;
    case '?':
      // Query halt reason.
      SendResponse("S05");
      handled = true;
      break;
    case 'c':
      // Continue (vCont should be used instead).
      // NOTE: reply is sent on halt, not right now.
      SendResponse("S05");
      handled = true;
      break;
    case 's':
      // Single step.
      // NOTE: reply is sent on halt, not right now.
      SendResponse("S05");
      handled = true;
      break;
  }
  if (!handled) {
    XELOGE("Unknown GDB packet: %s", buffer_ptr);
    SendResponse("");
  }
  return true;
}

void GdbCommandProcessor::SendResponse(const char* value, size_t length) {
  XELOGI("GDB <- %s", value);
  uint8_t checksum = 0;
  for (size_t i = 0; i < length; ++i) {
    uint8_t c = uint8_t(value[i]);
    if (!c) {
      break;
    }
    checksum += c;
  }
  char crc[4];
  sprintf(crc, "#%.2X", checksum);
  std::pair<const void*, size_t> buffers[] = {
      {"$", 1}, {value, length}, {crc, 3},
  };
  server_->client()->Send(buffers, xe::countof(buffers));
}

bool GdbCommandProcessor::Process_qSupported(const std::string& line) {
  StringBuffer response;

  // Read in the features the client supports.
  // qSupported[:gdbfeature[;gdbfeature]...]
  size_t feature_offset = line.find(':');
  while (feature_offset != std::string::npos) {
    size_t next_offset = line.find(';', feature_offset + 1);
    std::string feature =
        line.substr(feature_offset + 1, next_offset - feature_offset - 1);
    feature_offset = next_offset;
    if (feature.find("multiprocess") == 0) {
      bool is_supported = feature[12] == '+';
    } else if (feature.find("xmlRegisters") == 0) {
      // xmlRegisters=A,B,C
    } else if (feature.find("qRelocInsn") == 0) {
      bool is_supported = feature[10] == '+';
    } else if (feature.find("swbreak") == 0) {
      bool is_supported = feature[7] == '+';
    } else if (feature.find("hwbreak") == 0) {
      bool is_supported = feature[7] == '+';
    } else {
      XELOGW("Unknown GDB client support feature: %s", feature.c_str());
    }
  }

  response.Append("PacketSize=4000;");
  response.Append("QStartNoAckMode+;");
  response.Append("qRelocInsn-;");
  response.Append("multiprocess-;");
  response.Append("ConditionalBreakpoints-;");
  response.Append("ConditionalTracepoints-;");
  response.Append("ReverseContinue-;");
  response.Append("ReverseStep-;");
  response.Append("swbreak+;");
  response.Append("hwbreak+;");

  SendResponse(response.GetString());
  return true;
}

bool GdbCommandProcessor::ProcessReadRegisters(const std::string& line) {
  StringBuffer response;

  for (int32_t n = 0; n < 32; n++) {
    // gpr
    response.AppendFormat("%08X", n);
  }
  for (int64_t n = 0; n < 32; n++) {
    // fpr
    response.AppendFormat("%016llX", n);
  }
  response.AppendFormat("%08X", 0x8202FB40);  // pc
  response.AppendFormat("%08X", 65);          // msr
  response.AppendFormat("%08X", 66);          // cr
  response.AppendFormat("%08X", 67);          // lr
  response.AppendFormat("%08X", 68);          // ctr
  response.AppendFormat("%08X", 69);          // xer
  response.AppendFormat("%08X", 70);          // fpscr

  SendResponse(response.GetString());
  return true;
}

}  // namespace gdb
}  // namespace server
}  // namespace debug
}  // namespace xe
