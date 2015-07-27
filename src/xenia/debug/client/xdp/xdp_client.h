/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_CLIENT_XDP_XDP_CLIENT_H_
#define XENIA_DEBUG_CLIENT_XDP_XDP_CLIENT_H_

#include <memory>
#include <mutex>

#include "xenia/base/socket.h"
#include "xenia/base/threading.h"
#include "xenia/debug/proto/packet_reader.h"
#include "xenia/debug/proto/packet_writer.h"
#include "xenia/debug/proto/xdp_protocol.h"

namespace xe {
namespace debug {
namespace client {
namespace xdp {

using proto::ModuleListEntry;
using proto::ThreadListEntry;

enum class ExecutionState {
  kRunning,
  kStopped,
  kExited,
};

// NOTE: all callbacks are issued on the client thread and should be marshalled
// to the UI thread. All other methods can be called from any thread.

class ClientListener {
 public:
  virtual void OnExecutionStateChanged(ExecutionState execution_state) = 0;
  virtual void OnModulesUpdated(
      std::vector<const ModuleListEntry*> entries) = 0;
  virtual void OnThreadsUpdated(
      std::vector<const ThreadListEntry*> entries) = 0;
};

class XdpClient {
 public:
  XdpClient();
  ~XdpClient();

  Socket* socket() const { return socket_.get(); }
  void set_listener(ClientListener* listener) { listener_ = listener; }

  ExecutionState execution_state() const { return execution_state_; }

  bool Connect(std::string hostname, uint16_t port);

  void Continue();
  void StepOne(uint32_t thread_id);
  void StepTo(uint32_t thread_id, uint32_t target_pc);
  void Interrupt();
  void Exit();

 private:
  bool HandleSocketEvent();
  bool ProcessBuffer(const uint8_t* buffer, size_t buffer_length);
  bool ProcessPacket(const proto::Packet* packet);
  void Flush();

  void BeginUpdateAllState();

  std::recursive_mutex mutex_;
  std::unique_ptr<Socket> socket_;
  std::unique_ptr<xe::threading::Thread> thread_;
  std::vector<uint8_t> receive_buffer_;

  proto::PacketReader packet_reader_;
  proto::PacketWriter packet_writer_;

  ClientListener* listener_ = nullptr;

  ExecutionState execution_state_ = ExecutionState::kStopped;
};

}  // namespace xdp
}  // namespace client
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_CLIENT_XDP_XDP_CLIENT_H_
