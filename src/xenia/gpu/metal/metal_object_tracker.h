/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_OBJECT_TRACKER_H_
#define XENIA_GPU_METAL_OBJECT_TRACKER_H_

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace metal {

// Singleton Metal object tracker for debugging autorelease pool issues
class MetalObjectTracker {
 public:
  static MetalObjectTracker& Instance() {
    static MetalObjectTracker instance;
    return instance;
  }

  struct ObjectInfo {
    void* ptr;
    std::string type;
    std::string location;
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point released;
    bool is_released;
    std::thread::id thread_id;
    std::string thread_name;
  };

  void TrackCreation(void* obj, const std::string& type, const std::string& location = "") {
    if (!obj) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = objects_[obj];
    info.ptr = obj;
    info.type = type;
    info.location = location;
    info.created = std::chrono::steady_clock::now();
    info.is_released = false;
    info.thread_id = std::this_thread::get_id();
    
    // Get thread name
    char thread_name[256] = {0};
    pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
    info.thread_name = thread_name;
    
    alive_count_++;
    total_created_++;
    
    XELOGI("MetalObjectTracker: Created {} at {} on thread {} [{}] (alive: {}, total: {})",
           type, obj, info.thread_name, 
           std::hash<std::thread::id>{}(info.thread_id),
           alive_count_.load(), total_created_.load());
  }

  void TrackRelease(void* obj) {
    if (!obj) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = objects_.find(obj);
    if (it == objects_.end()) {
      XELOGW("MetalObjectTracker: Releasing untracked object at {}", obj);
      return;
    }
    
    auto& info = it->second;
    if (info.is_released) {
      XELOGE("MetalObjectTracker: Double release detected for {} at {}", 
             info.type, obj);
      return;
    }
    
    info.released = std::chrono::steady_clock::now();
    info.is_released = true;
    
    alive_count_--;
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        info.released - info.created).count();
    
    // Get current thread info
    char thread_name[256] = {0};
    pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));
    
    XELOGI("MetalObjectTracker: Released {} at {} on thread {} (lifetime: {}ms, alive: {})",
           info.type, obj, thread_name, duration, alive_count_.load());
    
    // Warn if releasing on different thread
    if (info.thread_id != std::this_thread::get_id()) {
      XELOGW("MetalObjectTracker: {} created on thread {} but released on thread {}",
             info.type, info.thread_name, thread_name);
    }
  }

  void PrintReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    XELOGI("====== Metal Object Tracker Report ======");
    XELOGI("Total created: {}", total_created_.load());
    XELOGI("Currently alive: {}", alive_count_.load());
    
    if (alive_count_ > 0) {
      XELOGI("=== Leaked Objects ===");
      for (const auto& [ptr, info] : objects_) {
        if (!info.is_released) {
          auto now = std::chrono::steady_clock::now();
          auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - info.created).count();
          XELOGW("  LEAK: {} at {} created on thread {} (age: {}ms) at {}",
                 info.type, ptr, info.thread_name, age, info.location);
        }
      }
    }
    
    XELOGI("==========================================");
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    objects_.clear();
    alive_count_ = 0;
    total_created_ = 0;
  }

  size_t GetAliveCount() const { return alive_count_.load(); }

 private:
  MetalObjectTracker() = default;
  ~MetalObjectTracker() {
    if (alive_count_ > 0) {
      PrintReport();
    }
  }

  std::mutex mutex_;
  std::map<void*, ObjectInfo> objects_;
  std::atomic<size_t> alive_count_{0};
  std::atomic<size_t> total_created_{0};
};

// Macro helpers for tracking
#define TRACK_METAL_OBJECT(obj, type) \
  MetalObjectTracker::Instance().TrackCreation(obj, type, __FUNCTION__)

#define TRACK_METAL_RELEASE(obj) \
  MetalObjectTracker::Instance().TrackRelease(obj)

// RAII wrapper for automatic tracking
template<typename T>
class TrackedMetalObject {
 public:
  TrackedMetalObject(T* obj, const std::string& type, const std::string& location = "")
      : obj_(obj), type_(type) {
    if (obj_) {
      MetalObjectTracker::Instance().TrackCreation(obj_, type_, location);
    }
  }
  
  ~TrackedMetalObject() {
    Release();
  }
  
  TrackedMetalObject(TrackedMetalObject&& other)
      : obj_(other.obj_), type_(std::move(other.type_)) {
    other.obj_ = nullptr;
  }
  
  TrackedMetalObject& operator=(TrackedMetalObject&& other) {
    if (this != &other) {
      Release();
      obj_ = other.obj_;
      type_ = std::move(other.type_);
      other.obj_ = nullptr;
    }
    return *this;
  }
  
  void Release() {
    if (obj_) {
      MetalObjectTracker::Instance().TrackRelease(obj_);
      if (obj_->release) {
        obj_->release();
      }
      obj_ = nullptr;
    }
  }
  
  T* Get() const { return obj_; }
  T* operator->() const { return obj_; }
  operator T*() const { return obj_; }
  
 private:
  T* obj_ = nullptr;
  std::string type_;
  
  TrackedMetalObject(const TrackedMetalObject&) = delete;
  TrackedMetalObject& operator=(const TrackedMetalObject&) = delete;
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_OBJECT_TRACKER_H_