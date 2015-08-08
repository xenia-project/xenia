/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/debug_client.h"

#include "xenia/base/logging.h"
#include "xenia/ui/loop.h"

namespace xe {
namespace debug {

using namespace xe::debug::proto;  // NOLINT(build/namespaces)

constexpr size_t kReceiveBufferSize = 1 * 1024 * 1024;
constexpr size_t kTransmitBufferSize = 1 * 1024 * 1024;

DebugClient::DebugClient()
    : packet_reader_(kReceiveBufferSize), packet_writer_(kTransmitBufferSize) {
  receive_buffer_.resize(kReceiveBufferSize);
}

DebugClient::~DebugClient() {
  socket_->Close();
  xe::threading::Wait(thread_.get(), true);
  thread_.reset();
}

bool DebugClient::Connect(std::string hostname, uint16_t port) {
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

bool DebugClient::HandleSocketEvent() {
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

bool DebugClient::ProcessBuffer(const uint8_t* buffer, size_t buffer_length) {
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

    // Emit packet. Possibly dispatch to a loop, if desired.
    if (loop_) {
      auto clone = packet_reader_.ClonePacket();
      auto clone_ptr = clone.release();
      loop_->Post([this, clone_ptr]() {
        std::unique_ptr<PacketReader> clone(clone_ptr);
        auto packet = clone->Begin();
        if (!ProcessPacket(clone.get(), packet)) {
          // Failed to process packet, but there's no good way to report that.
          XELOGE("Failed to process incoming packet");
          assert_always();
        }
        clone->End();
      });
    } else {
      // Process inline.
      if (!ProcessPacket(&packet_reader_, packet)) {
        // Failed to process packet.
        XELOGE("Failed to process incoming packet");
        return false;
      }
    }

    packet_reader_.End();
  }

  return true;
}

bool DebugClient::ProcessPacket(proto::PacketReader* packet_reader,
                                const proto::Packet* packet) {
  // Hold lock during processing.
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  switch (packet->packet_type) {
    case PacketType::kGenericResponse: {
      //
    } break;
    case PacketType::kExecutionNotification: {
      auto body = packet_reader->Read<ExecutionNotification>();
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
      auto body = packet_reader->Read<ModuleListResponse>();
      auto entries = packet_reader->ReadArray<ModuleListEntry>(body->count);
      listener_->OnModulesUpdated(std::move(entries));
    } break;
    case PacketType::kThreadListResponse: {
      auto body = packet_reader->Read<ThreadListResponse>();
      auto entries = packet_reader->ReadArray<ThreadListEntry>(body->count);
      listener_->OnThreadsUpdated(std::move(entries));
    } break;
    case PacketType::kThreadCallStacksResponse: {
      auto body = packet_reader->Read<ThreadCallStacksResponse>();
      for (size_t i = 0; i < body->count; ++i) {
        auto entry = packet_reader->Read<ThreadCallStackEntry>();
        auto frames =
            packet_reader->ReadArray<ThreadCallStackFrame>(entry->frame_count);
        listener_->OnThreadCallStackUpdated(entry->thread_handle,
                                            std::move(frames));
      }
    } break;
    default: {
      XELOGE("Unknown incoming packet type");
      return false;
    } break;
  }
  return true;
}

void DebugClient::Flush() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!packet_writer_.buffer_offset()) {
    return;
  }
  socket_->Send(packet_writer_.buffer(), packet_writer_.buffer_offset());
  packet_writer_.Reset();
}

void DebugClient::Continue() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kContinue;
  packet_writer_.End();
  Flush();
}

void DebugClient::StepOne(uint32_t thread_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kStep;
  body->step_mode = ExecutionRequest::StepMode::kStepOne;
  body->thread_id = thread_id;
  packet_writer_.End();
  Flush();
}

void DebugClient::StepTo(uint32_t thread_id, uint32_t target_pc) {
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

void DebugClient::Interrupt() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kInterrupt;
  packet_writer_.End();
  Flush();
}

void DebugClient::Exit() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  packet_writer_.Begin(PacketType::kExecutionRequest);
  auto body = packet_writer_.Append<ExecutionRequest>();
  body->action = ExecutionRequest::Action::kExit;
  packet_writer_.End();
  Flush();
}

void DebugClient::BeginUpdateAllState() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  // Request all module infos.
  packet_writer_.Begin(PacketType::kModuleListRequest);
  packet_writer_.End();
  // Request all thread infos.
  packet_writer_.Begin(PacketType::kThreadListRequest);
  packet_writer_.End();
  // Request the full call stacks for all threads.
  packet_writer_.Begin(PacketType::kThreadCallStacksRequest);
  packet_writer_.End();

  Flush();
}

}  // namespace debug
}  // namespace xe
