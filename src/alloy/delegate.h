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
#include <mutex>
#include <vector>

#include <alloy/core.h>

namespace alloy {

// TODO(benvanik): go lockfree, and don't hold the lock while emitting.

template <typename T>
class Delegate {
 public:
  typedef std::function<void(T&)> Listener;

  void AddListener(Listener const& listener) {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.push_back(listener);
  }

  void RemoveListener(Listener const& listener) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
      if (it == listener) {
        listeners_.erase(it);
        break;
      }
    }
  }

  void RemoveAllListeners() {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.clear();
  }

  void operator()(T& e) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& listener : listeners_) {
      listener(e);
    }
  }

 private:
  std::mutex lock_;
  std::vector<Listener> listeners_;
};

}  // namespace alloy

#endif  // ALLOY_DELEGATE_H_
