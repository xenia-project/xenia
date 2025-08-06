# Thread Hang Debug Plan - Comprehensive Analysis

## Current Understanding

### What We Know:
1. **The hang occurs AFTER successful execution**:
   - All Metal objects are properly released (0 leaks)
   - GPU Commands thread completes WorkerThreadMain()
   - XHostThread::Execute() returns successfully
   - The hang happens during thread cleanup/join

2. **The hang is NOT a crash anymore**:
   - We fixed the autorelease pool crashes
   - The process runs at high CPU (194%) suggesting a busy wait
   - The main thread appears to be stuck in Wait()

3. **Thread lifecycle observations**:
   - GPU Commands thread: Created → Executes → Returns from host_fn → Calls Exit()
   - VSync thread: Exits cleanly without issues
   - Main thread: Waits for GPU Commands thread to terminate

## Root Cause Hypothesis

The hang is likely caused by one of these scenarios:

### Scenario A: Thread State Mismatch
- XHostThread calls Exit() which calls pthread_exit()
- PosixThread expects the thread to return normally from ThreadStartRoutine
- The thread state isn't properly synchronized between XThread and PosixThread

### Scenario B: Double Signal/Wait Issue  
- The thread signals completion in two places
- Wait() is checking the wrong condition
- Deadlock between XObject wait and pthread wait

### Scenario C: Pool Management Conflict
- GPU thread has nested autorelease pools
- ThreadStartRoutine's pool management conflicts with GPU thread's own pools
- TLS cleanup hangs trying to drain already-drained pools

## Detailed Debug Plan

### Phase 1: Pinpoint Exact Hang Location

#### 1.1 Add Detailed Wait Logging
```cpp
// In CommandProcessor::Shutdown()
XELOGI("Shutdown: About to call worker_thread_->Wait()");
XELOGI("Shutdown: Worker thread state before wait: running={}", worker_thread_->is_running());
worker_thread_->Wait(0, 0, 0, nullptr);
XELOGI("Shutdown: Wait() returned");
```

#### 1.2 Trace Thread Termination Path
```cpp
// In XThread::Exit()
XELOGI("XThread::Exit: Entry for thread {}", thread_id_);
XELOGI("XThread::Exit: About to call Thread::Exit()");
xe::threading::Thread::Exit(exit_code);
XELOGI("XThread::Exit: After Thread::Exit() - SHOULD NOT REACH");
```

#### 1.3 Monitor PosixThread State
```cpp
// In PosixThread::Exit()
XELOGI("PosixThread::Exit: Setting signaled_ = true");
signaled_ = true;
XELOGI("PosixThread::Exit: About to call pthread_exit()");
pthread_exit(reinterpret_cast<void*>(exit_code));
```

### Phase 2: Test Different Termination Methods

#### 2.1 Method A: Skip pthread_exit for Host Threads
```cpp
// In XThread::Exit()
if (!is_guest_thread()) {
  // Host threads can skip pthread_exit
  running_ = false;
  if (thread_) {
    thread_->set_signaled();  // Manually signal
  }
  return X_STATUS_SUCCESS;  // Return instead of pthread_exit
}
```

#### 2.2 Method B: Use pthread_cancel Instead
```cpp
// In CommandProcessor::Shutdown()
if (worker_thread_->thread_) {
  pthread_cancel(worker_thread_->thread_->native_handle());
}
worker_thread_->Wait(0, 0, 0, nullptr);
```

#### 2.3 Method C: Detach Thread Instead of Join
```cpp
// In PosixThread destructor
if (thread_ && !signaled_) {
  pthread_detach(thread_);  // Instead of cancel+join
}
```

### Phase 3: Analyze Wait Conditions

#### 3.1 Check What Wait() Is Actually Waiting For
```cpp
// Add to XThread::Wait or wherever it's implemented
XELOGI("Wait: Checking conditions - signaled={}, running={}, thread_alive={}",
       signaled_, running_, (thread_ != nullptr));
```

#### 3.2 Verify Signal Propagation
```cpp
// Check if XObject signaling works
XELOGI("XThread state: signal_state={}", guest_object<X_KTHREAD>()->header.signal_state);
```

### Phase 4: Pool Management Investigation

#### 4.1 Track Pool Creation/Destruction
```cpp
// Use OBJC_PRINT_POOL_STACK=YES environment variable
// Add logging at every pool push/pop
XELOGI("Pool push at {}", __FUNCTION__);
void* pool = objc_autoreleasePoolPush();
XELOGI("Pool pushed: 0x{:x}", (uintptr_t)pool);
```

#### 4.2 Check for Pool Ordering Issues
- Ensure pools are drained in LIFO order
- Verify no pools are leaked or double-drained

### Phase 5: Alternative Solutions

#### 5.1 Timeout-Based Wait
```cpp
// In CommandProcessor::Shutdown()
auto timeout = std::chrono::milliseconds(1000);
if (!worker_thread_->Wait(timeout)) {
  XELOGW("Worker thread didn't exit cleanly, forcing termination");
  // Force terminate
}
```

#### 5.2 Cooperative Shutdown
```cpp
// Add shutdown flag that GPU thread checks
class CommandProcessor {
  std::atomic<bool> shutdown_requested_{false};
  
  void WorkerThreadMain() {
    while (!shutdown_requested_ && worker_running_) {
      // Main loop
    }
    // Clean exit without Exit() call
  }
};
```

#### 5.3 Skip Thread Join Entirely
```cpp
// For non-critical threads like GPU Commands
// Let the OS clean up on process exit
worker_thread_.release();  // Don't wait or join
```

## Testing Strategy

### Test 1: Minimal Reproduction
Create a simple XHostThread that just sleeps and exits to isolate the issue:
```cpp
auto test_thread = new XHostThread(kernel_state, 128*1024, 0, []() {
  XELOGI("Test thread running");
  Sleep(100);
  XELOGI("Test thread exiting");
  return 0;
});
test_thread->Create();
test_thread->Wait(0, 0, 0, nullptr);
```

### Test 2: Remove Metal/GPU Code
Comment out all Metal operations in WorkerThreadMain to see if it's Metal-specific

### Test 3: Different Thread Configurations
- Test with different stack sizes
- Test with suspended creation
- Test with different priority levels

## Monitoring Points

Add these environment variables for debugging:
```bash
OBJC_PRINT_POOL_STACK=YES
OBJC_DEBUG_MISSING_POOLS=YES  
OBJC_DEBUG_POOL_ALLOCATION=YES
NSZombieEnabled=YES
MallocStackLogging=1
```

## Success Criteria

The fix is successful when:
1. ✅ Trace dump completes and exits cleanly
2. ✅ No hang at shutdown
3. ✅ No autorelease pool warnings
4. ✅ Exit code 0
5. ✅ Can run multiple times in succession

## Implementation Order

1. **First**: Add comprehensive logging to identify exact hang point
2. **Second**: Test simplest fix (skip pthread_exit for host threads)
3. **Third**: Implement cooperative shutdown if needed
4. **Fourth**: Use timeout-based termination as fallback
5. **Last Resort**: Detach threads instead of joining

## Key Files to Modify

1. `src/xenia/base/threading_mac.cc` - PosixThread implementation
2. `src/xenia/kernel/xthread.cc` - XThread::Exit() logic
3. `src/xenia/gpu/command_processor.cc` - Shutdown sequence
4. `src/xenia/kernel/xobject.cc` - Wait/Signal implementation (if exists)

## Next Immediate Steps

1. Add logging to identify exact hang location
2. Check if Wait() is waiting on the correct condition
3. Test skipping pthread_exit for host threads
4. Implement timeout-based wait as safety net