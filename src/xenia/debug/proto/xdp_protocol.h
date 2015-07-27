/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTO_XDP_PROTOCOL_H_
#define XENIA_DEBUG_PROTO_XDP_PROTOCOL_H_

#include <cstdint>

namespace xe {
namespace debug {
namespace proto {

enum class PacketType : uint16_t {
  kUnknown = 0,
  kGenericResponse = 1,

  kExecutionRequest = 10,
  kExecutionNotification = 11,

  kModuleListRequest = 20,
  kModuleListResponse = 21,

  kThreadListRequest = 30,
  kThreadListResponse = 31,
};

using request_id_t = uint16_t;

struct Packet {
  PacketType packet_type;
  request_id_t request_id;
  uint32_t body_length;
  uint8_t* body() { return reinterpret_cast<uint8_t*>(this) + sizeof(this); }
};
static_assert(sizeof(Packet) == 8, "Fixed packet size");

// Generic response to requests (S->C).
// Used for any request type that doesn't define its own response.
struct GenericResponse {
  static const PacketType type = PacketType::kGenericResponse;

  enum class Code : uint32_t {
    // Command was received and successfully acted upon.
    // If the request is handled asynchronously this only indicates that the
    // command was recognized.
    kSuccess = 0,
    // Command was invalid or could not be executed.
    // A string will follow this response containing an error message.
    kError = 1,
  };

  Code code;

  bool is_success() const { return code == Code::kSuccess; }
};

// Requests execution state changes (C->S).
// Response is a GenericResponse, and as execution changes one or more
// ExecutionNotification packets will be sent S->C.
struct ExecutionRequest {
  static const PacketType type = PacketType::kExecutionRequest;

  enum class Action : uint32_t {
    // Continues execution until the next breakpoint.
    kContinue,
    // Steps execution by the given step_mode.
    kStep,
    // Interrupts execution and breaks.
    kInterrupt,
    // Stops execution and causes the server to exit.
    kExit,
  };

  enum class StepMode : uint32_t {
    // Steps a single statement, following branches.
    kStepOne,
    // Runs until the given target_pc.
    kStepTo,
  };

  Action action;
  StepMode step_mode;
  uint32_t thread_id;
  uint32_t target_pc;
};

// Notifies clients of changes in execution state (S->C).
// This is fired asynchronously from any request and possibly due to events
// external to the client requested ones.
// After stopping clients should query execution state.
struct ExecutionNotification {
  static const PacketType type = PacketType::kExecutionNotification;

  enum class State : uint32_t {
    // Execution has stopped for stop_reason.
    kStopped,
    // Execution has resumed and the target is now running.
    kRunning,
    // The target has exited.
    kExited,
  };

  enum class Reason : uint32_t {
    // Stopped because the client requested it with Action::kInterrupt.
    kInterrupt,
    // Stopped due to a completed step.
    kStep,
    // Stopped at a breakpoint.
    kBreakpoint,
    // TODO(benvanik): others? catches? library loads? etc?
  };

  State current_state;
  Reason stop_reason;
};

struct ModuleListRequest {
  static const PacketType type = PacketType::kModuleListRequest;
};
struct ModuleListResponse {
  static const PacketType type = PacketType::kModuleListResponse;

  uint32_t count;
  // ModuleListEntry[count]
};
struct ModuleListEntry {
  uint32_t module_handle;
  uint32_t module_ptr;
  bool is_kernel_module;
  char path[256];
  char name[256];
};

struct ThreadListRequest {
  static const PacketType type = PacketType::kThreadListRequest;
};
struct ThreadListResponse {
  static const PacketType type = PacketType::kThreadListResponse;

  uint32_t count;
  // ThreadListEntry[count]
};
struct ThreadListEntry {
  uint32_t thread_handle;
  uint32_t thread_id;
  bool is_host_thread;
  char name[64];
  uint32_t exit_code;
  int32_t priority;
  uint32_t affinity;
  uint32_t xapi_thread_startup;
  uint32_t start_address;
  uint32_t start_context;
  uint32_t creation_flags;
  uint32_t stack_address;
  uint32_t stack_size;
  uint32_t stack_base;
  uint32_t stack_limit;
  uint32_t pcr_address;
  uint32_t tls_address;
};

}  // namespace proto
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_PROTO_XDP_PROTOCOL_H_
