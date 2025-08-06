/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_THREAD_MONITOR_H_
#define XENIA_BASE_THREAD_MONITOR_H_

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xenia/base/logging.h"

namespace xe {
namespace threading {

// Thread lifecycle monitor to track thread creation, execution, and cleanup
class ThreadMonitor {
 public:
  enum class EventType {
    kCreated,           // Thread object created
    kStarted,           // Thread started executing
    kNameSet,           // Thread name set
    kPoolCreated,       // Autorelease pool created
    kPoolDrained,       // Autorelease pool drained
    kExiting,           // Thread is about to exit
    kExited,            // Thread has exited
    kTerminating,       // Thread is being terminated
    kTerminated,        // Thread has been terminated
    kJoinStarted,       // Thread join/wait started
    kJoinCompleted,     // Thread join/wait completed
    kTLSCleanup,        // Thread-local storage cleanup
    kPthreadExit,       // pthread_exit called
    kReturnFromFunc,    // Thread function returned normally
    kError              // Error occurred
  };

  struct ThreadEvent {
    EventType type;
    std::thread::id thread_id;
    uint32_t system_id;
    std::string thread_name;
    std::string details;
    std::chrono::steady_clock::time_point timestamp;
    void* stack_trace[16];  // For debugging crashes
    int stack_depth;
  };

  struct ThreadInfo {
    std::thread::id thread_id;
    uint32_t system_id;
    std::string name;
    bool has_autorelease_pool;
    bool is_gpu_thread;
    bool is_host_thread;
    std::vector<ThreadEvent> events;
    std::chrono::steady_clock::time_point created_time;
    std::chrono::steady_clock::time_point exit_time;
    bool is_alive;
  };

  static ThreadMonitor& Get() {
    static ThreadMonitor instance;
    return instance;
  }

  // Track thread lifecycle events
  void RecordEvent(EventType type, const std::string& details = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto thread_id = std::this_thread::get_id();
    uint32_t system_id = GetCurrentThreadSystemId();
    
    ThreadEvent event;
    event.type = type;
    event.thread_id = thread_id;
    event.system_id = system_id;
    event.details = details;
    event.timestamp = std::chrono::steady_clock::now();
    event.stack_depth = CaptureStackTrace(event.stack_trace, 16);
    
    // Get or create thread info
    auto& info = threads_[thread_id];
    if (info.thread_id == std::thread::id()) {
      info.thread_id = thread_id;
      info.system_id = system_id;
      info.created_time = event.timestamp;
      info.is_alive = true;
    }
    
    // Update thread name if available
    if (!event.thread_name.empty()) {
      info.name = event.thread_name;
    }
    
    // Track autorelease pool state
    if (type == EventType::kPoolCreated) {
      info.has_autorelease_pool = true;
    } else if (type == EventType::kPoolDrained) {
      info.has_autorelease_pool = false;
    }
    
    // Track thread exit
    if (type == EventType::kExited || type == EventType::kTerminated) {
      info.is_alive = false;
      info.exit_time = event.timestamp;
    }
    
    info.events.push_back(event);
    
    // Log important events immediately
    LogEvent(event, info);
  }

  void SetThreadName(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto thread_id = std::this_thread::get_id();
    auto& info = threads_[thread_id];
    info.name = name;
    info.is_gpu_thread = (name.find("GPU") != std::string::npos);
    info.is_host_thread = (name.find("Host") != std::string::npos);
  }

  void PrintReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    XELOGI("====== Thread Lifecycle Monitor Report ======");
    XELOGI("Total threads tracked: {}", threads_.size());
    
    int alive_count = 0;
    int dead_count = 0;
    int with_pools = 0;
    
    for (const auto& [tid, info] : threads_) {
      if (info.is_alive) {
        alive_count++;
      } else {
        dead_count++;
      }
      if (info.has_autorelease_pool) {
        with_pools++;
      }
      
      XELOGI("Thread: {} (system_id={}, alive={}, has_pool={})",
             info.name.empty() ? "<unnamed>" : info.name,
             info.system_id, info.is_alive, info.has_autorelease_pool);
      
      // Show last few events
      size_t start = info.events.size() > 5 ? info.events.size() - 5 : 0;
      for (size_t i = start; i < info.events.size(); i++) {
        const auto& event = info.events[i];
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            event.timestamp - info.created_time).count();
        XELOGI("  [+{}ms] {} - {}", duration, 
               EventTypeToString(event.type), event.details);
      }
    }
    
    XELOGI("Summary: {} alive, {} dead, {} with autorelease pools",
           alive_count, dead_count, with_pools);
    
    // Check for potential issues
    CheckForIssues();
  }

  void CheckForIssues() {
    XELOGI("=== Checking for Thread Issues ===");
    
    for (const auto& [tid, info] : threads_) {
      // Check for threads that exited with autorelease pools
      if (!info.is_alive && info.has_autorelease_pool) {
        XELOGW("Thread {} exited with autorelease pool still active!", info.name);
      }
      
      // Check for GPU threads without proper cleanup
      if (info.is_gpu_thread && !info.is_alive) {
        bool had_tls_cleanup = false;
        bool had_pthread_exit = false;
        for (const auto& event : info.events) {
          if (event.type == EventType::kTLSCleanup) had_tls_cleanup = true;
          if (event.type == EventType::kPthreadExit) had_pthread_exit = true;
        }
        
        if (had_pthread_exit && had_tls_cleanup) {
          XELOGW("GPU thread {} called pthread_exit and had TLS cleanup - potential crash site", info.name);
        }
      }
      
      // Check for abnormal termination patterns
      if (!info.events.empty()) {
        auto last_event = info.events.back();
        if (last_event.type == EventType::kError) {
          XELOGE("Thread {} had error as last event: {}", info.name, last_event.details);
        }
      }
    }
  }

 private:
  ThreadMonitor() = default;
  
  std::mutex mutex_;
  std::map<std::thread::id, ThreadInfo> threads_;
  
  static const char* EventTypeToString(EventType type) {
    switch (type) {
      case EventType::kCreated: return "Created";
      case EventType::kStarted: return "Started";
      case EventType::kNameSet: return "NameSet";
      case EventType::kPoolCreated: return "PoolCreated";
      case EventType::kPoolDrained: return "PoolDrained";
      case EventType::kExiting: return "Exiting";
      case EventType::kExited: return "Exited";
      case EventType::kTerminating: return "Terminating";
      case EventType::kTerminated: return "Terminated";
      case EventType::kJoinStarted: return "JoinStarted";
      case EventType::kJoinCompleted: return "JoinCompleted";
      case EventType::kTLSCleanup: return "TLSCleanup";
      case EventType::kPthreadExit: return "PthreadExit";
      case EventType::kReturnFromFunc: return "ReturnFromFunc";
      case EventType::kError: return "Error";
      default: return "Unknown";
    }
  }
  
  void LogEvent(const ThreadEvent& event, const ThreadInfo& info) {
    if (event.type == EventType::kError ||
        event.type == EventType::kTerminating ||
        event.type == EventType::kPthreadExit ||
        event.type == EventType::kTLSCleanup) {
      XELOGI("[ThreadMonitor] {} - Thread {} ({}): {}",
             EventTypeToString(event.type),
             info.name.empty() ? "<unnamed>" : info.name,
             event.system_id,
             event.details);
    }
  }
  
  // Platform-specific functions
  static uint32_t GetCurrentThreadSystemId();
  static int CaptureStackTrace(void** buffer, int max_frames);
};

// Convenience macros for tracking
#define THREAD_MONITOR_EVENT(type, details) \
  xe::threading::ThreadMonitor::Get().RecordEvent( \
      xe::threading::ThreadMonitor::EventType::type, details)

#define THREAD_MONITOR_SET_NAME(name) \
  xe::threading::ThreadMonitor::Get().SetThreadName(name)

#define THREAD_MONITOR_REPORT() \
  xe::threading::ThreadMonitor::Get().PrintReport()

}  // namespace threading
}  // namespace xe

#endif  // XENIA_BASE_THREAD_MONITOR_H_