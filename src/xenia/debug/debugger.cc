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

#include <algorithm>
#include <utility>

#include "third_party/capstone/include/capstone/capstone.h"
#include "third_party/capstone/include/capstone/x86.h"
#include "xenia/base/debugging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/frontend/ppc_instr.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_module.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"

#if 0 && DEBUG
#define DEFAULT_DEBUG_FLAG true
#else
#define DEFAULT_DEBUG_FLAG false
#endif

DEFINE_bool(debug, DEFAULT_DEBUG_FLAG,
            "Allow debugging and retain debug information.");
DEFINE_string(debug_session_path, "", "Debug output path.");
DEFINE_bool(break_on_start, false, "Break into the debugger on startup.");

namespace xe {
namespace debug {

using xe::cpu::ThreadState;
using xe::kernel::XObject;
using xe::kernel::XThread;

ThreadExecutionInfo::ThreadExecutionInfo() = default;

ThreadExecutionInfo::~ThreadExecutionInfo() = default;

Debugger::Debugger(Emulator* emulator) : emulator_(emulator) {
  ExceptionHandler::Install(ExceptionCallbackThunk, this);

  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle_) != CS_ERR_OK) {
    assert_always("Failed to initialize capstone");
  }
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_ON);
  cs_option(capstone_handle_, CS_OPT_SKIPDATA, CS_OPT_OFF);
}

Debugger::~Debugger() {
  ExceptionHandler::Uninstall(ExceptionCallbackThunk, this);

  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }

  set_debug_listener(nullptr);
  StopSession();
}

void Debugger::set_debug_listener(DebugListener* debug_listener) {
  if (debug_listener == debug_listener_) {
    return;
  }
  if (debug_listener_) {
    // Detach old debug listener.
    debug_listener_->OnDetached();
    debug_listener_ = nullptr;
  }
  if (debug_listener) {
    debug_listener_ = debug_listener;
  } else {
    if (execution_state_ == ExecutionState::kPaused) {
      XELOGI("Debugger detaching while execution is paused; continuing...");
      Continue();
    }
  }
}

void Debugger::DemandDebugListener() {
  if (debug_listener_) {
    // Already present.
    debug_listener_->OnFocus();
    return;
  }
  if (!debug_listener_handler_) {
    XELOGE("Debugger demanded a listener but no handler was registered.");
    xe::debugging::Break();
    return;
  }
  set_debug_listener(debug_listener_handler_(this));
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

  return true;
}

void Debugger::PreLaunch() {
  if (FLAGS_break_on_start) {
    // Start paused.
    XELOGI("Breaking into debugger because of --break_on_start...");
    execution_state_ = ExecutionState::kRunning;
    Pause();
  } else {
    // Start running.
    execution_state_ = ExecutionState::kRunning;
  }
}

void Debugger::StopSession() {
  auto global_lock = global_critical_region_.Acquire();

  FlushSession();

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

std::vector<ThreadExecutionInfo*> Debugger::QueryThreadExecutionInfos() {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<ThreadExecutionInfo*> result;
  for (auto& it : thread_execution_infos_) {
    result.push_back(it.second.get());
  }
  return result;
}

ThreadExecutionInfo* Debugger::QueryThreadExecutionInfo(
    uint32_t thread_handle) {
  auto global_lock = global_critical_region_.Acquire();
  const auto& it = thread_execution_infos_.find(thread_handle);
  if (it == thread_execution_infos_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void Debugger::AddBreakpoint(Breakpoint* breakpoint) {
  auto global_lock = global_critical_region_.Acquire();

  // Add to breakpoints map.
  breakpoints_.push_back(breakpoint);

  if (execution_state_ == ExecutionState::kRunning) {
    breakpoint->Resume();
  }
}

void Debugger::RemoveBreakpoint(Breakpoint* breakpoint) {
  auto global_lock = global_critical_region_.Acquire();

  // Uninstall (if needed).
  if (execution_state_ == ExecutionState::kRunning) {
    breakpoint->Suspend();
  }

  // Remove from breakpoint map.
  auto it = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  breakpoints_.erase(it);
}

bool Debugger::SuspendAllThreads() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto& it : thread_execution_infos_) {
    auto thread_info = it.second.get();
    if (thread_info->suspended) {
      // Already suspended - ignore.
      continue;
    } else if (thread_info->state == ThreadExecutionInfo::State::kZombie ||
               thread_info->state == ThreadExecutionInfo::State::kExited) {
      // Thread is dead and cannot be suspended - ignore.
      continue;
    } else if (!thread_info->thread->can_debugger_suspend()) {
      // Thread is a host thread, and we aren't suspending those (for now).
      continue;
    } else if (XThread::IsInThread() &&
               thread_info->thread_handle ==
                   XThread::GetCurrentThreadHandle()) {
      // Can't suspend ourselves.
      continue;
    }
    bool did_suspend = XSUCCEEDED(thread_info->thread->Suspend(nullptr));
    assert_true(did_suspend);
    thread_info->suspended = true;
  }
  return true;
}

bool Debugger::ResumeThread(uint32_t thread_handle) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_execution_infos_.find(thread_handle);
  if (it == thread_execution_infos_.end()) {
    return false;
  }
  auto thread_info = it->second.get();
  assert_true(thread_info->suspended);
  assert_false(thread_info->state == ThreadExecutionInfo::State::kExited ||
               thread_info->state == ThreadExecutionInfo::State::kZombie);
  thread_info->suspended = false;
  return XSUCCEEDED(thread_info->thread->Resume());
}

bool Debugger::ResumeAllThreads() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto& it : thread_execution_infos_) {
    auto thread_info = it.second.get();
    if (!thread_info->suspended) {
      // Not suspended by us - ignore.
      continue;
    } else if (thread_info->state == ThreadExecutionInfo::State::kZombie ||
               thread_info->state == ThreadExecutionInfo::State::kExited) {
      // Thread is dead and cannot be resumed - ignore.
      continue;
    } else if (!thread_info->thread->can_debugger_suspend()) {
      // Thread is a host thread, and we aren't suspending those (for now).
      continue;
    } else if (XThread::IsInThread() &&
               thread_info->thread_handle ==
                   XThread::GetCurrentThreadHandle()) {
      // Can't resume ourselves.
      continue;
    }
    thread_info->suspended = false;
    bool did_resume = XSUCCEEDED(thread_info->thread->Resume());
    assert_true(did_resume);
  }
  return true;
}

void Debugger::UpdateThreadExecutionStates(uint32_t override_handle,
                                           X64Context* override_context) {
  auto global_lock = global_critical_region_.Acquire();
  auto stack_walker = emulator_->processor()->stack_walker();
  uint64_t frame_host_pcs[64];
  xe::cpu::StackFrame cpu_frames[64];
  for (auto& it : thread_execution_infos_) {
    auto thread_info = it.second.get();
    auto thread = thread_info->thread;

    // Grab PPC context.
    // Note that this is only up to date if --store_all_context_values is
    // enabled (or --debug).
    if (thread->can_debugger_suspend()) {
      std::memcpy(&thread_info->guest_context,
                  thread->thread_state()->context(),
                  sizeof(thread_info->guest_context));
    }

    // Grab stack trace and X64 context then resolve all symbols.
    uint64_t hash;
    X64Context* in_host_context = nullptr;
    if (override_handle == thread_info->thread_handle) {
      // If we were passed an override context we use that. Otherwise, ask the
      // stack walker for a new context.
      in_host_context = override_context;
    }
    size_t count = stack_walker->CaptureStackTrace(
        thread->GetWaitHandle()->native_handle(), frame_host_pcs, 0,
        xe::countof(frame_host_pcs), in_host_context,
        &thread_info->host_context, &hash);
    stack_walker->ResolveStack(frame_host_pcs, cpu_frames, count);
    thread_info->frames.resize(count);
    for (size_t i = 0; i < count; ++i) {
      auto& cpu_frame = cpu_frames[i];
      auto& frame = thread_info->frames[i];
      frame.host_pc = cpu_frame.host_pc;
      frame.host_function_address = cpu_frame.host_symbol.address;
      frame.guest_pc = cpu_frame.guest_pc;
      frame.guest_function_address = 0;
      frame.guest_function = nullptr;
      auto function = cpu_frame.guest_symbol.function;
      if (cpu_frame.type == cpu::StackFrame::Type::kGuest && function) {
        frame.guest_function_address = function->address();
        frame.guest_function = function;
      } else {
        std::strncpy(frame.name, cpu_frame.host_symbol.name,
                     xe::countof(frame.name));
        frame.name[xe::countof(frame.name) - 1] = 0;
      }
    }
  }
}

void Debugger::SuspendAllBreakpoints() {
  for (auto breakpoint : breakpoints_) {
    breakpoint->Suspend();
  }
}

void Debugger::ResumeAllBreakpoints() {
  for (auto breakpoint : breakpoints_) {
    breakpoint->Resume();
  }
}

void Debugger::Show() {
  if (debug_listener_) {
    debug_listener_->OnFocus();
  } else {
    DemandDebugListener();
  }
}

void Debugger::Pause() {
  {
    auto global_lock = global_critical_region_.Acquire();
    assert_true(execution_state_ == ExecutionState::kRunning);
    SuspendAllThreads();
    SuspendAllBreakpoints();
    UpdateThreadExecutionStates();
    execution_state_ = ExecutionState::kPaused;
    if (debug_listener_) {
      debug_listener_->OnExecutionPaused();
    }
  }
  DemandDebugListener();
}

void Debugger::Continue() {
  auto global_lock = global_critical_region_.Acquire();
  if (execution_state_ == ExecutionState::kRunning) {
    return;
  } else if (execution_state_ == ExecutionState::kStepping) {
    assert_always("cancel stepping not done yet");
  }
  execution_state_ = ExecutionState::kRunning;
  ResumeAllBreakpoints();
  ResumeAllThreads();
  if (debug_listener_) {
    debug_listener_->OnExecutionContinued();
  }
}

void Debugger::StepGuestInstruction(uint32_t thread_handle) {
  auto global_lock = global_critical_region_.Acquire();
  assert_true(execution_state_ == ExecutionState::kPaused);
  execution_state_ = ExecutionState::kStepping;

  auto thread_info = thread_execution_infos_[thread_handle].get();

  uint32_t next_pc = CalculateNextGuestInstruction(
      thread_info, thread_info->frames[0].guest_pc);

  assert_null(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint = std::make_unique<StepBreakpoint>(
      this, StepBreakpoint::AddressType::kGuest, next_pc);
  AddBreakpoint(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint->Resume();

  // ResumeAllBreakpoints();
  ResumeThread(thread_handle);
}

void Debugger::StepHostInstruction(uint32_t thread_handle) {
  auto global_lock = global_critical_region_.Acquire();
  assert_true(execution_state_ == ExecutionState::kPaused);
  execution_state_ = ExecutionState::kStepping;

  auto thread_info = thread_execution_infos_[thread_handle].get();
  uint64_t new_host_pc =
      CalculateNextHostInstruction(thread_info, thread_info->frames[0].host_pc);

  assert_null(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint = std::make_unique<StepBreakpoint>(
      this, StepBreakpoint::AddressType::kHost, new_host_pc);
  AddBreakpoint(thread_info->step_breakpoint.get());
  thread_info->step_breakpoint->Resume();

  // ResumeAllBreakpoints();
  ResumeThread(thread_handle);
}

void Debugger::OnThreadCreated(XThread* thread) {
  auto global_lock = global_critical_region_.Acquire();
  auto thread_info = std::make_unique<ThreadExecutionInfo>();
  thread_info->thread_handle = thread->handle();
  thread_info->thread = thread;
  thread_info->state = ThreadExecutionInfo::State::kAlive;
  thread_info->suspended = false;
  thread_execution_infos_.emplace(thread_info->thread_handle,
                                  std::move(thread_info));
}

void Debugger::OnThreadExit(XThread* thread) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_execution_infos_.find(thread->handle());
  assert_true(it != thread_execution_infos_.end());
  auto thread_info = it->second.get();
  thread_info->state = ThreadExecutionInfo::State::kExited;
}

void Debugger::OnThreadDestroyed(XThread* thread) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_execution_infos_.find(thread->handle());
  assert_true(it != thread_execution_infos_.end());
  auto thread_info = it->second.get();
  // TODO(benvanik): retain the thread?
  thread_info->thread = nullptr;
  thread_info->state = ThreadExecutionInfo::State::kZombie;
}

void Debugger::OnThreadEnteringWait(XThread* thread) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_execution_infos_.find(thread->handle());
  assert_true(it != thread_execution_infos_.end());
  auto thread_info = it->second.get();
  thread_info->state = ThreadExecutionInfo::State::kWaiting;
}

void Debugger::OnThreadLeavingWait(XThread* thread) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = thread_execution_infos_.find(thread->handle());
  assert_true(it != thread_execution_infos_.end());
  auto thread_info = it->second.get();
  if (thread_info->state == ThreadExecutionInfo::State::kWaiting) {
    thread_info->state = ThreadExecutionInfo::State::kAlive;
  }
}

void Debugger::OnFunctionDefined(cpu::Function* function) {
  auto global_lock = global_critical_region_.Acquire();
  // TODO(benvanik): breakpoints?
  // Man, I'd love not to take this lock.
  // std::vector<Breakpoint*> breakpoints;
  //{
  //  std::lock_guard<std::recursive_mutex> lock(mutex_);
  //  for (uint32_t address = function->address();
  //       address <= function->end_address(); address += 4) {
  //    auto range = breakpoints_.equal_range(address);
  //    if (range.first == range.second) {
  //      continue;
  //    }
  //    for (auto it = range.first; it != range.second; ++it) {
  //      Breakpoint* breakpoint = it->second;
  //      breakpoints.push_back(breakpoint);
  //    }
  //  }
  //}
  // if (breakpoints.size()) {
  //  // Breakpoints to add!
  //  for (auto breakpoint : breakpoints) {
  //    function->AddBreakpoint(breakpoint);
  //  }
  //}
}

bool Debugger::ExceptionCallbackThunk(Exception* ex, void* data) {
  return reinterpret_cast<Debugger*>(data)->ExceptionCallback(ex);
}

bool Debugger::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    // We only care about illegal instructions. Other things will be handled by
    // other handlers (probably). If nothing else picks it up we'll be called
    // with OnUnhandledException to do real crash handling.
    return false;
  }

  // Verify an expected illegal instruction.
  auto instruction_bytes =
      xe::load_and_swap<uint16_t>(reinterpret_cast<void*>(ex->pc()));
  if (instruction_bytes != 0x0F0B) {
    // Not our ud2 - not us.
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Suspend all threads (but ourselves).
  SuspendAllThreads();

  // Lookup thread info block.
  auto it = thread_execution_infos_.find(XThread::GetCurrentThreadHandle());
  if (it == thread_execution_infos_.end()) {
    // Not found - exception on a thread we don't know about?
    assert_always("UD2 on a thread we don't track");
    return false;
  }
  auto thread_info = it->second.get();

  // Run through and uninstall all breakpoint UD2s to get us back to a clean
  // state.
  if (execution_state_ != ExecutionState::kStepping) {
    SuspendAllBreakpoints();
  }

  // Update all thread states with their latest values, using the context we got
  // from the exception instead of a sampled value (as it would just show the
  // exception handler).
  UpdateThreadExecutionStates(thread_info->thread_handle, ex->thread_context());

  // Whether we should block waiting for a continue event.
  bool wait_for_continue = false;

  // if (thread_info->restore_original_breakpoint &&
  //    ex->pc() == thread_info->restore_step_breakpoint.host_address) {
  //  assert_always("TODO");
  //  // We were temporarily stepping to restore a breakpoint. Reinstall it.
  //  PatchBreakpoint(thread_info->restore_original_breakpoint);
  //  thread_info->restore_original_breakpoint = nullptr;
  //  thread_info->is_stepping = false;
  //  execution_state_ = ExecutionState::kRunning;
  //  return true;
  //}

  if (thread_info->step_breakpoint.get()) {
    // Thread is stepping. This is likely a stepping breakpoint.
    if (thread_info->step_breakpoint->ContainsHostAddress(ex->pc())) {
      // Our step request has completed. Remove the breakpoint and fire event.
      thread_info->step_breakpoint->Suspend();
      RemoveBreakpoint(thread_info->step_breakpoint.get());
      thread_info->step_breakpoint.reset();
      OnStepCompleted(thread_info);
      wait_for_continue = true;
    }
  }

  // If we weren't stepping check other breakpoints.
  if (!wait_for_continue && execution_state_ != ExecutionState::kStepping) {
    // TODO(benvanik): faster lookup.
    for (auto breakpoint : breakpoints_) {
      if (!breakpoint->is_enabled() ||
          breakpoint->type() != Breakpoint::Type::kCode) {
        continue;
      }
      auto code_breakpoint = static_cast<CodeBreakpoint*>(breakpoint);
      if (code_breakpoint->address_type() ==
              CodeBreakpoint::AddressType::kHost &&
          code_breakpoint->host_address() == ex->pc()) {
        // Hit host address breakpoint, which we can be confident is where we
        // want to be.
        OnBreakpointHit(thread_info, breakpoint);
        wait_for_continue = true;
      } else if (code_breakpoint->ContainsHostAddress(ex->pc())) {
        // Hit guest address breakpoint - maybe.
        OnBreakpointHit(thread_info, breakpoint);
        wait_for_continue = true;
      }
    }
    // if (breakpoint->is_stepping()) {
    //  assert_always("TODO");
    //  // Hit a stepping breakpoint for another thread.
    //  // Skip it by uninstalling the previous step, adding a step for one
    //  // after us, and resuming just our thread. The step handler will
    //  // reinstall the restore_breakpoint after stepping and continue.
    //  thread_info->restore_original_breakpoint = breakpoint;
    //  // thread_info->restore_step_breakpoint(rip + N)
    //  PatchBreakpoint(&thread_info->restore_step_breakpoint);
    //  thread_info->is_stepping = true;
    //  execution_state_ = ExecutionState::kStepping;
    //  return true;
    //}
  }

  // We are waiting on the debugger now. Either wait for it to continue, add a
  // new step, or direct us somewhere else.
  if (wait_for_continue) {
    // The debugger will ResumeAllThreads or just resume us (depending on what
    // it wants to do).
    execution_state_ = ExecutionState::kPaused;
    thread_info->suspended = true;

    // Must unlock, or we will deadlock.
    global_lock.unlock();

    thread_info->thread->Suspend(nullptr);
  }

  // Apply thread context changes.
  // TODO(benvanik): apply to all threads?
  ex->set_resume_pc(thread_info->host_context.rip);

  // Resume execution.
  return true;
}

void Debugger::OnStepCompleted(ThreadExecutionInfo* thread_info) {
  auto global_lock = global_critical_region_.Acquire();
  execution_state_ = ExecutionState::kPaused;
  if (debug_listener_) {
    debug_listener_->OnExecutionPaused();
  }

  // Note that we stay suspended.
}

void Debugger::OnBreakpointHit(ThreadExecutionInfo* thread_info,
                               Breakpoint* breakpoint) {
  auto global_lock = global_critical_region_.Acquire();
  execution_state_ = ExecutionState::kPaused;
  if (debug_listener_) {
    debug_listener_->OnExecutionPaused();
  }

  // Note that we stay suspended.
}

bool Debugger::OnUnhandledException(Exception* ex) {
  // If we have no listener return right away.
  // TODO(benvanik): DemandDebugListener()?
  if (!debug_listener_) {
    return false;
  }

  // If this isn't a managed thread, fail - let VS handle it for now.
  if (!XThread::IsInThread()) {
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Suspend all guest threads (but this one).
  SuspendAllThreads();

  UpdateThreadExecutionStates(XThread::GetCurrentThreadHandle(),
                              ex->thread_context());

  // Stop and notify the listener.
  // This will take control.
  assert_true(execution_state_ == ExecutionState::kRunning);
  execution_state_ = ExecutionState::kPaused;

  // Notify debugger that exceution stopped.
  // debug_listener_->OnException(info);
  debug_listener_->OnExecutionPaused();

  // Suspend self.
  XThread::GetCurrentThread()->Suspend(nullptr);

  return true;
}

bool TestPpcCondition(const xe::cpu::frontend::PPCContext* context, uint32_t bo,
                      uint32_t bi, bool check_ctr, bool check_cond) {
  bool ctr_ok = true;
  if (check_ctr) {
    if (xe::cpu::frontend::select_bits(bo, 2, 2)) {
      ctr_ok = true;
    } else {
      uint32_t new_ctr_value = static_cast<uint32_t>(context->ctr - 1);
      if (xe::cpu::frontend::select_bits(bo, 1, 1)) {
        ctr_ok = new_ctr_value == 0;
      } else {
        ctr_ok = new_ctr_value != 0;
      }
    }
  }
  bool cond_ok = true;
  if (check_cond) {
    if (xe::cpu::frontend::select_bits(bo, 4, 4)) {
      cond_ok = true;
    } else {
      uint8_t cr = *(reinterpret_cast<const uint8_t*>(&context->cr0) +
                     (4 * (bi >> 2)) + (bi & 3));
      if (xe::cpu::frontend::select_bits(bo, 3, 3)) {
        cond_ok = cr != 0;
      } else {
        cond_ok = cr == 0;
      }
    }
  }
  return ctr_ok && cond_ok;
}

uint32_t Debugger::CalculateNextGuestInstruction(
    ThreadExecutionInfo* thread_info, uint32_t current_pc) {
  xe::cpu::frontend::InstrData i;
  i.address = current_pc;
  i.code = xe::load_and_swap<uint32_t>(
      emulator_->memory()->TranslateVirtual(i.address));
  i.type = xe::cpu::frontend::GetInstrType(i.code);
  if (!i.type) {
    return current_pc + 4;
  } else if (i.code == 0x4E800020) {
    // blr -- unconditional branch to LR.
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.lr);
    return target_pc;
  } else if (i.code == 0x4E800420) {
    // bctr -- unconditional branch to CTR.
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.ctr);
    return target_pc;
  } else if (i.type->opcode == 0x48000000) {
    // b/ba/bl/bla
    uint32_t target_pc =
        static_cast<uint32_t>(xe::cpu::frontend::XEEXTS26(i.I.LI << 2)) +
        (i.I.AA ? 0u : i.address);
    return target_pc;
  } else if (i.type->opcode == 0x40000000) {
    // bc/bca/bcl/bcla
    uint32_t target_pc =
        static_cast<uint32_t>(xe::cpu::frontend::XEEXTS16(i.B.BD << 2)) +
        (i.B.AA ? 0u : i.address);
    bool test_passed = TestPpcCondition(&thread_info->guest_context, i.B.BO,
                                        i.B.BI, true, true);
    return test_passed ? target_pc : current_pc + 4;
  } else if (i.type->opcode == 0x4C000020) {
    // bclr/bclrl
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.lr);
    bool test_passed = TestPpcCondition(&thread_info->guest_context, i.XL.BO,
                                        i.XL.BI, true, true);
    return test_passed ? target_pc : current_pc + 4;
  } else if (i.type->opcode == 0x4C000420) {
    // bcctr/bcctrl
    uint32_t target_pc = static_cast<uint32_t>(thread_info->guest_context.ctr);
    bool test_passed = TestPpcCondition(&thread_info->guest_context, i.XL.BO,
                                        i.XL.BI, false, true);
    return test_passed ? target_pc : current_pc + 4;
  } else {
    return current_pc + 4;
  }
}

uint64_t ReadCapstoneReg(X64Context* context, x86_reg reg) {
  switch (reg) {
    case X86_REG_RAX:
      return context->rax;
    case X86_REG_RCX:
      return context->rcx;
    case X86_REG_RDX:
      return context->rdx;
    case X86_REG_RBX:
      return context->rbx;
    case X86_REG_RSP:
      return context->rsp;
    case X86_REG_RBP:
      return context->rbp;
    case X86_REG_RSI:
      return context->rsi;
    case X86_REG_RDI:
      return context->rdi;
    case X86_REG_R8:
      return context->r8;
    case X86_REG_R9:
      return context->r9;
    case X86_REG_R10:
      return context->r10;
    case X86_REG_R11:
      return context->r11;
    case X86_REG_R12:
      return context->r12;
    case X86_REG_R13:
      return context->r13;
    case X86_REG_R14:
      return context->r14;
    case X86_REG_R15:
      return context->r15;
    default:
      assert_unhandled_case(reg);
      return 0;
  }
}

#define X86_EFLAGS_CF 0x00000001  // Carry Flag
#define X86_EFLAGS_PF 0x00000004  // Parity Flag
#define X86_EFLAGS_ZF 0x00000040  // Zero Flag
#define X86_EFLAGS_SF 0x00000080  // Sign Flag
#define X86_EFLAGS_OF 0x00000800  // Overflow Flag
bool TestCapstoneEflags(uint32_t eflags, uint32_t insn) {
  // http://www.felixcloutier.com/x86/Jcc.html
  switch (insn) {
    case X86_INS_JAE:
      // CF=0 && ZF=0
      return ((eflags & X86_EFLAGS_CF) == 0) && ((eflags & X86_EFLAGS_ZF) == 0);
    case X86_INS_JA:
      // CF=0
      return (eflags & X86_EFLAGS_CF) == 0;
    case X86_INS_JBE:
      // CF=1 || ZF=1
      return ((eflags & X86_EFLAGS_CF) == X86_EFLAGS_CF) ||
             ((eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF);
    case X86_INS_JB:
      // CF=1
      return (eflags & X86_EFLAGS_CF) == X86_EFLAGS_CF;
    case X86_INS_JE:
      // ZF=1
      return (eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF;
    case X86_INS_JGE:
      // SF=OF
      return (eflags & X86_EFLAGS_SF) == (eflags & X86_EFLAGS_OF);
    case X86_INS_JG:
      // ZF=0 && SF=OF
      return ((eflags & X86_EFLAGS_ZF) == 0) &&
             ((eflags & X86_EFLAGS_SF) == (eflags & X86_EFLAGS_OF));
    case X86_INS_JLE:
      // ZF=1 || SF!=OF
      return ((eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF) ||
             ((eflags & X86_EFLAGS_SF) != X86_EFLAGS_OF);
    case X86_INS_JL:
      // SF!=OF
      return (eflags & X86_EFLAGS_SF) != (eflags & X86_EFLAGS_OF);
    case X86_INS_JNE:
      // ZF=0
      return (eflags & X86_EFLAGS_ZF) == 0;
    case X86_INS_JNO:
      // OF=0
      return (eflags & X86_EFLAGS_OF) == 0;
    case X86_INS_JNP:
      // PF=0
      return (eflags & X86_EFLAGS_PF) == 0;
    case X86_INS_JNS:
      // SF=0
      return (eflags & X86_EFLAGS_SF) == 0;
    case X86_INS_JO:
      // OF=1
      return (eflags & X86_EFLAGS_OF) == X86_EFLAGS_OF;
    case X86_INS_JP:
      // PF=1
      return (eflags & X86_EFLAGS_PF) == X86_EFLAGS_PF;
    case X86_INS_JS:
      // SF=1
      return (eflags & X86_EFLAGS_SF) == X86_EFLAGS_SF;
    default:
      assert_unhandled_case(insn);
      return false;
  }
}

uint64_t Debugger::CalculateNextHostInstruction(
    ThreadExecutionInfo* thread_info, uint64_t current_pc) {
  auto machine_code_ptr = reinterpret_cast<const uint8_t*>(current_pc);
  size_t remaining_machine_code_size = 64;
  uint64_t host_address = current_pc;
  cs_insn insn = {0};
  cs_detail all_detail = {0};
  insn.detail = &all_detail;
  cs_disasm_iter(capstone_handle_, &machine_code_ptr,
                 &remaining_machine_code_size, &host_address, &insn);
  auto& detail = all_detail.x86;
  switch (insn.id) {
    default:
      // Not a branching instruction - just move over it.
      return current_pc + insn.size;
    case X86_INS_CALL: {
      assert_true(detail.op_count == 1);
      assert_true(detail.operands[0].type == X86_OP_REG);
      uint64_t target_pc =
          ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
      return target_pc;
    } break;
    case X86_INS_RET: {
      assert_zero(detail.op_count);
      auto stack_ptr =
          reinterpret_cast<uint64_t*>(thread_info->host_context.rsp);
      uint64_t target_pc = stack_ptr[0];
      return target_pc;
    } break;
    case X86_INS_JMP: {
      assert_true(detail.op_count == 1);
      if (detail.operands[0].type == X86_OP_IMM) {
        uint64_t target_pc = static_cast<uint64_t>(detail.operands[0].imm);
        return target_pc;
      } else if (detail.operands[0].type == X86_OP_REG) {
        uint64_t target_pc =
            ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
        return target_pc;
      } else {
        // TODO(benvanik): find some more uses of this.
        assert_always("jmp branch emulation not yet implemented");
        return current_pc + insn.size;
      }
    } break;
    case X86_INS_JCXZ:
    case X86_INS_JECXZ:
    case X86_INS_JRCXZ:
      assert_always("j*cxz branch emulation not yet implemented");
      return current_pc + insn.size;
    case X86_INS_JAE:
    case X86_INS_JA:
    case X86_INS_JBE:
    case X86_INS_JB:
    case X86_INS_JE:
    case X86_INS_JGE:
    case X86_INS_JG:
    case X86_INS_JLE:
    case X86_INS_JL:
    case X86_INS_JNE:
    case X86_INS_JNO:
    case X86_INS_JNP:
    case X86_INS_JNS:
    case X86_INS_JO:
    case X86_INS_JP:
    case X86_INS_JS: {
      assert_true(detail.op_count == 1);
      assert_true(detail.operands[0].type == X86_OP_IMM);
      uint64_t target_pc = static_cast<uint64_t>(detail.operands[0].imm);
      bool test_passed =
          TestCapstoneEflags(thread_info->host_context.eflags, insn.id);
      if (test_passed) {
        return target_pc;
      } else {
        return current_pc + insn.size;
      }
    } break;
  }
}

}  // namespace debug
}  // namespace xe
