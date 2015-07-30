/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_SERVER_MI_MI_SERVER_H_
#define XENIA_DEBUG_SERVER_MI_MI_SERVER_H_

#include <memory>

#include "xenia/base/socket.h"
#include "xenia/base/threading.h"
#include "xenia/debug/debug_server.h"

namespace xe {
namespace debug {
namespace server {
namespace mi {

class MICommandProcessor;

class MIServer : public DebugServer {
 public:
  MIServer(Debugger* debugger);
  ~MIServer() override;

  Socket* client() const { return client_.get(); }

  bool Initialize() override;

  void PostSynchronous(std::function<void()> fn) override;

 private:
  void AcceptClient(std::unique_ptr<Socket> client);
  bool HandleClientEvent();

  std::unique_ptr<SocketServer> socket_server_;

  std::unique_ptr<Socket> client_;
  std::unique_ptr<xe::threading::Thread> client_thread_;
  std::vector<uint8_t> receive_buffer_;

  std::unique_ptr<MICommandProcessor> command_processor_;
};

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_SERVER_MI_MI_SERVER_H_
