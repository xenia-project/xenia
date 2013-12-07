/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TYPE_POOL_H_
#define ALLOY_TYPE_POOL_H_

#include <alloy/core.h>


namespace alloy {


template<class T, typename A>
class TypePool {
public:
  TypePool() {
    lock_ = AllocMutex(10000);
  }

  ~TypePool() {
    Reset();
    FreeMutex(lock_);
  }

  void Reset() {
    LockMutex(lock_);
    for (TList::iterator it = list_.begin(); it != list_.end(); ++it) {
      T* value = *it;
      delete value;
    }
    list_.clear();
    UnlockMutex(lock_);
  }

  T* Allocate(A arg0) {
    T* result = 0;
    LockMutex(lock_);
    if (list_.size()) {
      result = list_.back();
      list_.pop_back();
    }
    UnlockMutex(lock_);
    if (!result) {
      result = new T(arg0);
    }
    return result;
  }

  void Release(T* value) {
    LockMutex(lock_);
    list_.push_back(value);
    UnlockMutex(lock_);
  }

private:
  Mutex*  lock_;
  typedef std::vector<T*> TList;
  TList   list_;
};


}  // namespace alloy


#endif  // ALLOY_TYPE_POOL_H_
