# Metal Backend Thread and Object Lifecycle Fixes

## Summary of Changes Made

### 1. Autorelease Pool Management
**Problem**: Metal objects were being created without autorelease pools, causing memory leaks and crashes during thread cleanup.

**Changes Made**:
- Added autorelease pool management to `XHostThread::Execute()` in `src/xenia/kernel/xthread.cc`
- Added per-loop autorelease pools in `CommandProcessor::WorkerThreadMain()` in `src/xenia/gpu/command_processor.cc`
- Ensured proper autorelease pool creation/drainage for all threads that use Metal

### 2. Metal Object Lifecycle Tracking
**Problem**: Command buffers and other Metal objects were leaking, causing crashes when autorelease pools were drained.

**Created**: `src/xenia/gpu/metal/metal_object_tracker.h`
- Comprehensive tracking system for Metal object creation/release
- Thread affinity tracking
- Leak detection and reporting
- Lifetime tracking

**Instrumented**:
- Command buffer creation/release
- Render encoder creation/release  
- Buffer allocations (vertex, index, uniform buffers)
- Texture and render pass descriptors

### 3. Command Buffer Submission Flow
**Problem**: Command buffers were created but never committed during trace playback.

**Root Cause**: 
- `IssueDraw` was creating its own local command buffer instead of using `current_command_buffer_`
- Trace playback doesn't call `OnPrimaryBufferEnd()` or `IssueSwap()`
- Command buffer was only committed in `ShutdownContext`

**Fixes**:
- Modified `IssueDraw` to use `current_command_buffer_` from `BeginSubmission`
- Implemented `OnPrimaryBufferEnd()` to submit on primary buffer completion (matching D3D12)
- Ensured `EndSubmission` properly commits and releases command buffers
- Added proper cleanup in `ShutdownContext`

### 4. Results
- ✅ Metal object leaks fixed (0 objects alive at shutdown)
- ✅ Command buffers properly committed and released
- ✅ Draw commands successfully executed
- ❌ Thread shutdown crash still occurring

## Remaining Issue: Thread Shutdown Crash

### Stack Trace Analysis
```
GPU Commands#0    0x000000019f4400ec in objc_release ()
#1    0x000000019f447c84 in AutoreleasePoolPage::releaseUntil ()
#2    0x000000019f444150 in objc_autoreleasePoolPop ()
#3    0x000000019f478d18 in objc_tls_direct_base<AutoreleasePoolPage*, (tls_key)3, AutoreleasePoolPage::HotPageDealloc>::dtor_ ()
#4    0x00000001013ba600 in _pthread_tsd_cleanup ()
```

### Problem
The crash occurs in `objc_release()` during thread-local storage cleanup when the GPU Commands thread exits. This happens AFTER our explicit autorelease pool cleanup in `PosixThread::ThreadStartRoutine`.

### Hypothesis
The crash is happening in thread-local storage cleanup (`_pthread_tsd_cleanup`), specifically in the destructor for thread-local autorelease pool data. This suggests:

1. Some Objective-C objects are being autoreleased AFTER our pool is drained
2. The thread-local autorelease pool state is corrupted
3. There's a race condition during thread shutdown

### Key Observations
- The crash happens at line 506 of `threading_mac.cc` in `PosixThread::Terminate`
- It's calling `pthread_exit()` which triggers TLS cleanup
- The TLS cleanup is trying to clean up autorelease pool data
- Our Metal objects are all properly released (0 alive)

## Files Modified

### Core Threading
1. `src/xenia/kernel/xthread.cc`
   - Added autorelease pool management to XHostThread::Execute()
   - Added selective pool creation based on thread type

2. `src/xenia/base/threading_mac.cc`
   - Already has autorelease pools in PosixThread::ThreadStartRoutine
   - Pool created at line 317, drained at line 380

3. `src/xenia/gpu/command_processor.cc`
   - Added per-loop autorelease pools in WorkerThreadMain()
   - Added Metal object tracking reports

### Metal Implementation
1. `src/xenia/gpu/metal/metal_command_processor.mm`
   - Fixed command buffer usage (use member variable, not local)
   - Added OnPrimaryBufferEnd() implementation
   - Added proper EndSubmission handling
   - Added Metal object tracking instrumentation

2. `src/xenia/gpu/metal/metal_command_processor.h`
   - Added OnPrimaryBufferEnd() override
   - Removed hacky draw counting

3. `src/xenia/gpu/metal/metal_object_tracker.h` (NEW)
   - Complete object lifecycle tracking system

## Design Decisions

### Why OnPrimaryBufferEnd?
Following D3D12's pattern which has `d3d12_submit_on_primary_buffer_end` (defaults to true). This ensures command buffers are submitted after processing a primary buffer, reducing latency.

### Why Not Periodic Submission?
We initially tried submitting every N draws, but this is a hack. The proper backends (D3D12/Vulkan) don't do this. They rely on:
1. IssueSwap for swap commands
2. OnPrimaryBufferEnd for primary buffer completion  
3. ShutdownContext for cleanup

### Command Buffer Lifecycle
1. Created in BeginSubmission
2. Commands added in IssueDraw
3. Committed in EndSubmission
4. Released after commit

## Testing Results

### Before Fixes
- Metal objects leaked (command buffers, encoders, buffers)
- Crash in autorelease pool cleanup
- No PNG output

### After Fixes
- No Metal object leaks (0 objects alive at shutdown)
- Commands execute successfully
- Still crashes during thread exit (different issue)

## Next Steps

1. **Investigate Thread Exit Crash**
   - The crash is in pthread TLS cleanup, not our code
   - May need to ensure all Objective-C objects are released before pthread_exit
   - Consider using pthread_jit_write_protect_np reset (already done)

2. **Potential Solutions**
   - Don't call pthread_exit, let thread return naturally
   - Clear all TLS data before thread exit
   - Investigate if Metal-cpp is setting up its own autorelease pools

3. **Alternative Approach**
   - Since Metal objects are properly cleaned up, the crash might be benign
   - Could catch and suppress the crash during shutdown
   - Focus on getting PNG generation working first