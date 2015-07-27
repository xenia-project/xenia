/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/client/xdp/xdp_client.h"

#include "xenia/base/logging.h"
#include "xenia/debug/debugger.h"

namespace xe {
namespace debug {
namespace client {
namespace xdp {

using namespace xe::debug::proto;

constexpr size_t kReceiveBufferSize = 1 * 1024 * 1024;
constexpr size_t kTransmitBufferSize = 1 * 1024 * 1024;

XdpClient::XdpClient()
    : packet_reader_(kReceiveBufferSize), packet_writer_(kTransmitBufferSize) {
  receive_buffer_.resize(kReceiveBufferSize);
}

XdpClient::~XdpClient() {
  socket_->Close();
  xe::threading::Wait(thread_.get(), true);
  thread_.reset();
}

bool XdpClient::Connect(std::string hostname, uint16_t port) {
  socket_ = Socket::Connect(std::move(hostname), port);
  if (!socket_) {
    XELOGE("Unable to connect to remote host");
    return false;
  }

  // Create a thread to manage the connection.
  thread_ = xe::threading::Thread::Create({}, [this]() {
    // Main loop.
    bool running = true;
    while (running) {
      auto wait_result = xe::threading::Wait(socket_->wait_handle(), true);
      switch (wait_result) {
        case xe::threading::WaitResult::kSuccess:
          // Event (read or close).
          running = HandleSocketEvent();
          continue;
        case xe::threading::WaitResult::kAbandoned:
        case xe::threading::WaitResult::kFailed:
          // Error - kill the thread.
          running = false;
          continue;
        default:
          // Eh. Continue.
          continue;
      }
    }

    // Kill socket (likely already disconnected).
    socket_.reset();
  });

  return true;
}

bool XdpClient::HandleSocketEvent() {
  if (!socket_->is_connected()) {
    // Known-disconnected.
    return false;
  }
  // Attempt to read into our buffer.
  size_t bytes_read =
      socket_->Receive(receive_buffer_.data(), receive_buffer_.capacity());
  if (bytes_read == -1) {
    // Disconnected.
    return false;
  } else if (bytes_read == 0) {
    // No data available. Wait again.
    return true;
  }

  // Pass off the command processor to do with it what it wants.
  if (!ProcessBuffer(receive_buffer_.data(), bytes_read)) {
    // Error.
    XELOGE("Error processing incoming XDP command; forcing disconnect");
    return false;
  }

  return true;
}

bool XdpClient::ProcessBuffer(const uint8_t* buffer, size_t buffer_length) {
  // Grow and append the bytes to the receive buffer.
  packet_reader_.AppendBuffer(buffer, buffer_length);

  // Process all packets in the buffer.
  while (true) {
    auto packet = packet_reader_.Begin();
    if (!packet) {
      // Out of packets. Compact any unused space.
      packet_reader_.Compact();
      break;
    }

    // Emit packet.
    if (!ProcessPacket(packet)) {
      // Failed to process packet.
      XELOGE("Failed to process incoming packet");
      return false;
    }

    packet_reader_.End();
  }

  return true;
}

bool XdpClient::ProcessPacket(const proto::Packet* packet) {
  // Hold lock during processing.
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  switch (packet->packet_type) {
    case PacketType::kGenericResponse: {
      //
    } break;
    case PacketType::kExecutionNotification: {
      auto body = packet_reader_.Read<ExecutionNotification>();
      switch (body->current_state) {
        case ExecutionNotification::State::kStopped: {
          // body->stop_reason
          execution_state_ = ExecutionState::kStopped;
          listener_->OnExecutionStateChanged(execution_state_);
          BeginUpdateAllState();
        } break;
        case ExecutionNotification::State::kRunning: {
          execution_state_ = ExecutionState::kRunning;
          listener_->OnExecutionStateChanged(execution_state_);
        } break;
        case ExecutionNotification::State::kExited: {
          execution_state_ = ExecutionState::kExited;
          listener_->OnExecutionStateChanged(execution_state_);
          BeginUpdateAllState();
        } break;
      }
    } break;
    case PacketType::kModuleListResponse: {
      auto body = packet_reader_.Read<ModuleListResponse>();
      auto entries = packet_reader_.ReadArray<ModuleListEntry>(body->count);
      listener_->OnModulesUpdated(entries);
    } break;
    case PacketType::kThreadListResponse: {
      auto body = packet_reader_.Read<ThreadListResponse>();
      auto entries = packet_reader_.ReadArray<ThreadListEntry>(body->count);
      listener_->OnThreadsUpdated(entries);
    } break;
    default: {
      XELOGE("Unknown incoming packet type");
      return false;
    } break;
  }
  return true;
}

void XdpClient::Flush() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!packet_writer_.buffer_offset()) {
    return;
  }
  socket_->Send(packet_writer_.buffer(), packet_writer_.buffer_offset());
  packet_writer_.Reset();
}

void XdpClient::Continue() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kContinue;
  packet_writer_.End();
  Flush();
}

void XdpClient::StepOne(uint32_t thread_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kStep;
  body->step_mode = ExecutionRequest::StepMode::kStepOne;
  body->thread_id = thread_id;
  packet_writer_.End();
  Flush();
}

void XdpClient::StepTo(uint32_t thread_id, uint32_t target_pc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kStep;
  body->step_mode = ExecutionRequest::StepMode::kStepTo;
  body->thread_id = thread_id;
  body->target_pc = target_pc;
  packet_writer_.End();
  Flush();
}

void XdpClient::Interrupt() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kInterrupt;
  packet_writer_.End();
  Flush();
}

void XdpClient::Exit() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kExit;
  packet_writer_.End();
  Flush();
}

void XdpClient::BeginUpdateAllState() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  packet_writer_.Begin(PacketType::kModuleListRequest);
  packet_writer_.End();
  packet_writer_.Begin(PacketType::kThreadListRequest);
  packet_writer_.End();

  Flush();
}

}  // namespace xdp
}  // namespace client
}  // namespace debug
}  // namespace xe
