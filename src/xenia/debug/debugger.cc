/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/debugger.h"

#include <gflags/gflags.h>

#include <mutex>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/debug/server/gdb/gdb_server.h"
#include "xenia/debug/server/mi/mi_server.h"
#include "xenia/debug/server/xdp/xdp_server.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xkernel_module.h"
#include "xenia/kernel/objects/xmodule.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/kernel/objects/xuser_module.h"

#if 0 && DEBUG
#define DEFAULT_DEBUG_FLAG true
#else
#define DEFAULT_DEBUG_FLAG false
#endif

DEFINE_bool(debug, DEFAULT_DEBUG_FLAG,
            "Allow debugging and retain debug information.");
DEFINE_string(debug_session_path, "", "Debug output path.");
DEFINE_bool(wait_for_debugger, false,
            "Waits for a debugger to attach before starting the game.");
DEFINE_bool(exit_with_debugger, true, "Exit whe the debugger disconnects.");

DEFINE_string(debug_server, "xdp", "Debug server protocol [gdb, mi, xdp].");

namespace xe {
namespace debug {

using namespace xe::kernel;

using xe::cpu::ThreadState;

Breakpoint::Breakpoint(Type type, uint32_t address)
    : type_(type), address_(address) {}

Breakpoint::~Breakpoint() = default;

Debugger::Debugger(Emulator* emulator) : emulator_(emulator) {}

Debugger::~Debugger() { StopSession(); }

void Debugger::set_attached(bool attached) {
  if (is_attached_ && !attached) {
    // Debugger detaching.
    if (FLAGS_exit_with_debugger) {
      XELOGE("Debugger detached, --exit_with_debugger is killing us");
      exit(1);
      return;
    }
  }
  is_attached_ = attached;
  if (is_attached_) {
    attach_fence_.Signal();
  }
}

bool Debugger::StartSession() {
  auto session_path = xe::to_wstring(FLAGS_debug_session_path);
  if (!session_path.empty()) {
    session_path = xe::to_absolute_path(session_path);
    xe::filesystem::CreateFolder(session_path);
  }

  functions_path_ = xe::join_paths(session_path, L"functions");
  functions_file_ =
      ChunkedMappedMemoryWriter::Open(functions_path_, 32 * 1024 * 1024, false);

  functions_trace_path_ = xe::join_paths(session_path, L"functions.trace");
  functions_trace_file_ = ChunkedMappedMemoryWriter::Open(
      functions_trace_path_, 32 * 1024 * 1024, true);

  if (FLAGS_debug_server == "gdb") {
    server_ = std::make_unique<xe::debug::server::gdb::GdbServer>(this);
    if (!server_->Initialize()) {
      XELOGE("Unable to initialize GDB debug server");
      return false;
    }
  } else if (FLAGS_debug_server == "gdb") {
    server_ = std::make_unique<xe::debug::server::mi::MIServer>(this);
    if (!server_->Initialize()) {
      XELOGE("Unable to initialize MI debug server");
      return false;
    }
  } else {
    server_ = std::make_unique<xe::debug::server::xdp::XdpServer>(this);
    if (!server_->Initialize()) {
      XELOGE("Unable to initialize XDP debug server");
      return false;
    }
  }

  return true;
}

void Debugger::PreLaunch() {
  if (FLAGS_wait_for_debugger) {
    // Wait for the first client.
    XELOGI("Waiting for debugger because of --wait_for_debugger...");
    attach_fence_.Wait();
    XELOGI("Debugger attached, breaking and waiting for continue...");

    // Start paused.
    execution_state_ = ExecutionState::kRunning;
    server_->PostSynchronous([this]() { Interrupt(); });
  } else {
    // Start running.
    execution_state_ = ExecutionState::kRunning;
  }
}

void Debugger::StopSession() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  FlushSession();

  // Kill debug server.
  server_.reset();

  functions_file_.reset();
  functions_trace_file_.reset();
}

void Debugger::FlushSession() {
  if (functions_file_) {
    functions_file_->Flush();
  }
  if (functions_trace_file_) {
    functions_trace_file_->Flush();
  }
}

uint8_t* Debugger::AllocateFunctionData(size_t size) {
  if (!functions_file_) {
    return nullptr;
  }
  return functions_file_->Allocate(size);
}

uint8_t* Debugger::AllocateFunctionTraceData(size_t size) {
  if (!functions_trace_file_) {
    return nullptr;
  }
  return functions_trace_file_->Allocate(size);
}

void Debugger::DumpThreadStacks() {
  auto stack_walker = emulator()->processor()->stack_walker();
  auto threads =
      emulator_->kernel_state()->object_table()->GetObjectsByType<XThread>(
          XObject::kTypeThread);
  for (auto& thread : threads) {
    XELOGI("Thread %s (%s)", thread->name().c_str(),
           thread->is_guest_thread() ? "guest" : "host");
    uint64_t frame_host_pcs[64];
    uint64_t hash;
    size_t count = stack_walker->CaptureStackTrace(
        thread->GetWaitHandle()->native_handle(), frame_host_pcs, 0, 64, &hash);
    cpu::StackFrame frames[64];
    stack_walker->ResolveStack(frame_host_pcs, frames, count);
    for (size_t i = 0; i < count; ++i) {
      auto& frame = frames[i];
      if (frame.type == cpu::StackFrame::Type::kHost) {
        XELOGI("  %.2lld %.16llX          %s", count - i - 1, frame.host_pc,
               frame.host_symbol.name);
      } else {
        auto function_info = frame.guest_symbol.function_info;
        XELOGI("  %.2lld %.16llX %.8X %s", count - i - 1, frame.host_pc,
               frame.guest_pc,
               function_info ? function_info->name().c_str() : "?");
      }
    }
  }
}

int Debugger::AddBreakpoint(Breakpoint* breakpoint) {
  // Add to breakpoints map.
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    breakpoints_.insert(
        std::pair<uint32_t, Breakpoint*>(breakpoint->address(), breakpoint));
  }

  // Find all functions that contain the breakpoint address.
  auto fns =
      emulator_->processor()->FindFunctionsWithAddress(breakpoint->address());

  // Add.
  for (auto fn : fns) {
    if (fn->AddBreakpoint(breakpoint)) {
      return 1;
    }
  }

  return 0;
}

int Debugger::RemoveBreakpoint(Breakpoint* breakpoint) {
  // Remove from breakpoint map.
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto range = breakpoints_.equal_range(breakpoint->address());
    if (range.first == range.second) {
      return 1;
    }
    bool found = false;
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second == breakpoint) {
        breakpoints_.erase(it);
        found = true;
        break;
      }
    }
    if (!found) {
      return 1;
    }
  }

  // Find all functions that have the breakpoint set.
  auto fns =
      emulator_->processor()->FindFunctionsWithAddress(breakpoint->address());

  // Remove.
  for (auto fn : fns) {
    fn->RemoveBreakpoint(breakpoint);
  }

  return 0;
}

void Debugger::FindBreakpoints(uint32_t address,
                               std::vector<Breakpoint*>& out_breakpoints) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  out_breakpoints.clear();

  auto range = breakpoints_.equal_range(address);
  if (range.first == range.second) {
    return;
  }

  for (auto it = range.first; it != range.second; ++it) {
    Breakpoint* breakpoint = it->second;
    out_breakpoints.push_back(breakpoint);
  }
}

bool Debugger::SuspendAllThreads() {
  auto threads =
      emulator_->kernel_state()->object_table()->GetObjectsByType<XThread>(
          XObject::kTypeThread);
  for (auto& thread : threads) {
    if (!XSUCCEEDED(thread->Suspend(nullptr))) {
      return false;
    }
  }
  return true;
}

bool Debugger::ResumeThread(uint32_t thread_id) {
  auto thread = emulator_->kernel_state()->GetThreadByID(thread_id);
  if (!thread) {
    return false;
  }
  return XSUCCEEDED(thread->Resume());
}

bool Debugger::ResumeAllThreads() {
  auto threads =
      emulator_->kernel_state()->object_table()->GetObjectsByType<XThread>(
          XObject::kTypeThread);
  for (auto& thread : threads) {
    if (!XSUCCEEDED(thread->Resume())) {
      return false;
    }
  }
  return true;
}

void Debugger::Interrupt() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  assert_true(execution_state_ == ExecutionState::kRunning);
  SuspendAllThreads();
  execution_state_ = ExecutionState::kStopped;
  server_->OnExecutionInterrupted();

  // TEST CODE.
  // TODO(benvanik): remove when UI shows threads.
  DumpThreadStacks();
}

void Debugger::Continue() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  assert_true(execution_state_ == ExecutionState::kStopped);
  ResumeAllThreads();
  execution_state_ = ExecutionState::kRunning;
  server_->OnExecutionContinued();
}

void Debugger::StepOne(uint32_t thread_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  assert_true(execution_state_ == ExecutionState::kStopped);
  //
}

void Debugger::StepTo(uint32_t thread_id, uint32_t target_pc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  assert_true(execution_state_ == ExecutionState::kStopped);
  //
}

void Debugger::OnThreadCreated(xe::kernel::XThread* thread) {
  // TODO(benvanik): notify transports.
}

void Debugger::OnThreadExit(xe::kernel::XThread* thread) {
  // TODO(benvanik): notify transports.
}

void Debugger::OnThreadDestroyed(xe::kernel::XThread* thread) {
  // TODO(benvanik): notify transports.
}

void Debugger::OnFunctionDefined(cpu::FunctionInfo* symbol_info,
                                 cpu::Function* function) {
  // Man, I'd love not to take this lock.
  std::vector<Breakpoint*> breakpoints;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (uint32_t address = symbol_info->address();
         address <= symbol_info->end_address(); address += 4) {
      auto range = breakpoints_.equal_range(address);
      if (range.first == range.second) {
        continue;
      }
      for (auto it = range.first; it != range.second; ++it) {
        Breakpoint* breakpoint = it->second;
        breakpoints.push_back(breakpoint);
      }
    }
  }

  if (breakpoints.size()) {
    // Breakpoints to add!
    for (auto breakpoint : breakpoints) {
      function->AddBreakpoint(breakpoint);
    }
  }
}

void Debugger::OnBreakpointHit(xe::kernel::XThread* thread,
                               Breakpoint* breakpoint) {
  // Suspend all threads immediately.
  SuspendAllThreads();

  // TODO(benvanik): notify transports.

  // Note that we stay suspended.
}

}  // namespace debug
}  // namespace xe
