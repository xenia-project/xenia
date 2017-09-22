/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/loop_gtk.h"

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

class PostedFn {
 public:
  explicit PostedFn(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Call() { fn_(); }

 private:
  std::function<void()> fn_;
};

std::unique_ptr<Loop> Loop::Create() { return std::make_unique<GTKLoop>(); }

GTKLoop::GTKLoop() : thread_id_() {
  gtk_init(nullptr, nullptr);
  xe::threading::Fence init_fence;
  thread_ = std::thread([&init_fence, this]() {
    xe::threading::set_name("GTK Loop");

    thread_id_ = std::this_thread::get_id();
    init_fence.Signal();

    ThreadMain();

    quit_fence_.Signal();
  });
  init_fence.Wait();
}

GTKLoop::~GTKLoop() {
  Quit();
  thread_.join();
}

void GTKLoop::ThreadMain() { gtk_main(); }

bool GTKLoop::is_on_loop_thread() {
  return thread_id_ == std::this_thread::get_id();
}

gboolean _posted_fn_thunk(gpointer posted_fn) {
  PostedFn* Fn = reinterpret_cast<PostedFn*>(posted_fn);
  Fn->Call();
  return G_SOURCE_REMOVE;
}

void GTKLoop::Post(std::function<void()> fn) {
  assert_true(thread_id_ != std::thread::id());
  gdk_threads_add_idle(_posted_fn_thunk,
                       reinterpret_cast<gpointer>(new PostedFn(std::move(fn))));
}

void GTKLoop::PostDelayed(std::function<void()> fn, uint64_t delay_millis) {
  gdk_threads_add_timeout(
      delay_millis, _posted_fn_thunk,
      reinterpret_cast<gpointer>(new PostedFn(std::move(fn))));
}

void GTKLoop::Quit() { assert_true(thread_id_ != std::thread::id()); }

void GTKLoop::AwaitQuit() { quit_fence_.Wait(); }

}  // namespace ui
}  // namespace xe
