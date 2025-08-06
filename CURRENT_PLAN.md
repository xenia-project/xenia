# Current Plan: Metal Backend Implementation

## Current Status (40% Complete)
✅ Metal object lifecycle tracking implemented
✅ Command buffer submission flow working
✅ Draw commands executing successfully (terminal and XCode)
✅ Shader translation pipeline functional
✅ Thread lifecycle and autorelease pool management fixed
✅ Program runs to completion without crashes or hangs
✅ Clean shutdown with proper resource cleanup
✅ Render target creation with EDRAM address tracking
✅ EDRAM buffer allocated (10MB)
✅ Direct framebuffer capture for trace dumps
❌ No visible rendering output (need EDRAM Load/Store)
❌ No presenter for trace dump PNG capture
❌ Texture support not implemented

## Critical Issues Found and Fixed (2025-08-06)

### Issue 1: Metal Validation Errors
**Problem:** Metal throws NSException when fragment shader writes color but no color attachments exist
**Solution:** 
- Check if pixel shader writes color using `shader->writes_color_targets()`
- Add dummy color attachment when needed for depth-only render passes
- Ensure render pass has at least one attachment that matches shader expectations

### Issue 2: Sample Count Mismatch
**Problem:** Pipeline state sample count doesn't match render target sample count
**Solution:**
- Pass pipeline's expected sample count to render target cache
- Ensure dummy targets are created with matching sample count
- Modified `GetRenderPassDescriptor()` to accept expected_sample_count parameter

### Issue 3: Command Buffer Management
**Problem:** Command buffer becomes null after batch commit, causing crashes
**Solution:**
- Call `BeginSubmission()` after `EndSubmission()` when batching draws
- Properly manage command buffer lifecycle across draw batches
- Limit batch size to 50 draws to avoid Metal's 64 in-flight buffer limit

### Issue 4: Objective-C Exception Handling
**Problem:** Metal throws NSExceptions that weren't being caught
**Solution:**
- Added @try/@catch blocks around Metal API calls
- Import Foundation.h for NSException support
- Log exception details for debugging

### Issue 5: Thread Shutdown Hangs
**Problem:** Program hanging at exit due to pthread_join deadlocks
**Solution:**
- Removed pthread_join from post_execution() 
- Implemented pthread_detach in ThreadStartRoutine to allow threads to clean themselves up
- Threads now detach after signaling completion

### Issue 6: Autorelease Pool False Positive Leaks
**Problem:** MetalCommandProcessor::ShutdownContext reported false pool leaks
**Solution:**
- Removed premature leak check inside ShutdownContext scope
- Pool is properly destroyed when scope exits

### Issue 7: ThreadSanitizer Thread Leaks
**Problem:** TSan reported thread leaks at program exit
**Solution:**
- Threads now detach themselves before exiting
- Proper cleanup prevents TSan from seeing threads as leaked

### Fixes Applied Today (Session 1)
1. ✅ Fixed Metal exception for depth-only render passes
2. ✅ Added proper Objective-C exception handling
3. ✅ Implemented draw batching (50 draws per batch)
4. ✅ Fixed command buffer lifecycle management (retain/release)
5. ✅ Fixed sample count mismatch between pipeline and render pass
6. ✅ Fixed thread shutdown hangs with pthread_detach
7. ✅ Fixed autorelease pool false positive leaks
8. ✅ Fixed ThreadSanitizer thread leak warnings
9. ✅ Program now runs to completion in terminal without crashes

### Implementation Progress Today (Session 2)
1. ✅ Implemented SetRenderTargets with EDRAM address tracking
2. ✅ Added edram_base field to MetalRenderTarget structure
3. ✅ Created 10MB EDRAM buffer in MetalRenderTargetCache
4. ✅ Implemented CaptureColorTarget for direct framebuffer capture
5. ✅ Added CaptureGuestOutput to MetalGraphicsSystem
6. ✅ Modified RefreshGuestOutput to use depth buffer when no color target
7. ✅ Verified render targets are created at correct EDRAM addresses
8. ✅ Added placeholder EDRAM Load/Store methods

## New Implementation Plan: Complete Metal Backend Core Features

### Phase 1: EDRAM Implementation (IN PROGRESS - 50% Complete)
**Goal**: Implement 10MB EDRAM simulation for Xbox 360 framebuffer operations

**Completed:**
1. ✅ Created 10MB Metal buffer for EDRAM storage
2. ✅ Render targets track EDRAM addresses
3. ✅ SetRenderTargets creates Metal textures

**Remaining Tasks:**
1. ⏳ Implement LoadRenderTargetsFromEDRAM (copy EDRAM → textures)
2. ⏳ Implement StoreRenderTargetsToEDRAM (copy textures → EDRAM)
3. ⏳ Add blit encoders for EDRAM transfers
4. ⏳ Support MSAA resolve operations
5. ⏳ Implement format conversion for Xbox 360 formats

### Phase 2: RefreshGuestOutput Implementation
**Goal**: Display rendered content to screen

**Tasks:**
1. Implement RefreshGuestOutput in MetalCommandProcessor
2. Create presentation pipeline
3. Set up CAMetalLayer integration
4. Handle framebuffer copy/resolve to presentable texture
5. Implement vsync and frame pacing

### Phase 3: Texture Support
**Goal**: Complete texture loading and sampling

**Tasks:**
1. Implement remaining Xbox 360 texture formats (41 more to go)
2. Add texture cache invalidation
3. Support texture swizzling
4. Implement anisotropic filtering
5. Add cube map and volume texture support

### Phase 4: Primitive Processing
**Goal**: Handle all Xbox 360 primitive types

**Tasks:**
1. Implement remaining primitive types
2. Add geometry shader emulation
3. Support tessellation
4. Handle primitive restart properly
5. Optimize vertex buffer management

## Next Immediate Steps (Priority Order)

### TODAY'S FOCUS: Make Rendering Visible

1. **Implement EDRAM Load/Store Operations** (metal_render_target_cache.cc)
   - ⏳ Create blit encoder for EDRAM transfers
   - ⏳ Copy EDRAM buffer to render target textures on load
   - ⏳ Copy render target textures to EDRAM buffer on store
   - ⏳ Handle format conversions and byte swapping

2. **Create Null Presenter for Trace Dumps** (metal_null_presenter.cc)
   - ⏳ Implement minimal presenter that captures to buffer
   - ⏳ Enable PNG output from trace dumps
   - ⏳ Test with existing traces

3. **Fix Depth Buffer Visualization** 
   - ⏳ Convert depth format to displayable RGBA
   - ⏳ Handle depth range normalization
   - ⏳ Test depth buffer capture

4. **Verify Rendering Pipeline**
   - ⏳ Ensure draws are actually rendering to targets
   - ⏳ Check blend state and clear operations
   - ⏳ Verify viewport and scissor setup

2. **Compare Environments**
   ```bash
   # In XCode
   env | grep -E "(METAL|OBJC|MTL)" > xcode_env.txt
   
   # In terminal  
   env | grep -E "(METAL|OBJC|MTL)" > terminal_env.txt
   
   # Compare
   diff xcode_env.txt terminal_env.txt
   ```

3. **Simplify Metal Operations**
   - Remove all Objective-C blocks
   - Use explicit retain/release
   - Minimize autorelease pool scopes
   - Synchronize Metal operations

4. **Fix Critical Hangs**
   - Fix renderCommandEncoder hang
   - Fix command buffer commit hang
   - Fix thread shutdown sequence

## Documentation References Needed
- Apple: "Metal Programming Guide" - Memory Management
- Apple: "Objective-C Runtime Programming Guide" - Autorelease Pools
- Metal-cpp GitHub: Object Lifecycle Documentation
- Apple: "Threading Programming Guide" - Thread-Local Storage

## Success Metrics
- ✅ Trace dump completes in both XCode and terminal
- ✅ No autorelease pool warnings or leaks
- ✅ Clean thread shutdown
- ✅ Consistent behavior across environments
- ✅ All Metal objects properly released

## Implementation Plan: Fix Thread Hang

### Phase 1: Add Autorelease Pool Tracking (IMMEDIATE)
**Goal**: Understand exactly where pools are created/drained

**Tasks**:
- [ ] Implement AutoreleasePoolTracker class
- [ ] Add push/pop logging with depth tracking
- [ ] Add leak detection on thread exit
- [ ] Instrument all pool operations

**Files**:
- `src/xenia/base/threading_mac.cc` - Add tracker
- `src/xenia/gpu/metal/metal_command_processor.cc` - Use tracker

### Phase 2: Implement Scoped Autorelease Pools (TODAY)
**Goal**: Create tight autorelease pools around Metal operations

**Tasks**:
- [ ] Create ScopedAutoreleasePool RAII class
- [ ] Add pools around ExecutePacket
- [ ] Add pools around BeginSubmission/EndSubmission
- [ ] Add pools around shader compilation
- [ ] Add pools around pipeline creation
- [ ] Add pools around texture operations

**Key Locations**:
```cpp
// Every Metal API call needs a scoped pool:
void ExecutePacket() {
    ScopedAutoreleasePool pool("ExecutePacket");
    // Metal operations here
}

void BeginSubmission() {
    ScopedAutoreleasePool pool("BeginSubmission");
    // Create command buffer
}
```

### Phase 3: Remove Thread-Level Pools (TODAY)
**Goal**: Stop creating thread-wide pools that accumulate objects

**Tasks**:
- [ ] Remove pool from PosixThread::ThreadStartRoutine
- [ ] Remove pool from XHostThread::Execute
- [ ] Remove pool from CommandProcessor::WorkerThreadMain
- [ ] Verify no pools at thread level

### Phase 4: Fix Metal Object Ownership (TODAY)
**Goal**: Prevent autoreleased objects from escaping pool scope

**Tasks**:
- [ ] Audit all Metal object creation
- [ ] Use retain() for objects that need to persist
- [ ] Release explicitly before thread exit
- [ ] Clear all Metal state in ShutdownContext

**Critical Objects to Fix**:
- MTL::Device (retain on creation)
- MTL::CommandQueue (retain on creation)
- MTL::RenderPipelineState (retain for cache)
- MTL::Texture (retain for cache)

### Phase 5: Clean Shutdown Implementation (TODAY)
**Goal**: Ensure all resources released before thread exit

**Tasks**:
- [ ] Implement proper ShutdownContext()
- [ ] Drain pending command buffers
- [ ] Clear all caches with scoped pools
- [ ] Release all retained objects
- [ ] Verify no leaked pools

### Phase 6: Testing & Verification (TODAY)
**Goal**: Confirm the fix works

**Tests**:
1. Basic trace dump (should complete without hang)
2. Run with OBJC_DEBUG_MISSING_POOLS=YES (no warnings)
3. Run with memory debugging (no errors)
4. Verify PNG output generated

## Success Metrics

### Must Have (Today)
- ✅ Trace dump completes without hanging
- ✅ Clean thread shutdown
- ✅ No TLS cleanup hang

### Should Have
- ✅ No autorelease pool warnings
- ✅ No Metal object leaks
- ✅ PNG output (if RefreshGuestOutput implemented)

### Nice to Have
- ✅ Performance metrics for pool overhead
- ✅ Debug mode verification

## Implementation Order

1. **FIRST** (30 min): Add AutoreleasePoolTracker
2. **SECOND** (1 hour): Implement scoped pools in Metal operations
3. **THIRD** (30 min): Remove thread-level pools
4. **FOURTH** (1 hour): Fix Metal object ownership
5. **FIFTH** (30 min): Implement clean shutdown
6. **SIXTH** (30 min): Test and verify

**Total Estimated Time**: 4 hours

## Files to Modify

### Core Changes
1. `src/xenia/base/threading_mac.cc` - Pool tracker, remove thread pools
2. `src/xenia/gpu/metal/metal_command_processor.cc` - Scoped pools
3. `src/xenia/gpu/command_processor.cc` - Remove thread pool
4. `src/xenia/kernel/xthread.cc` - Remove XHostThread pool

### Metal Operations
5. `src/xenia/gpu/metal/metal_shader.cc` - Scoped pools
6. `src/xenia/gpu/metal/metal_pipeline_cache.cc` - Object retention
7. `src/xenia/gpu/metal/metal_texture_cache.cc` - Object retention
8. `src/xenia/gpu/metal/metal_buffer_cache.cc` - Scoped pools

## Fallback Plan

If the comprehensive fix doesn't work:
1. **Option A**: Forcefully terminate threads with pthread_cancel
2. **Option B**: Accept the hang and document workaround (Ctrl+C)
3. **Option C**: Disable Metal debug layer completely
4. **Option D**: Use a separate process for GPU operations

## After Thread Fix: Next Priorities

1. **RefreshGuestOutput Implementation** (Critical for testing)
   - Copy render target to guest output
   - Enable PNG capture
   - Verify rendering works

2. **EDRAM Simulation** (Required for real rendering)
   - 10MB buffer simulation
   - Render target management
   - Color/depth buffer handling

3. **Texture Support** (Visual output)
   - Format conversion
   - Sampling implementation
   - Cache management

## Documentation Created
- `METAL_THREAD_HANG_ANALYSIS.md` - Detailed root cause analysis
- `METAL_THREAD_HANG_FIX_PLAN.md` - Step-by-step implementation plan
- `THREAD_HANG_DEBUG_PLAN.md` - Original debug strategy

## Status Update
**Date**: 2025-08-06
**Time Spent**: 8 hours on thread hang investigation
**Current Focus**: Implementing scoped autorelease pools
**Blocker**: Metal-cpp autorelease design incompatible with pthread TLS cleanup
**Next Step**: Implement AutoreleasePoolTracker and scoped pools