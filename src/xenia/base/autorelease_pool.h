/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_AUTORELEASE_POOL_H_
#define XENIA_BASE_AUTORELEASE_POOL_H_

#include <string>
#include <vector>
#include <mutex>

#include "xenia/base/platform.h"

#if XE_PLATFORM_MAC

namespace xe {

// Tracks autorelease pool creation and destruction to debug Metal hang issues
class AutoreleasePoolTracker {
 public:
  // Track a pool push operation
  static void* Push(const char* location);
  
  // Track a pool pop operation
  static void Pop(void* pool, const char* location);
  
  // Check for leaked pools on the current thread
  static void CheckLeaks();
  
  // Get current pool depth for this thread
  static int GetDepth();
  
  // Clear all tracking data for current thread (call before thread exit)
  static void Reset();
  
  // Enable/disable tracking (for performance)
  static void SetEnabled(bool enabled);
  
 private:
  struct PoolInfo {
    void* pool;
    std::string location;
    uint64_t push_time;
  };
  
  static thread_local int pool_depth_;
  static thread_local std::vector<PoolInfo> pool_stack_;
  static thread_local bool initialized_;
  static bool enabled_;
  static std::mutex tracker_mutex_;
  
  static void EnsureInitialized();
};

// RAII wrapper for autorelease pools with tracking
class ScopedAutoreleasePool {
 public:
  explicit ScopedAutoreleasePool(const char* name);
  ~ScopedAutoreleasePool();
  
  // Disable copy and move
  ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
  ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;
  ScopedAutoreleasePool(ScopedAutoreleasePool&&) = delete;
  ScopedAutoreleasePool& operator=(ScopedAutoreleasePool&&) = delete;
  
 private:
  void* pool_;
  const char* name_;
};

}  // namespace xe

// Convenience macros for tracking
#define XE_AUTORELEASE_POOL_PUSH(location) \
  xe::AutoreleasePoolTracker::Push(location)

#define XE_AUTORELEASE_POOL_POP(pool, location) \
  xe::AutoreleasePoolTracker::Pop(pool, location)

#define XE_AUTORELEASE_POOL_CHECK_LEAKS() \
  xe::AutoreleasePoolTracker::CheckLeaks()

#define XE_SCOPED_AUTORELEASE_POOL(name) \
  xe::ScopedAutoreleasePool _autorelease_pool_##__LINE__(name)

#else  // !XE_PLATFORM_MAC

// No-op implementations for non-Mac platforms
#define XE_AUTORELEASE_POOL_PUSH(location) nullptr
#define XE_AUTORELEASE_POOL_POP(pool, location) (void)0
#define XE_AUTORELEASE_POOL_CHECK_LEAKS() (void)0
#define XE_SCOPED_AUTORELEASE_POOL(name) (void)0

#endif  // XE_PLATFORM_MAC

#endif  // XENIA_BASE_AUTORELEASE_POOL_H_