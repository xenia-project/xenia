# Metal Backend Thread Crash/Hang Debug Plan

## Problem Statement
The Metal trace dump is crashing or hanging during trace execution, preventing TraceDump::Run() from reaching PNG capture. The crash occurs after Metal device initialization but before trace playback commands are processed.

## Key Observations

### 1. Thread Architecture Analysis
- **PosixThread (threading_mac.cc)**: 
  - ✅ Properly creates autorelease pool at thread start (line 317)
  - ✅ Properly cleans up autorelease pool at thread exit (line 380)
  - Uses objc_autoreleasePoolPush/Pop for thread-safe management

- **XHostThread (xthread.cc)**:
  - ❌ NO autorelease pool management in Execute()
  - GPU command processor thread runs through XHostThread
  - VSync thread also runs through XHostThread
  - Both critical Metal threads lack autorelease pools

### 2. Metal-cpp Documentation Requirements
From third_party/metal-cpp/README.md:
- **CRITICAL**: "When you create an autoreleased object and there is no enclosing AutoreleasePool, the object is leaked."
- **CRITICAL**: "You normally create an AutoreleasePool in your program's main function, and in the entry function for every thread you create."
- **Debug**: Use OBJC_DEBUG_MISSING_POOLS=YES to detect leaks

### 3. Metal Command Processor Issues
- ShutdownContext() is being called but presenter is null
- Trace execution stops after thread creation
- No autorelease pool for Metal API calls in GPU thread

## Root Cause Hypothesis
**The GPU command processor thread (XHostThread) is creating Metal objects without an autorelease pool, causing memory corruption or hangs when Metal-cpp tries to autorelease objects.**

## Debugging Steps

### Step 1: Enable Autorelease Pool Debugging
```bash
export OBJC_DEBUG_MISSING_POOLS=YES
export METAL_DEVICE_WRAPPER_TYPE=1
export METAL_DEBUG_ERROR_MODE=5
```

### Step 2: Add Autorelease Pool to XHostThread::Execute()
The XHostThread::Execute() method needs autorelease pool management for macOS:

```cpp
void XHostThread::Execute() {
#if XE_PLATFORM_MAC
  void* autorelease_pool = objc_autoreleasePoolPush();
#endif
  
  // ... existing code ...
  
#if XE_PLATFORM_MAC
  if (autorelease_pool) {
    objc_autoreleasePoolPop(autorelease_pool);
  }
#endif
}
```

### Step 3: Add Autorelease Pool to CommandProcessor::WorkerThreadMain()
The worker thread main loop needs its own pool that's drained periodically:

```cpp
void CommandProcessor::WorkerThreadMain() {
#if XE_PLATFORM_MAC
  void* main_pool = objc_autoreleasePoolPush();
#endif

  while (worker_running_) {
#if XE_PLATFORM_MAC
    void* loop_pool = objc_autoreleasePoolPush();
#endif
    
    // Process commands...
    
#if XE_PLATFORM_MAC
    objc_autoreleasePoolPop(loop_pool);
#endif
  }

#if XE_PLATFORM_MAC
  objc_autoreleasePoolPop(main_pool);
#endif
}
```

### Step 4: Check for Race Conditions
1. Verify presenter lifecycle vs thread shutdown timing
2. Check if graphics_system_ is being destroyed before threads exit
3. Ensure proper synchronization in ShutdownContext()

### Step 5: Compare with Xenia Canary
Check if Xenia Canary has any relevant fixes:
- Thread lifecycle improvements
- Autorelease pool management
- Metal backend updates (if any)

### Step 6: Add Comprehensive Logging
Add logging at key points:
1. Thread creation/destruction
2. Autorelease pool creation/drainage
3. Metal object creation/destruction
4. Command processor state transitions

## Implementation Priority

1. **IMMEDIATE**: Add autorelease pool to XHostThread::Execute()
2. **HIGH**: Add periodic pool drainage in WorkerThreadMain()
3. **HIGH**: Enable OBJC_DEBUG_MISSING_POOLS for testing
4. **MEDIUM**: Add synchronization to prevent premature presenter destruction
5. **LOW**: Review Xenia Canary for additional improvements

## Testing Plan

1. Run with OBJC_DEBUG_MISSING_POOLS=YES to detect leaks
2. Use `leaks --autoreleasePools` to analyze pool usage
3. Monitor with Instruments for Zombies and Leaks
4. Test with minimal trace to isolate issue
5. Verify PNG generation after fixes

## Expected Outcome
After implementing autorelease pools in XHostThread and CommandProcessor threads, the Metal trace dump should:
1. Complete trace playback without crashes
2. Reach TraceDump::Run() PNG capture code
3. Successfully generate PNG output files