/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include "xenia/cpu/testing/util.h"

#include <array>
#include <cstring>
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/stack_walker.h"

#if XE_PLATFORM_LINUX
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif  // XE_PLATFORM_LINUX

// How many stacks of template (call<...3,2,1,0>) to call.
#define call_depth 0

#define TEST_FRAME_STACK(l)                                        \
  do {                                                             \
    auto& frame = frames[l];                                       \
    REQUIRE(frame.type == xe::cpu::StackFrame::Type::kHost);       \
    REQUIRE(frame.guest_pc == 0);                                  \
    std::string function_name = __FUNCTION__;                      \
    REQUIRE(std::string(frame.host_symbol.name) == function_name); \
  } while (0)

#if defined(XE_PLATFORM_LINUX)
// Linux demangling has type suffix (ul) which is not included in the
// __FUNCTION__ macro.
#define PLATFORM_TO_STRING(x) std::to_string(x) + "ul"
// Linux demangling includes the class namespace
#define PLATFORM_TEMPLATE_STRUCT(s, n) \
  std::string(s) + "<" + PLATFORM_TO_STRING(n) + ">::"
#else
#define PLATFORM_TO_STRING(n) std::to_string(n)
#define PLATFORM_TEMPLATE_STRUCT(s, n) std::string()
#endif

#define TEST_RECURSIVE_FRAME_STACK(s, l, n)                                  \
  do {                                                                       \
    auto& frame = frames[l];                                                 \
    REQUIRE(frame.type == xe::cpu::StackFrame::Type::kHost);                 \
    REQUIRE(frame.guest_pc == 0);                                            \
    std::string function_name = PLATFORM_TEMPLATE_STRUCT(#s, n) +            \
                                __FUNCTION__ + "<" + PLATFORM_TO_STRING(l) + \
                                ">";                                         \
    REQUIRE(std::string(frame.host_symbol.name) == function_name);           \
  } while (0)

template <uint64_t N>
struct local_stack_caller_t {
  typedef std::array<uint64_t, N> frame_host_pc_array_t;
  typedef std::array<xe::cpu::StackFrame, N> frame_array_t;

  template <uint64_t L>
  void call(std::unique_ptr<xe::cpu::backend::x64::X64CodeCache>& cache,
            std::unique_ptr<xe::cpu::StackWalker>& stack_walker,
            frame_host_pc_array_t& frame_host_pcs, uint64_t& hash,
            frame_array_t& frames, uint64_t depth, size_t& out_frame_count) {
    call<L - 1>(cache, stack_walker, frame_host_pcs, hash, frames, depth + 1,
                out_frame_count);

    TEST_RECURSIVE_FRAME_STACK(local_stack_caller_t, L, N);
  }

  template <>
  void call<0>(std::unique_ptr<xe::cpu::backend::x64::X64CodeCache>& cache,
               std::unique_ptr<xe::cpu::StackWalker>& stack_walker,
               frame_host_pc_array_t& frame_host_pcs, uint64_t& hash,
               frame_array_t& frames, uint64_t depth, size_t& out_frame_count) {
    out_frame_count = stack_walker->CaptureStackTrace(
        frame_host_pcs.data(), 0, frame_host_pcs.size(), &hash);
    REQUIRE(out_frame_count == depth);
    bool resolved = stack_walker->ResolveStack(frame_host_pcs.data(),
                                               frames.data(), out_frame_count);
    REQUIRE(resolved);

    TEST_RECURSIVE_FRAME_STACK(local_stack_caller_t, 0, N);
  }
};

TEST_CASE("Local Stack Walker", "stack_walker") {
  // How many stacks in is catch.hpp already at in this frame.
#if defined(XE_PLATFORM_LINUX)
#if !defined(NDEBUG)
  const uint32_t current_stack_depth = 15;
#else
  const uint32_t current_stack_depth = 10;
#endif  // defined(NDEBUG)
#else
  const uint32_t current_stack_depth = 16;
#endif  // defined(XE_PLATFORM_LINUX)
  // Extra slots for frames to test, they should all be null initialized.
  const uint32_t stack_padding = 0x10;

  const uint32_t frame_array_size =
      call_depth + current_stack_depth + stack_padding;
  std::array<uint64_t, frame_array_size> frame_host_pcs = {};
  uint64_t hash = 0;
  std::array<xe::cpu::StackFrame, frame_host_pcs.size()> frames = {};
  local_stack_caller_t<frame_host_pcs.size()> local_stack_caller;

  auto code_cache = xe::cpu::backend::x64::X64CodeCache::Create();
  REQUIRE(code_cache);
  auto stack_walker = xe::cpu::StackWalker::Create(code_cache.get());
  REQUIRE(stack_walker);
  size_t frame_count = 0;
  local_stack_caller.call<call_depth>(code_cache, stack_walker, frame_host_pcs,
                                      hash, frames, current_stack_depth,
                                      frame_count);

  TEST_FRAME_STACK(call_depth + 1);

  REQUIRE(hash != 0);
  for (uint32_t i = 0; i < frame_host_pcs.size(); ++i) {
    if (i < frame_count) {
      REQUIRE(frame_host_pcs[i] != 0);
    } else {
      REQUIRE(frame_host_pcs[i] == 0);
    }
  }

  for (uint32_t i = 0; i < frame_host_pcs.size(); ++i) {
    if (i < frame_count) {
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc != 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address != 0);
      REQUIRE(std::strcmp(frames[i].host_symbol.name, "") != 0);
      REQUIRE(frames[i].guest_symbol.function != nullptr);
    } else {
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc == 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address == 0);
      REQUIRE(std::strcmp(frames[i].host_symbol.name, "") == 0);
      REQUIRE(frames[i].guest_symbol.function == nullptr);
    }
  }
}

template <uint64_t L>
NOINLINE void threaded_stack_call(std::mutex& mutex,
                                  std::condition_variable& cv, bool& ready,
                                  bool& finished) {
  threaded_stack_call<L - 1>(mutex, cv, ready, finished);
}

template <>
NOINLINE void threaded_stack_call<0>(std::mutex& mutex,
                                     std::condition_variable& cv, bool& ready,
                                     bool& finished) {
  // Notify that recursion base is reached
  {
    std::unique_lock<std::mutex> lock(mutex);
    ready = true;
    cv.notify_all();
  }
  // Stay here until testing is over
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&finished] { return finished; });
  }
}

TEST_CASE("Threaded Stack Walker", "stack_walker") {
  // How many stacks in is the thread when the function inside the thread
  // lambda is called.
#if defined(XE_PLATFORM_LINUX)
#if !defined(NDEBUG)
  const uint32_t initial_thread_stack_depth = 10;
#else
  const uint32_t initial_thread_stack_depth = 5;
#endif  // defined(NDEBUG)
#else
  const uint32_t initial_thread_stack_depth = 11;
#endif  // defined(XE_PLATFORM_LINUX)
  // Extra slots for frames to test, they should all be null initialized.
  const uint32_t stack_padding = 0x10;

  const uint32_t frame_array_size =
      call_depth + initial_thread_stack_depth + stack_padding;
  std::array<uint64_t, frame_array_size> frame_host_pcs = {};
  uint64_t hash = 0;
  std::array<xe::cpu::StackFrame, frame_host_pcs.size()> frames = {};
  xe::X64Context out_host_context = {};

  bool ready = false;
  bool finished = false;
  std::mutex mutex;
  std::condition_variable cv;
  auto thread = std::thread([&mutex, &cv, &ready, &finished] {
    threaded_stack_call<call_depth>(mutex, cv, ready, finished);
  });
  // Wait until the thread has reached recursion base
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&ready] { return ready; });
  }
  auto code_cache = xe::cpu::backend::x64::X64CodeCache::Create();
  REQUIRE(code_cache);
  auto stack_walker = xe::cpu::StackWalker::Create(code_cache.get());
  REQUIRE(stack_walker);
  auto frame_count = stack_walker->CaptureStackTrace(
      reinterpret_cast<void*>(thread.native_handle()), frame_host_pcs.data(), 0,
      frame_host_pcs.size(), nullptr, &out_host_context, &hash);
  // Can be more due to cv wait
  REQUIRE(frame_count >= call_depth + initial_thread_stack_depth);
  size_t extra_wait_frames =
      frame_count - call_depth - initial_thread_stack_depth;

  bool resolved = stack_walker->ResolveStack(frame_host_pcs.data(),
                                             frames.data(), frame_count);
  REQUIRE(resolved);

  for (uint32_t i = 0; i < frame_host_pcs.size(); ++i) {
    if (i < extra_wait_frames) {
      // cv.wait frames
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc != 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address != 0);
      REQUIRE(std::strcmp(frames[i].host_symbol.name, "") != 0);
      REQUIRE(frames[i].guest_symbol.function != nullptr);
    } else if (i < extra_wait_frames + call_depth) {
      // Calls to threaded_stack_call<...>
      size_t level = i - extra_wait_frames;
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc != 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address != 0);
      auto function_name =
          "threaded_stack_call<" + PLATFORM_TO_STRING(level) + ">";
      REQUIRE(std::string(frames[i].host_symbol.name) == function_name);
      REQUIRE(frames[i].guest_symbol.function != nullptr);
    } else if (i < frame_count) {
      // Thread invocation and lambda
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc != 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address != 0);
      REQUIRE(std::strcmp(frames[i].host_symbol.name, "") != 0);
      REQUIRE(frames[i].guest_symbol.function != nullptr);
    } else {
      // Zero-initialized padding frames
      REQUIRE(frames[i].type == xe::cpu::StackFrame::Type::kHost);
      REQUIRE(frames[i].host_pc == 0);
      REQUIRE(frames[i].guest_pc == 0);
      REQUIRE(frames[i].host_symbol.address == 0);
      REQUIRE(std::strcmp(frames[i].host_symbol.name, "") == 0);
      REQUIRE(frames[i].guest_symbol.function == nullptr);
    }
  }

  // Allow the thread to end
  {
    std::unique_lock<std::mutex> lock(mutex);
    finished = true;
    cv.notify_all();
  }
  thread.join();
}
