/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xdb/postmortem_debug_target.h>

#include <poly/poly.h>
#include <xdb/postmortem_cursor.h>
#include <xenia/logging.h>

namespace xdb {

using xdb::protocol::EventType;

PostmortemDebugTarget::PostmortemDebugTarget()
    : file_(nullptr),
      file_mapping_(nullptr),
      trace_base_(0),
      process_start_event_(nullptr),
      process_exit_event_(nullptr) {}

PostmortemDebugTarget::~PostmortemDebugTarget() {
  if (trace_base_) {
    UnmapViewOfFile(trace_base_);
  }
  CloseHandle(file_mapping_);
  CloseHandle(file_);
}

bool PostmortemDebugTarget::LoadTrace(const std::wstring& path,
                                      const std::wstring& content_path) {
  file_ = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                     OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (!file_) {
    XELOGE("Could not open trace file for writing");
    return false;
  }

  file_mapping_ =
      CreateFileMapping(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!file_mapping_) {
    XELOGE("Could not create trace file mapping");
    return false;
  }

  trace_base_ = reinterpret_cast<const uint8_t*>(
      MapViewOfFile(file_mapping_, FILE_MAP_READ, 0, 0, 0));
  if (!trace_base_) {
    XELOGE("Could not map view of trace file");
    return false;
  }

  // Find the process start event - it should be near the top and we need it for
  // path lookup.
  const uint8_t* ptr = trace_base_ + 8;
  EventType event_type;
  while ((event_type = poly::load<EventType>(ptr)) !=
         EventType::END_OF_STREAM) {
    switch (event_type) {
      case EventType::PROCESS_START: {
        process_start_event_ = protocol::ProcessStartEvent::Get(ptr);
        break;
      }
    }
    if (process_start_event_) {
      break;
    }
    ptr += protocol::kEventSizes[static_cast<uint8_t>(event_type)];
  }

  bool initialized_filesystem = false;
  if (!content_path.empty()) {
    initialized_filesystem = InitializeFileSystem(content_path);
  } else {
    // If no path was provided just use what's in the trace.
    auto trace_content_path =
        poly::to_wstring(std::string(process_start_event_->launch_path));
    initialized_filesystem = InitializeFileSystem(trace_content_path);
  }
  if (!initialized_filesystem) {
    XELOGE("Unable to initialize filesystem.");
    return false;
  }

  return true;
}

bool PostmortemDebugTarget::Prepare() {
  std::atomic<bool> cancelled(false);
  return Prepare(cancelled);
}

bool PostmortemDebugTarget::Prepare(std::atomic<bool>& cancelled) {
  // TODO(benvanik): scan file, build indicies, etc.

  const uint8_t* ptr = trace_base_ + 8;
  EventType event_type;
  while ((event_type = poly::load<EventType>(ptr)) !=
         EventType::END_OF_STREAM) {
    switch (event_type) {
      case EventType::PROCESS_START: {
        assert_true(process_start_event_ ==
                    protocol::ProcessStartEvent::Get(ptr));
        break;
      }
      case EventType::PROCESS_EXIT: {
        process_exit_event_ = protocol::ProcessExitEvent::Get(ptr);
        break;
      }
      case EventType::MODULE_LOAD: {
        auto ev = protocol::ModuleLoadEvent::Get(ptr);
        break;
      }
      case EventType::MODULE_UNLOAD: {
        auto ev = protocol::ModuleUnloadEvent::Get(ptr);
        break;
      }
      case EventType::THREAD_CREATE: {
        auto ev = protocol::ThreadCreateEvent::Get(ptr);
        break;
      }
      case EventType::THREAD_INFO: {
        auto ev = protocol::ThreadInfoEvent::Get(ptr);
        break;
      }
      case EventType::THREAD_EXIT: {
        auto ev = protocol::ThreadExitEvent::Get(ptr);
        break;
      }
      case EventType::FUNCTION_COMPILED: {
        auto ev = protocol::FunctionCompiledEvent::Get(ptr);
        break;
      }
      case EventType::OUTPUT_STRING: {
        auto ev = protocol::OutputStringEvent::Get(ptr);
        break;
      }
      case EventType::KERNEL_CALL: {
        auto ev = protocol::KernelCallEvent::Get(ptr);
        break;
      }
      case EventType::KERNEL_CALL_RETURN: {
        auto ev = protocol::KernelCallReturnEvent::Get(ptr);
        break;
      }
      case EventType::USER_CALL: {
        auto ev = protocol::UserCallEvent::Get(ptr);
        break;
      }
      case EventType::USER_CALL_RETURN: {
        auto ev = protocol::UserCallReturnEvent::Get(ptr);
        break;
      }
      case EventType::INSTR: {
        auto ev = protocol::InstrEvent::Get(ptr);
        break;
      }
      case EventType::INSTR_R8: {
        auto ev = protocol::InstrEventR8::Get(ptr);
        break;
      }
      case EventType::INSTR_R8_R8: {
        auto ev = protocol::InstrEventR8R8::Get(ptr);
        break;
      }
      case EventType::INSTR_R8_R16: {
        auto ev = protocol::InstrEventR8R16::Get(ptr);
        break;
      }
      case EventType::INSTR_R16: {
        auto ev = protocol::InstrEventR16::Get(ptr);
        break;
      }
      case EventType::INSTR_R16_R8: {
        auto ev = protocol::InstrEventR16R8::Get(ptr);
        break;
      }
      case EventType::INSTR_R16_R16: {
        auto ev = protocol::InstrEventR16R16::Get(ptr);
        break;
      }
    }
    ptr += protocol::kEventSizes[static_cast<uint8_t>(event_type)];
  };

  trace_length_ = ptr - trace_base_;

  return true;
}

std::unique_ptr<Cursor> PostmortemDebugTarget::CreateCursor() {
  auto cursor = std::make_unique<PostmortemCursor>(this);
  return std::unique_ptr<Cursor>(cursor.release());
}

}  // namespace xdb
