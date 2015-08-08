/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_DELEGATE_H_
#define XENIA_BASE_DELEGATE_H_

#include <functional>
#include <mutex>
#include <vector>

namespace xe {

// TODO(benvanik): go lockfree, and don't hold the lock while emitting.

template <typename... Args>
class Delegate {
 public:
  typedef std::function<void(Args...)> Listener;

  void AddListener(Listener const& listener) {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.push_back(listener);
  }

  void RemoveAllListeners() {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.clear();
  }

  void operator()(Args... args) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& listener : listeners_) {
      listener(args...);
    }
  }

 private:
  std::mutex lock_;
  std::vector<Listener> listeners_;
};

template <>
class Delegate<void> {
 public:
  typedef std::function<void()> Listener;

  void AddListener(Listener const& listener) {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.push_back(listener);
  }

  void RemoveAllListeners() {
    std::lock_guard<std::mutex> guard(lock_);
    listeners_.clear();
  }

  void operator()() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& listener : listeners_) {
      listener();
    }
  }

 private:
  std::mutex lock_;
  std::vector<Listener> listeners_;
};

}  // namespace xe

#endif  // XENIA_BASE_DELEGATE_H_
