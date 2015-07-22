/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_TRANSPORT_GDB_GDB_TRANSPORT_H_
#define XENIA_DEBUG_TRANSPORT_GDB_GDB_TRANSPORT_H_

#include <memory>

#include "xenia/base/socket.h"
#include "xenia/base/threading.h"
#include "xenia/debug/transport.h"

namespace xe {
namespace debug {
namespace transport {
namespace gdb {

class GdbTransport : public Transport {
 public:
  GdbTransport(Debugger* debugger);
  ~GdbTransport() override;

  bool Initialize() override;

 private:
  void AcceptClient(std::unique_ptr<Socket> client);
  bool HandleClientEvent();

  std::unique_ptr<SocketServer> socket_server_;

  std::unique_ptr<Socket> client_;
  std::unique_ptr<xe::threading::Thread> client_thread_;
  std::vector<uint8_t> receive_buffer_;
};

}  // namespace gdb
}  // namespace transport
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_TRANSPORT_GDB_GDB_TRANSPORT_H_
