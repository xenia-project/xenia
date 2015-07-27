/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_SERVER_MI_MI_COMMAND_PROCESSOR_H_
#define XENIA_DEBUG_SERVER_MI_MI_COMMAND_PROCESSOR_H_

#include <cstdint>
#include <vector>

#include "xenia/debug/debugger.h"
#include "xenia/debug/server/mi/mi_protocol.h"

namespace xe {
namespace debug {
namespace server {
namespace mi {

class MIServer;

class MICommandProcessor {
 public:
  MICommandProcessor(MIServer* server, Debugger* debugger);
  ~MICommandProcessor() = default;

  bool ProcessBuffer(const uint8_t* buffer, size_t buffer_length);

 private:
  bool ProcessPacket(const char* buffer, size_t buffer_length);
  void SendConsoleStreamOutput(const char* value);
  void SendTargetStreamOutput(const char* value);
  void SendLogStreamOutput(const char* value);
  void SendResult(const std::string& prefix, ResultClass result_class);
  void SendErrorResult(const std::string& prefix, const char* message = nullptr,
                       const char* code = nullptr);

  MIServer* server_ = nullptr;
  Debugger* debugger_ = nullptr;
  std::vector<uint8_t> receive_buffer_;
  size_t receive_offset_ = 0;
  std::vector<uint8_t> transmit_buffer_;

  // Token of the last -exec-* command. Sent back on stops.
  std::string exec_token_;
};

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_SERVER_MI_MI_COMMAND_PROCESSOR_H_
