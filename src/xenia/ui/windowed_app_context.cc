/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/windowed_app_context.h"

#include "xenia/base/assert.h"
#include "xenia/base/threading.h"

namespace xe {
namespace ui {

WindowedAppContext::~WindowedAppContext() {
  // The UI thread is responsible for managing the lifetime of the context.
  assert_true(IsInUIThread());

  // It's okay to destroy the context from a platform's internal UI loop
  // callback, primarily on platforms where the loop is run by the OS itself,
  // and the context can't be created and destroyed in a RAII way, rather, it's
  // created in an initialization handler and destroyed in a shutdown handler
  // called by the OS. However, destruction must not be done from within the
  // queued functions - as in this case, the pending function container, the
  // mutex, will be accessed after having been destroyed already.

  // Make sure CallInUIThreadDeferred doesn't call
  // NotifyUILoopOfPendingFunctions, which is virtual.
  is_in_destructor_ = true;
  // Make sure the final ExecutePendingFunctionsFromUIThread doesn't call
  // PlatformQuitFromUIThread, which is virtual.
  has_quit_ = true;

  // Platform-specific quit is expected to be performed by the subclass (the
  // order of it vs. the final ExecutePendingFunctionsFromUIThread shouldn't
  // matter anymore, the implementation may assume that no pending functions
  // will be requested for execution specifically via the platform-specific
  // loop, as there should be no more references to the context in other
  // threads), can't call the virtual PlatformQuitFromUIThread anymore.
  ExecutePendingFunctionsFromUIThread(true);
}

bool WindowedAppContext::CallInUIThreadDeferred(
    std::function<void()> function) {
  {
    std::unique_lock<std::mutex> pending_functions_lock(
        pending_functions_mutex_);
    if (!pending_functions_accepted_) {
      // Will not be called as the loop will not be executed anymore.
      return false;
    }
    pending_functions_.emplace_back(std::move(function));
  }
  // Notify unconditionally, even if currently running pending functions. It's
  // possible for pending functions themselves to run inner platform message
  // loops, such as when displaying dialogs - in this case, the notification is
  // needed to run the new function from such an inner loop. A modal loop can be
  // started even in leftovers happening during the quit, where there's still
  // opportunity for enqueueing and executing new pending functions - so only
  // checking if called in the destructor (it's safe to check this without
  // locking a mutex as it's assumed that if the object is already being
  // destroyed, no other threads can have references to it - any access would
  // result in a race condition anyway) as the subclass has already been
  // destroyed. Having pending_functions_mutex_ unlocked also means that
  // NotifyUILoopOfPendingFunctions may be done while the UI thread is calling
  // or has already called PlatformQuitFromUIThread - but it's better than
  // keeping pending_functions_mutex_ locked as NotifyUILoopOfPendingFunctions
  // may be implemented as pushing to a fixed-size pipe, in which case it will
  // have to wait until free space is available, but if the UI thread tries to
  // lock the mutex afterwards to execute pending functions (and encouters
  // contention), nothing will be able to receive from the pipe anymore and thus
  // free the space, causing a deadlock.
  if (!is_in_destructor_) {
    NotifyUILoopOfPendingFunctions();
  }
  return true;
}

bool WindowedAppContext::CallInUIThread(std::function<void()> function) {
  if (IsInUIThread()) {
    // The intention is just to make sure the code is executed in the UI thread,
    // don't defer execution if no need to.
    function();
    return true;
  }
  return CallInUIThreadDeferred(std::move(function));
}

bool WindowedAppContext::CallInUIThreadSynchronous(
    std::function<void()> function) {
  if (IsInUIThread()) {
    // Prevent deadlock if called from the UI thread.
    function();
    return true;
  }
  xe::threading::Fence fence;
  if (!CallInUIThreadDeferred([&function, &fence]() {
        function();
        fence.Signal();
      })) {
    return false;
  }
  fence.Wait();
  return true;
}

void WindowedAppContext::QuitFromUIThread() {
  assert_true(IsInUIThread());
  bool has_quit_previously = has_quit_;
  // Make sure PlatformQuitFromUIThread is called only once, not from nested
  // pending function execution during the quit - otherwise it will be called
  // when it's still possible to add new pending functions. This isn't as wrong
  // as calling PlatformQuitFromUIThread from the destructor, but still a part
  // of the contract for simplicity.
  has_quit_ = true;
  // Executing pending function unconditionally because it's the contract of
  // this method that functions are executed immediately.
  ExecutePendingFunctionsFromUIThread(true);
  if (has_quit_previously) {
    // Potentially calling QuitFromUIThread from inside a pending function (in
    // the worst and dangerous case, from a pending function executed in the
    // destructor - and PlatformQuitFromUIThread is virtual).
    return;
  }
  // Call the platform-specific shutdown while letting it assume that no new
  // functions will be queued anymore (but NotifyUILoopOfPendingFunctions may
  // still be called after PlatformQuitFromUIThread as the two are not
  // interlocked). This is different than the order in the destruction, but
  // there this assumption is ensured by the expectation that there should be no
  // more references to the context in other threads that would allow queueing
  // new functions with calling NotifyUILoopOfPendingFunctions.
  PlatformQuitFromUIThread();
}

void WindowedAppContext::ExecutePendingFunctionsFromUIThread(bool is_final) {
  assert_true(IsInUIThread());
  std::unique_lock<std::mutex> pending_functions_lock(pending_functions_mutex_);
  while (!pending_functions_.empty()) {
    // Removing the function from the queue before executing it, as the function
    // itself may call ExecutePendingFunctionsFromUIThread - if it's kept, the
    // inner loop will try to execute it again, resulting in potentially endless
    // recursion, and even if it's terminated, each level will be trying to
    // remove the same function from the queue - instead, actually removing
    // other functions, or even beyond the end of the queue.
    std::function<void()> function = std::move(pending_functions_.front());
    pending_functions_.pop_front();
    // Call the function with the lock released as it may take an indefinitely
    // long time to execute if it opens some dialog (possibly with its own
    // platform message loop), and in that case, without unlocking, no other
    // thread would be able to add new pending functions (which would result in
    // unintended waits for user input). This also allows using std::mutex
    // instead of std::recursive_mutex.
    pending_functions_lock.unlock();
    function();
    pending_functions_lock.lock();
  }
  if (is_final) {
    // Atomically with completion of the pending functions loop, disallow adding
    // new functions after executing the existing ones - it was possible to
    // enqueue new functions from the leftover ones as there still was
    // opportunity to call them, so it wasn't necessary to disallow adding
    // before executing, but now new functions will potentially never be
    // executed. This is done even if this is just an inner pending functions
    // execution and there's still potential possibility of adding and executing
    // new functions in the outer loops - for simplicity and consistency (so
    // QuitFromUIThread's behavior doesn't depend as much on the location of the
    // call - inside a pending function or from some system callback of the
    // window), assuming after a PlatformQuitFromUIThread call, it's not
    // possible to add new pending functions anymore.
    pending_functions_accepted_ = false;
  }
}

}  // namespace ui
}  // namespace xe
