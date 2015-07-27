/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_SERVER_GDB_GDB_COMMAND_PROCESSOR_H_
#define XENIA_DEBUG_SERVER_GDB_GDB_COMMAND_PROCESSOR_H_

#include <cstdint>
#include <vector>

#include "xenia/debug/debugger.h"

namespace xe {
namespace debug {
namespace server {
namespace gdb {

class GdbServer;

class GdbCommandProcessor {
 public:
  GdbCommandProcessor(GdbServer* server, Debugger* debugger);
  ~GdbCommandProcessor() = default;

  bool ProcessBuffer(const uint8_t* buffer, size_t buffer_length);

 private:
  bool ProcessPacket(const char* buffer, size_t buffer_length);
  void SendResponse(const char* value, size_t length);
  void SendResponse(const char* value) {
    SendResponse(value, std::strlen(value));
  }
  void SendResponse(std::string value) {
    SendResponse(value.data(), value.size());
  }

  bool Process_qSupported(const std::string& line);
  bool ProcessReadRegisters(const std::string& line);

  GdbServer* server_ = nullptr;
  Debugger* debugger_ = nullptr;
  std::vector<uint8_t> receive_buffer_;
  size_t receive_offset_ = 0;
  std::vector<uint8_t> transmit_buffer_;

  bool no_ack_mode_ = false;
};

}  // namespace gdb
}  // namespace server
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_SERVER_GDB_GDB_COMMAND_PROCESSOR_H_
