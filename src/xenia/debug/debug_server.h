/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUG_SERVER_H_
#define XENIA_DEBUG_DEBUG_SERVER_H_

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/base/socket.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"
#include "xenia/debug/breakpoint.h"
#include "xenia/debug/proto/packet_reader.h"
#include "xenia/debug/proto/packet_writer.h"
#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {

class Debugger;

class DebugServer {
 public:
  explicit DebugServer(Debugger* debugger);
  ~DebugServer();

  Debugger* debugger() const { return debugger_; }
  Socket* client() const { return client_.get(); }

  bool Initialize();

  void PostSynchronous(std::function<void()> fn);

  // TODO(benvanik): better thread type (XThread?)
  // void OnThreadCreated(ThreadState* thread_state);
  // void OnThreadDestroyed(ThreadState* thread_state);

  void OnExecutionContinued();
  void OnExecutionInterrupted();
  /*void OnBreakpointHit(xe::cpu::ThreadState* thread_state,
                               Breakpoint* breakpoint);*/

 private:
  void AcceptClient(std::unique_ptr<Socket> client);
  bool HandleClientEvent();

  bool ProcessBuffer(const uint8_t* buffer, size_t buffer_length);
  bool ProcessPacket(const proto::Packet* packet);

  void Flush();
  void SendSuccess(proto::request_id_t request_id);
  void SendError(proto::request_id_t request_id,
                 const char* error_message = nullptr);
  void SendError(proto::request_id_t request_id,
                 const std::string& error_message);

  Debugger* debugger_ = nullptr;

  std::unique_ptr<SocketServer> socket_server_;

  std::unique_ptr<Socket> client_;
  std::unique_ptr<xe::threading::Thread> client_thread_;

  std::mutex post_mutex_;
  std::unique_ptr<xe::threading::Event> post_event_;
  std::list<std::function<void()>> post_queue_;

  std::vector<uint8_t> receive_buffer_;
  proto::PacketReader packet_reader_;
  proto::PacketWriter packet_writer_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_DEBUG_SERVER_H_
