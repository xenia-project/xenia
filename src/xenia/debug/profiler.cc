/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/debug/profiler.h"

#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/debug/debugger.h"
#include "xenia/kernel/xthread.h"
#include "xenia/emulator.h"

// Credits go to Very Sleepy for the general design
// https://github.com/VerySleepy/verysleepy

namespace xe {
namespace debug {

Profiler::Profiler(Debugger* debugger) : debugger_(debugger) {}

void Profiler::Start() {
  assert_false(is_profiling_);

  total_samples_collected_.exchange(0);
  is_profiling_ = true;
  timer_.Start();

  auto params = threading::Thread::CreationParameters();
  worker_thread_ = xe::threading::Thread::Create(
      params, std::bind(&Profiler::ProfilerEntry, this));
}

void Profiler::Stop() {
  assert_true(is_profiling_);

  is_profiling_ = false;
  timer_.Stop();
  profiling_stop_fence_.Wait();
  worker_thread_.reset();

  CalculateSamplesPerInstruction();
}

void Profiler::OnThreadsChanged() {
  auto lock = global_critical_region::AcquireDirect();
  should_refresh_threads_ = true;
}

void Profiler::CalculateSamplesPerInstruction() {
  auto cache = debugger_->emulator()->processor()->backend()->code_cache();
  auto memory = debugger_->emulator()->memory();

  for (auto& sample : samples_) {
    // Translate x86 address to ppc address
    auto host_addr = sample.first.second.host_frames[0];
    auto func = cache->LookupFunction(host_addr);
    if (!func) {
      continue;
    }

    uint32_t ppc_addr = func->MapMachineCodeToGuestAddress(host_addr);
    auto code = xe::load_and_swap<uint32_t>(memory->TranslateVirtual(ppc_addr));
    auto& disasm = cpu::ppc::GetOpcodeDisasmInfo(cpu::ppc::LookupOpcode(code));
    samples_per_instruction_[disasm.name] += sample.second;
  }
}

void Profiler::CalculateSamplesPerFunction() {
  auto cache = debugger_->emulator()->processor()->backend()->code_cache();
  auto memory = debugger_->emulator()->memory();

  for (auto& sample : samples_) {
    // Translate x86 address to ppc address
    auto host_addr = sample.first.second.host_frames[0];
    auto func = cache->LookupFunction(host_addr);
    if (!func) {
      continue;
    }

    samples_per_function_[func] += sample.second;
  }
}

void Profiler::ProfilerEntry() {
  threading::Thread::GetCurrentThread()->set_name("Profiler Thread");
  std::vector<threading::Thread*> thread_list;

  // Fetch a list of threads from the debugger.
  should_refresh_threads_ = true;

  // Called when we should profile.
  while (is_profiling_) {
    if (should_refresh_threads_) {
      auto lock = global_critical_region::AcquireDirect();
      auto exec_infos = debugger_->QueryThreadExecutionInfos();
      for (auto info : exec_infos) {
        if (info->thread && !info->thread->is_host_object()) {
          thread_list.push_back(info->thread->thread());
        }
      }

      should_refresh_threads_ = false;
    }

    // Reshuffle the thread list so we don't starve threads. Probably don't need
    // to do this every run.
    std::random_shuffle(thread_list.begin(), thread_list.end());
    for (size_t i = 0; i < thread_list.size(); i++) {
      // FIXME: This is totally a race condition. We can't take the global lock
      // though, because that would screw with profiling.
      if (should_refresh_threads_) {
        break;
      }

      SampleThread(thread_list[i]);
    }
  }

  profiling_stop_fence_.Signal();
}

void Profiler::SampleThread(threading::Thread* thread) {
  auto stack_walker = debugger_->emulator()->processor()->stack_walker();
  thread->Suspend();

  CallStack stack;
  stack.depth = stack_walker->CaptureStackTrace(
      thread->native_handle(), stack.host_frames, 0,
      xe::countof(stack.host_frames), nullptr, nullptr);
  assert_true(stack.depth > 0);

  thread->Resume();

  // Discard the sample if a stack trace could not be captured.
  if (stack.depth > 0) {
    samples_[{thread->system_id(), stack}]++;
    total_samples_[thread->system_id()]++;
    total_samples_collected_.fetch_add(1);
  }
}

}  // namespace debug
}  // namespace xe
