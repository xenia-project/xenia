/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/debug_server.h"

#include <gflags/gflags.h>

#include "xenia/base/logging.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/debug/debugger.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/objects/xmodule.h"
#include "xenia/kernel/objects/xthread.h"

DEFINE_int32(debug_server_port, 9002, "Debugger XDP server TCP port.");

namespace xe {
namespace debug {

using namespace xe::debug::proto;
using namespace xe::kernel;

constexpr size_t kReceiveBufferSize = 32 * 1024;
constexpr size_t kReadBufferSize = 1 * 1024 * 1024;
constexpr size_t kWriteBufferSize = 1 * 1024 * 1024;

DebugServer::DebugServer(Debugger* debugger)
    : debugger_(debugger),
      packet_reader_(kReadBufferSize),
      packet_writer_(kWriteBufferSize) {
  receive_buffer_.resize(kReceiveBufferSize);
}

DebugServer::~DebugServer() = default;

bool DebugServer::Initialize() {
  post_event_ = xe::threading::Event::CreateAutoResetEvent(false);

  socket_server_ = SocketServer::Create(uint16_t(FLAGS_debug_server_port),
                                        [this](std::unique_ptr<Socket> client) {
                                          AcceptClient(std::move(client));
                                        });
  if (!socket_server_) {
    XELOGE("Unable to create XDP socket server - port in use?");
    return false;
  }

  return true;
}

void DebugServer::PostSynchronous(std::function<void()> fn) {
  xe::threading::Fence fence;
  {
    std::lock_guard<std::mutex> lock(post_mutex_);
    post_queue_.push_back([&fence, fn]() {
      fn();
      fence.Signal();
    });
  }
  post_event_->Set();
  fence.Wait();
}

void DebugServer::AcceptClient(std::unique_ptr<Socket> client) {
  // If we have an existing client, kill it and join its thread.
  if (client_) {
    // TODO(benvanik): XDP say goodbye?

    client_->Close();
    xe::threading::Wait(client_thread_.get(), true);
    client_thread_.reset();
  }

  // Take ownership of the new one.
  client_ = std::move(client);

  // Create a thread to manage the connection.
  client_thread_ = xe::threading::Thread::Create({}, [this]() {
    xe::threading::set_name("XDP Debug Server");

    // Let the debugger know we are present.
    debugger_->set_attached(true);

    // Notify the client of the current state.
    if (debugger_->execution_state() == ExecutionState::kRunning) {
      OnExecutionContinued();
    } else {
      OnExecutionInterrupted();
    }

    // Main loop.
    bool running = true;
    while (running) {
      xe::threading::WaitHandle* wait_handles[] = {
          client_->wait_handle(), post_event_.get(),
      };
      auto wait_result = xe::threading::WaitMultiple(
          wait_handles, xe::countof(wait_handles), false, true);
      switch (wait_result.first) {
        case xe::threading::WaitResult::kSuccess:
          // Event (read or close).
          switch (wait_result.second) {
            case 0: {
              running = HandleClientEvent();
            } break;
            case 1: {
              bool has_remaining = true;
              while (has_remaining) {
                std::function<void()> fn;
                {
                  std::lock_guard<std::mutex> lock(post_mutex_);
                  fn = std::move(post_queue_.front());
                  post_queue_.pop_front();
                  has_remaining = !post_queue_.empty();
                  if (!has_remaining) {
                    post_event_->Reset();
                  }
                }
                fn();
              }
            } break;
          }
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

    // Kill client (likely already disconnected).
    client_.reset();

    // Notify debugger we are no longer attached.
    debugger_->set_attached(false);
  });
}

bool DebugServer::HandleClientEvent() {
  if (!client_->is_connected()) {
    // Known-disconnected.
    return false;
  }
  // Attempt to read into our buffer.
  size_t bytes_read =
      client_->Receive(receive_buffer_.data(), receive_buffer_.capacity());
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
bool DebugServer::ProcessBuffer(const uint8_t* buffer, size_t buffer_length) {
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

  Flush();
  return true;
}

bool DebugServer::ProcessPacket(const proto::Packet* packet) {
  auto emulator = debugger()->emulator();
  auto kernel_state = emulator->kernel_state();
  auto object_table = kernel_state->object_table();

  switch (packet->packet_type) {
    case PacketType::kExecutionRequest: {
      auto body = packet_reader_.Read<ExecutionRequest>();
      switch (body->action) {
        case ExecutionRequest::Action::kContinue: {
          debugger_->Continue();
        } break;
        case ExecutionRequest::Action::kStep: {
          switch (body->step_mode) {
            case ExecutionRequest::StepMode::kStepOne: {
              debugger_->StepOne(body->thread_id);
            } break;
            case ExecutionRequest::StepMode::kStepTo: {
              debugger_->StepTo(body->thread_id, body->target_pc);
            } break;
            default:
              assert_unhandled_case(body->step_mode);
              return false;
          }
        } break;
        case ExecutionRequest::Action::kInterrupt: {
          debugger_->Interrupt();
        } break;
        case ExecutionRequest::Action::kExit: {
          XELOGI("Debug client requested exit");
          exit(1);
        } break;
      }
      SendSuccess(packet->request_id);
    } break;
    case PacketType::kModuleListRequest: {
      packet_writer_.Begin(PacketType::kModuleListResponse);
      auto body = packet_writer_.Append<ModuleListResponse>();
      auto modules =
          object_table->GetObjectsByType<XModule>(XObject::Type::kTypeModule);
      body->count = uint32_t(modules.size());
      for (auto& module : modules) {
        auto entry = packet_writer_.Append<ModuleListEntry>();
        entry->module_handle = module->handle();
        entry->module_ptr = module->hmodule_ptr();
        entry->is_kernel_module =
            module->module_type() == XModule::ModuleType::kKernelModule;
        std::strncpy(entry->path, module->path().c_str(),
                     xe::countof(entry->path));
        std::strncpy(entry->name, module->name().c_str(),
                     xe::countof(entry->name));
      }
      packet_writer_.End();
    } break;
    case PacketType::kThreadListRequest: {
      packet_writer_.Begin(PacketType::kThreadListResponse);
      auto body = packet_writer_.Append<ThreadListResponse>();
      auto threads =
          object_table->GetObjectsByType<XThread>(XObject::Type::kTypeThread);
      body->count = uint32_t(threads.size());
      for (auto& thread : threads) {
        auto entry = packet_writer_.Append<ThreadListEntry>();
        entry->thread_handle = thread->handle();
        entry->thread_id = thread->thread_id();
        entry->is_host_thread = !thread->is_guest_thread();
        std::strncpy(entry->name, thread->name().c_str(),
                     xe::countof(entry->name));
        entry->exit_code;
        entry->priority = thread->priority();
        entry->xapi_thread_startup =
            thread->creation_params()->xapi_thread_startup;
        entry->start_address = thread->creation_params()->start_address;
        entry->start_context = thread->creation_params()->start_context;
        entry->creation_flags = thread->creation_params()->creation_flags;
        entry->stack_address = thread->thread_state()->stack_address();
        entry->stack_size = thread->thread_state()->stack_size();
        entry->stack_base = thread->thread_state()->stack_base();
        entry->stack_limit = thread->thread_state()->stack_limit();
        entry->pcr_address = thread->pcr_ptr();
        entry->tls_address = thread->tls_ptr();
      }
      packet_writer_.End();
    } break;
    case PacketType::kThreadCallStacksRequest: {
      packet_writer_.Begin(PacketType::kThreadCallStacksResponse);
      auto body = packet_writer_.Append<ThreadCallStacksResponse>();
      auto stack_walker = emulator->processor()->stack_walker();
      auto threads =
          emulator->kernel_state()->object_table()->GetObjectsByType<XThread>(
              XObject::kTypeThread);
      body->count = uint32_t(threads.size());
      uint64_t frame_host_pcs[64];
      cpu::StackFrame frames[64];
      for (auto& thread : threads) {
        uint64_t hash;
        size_t count = stack_walker->CaptureStackTrace(
            thread->GetWaitHandle()->native_handle(), frame_host_pcs, 0, 64,
            &hash);
        stack_walker->ResolveStack(frame_host_pcs, frames, count);
        auto thread_entry_body = packet_writer_.Append<ThreadCallStackEntry>();
        thread_entry_body->thread_handle = thread->handle();
        thread_entry_body->frame_count = uint32_t(count);
        for (size_t i = 0; i < count; ++i) {
          auto& frame = frames[i];
          auto frame_body = packet_writer_.Append<ThreadCallStackFrame>();
          frame_body->host_pc = frame.host_pc;
          frame_body->host_function_address = frame.host_symbol.address;
          frame_body->guest_pc = frame.guest_pc;
          frame_body->guest_function_address = 0;
          auto function_info = frame.guest_symbol.function_info;
          if (frame.type == cpu::StackFrame::Type::kGuest && function_info) {
            frame_body->guest_function_address = function_info->address();
            std::strncpy(frame_body->name, function_info->name().c_str(),
                         xe::countof(frame_body->name));
          } else {
            std::strncpy(frame_body->name, frame.host_symbol.name,
                         xe::countof(frame_body->name));
          }
        }
      }
      packet_writer_.End();
    } break;
    default: {
      XELOGE("Unknown packet type");
      SendError(packet->request_id, "Unknown packet type");
      return false;
    } break;
  }
  return true;
}

void DebugServer::OnExecutionContinued() {
  packet_writer_.Begin(PacketType::kExecutionNotification);
  auto body = packet_writer_.Append<ExecutionNotification>();
  body->current_state = ExecutionNotification::State::kRunning;
  packet_writer_.End();
  Flush();
}

void DebugServer::OnExecutionInterrupted() {
  packet_writer_.Begin(PacketType::kExecutionNotification);
  auto body = packet_writer_.Append<ExecutionNotification>();
  body->current_state = ExecutionNotification::State::kStopped;
  body->stop_reason = ExecutionNotification::Reason::kInterrupt;
  packet_writer_.End();
  Flush();
}

void DebugServer::Flush() {
  if (!packet_writer_.buffer_offset()) {
    return;
  }
  client_->Send(packet_writer_.buffer(), packet_writer_.buffer_offset());
  packet_writer_.Reset();
}

void DebugServer::SendSuccess(proto::request_id_t request_id) {
  packet_writer_.Begin(PacketType::kGenericResponse, request_id);
  auto body = packet_writer_.Append<GenericResponse>();
  body->code = GenericResponse::Code::kSuccess;
  packet_writer_.End();
  Flush();
}

void DebugServer::SendError(proto::request_id_t request_id,
                            const char* error_message) {
  packet_writer_.Begin(PacketType::kGenericResponse, request_id);
  auto body = packet_writer_.Append<GenericResponse>();
  body->code = GenericResponse::Code::kError;
  packet_writer_.AppendString(error_message ? error_message : "");
  packet_writer_.End();
  Flush();
}

void DebugServer::SendError(proto::request_id_t request_id,
                            const std::string& error_message) {
  packet_writer_.Begin(PacketType::kGenericResponse, request_id);
  auto body = packet_writer_.Append<GenericResponse>();
  body->code = GenericResponse::Code::kError;
  packet_writer_.AppendString(error_message);
  packet_writer_.End();
  Flush();
}

}  // namespace debug
}  // namespace xe
