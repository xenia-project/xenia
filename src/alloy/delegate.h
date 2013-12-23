/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_DELEGATE_H_
#define ALLOY_DELEGATE_H_

#include <functional>

#include <alloy/core.h>
#include <alloy/mutex.h>


namespace alloy {


// TODO(benvanik): go lockfree, and don't hold the lock while emitting.

template <typename T>
class Delegate {
public:
  Delegate() {
    lock_ = AllocMutex();
  }
  ~Delegate() {
    FreeMutex(lock_);
  }

  typedef std::function<void(T&)> listener_t;

  void AddListener(listener_t const& listener) {
    LockMutex(lock_);
    listeners_.push_back(listener);
    UnlockMutex(lock_);
  }

  void RemoveListener(listener_t const& listener) {
    LockMutex(lock_);
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
      if (it == listener) {
        listeners_.erase(it);
        break;
      }
    }
    UnlockMutex(lock_);
  }

  void RemoveAllListeners() {
    LockMutex(lock_);
    listeners_.clear();
    UnlockMutex(lock_);
  }

  void operator()(T& e) const {
    LockMutex(lock_);
    for (auto &listener : listeners_) {
      listener(e);
    }
    UnlockMutex(lock_);
  }

private:
  alloy::Mutex* lock_;
  std::vector<listener_t> listeners_;
};


}  // namespace alloy


#endif  // ALLOY_DELEGATE_H_
