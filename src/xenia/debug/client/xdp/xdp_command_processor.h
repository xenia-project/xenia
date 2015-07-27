/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_CLIENT_XDP_XDP_COMMAND_PROCESSOR_H_
#define XENIA_DEBUG_CLIENT_XDP_XDP_COMMAND_PROCESSOR_H_

#include <cstdint>

#include "xenia/debug/proto/packet_reader.h"
#include "xenia/debug/proto/packet_writer.h"
#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace client {
namespace xdp {

class XdpClient;

class XdpCommandProcessor {
 public:
  XdpCommandProcessor(XdpClient* client);
  ~XdpCommandProcessor() = default;

  bool ProcessBuffer(const uint8_t* buffer, size_t buffer_length);

 private:
  bool ProcessPacket(const proto::Packet* packet);

  void Flush();

  XdpClient* client_ = nullptr;
};

}  // namespace xdp
}  // namespace client
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_CLIENT_XDP_XDP_COMMAND_PROCESSOR_H_
