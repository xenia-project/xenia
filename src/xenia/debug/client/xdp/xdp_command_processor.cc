/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/client/xdp/xdp_command_processor.h"

#include "xenia/base/logging.h"
#include "xenia/base/string_buffer.h"
#include "xenia/debug/client/xdp/xdp_client.h"

namespace xe {
namespace debug {
namespace client {
namespace xdp {

using namespace xe::debug::proto;

constexpr size_t kReceiveBufferSize = 1 * 1024 * 1024;
constexpr size_t kTransmitBufferSize = 1 * 1024 * 1024;

XdpCommandProcessor::XdpCommandProcessor(XdpClient* client)
    : client_(client),
      packet_reader_(kReceiveBufferSize),
      packet_writer_(kTransmitBufferSize) {}

}  // namespace xdp
}  // namespace client
}  // namespace debug
}  // namespace xe
