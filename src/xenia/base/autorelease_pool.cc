/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/autorelease_pool.h"

#if XE_PLATFORM_MAC

#include <chrono>
#include <objc/objc-runtime.h>
#include <pthread.h>

#include "xenia/base/logging.h"
#include "xenia/base/assert.h"

// Declare Objective-C runtime functions
extern "C" {
  void* objc_autoreleasePoolPush(void);
  void objc_autoreleasePoolPop(void*);
}

namespace xe {

// Static member definitions
thread_local int AutoreleasePoolTracker::pool_depth_ = 0;
thread_local std::vector<AutoreleasePoolTracker::PoolInfo> AutoreleasePoolTracker::pool_stack_;
thread_local bool AutoreleasePoolTracker::initialized_ = false;
bool AutoreleasePoolTracker::enabled_ = true;
std::mutex AutoreleasePoolTracker::tracker_mutex_;

void AutoreleasePoolTracker::EnsureInitialized() {
  if (!initialized_) {
    pool_stack_.reserve(32);  // Reserve space to avoid allocations
    initialized_ = true;
  }
}

void* AutoreleasePoolTracker::Push(const char* location) {
  if (!enabled_) {
    return objc_autoreleasePoolPush();
  }
  
  EnsureInitialized();
  
  void* pool = objc_autoreleasePoolPush();
  
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  
  PoolInfo info;
  info.pool = pool;
  info.location = location ? location : "unknown";
  info.push_time = timestamp;
  
  pool_stack_.push_back(info);
  pool_depth_++;
  
  // Get thread name for logging
  char thread_name[256] = {0};
  pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
  
  XELOGI("[AutoreleasePool] PUSH: depth={}, location='{}', ptr={:x}, thread='{}'", 
         pool_depth_, location, reinterpret_cast<uintptr_t>(pool), thread_name);
  
  // Warn if pool depth is getting excessive
  if (pool_depth_ > 10) {
    XELOGW("[AutoreleasePool] WARNING: Pool depth {} is unusually high at {}", 
           pool_depth_, location);
  }
  
  return pool;
}

void AutoreleasePoolTracker::Pop(void* pool, const char* location) {
  if (!enabled_) {
    objc_autoreleasePoolPop(pool);
    return;
  }
  
  if (!pool) {
    XELOGE("[AutoreleasePool] ERROR: Attempting to pop null pool at {}", 
           location ? location : "unknown");
    return;
  }
  
  EnsureInitialized();
  
  // Verify this matches the most recent push
  if (!pool_stack_.empty()) {
    auto& top = pool_stack_.back();
    if (top.pool != pool) {
      XELOGE("[AutoreleasePool] ERROR: Pool mismatch! Expected {:x} but got {:x} at {}",
             reinterpret_cast<uintptr_t>(top.pool),
             reinterpret_cast<uintptr_t>(pool),
             location ? location : "unknown");
      
      // Try to find the pool in the stack
      bool found = false;
      for (auto it = pool_stack_.rbegin(); it != pool_stack_.rend(); ++it) {
        if (it->pool == pool) {
          XELOGE("[AutoreleasePool] Pool was pushed at '{}' but not in expected order", 
                 it->location);
          found = true;
          break;
        }
      }
      
      if (!found) {
        XELOGE("[AutoreleasePool] Pool {:x} was never tracked!", 
               reinterpret_cast<uintptr_t>(pool));
      }
    } else {
      // Calculate how long the pool was active
      auto now = std::chrono::steady_clock::now().time_since_epoch();
      auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
      auto duration_us = timestamp - top.push_time;
      
      pool_stack_.pop_back();
      pool_depth_--;
      
      // Get thread name for logging
      char thread_name[256] = {0};
      pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
      
      XELOGI("[AutoreleasePool] POP: depth={}, location='{}', ptr={:x}, duration={}us, thread='{}'",
             pool_depth_, location, reinterpret_cast<uintptr_t>(pool), 
             duration_us, thread_name);
      
      // Warn if pool was held for a long time
      if (duration_us > 1000000) {  // 1 second
        XELOGW("[AutoreleasePool] WARNING: Pool at '{}' was held for {}ms",
               top.location, duration_us / 1000);
      }
    }
  } else {
    XELOGE("[AutoreleasePool] ERROR: Popping pool {:x} but stack is empty at {}",
           reinterpret_cast<uintptr_t>(pool), location ? location : "unknown");
  }
  
  // Actually pop the pool
  objc_autoreleasePoolPop(pool);
}

void AutoreleasePoolTracker::CheckLeaks() {
  if (!enabled_) {
    return;
  }
  
  EnsureInitialized();
  
  if (!pool_stack_.empty()) {
    char thread_name[256] = {0};
    pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
    
    XELOGE("[AutoreleasePool] LEAK DETECTED: {} pools not drained on thread '{}'",
           pool_stack_.size(), thread_name);
    
    for (const auto& info : pool_stack_) {
      XELOGE("[AutoreleasePool]   - Pool {:x} from '{}' (pushed {}us ago)",
             reinterpret_cast<uintptr_t>(info.pool), info.location,
             std::chrono::steady_clock::now().time_since_epoch().count() - info.push_time);
    }
    
    // Don't actually pop the pools here - that could cause crashes
    // Just report the leak
  } else {
    char thread_name[256] = {0};
    pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
    XELOGI("[AutoreleasePool] No leaks detected on thread '{}'", thread_name);
  }
}

int AutoreleasePoolTracker::GetDepth() {
  return pool_depth_;
}

void AutoreleasePoolTracker::Reset() {
  EnsureInitialized();
  
  if (!pool_stack_.empty()) {
    CheckLeaks();  // Report any leaks before clearing
  }
  
  pool_stack_.clear();
  pool_depth_ = 0;
  
  XELOGI("[AutoreleasePool] Tracker reset for thread");
}

void AutoreleasePoolTracker::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(tracker_mutex_);
  enabled_ = enabled;
  XELOGI("[AutoreleasePool] Tracking {}", enabled ? "enabled" : "disabled");
}

// ScopedAutoreleasePool implementation
ScopedAutoreleasePool::ScopedAutoreleasePool(const char* name) 
    : name_(name) {
  pool_ = AutoreleasePoolTracker::Push(name_);
}

ScopedAutoreleasePool::~ScopedAutoreleasePool() {
  AutoreleasePoolTracker::Pop(pool_, name_);
}

}  // namespace xe

#endif  // XE_PLATFORM_MAC