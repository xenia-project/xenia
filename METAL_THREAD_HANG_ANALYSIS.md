# Metal Backend Thread Hang Analysis

## Executive Summary
The Xenia Metal backend experiences a hang during shutdown when GPU threads exit. The hang occurs in macOS's Objective-C autorelease pool cleanup during pthread Thread Local Storage (TLS) destruction.

## Root Cause Analysis

### The Problem Chain
1. **Metal-cpp Design**: Metal-cpp wraps Metal's Objective-C API in C++. It uses `autorelease()` extensively to manage object lifetimes.

2. **Autorelease Pool Mechanics**: 
   - Objective-C autorelease pools are thread-local
   - Objects marked with `autorelease()` are added to the current thread's pool
   - Pools are drained (objects released) when the pool is destroyed

3. **pthread TLS Cleanup**:
   - When a pthread exits, it performs TLS cleanup
   - This includes draining any autorelease pools
   - This happens AFTER the thread function returns

4. **The Hang**:
   ```
   Thread Exit Sequence:
   1. GPU thread function returns
   2. Our autorelease pool (if any) is already drained
   3. pthread begins TLS cleanup
   4. TLS cleanup finds autoreleased Metal objects
   5. Attempts to release these objects
   6. HANG in objc_autoreleasePoolPop()
   ```

### Stack Trace Analysis
```
#0  0x000000019f4400ec in objc_release ()
#1  0x000000019f447c84 in AutoreleasePoolPage::releaseUntil ()
#2  0x000000019f444150 in objc_autoreleasePoolPop ()
#3  0x000000019f478d18 in objc_tls_direct_base<AutoreleasePoolPage*, ...>::dtor_ ()
#4  0x00000001013c2600 in _pthread_tsd_cleanup ()
#5  0x00000001013b9198 in _pthread_exit ()
```

The hang occurs when releasing Metal objects during TLS cleanup, suggesting corrupted or already-freed objects.

## Why Current Fixes Don't Work

### Attempt 1: Skip pthread_exit
- **What we tried**: Make threads return instead of calling pthread_exit
- **Result**: TLS cleanup still happens when thread terminates
- **Why it fails**: pthread ALWAYS does TLS cleanup, whether via pthread_exit or return

### Attempt 2: Manage Autorelease Pools
- **What we tried**: Create pools in XHostThread/CommandProcessor
- **Result**: Our pools are drained, but Metal objects escape
- **Why it fails**: Metal-cpp creates autoreleased objects outside our pool scope

### Attempt 3: Skip Pool Drain for GPU Threads
- **What we tried**: Don't drain pools for GPU threads
- **Result**: Pools leak or get cleaned up by TLS anyway
- **Why it fails**: Doesn't address the root cause of escaped autoreleased objects

## The Real Issue

Metal-cpp's design pattern:
```cpp
MTL::CommandBuffer* buffer = queue->commandBuffer();
// buffer is autoreleased internally by Metal-cpp
// It escapes our local autorelease pool scope
```

When our thread-level pool is drained, these escaped objects remain in TLS autorelease storage. During pthread TLS cleanup, the system tries to release them, causing the hang.

## Evidence

1. **MetalObjectTracker Output**: Shows objects being created/released correctly during normal operation
2. **Thread Monitor**: Shows threads reaching exit points successfully
3. **Hang Location**: Always in TLS cleanup, specifically autorelease pool drainage
4. **Pattern**: Only affects threads that use Metal objects (GPU Commands thread)

## Solution Requirements

1. **Prevent Autorelease Escape**: Ensure no Metal objects escape local autorelease pool scopes
2. **Explicit Ownership**: Use retain/release instead of autorelease where possible
3. **Pool Granularity**: Create autorelease pools around Metal operations, not entire threads
4. **Clean Shutdown**: Ensure all Metal objects are released before thread exit

## Implementation Strategy

### Phase 1: Audit Metal Object Lifecycle
- Track every Metal object creation
- Identify which objects are autoreleased
- Find where objects escape pool scope

### Phase 2: Implement Scoped Pools
- Create autorelease pools around Metal operations
- Not at thread level, but at operation level
- Ensure pools are drained before objects can escape

### Phase 3: Explicit Memory Management
- For long-lived Metal objects, use retain/release
- Avoid autorelease for objects that cross scope boundaries
- Clear all Metal state before thread exit

### Phase 4: Verification
- Test with OBJC_DEBUG_MISSING_POOLS=YES
- Use MetalObjectTracker to verify no leaks
- Ensure clean shutdown without hangs