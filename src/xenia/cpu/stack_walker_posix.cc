/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/stack_walker.h"

#include <condition_variable>
#include <mutex>
#include <unordered_set>
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <signal.h>

#include "stack_walker.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {

const int CAPTURE_SIGNAL = SIGUSR1;

struct PosixStackCapture {
  unw_context_t context_;
  std::vector<unw_cursor_t> cursors_;

  PosixStackCapture(unw_context_t&& context, size_t frame_offset,
                    size_t frame_count)
      : context_(context) {
    unw_cursor_t cursor;
    unw_init_local(&cursor, &context);

    for (size_t i = 0; i < frame_offset; ++i) {
      if (unw_step(&cursor) < 0) {
        return;
      }
    }
    for (uint32_t i = 0; i < frame_count; ++i) {
      int step_result = unw_step(&cursor);
      if (step_result == 0) {
        break;
      } else if (step_result < 0) {
        switch (-step_result) {
          case UNW_EUNSPEC:
            return;
          case UNW_ENOINFO:
            return;
          case UNW_EBADVERSION:
            return;
          case UNW_EINVALIDIP:
            return;
          case UNW_EBADFRAME:
            return;
          case UNW_ESTOPUNWIND:
            return;
          default:
            return;
        }
      }
      cursors_.push_back(cursor);
    }
  }
};
}  // namespace cpu
}  // namespace xe

namespace std {
// TODO(bwrsandman): This needs to be expanded on to avoid collisions of stacks
//  with same ip and depth
template <>
struct hash<xe::cpu::PosixStackCapture> {
  typedef xe::cpu::PosixStackCapture argument_t;
  typedef std::size_t result_t;
  result_t operator()(argument_t const& capture) const noexcept {
    if (capture.cursors_.empty()) {
      return 0;
    }
    result_t result = 0;
    unw_word_t ip;
    unw_cursor_t front_cursor = capture.cursors_.front();
    unw_get_reg(&front_cursor, UNW_REG_IP, &ip);
    auto h1 = std::hash<unw_word_t>{}(ip);
    auto h2 = std::hash<size_t>{}(capture.cursors_.size());
    return h1 ^ (h2 << 1);
  }
};

template <>
struct equal_to<xe::cpu::PosixStackCapture> {
  typedef xe::cpu::PosixStackCapture argument_t;
  typedef std::size_t result_t;
  result_t operator()(argument_t const& capture_1,
                      argument_t const& capture_2) const noexcept {
    if (capture_1.cursors_.size() != capture_2.cursors_.size()) {
      return false;
    }
    unw_word_t reg_1, reg_2;
    unw_cursor_t front_cursor_1 = capture_1.cursors_.front();
    unw_cursor_t front_cursor_2 = capture_2.cursors_.front();
    for (uint32_t i = 0; i < UNW_REG_LAST; ++i) {
      unw_get_reg(&front_cursor_1, i, &reg_1);
      unw_get_reg(&front_cursor_2, i, &reg_2);
      if (reg_1 != reg_2) {
        return false;
      }
    }
    return true;
  }
};
}  // namespace std

namespace xe {
namespace cpu {

#include <libiberty/demangle.h>
#include <memory>
std::string demangle(const char* mangled_name) {
  if (mangled_name == std::string()) {
    return "";
  }
  std::unique_ptr<char, decltype(&std::free)> ptr(
      cplus_demangle(mangled_name, DMGL_NO_OPTS), &std::free);
  return ptr ? ptr.get() : mangled_name;
}

static struct signal_handler_arg_t {
  std::mutex mutex;
  std::condition_variable cv;
  volatile bool set;
  unw_context_t context_;
} signal_handler_arg = {};

class PosixStackWalker : public StackWalker {
 public:
  explicit PosixStackWalker(backend::CodeCache* code_cache) {
    // Get the boundaries of the code cache so we can quickly tell if a symbol
    // is ours or not.
    // We store these globally so that the Sym* callbacks can access them.
    // They never change, so it's fine even if they are touched from multiple
    // threads.
    // code_cache_ = code_cache;
    // code_cache_min_ = code_cache_->base_address();
    // code_cache_max_ = code_cache_->base_address() +
    //   code_cache_->total_size();
  }

  bool Initialize() { return true; }

  size_t CaptureStackTrace(uint64_t* frame_host_pcs, size_t frame_offset,
                           size_t frame_count,
                           uint64_t* out_stack_hash) override {
    if (out_stack_hash) {
      *out_stack_hash = 0;
    }

    unw_context_t context;
    if (unw_getcontext(&context) != 0) {
      return false;
    }

    auto capture =
        PosixStackCapture(std::move(context), frame_offset, frame_count);

    for (uint32_t i = 0; i < capture.cursors_.size(); ++i) {
      frame_host_pcs[i] = reinterpret_cast<uintptr_t>(&capture.cursors_[i]);
    }
    // Two identical stack traces will generate identical hash values.
    if (out_stack_hash) {
      *out_stack_hash = captures_.hash_function()(capture);
    }

    auto size = capture.cursors_.size();

    captures_.insert(std::move(capture));

    return size;
  }

  size_t CaptureStackTrace(void* thread_handle, uint64_t* frame_host_pcs,
                           size_t frame_offset, size_t frame_count,
                           const X64Context* in_host_context,
                           X64Context* out_host_context,
                           uint64_t* out_stack_hash) override {
    if (out_stack_hash) {
      *out_stack_hash = 0;
    }

    // Install signal capture
    struct sigaction action {};
    struct sigaction previous_action {};
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = [](int signal, siginfo_t* info, void* context) {
      std::unique_lock<std::mutex> lock(signal_handler_arg.mutex);
      unw_getcontext(&signal_handler_arg.context_);
      signal_handler_arg.set = true;
      signal_handler_arg.cv.notify_one();
    };
    sigemptyset(&action.sa_mask);
    sigaction(CAPTURE_SIGNAL, &action, &previous_action);

    // Send signal
    pthread_kill(reinterpret_cast<pthread_t>(thread_handle), CAPTURE_SIGNAL);

    // Wait to have data back
    unw_context_t uc;
    {
      std::unique_lock<std::mutex> lock(signal_handler_arg.mutex);
      signal_handler_arg.cv.wait(lock);
      uc = signal_handler_arg.context_;
      signal_handler_arg.set = false;
    }

    // Restore original handler if it existed
    sigaction(CAPTURE_SIGNAL, &previous_action, nullptr);

    // Skip signal callback frame
    ++frame_offset;

    auto capture = PosixStackCapture(std::move(signal_handler_arg.context_),
                                     frame_offset, frame_count);

    for (uint32_t i = 0; i < capture.cursors_.size(); ++i) {
      frame_host_pcs[i] = reinterpret_cast<uintptr_t>(&capture.cursors_[i]);
    }
    // Two identical stack traces will generate identical hash values.
    if (out_stack_hash) {
      *out_stack_hash = captures_.hash_function()(capture);
    }

    auto size = capture.cursors_.size();

    captures_.insert(std::move(capture));

    return size;
  }

  bool ResolveStack(uint64_t* frame_host_pcs, StackFrame* frames,
                    size_t frame_count) override {
    for (size_t i = 0; i < frame_count; ++i) {
      auto& frame = frames[i];
      unw_cursor_t& cursor =
          *reinterpret_cast<unw_cursor_t*>(frame_host_pcs[i]);
      std::memset(&frame, 0, sizeof(frame));
      frame.host_pc = frame_host_pcs[i];

      // If in the generated range, we know it's ours.
      // if (frame.host_pc >= code_cache_min_ && frame.host_pc <
      //     code_cache_max_) {
      //
      // } else {
      //  // Host symbol, which means either emulator or system.
      //  frame.type = StackFrame::Type::kHost;
      // }
      std::array<char, sizeof(frame.host_symbol.name)> host_symbol_name = {};
      auto ok = unw_get_proc_name(&cursor, host_symbol_name.data(),
                                  host_symbol_name.size(),
                                  &frame.host_symbol.address);
      switch (ok) {
        case UNW_ESUCCESS:
          break;
        case UNW_EUNSPEC:
          return false;
        case UNW_ENOINFO:
          return false;
        case UNW_ENOMEM:
          return false;
        default:
          return false;
      }
      auto demangled_host_symbol_name = demangle(host_symbol_name.data());
      std::strncpy(frame.host_symbol.name, demangled_host_symbol_name.c_str(),
                   sizeof(frame.host_symbol.name));
      // unw_proc_info_t info = {};
      // ok = unw_get_proc_info(&cursor, &info);
    }
    return true;
  }

  // static xe::cpu::backend::CodeCache* code_cache_;
  // static uint32_t code_cache_min_;
  // static uint32_t code_cache_max_;
  std::unordered_set<PosixStackCapture> captures_;
};

std::unique_ptr<StackWalker> StackWalker::Create(
    backend::CodeCache* code_cache) {
  auto stack_walker = std::make_unique<PosixStackWalker>(code_cache);
  if (!stack_walker->Initialize()) {
    XELOGE("Unable to initialize stack walker: debug/save states disabled");
    return nullptr;
  }
  return std::unique_ptr<StackWalker>(stack_walker.release());
}

}  // namespace cpu
}  // namespace xe
