/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_WINDOWED_APP_CONTEXT_H_
#define XENIA_UI_WINDOWED_APP_CONTEXT_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

// Context for inputs provided by the entry point and interacting with the
// platform's UI loop, to be implemented by platforms.
class WindowedAppContext {
 public:
  WindowedAppContext(const WindowedAppContext& context) = delete;
  WindowedAppContext& operator=(const WindowedAppContext& context) = delete;
  virtual ~WindowedAppContext();

  // The thread where the object is created will be assumed to be the UI thread,
  // for the purpose of being able to perform CallInUIThreadSynchronous before
  // running the loop.
  bool IsInUIThread() const {
    return std::this_thread::get_id() == ui_thread_id_;
  }

  // CallInUIThreadDeferred and CallInUIThread are fire and forget - will be
  // executed at some point the future when the UI thread is running the loop
  // and is not busy doing other things.
  // Therefore, references to objects in the function may outlive the owners of
  // those objects, so use-after-free is very easy to create if not being
  // careful enough.
  // There are two solutions to this issue:
  // - Signaling a fence in the function, awaiting it before destroying objects
  //   referenced by the function (works for shutdown from non-UI threads, or
  //   for CallInUIThread, but not CallInUIThreadDeferred, in the UI thread).
  // - Calling ExecutePendingFunctionsFromUIThread in the UI thread before
  //   destroying objects referenced by the function (works for shutdown from
  //   the UI thread, though CallInUIThreadSynchronous doing
  //   ExecutePendingFunctionsFromUIThread is also an option for shutdown from
  //   any thread).
  // (These are not required if all the called function is doing is triggering a
  // quit with the context pointer captured by value, as the only object
  // involved will be the context itself, with a pointer that is valid until
  // it's destroyed - the most late location of the pending function execution
  // possible.)
  // Returning true if the function has been enqueued (it will be called at some
  // point, at worst, before exiting the loop) or called immediately, false if
  // it was dropped (if calling after exiting the loop).
  // It's okay to enqueue functions from queued functions already being
  // executed. As execution may be happening during the destruction of the
  // context as well, in this case, CallInUIThreadDeferred must make sure it
  // doesn't call anything virtual in the destructor.

  // Enqueues the function regardless of the current thread. Won't necessarily
  // be executed directly from the platform main loop as
  // ExecutePendingFunctionsFromUIThread may be called anywhere (for instance,
  // to await pending functions before destroying what they are referencing).
  bool CallInUIThreadDeferred(std::function<void()> function);
  // Executes the function immediately if already in the UI thread, enqueues it
  // otherwise.
  bool CallInUIThread(std::function<void()> function);
  bool CallInUIThreadSynchronous(std::function<void()> function);
  // It's okay to call this function from the queued functions themselves (such
  // as in the case of waiting for a pending async CallInUIThreadDeferred
  // described above - the wait may be done inside a CallInUIThreadSynchronous
  // function safely).
  void ExecutePendingFunctionsFromUIThread() {
    ExecutePendingFunctionsFromUIThread(false);
  }

  // If on the target platform, the program itself is supposed to run the UI
  // loop, this may be checked before doing blocking message waits as an
  // additional safety measure beyond what PlatformQuitFromUIThread guarantees,
  // and if true, the loop should be terminated (pending function will already
  // have been executed). This doesn't imply that pending functions have been
  // executed in all contexts, however - they can be executing from quitting
  // itself (in the worst case, in the destructor where virtual methods can't be
  // called), and in this case, this will be returning true.
  bool HasQuitFromUIThread() const {
    assert_true(IsInUIThread());
    return has_quit_;
  }

  // Immediately disallows adding new pending UI functions, executes the already
  // queued ones, and makes sure that the UI loop is aware that it was asked to
  // stop running. This must not destroy the context or the app directly - the
  // actual app shutdown will be initiated at some point outside the scope of
  // app's callbacks. May call virtual functions - invoke
  // ExecutePendingFunctionsFromUIThread(true) in the destructor instead, and
  // use the platform-specific destructor to invoke the needed
  // PlatformQuitFromUIThread logic (it should be safe, in case of destruction,
  // to perform platform-specific quit request logic before the common part -
  // NotifyUILoopOfPendingFunctions won't be called after the platform-specific
  // quit request logic in this case anyway as destruction expects that there
  // are no references to the object in other threads). Safe to call from within
  // pending functions - requesting quit from non-UI threads is possible via
  // methods like CallInUIThreadSynchronous. For deferred, rather than
  // immediate, quitting from the UI thread, CallInUIThreadDeferred may be used
  // (but the context pointer should be captured by value not to require
  // explicit completion forcing in case the storage of the pointer is lost
  // before the function is called).
  void QuitFromUIThread();
  // Callable from any thread. This is a special case where a completely
  // fire-and-forget CallInUIThreadDeferred is safe, as the function only
  // references nonvirtual functions of the context itself, and will be called
  // at most from the destructor. No need to return the result - it doesn't
  // matter if has quit already or not, as that's the intention anyway.
  void RequestDeferredQuit() {
    CallInUIThreadDeferred([this] { QuitFromUIThread(); });
  }

 protected:
  WindowedAppContext() : ui_thread_id_(std::this_thread::get_id()) {}

  // Can be called from any thread (including the UI thread) to ask the OS to
  // run an iteration of the UI loop (with or without processing internal UI
  // messages, this is platform-dependent) so pending functions will be executed
  // at some point. pending_functions_mutex_ will not be locked when this is
  // called (to make sure that, for example, the caller, waiting for space in
  // the OS's message queue, such as in case of a Linux pipe, won't be blocking
  // the UI thread that has started executing pending messages while pumping
  // that pipe, resulting in a deadlock) - implementations don't directly see
  // anything protected by it anyway, and a spurious notification shouldn't be
  // causing any damage, this is similar to how condition variables can be
  // signaled outside the critical section (signaling inside the critical
  // section may also cause contention if the thread waiting is woken up quickly
  // enough). This, however, means that NotifyUILoopOfPendingFunctions may be
  // called in a non-UI thread after the final pending message processing
  // followed by PlatformQuitFromUIThread in the UI thread - so it can still be
  // called after a quit.
  virtual void NotifyUILoopOfPendingFunctions() = 0;

  // Called when requesting a quit in the UI thread to tell the platform that
  // the UI loop needs to be terminated. The pending function queue is assumed
  // to be empty before this is called, and no new pending functions can be
  // added (but NotifyUILoopOfPendingFunctions may still be called as it's not
  // mutually exclusive with PlatformQuitFromUIThread - if this matters, the
  // platform implementation itself should be resolving this case).
  virtual void PlatformQuitFromUIThread() = 0;

  std::thread::id ui_thread_id_;

 private:
  // May be called with is_final == true from the destructor - must not call
  // anything virtual in this case.
  void ExecutePendingFunctionsFromUIThread(bool is_final);

  // Accessible by the UI thread.
  bool has_quit_ = false;
  bool is_in_destructor_ = false;

  // Synchronizes producers with each other and with the consumer, as well as
  // all of them with the pending_functions_accepted_ variable which indicates
  // whether shutdown has not been performed yet, as after the shutdown, no new
  // functions will be executed anymore, therefore no new function should be
  // queued, as it will never be called (thus CallInUIThreadSynchronous for it
  // will never return, for instance).
  std::mutex pending_functions_mutex_;
  std::deque<std::function<void()>> pending_functions_;
  // Protected by pending_functions_mutex_, writable by the UI thread, readable
  // by any thread. Must be set to false before exiting the main platform loop,
  // but before that, all pending functions must be executed no matter what, as
  // they may need to signal fences currently being awaited (like the
  // CallInUIThreadSynchronous fence).
  bool pending_functions_accepted_ = true;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_WINDOWED_APP_CONTEXT_H_
