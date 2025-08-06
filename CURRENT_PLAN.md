# Current Plan: Metal Backend Implementation

## Current Status (30% Complete)
✅ Metal object lifecycle fixed - no leaks
✅ Command buffer submission flow corrected
✅ Draw commands executing successfully
✅ Shader translation pipeline working
⚠️ Thread exit hangs (non-critical, shutdown only)
❌ No visible rendering output (RefreshGuestOutput stub)
❌ EDRAM integration missing
❌ Texture support not implemented

## Known Issue: Thread Exit Hang
The GPU Commands thread hangs during exit in `objc_release()` when pthread cleans up thread-local storage. This happens after Metal objects are cleaned but doesn't affect functionality - only prevents clean shutdown.

## Stack Trace
```
GPU Commands#0    0x000000019f4400ec in objc_release ()
#1    0x000000019f447c84 in AutoreleasePoolPage::releaseUntil ()
#2    0x000000019f444150 in objc_autoreleasePoolPop ()
#3    0x000000019f478d18 in objc_tls_direct_base<AutoreleasePoolPage*, (tls_key)3, AutoreleasePoolPage::HotPageDealloc>::dtor_ ()
#4    0x00000001013ba600 in _pthread_tsd_cleanup ()
#5    0x00000001013b1198 in _pthread_exit ()
#6    0x00000001013b2854 in pthread_exit ()
#7    0x00000001000916a4 in xe::threading::PosixThread::Terminate at threading_mac.cc:506
```

## Root Cause Analysis

### What We Know
1. All Metal objects are properly released (tracker shows 0 alive)
2. Our explicit autorelease pools are properly created and drained
3. The crash happens in pthread's TLS cleanup, not our code
4. It's trying to clean up autorelease pool thread-local data

### Hypothesis
The Objective-C runtime maintains its own thread-local autorelease pool state. When we call `pthread_exit()`, it tries to clean this up, but something is corrupted or there are still autoreleased objects we don't know about.

### Possible Causes
1. **Metal-cpp autoreleases**: Metal-cpp might be autoreleasing objects internally
2. **System frameworks**: NSUserDefaults and other system objects are being autoreleased (we see warnings)
3. **Race condition**: Thread exit happening while Metal operations still pending
4. **Double pool drain**: We might be draining a pool twice

## Investigation Steps

### 1. Check Thread Exit Path
- Look at how PosixThread::Terminate is called
- See if we can avoid pthread_exit and return naturally
- Check if the thread is being terminated vs exiting normally

### 2. System Object Autoreleases
We see these warnings:
```
objc[93944]: MISSING POOLS: Object 0xb63490dc0 of class MTLDebugFunction autoreleased with no pool in place
objc[93944]: MISSING POOLS: Object 0xb634923c0 of class MTLDebugFunction autoreleased with no pool in place
```
These happen AFTER thread exit, suggesting Metal debug layer is doing something after our cleanup.

### 3. Metal Debug Layer
The MTLDebugFunction objects are from Metal's debug layer. We should:
- Try running without METAL_DEVICE_WRAPPER_TYPE=1
- Check if debug layer has its own autorelease behavior

## Immediate Actions

### Option 1: Avoid pthread_exit
Modify thread termination to return from thread function instead of calling pthread_exit.

### Option 2: Disable Metal Debug Layer
Test without Metal validation to see if the debug layer is causing issues.

### Option 3: Drain Pools Earlier
Ensure all autorelease pools are drained before ANY thread cleanup.

## Test Plan

### Test 1: Without Metal Debug Layer
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```

### Test 2: With Explicit Pool Management
```bash
OBJC_DEBUG_MISSING_POOLS=YES \
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```

### Test 3: Check Thread Exit Path
Add logging to understand exact thread exit sequence.

## Success Criteria
- [ ] Trace dump completes without crash
- [ ] PNG file generated successfully  
- [ ] Clean thread shutdown
- [ ] No autorelease pool warnings

## Next Priority: Implement RefreshGuestOutput

The most critical missing piece is RefreshGuestOutput implementation to:
1. Copy render target data to guest output texture
2. Enable PNG capture for testing
3. Verify that rendering is actually working

### Implementation Steps
1. Get render target from cache
2. Create blit encoder to copy RT → guest output texture  
3. Synchronize and wait for copy
4. Return texture for PNG generation

## Alternative Thread Fix
The thread hang is non-critical (shutdown only). Potential solutions:
1. Use pthread_cancel instead of pthread_exit
2. Skip TLS cleanup for GPU threads
3. Accept the hang for now and use Ctrl+C to exit

## Notes
- The crash is consistent and reproducible
- It only affects shutdown, not runtime operation
- All Metal rendering operations complete successfully before the crash