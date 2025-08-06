# Metal Thread Hang Fix - Implementation Plan

## Overview
Fix the thread hang issue by properly managing Metal object lifecycles and autorelease pools.

## Step-by-Step Implementation

### Step 1: Add Autorelease Pool Tracking
**Goal**: Understand exactly when and where autorelease pools are created/drained

```cpp
// Add to threading_mac.cc
struct AutoreleasePoolTracker {
    static thread_local int pool_depth;
    static thread_local std::vector<void*> pool_stack;
    
    static void* Push(const char* location) {
        void* pool = objc_autoreleasePoolPush();
        pool_stack.push_back(pool);
        pool_depth++;
        XELOGI("AutoreleasePool PUSH: depth={}, location={}, ptr={:x}", 
               pool_depth, location, (uintptr_t)pool);
        return pool;
    }
    
    static void Pop(void* pool, const char* location) {
        if (!pool) return;
        pool_depth--;
        XELOGI("AutoreleasePool POP: depth={}, location={}, ptr={:x}", 
               pool_depth, location, (uintptr_t)pool);
        objc_autoreleasePoolPop(pool);
        pool_stack.pop_back();
    }
    
    static void CheckLeaks() {
        if (!pool_stack.empty()) {
            XELOGE("AutoreleasePool LEAK: {} pools not drained", pool_stack.size());
        }
    }
};
```

### Step 2: Remove Thread-Level Autorelease Pools
**Goal**: Stop creating thread-wide autorelease pools that can accumulate objects

**Files to modify**:
- `src/xenia/base/threading_mac.cc` - Remove pool creation in ThreadStartRoutine
- `src/xenia/kernel/xthread.cc` - Remove pool creation in XHostThread::Execute
- `src/xenia/gpu/command_processor.cc` - Will add scoped pools instead

### Step 3: Add Scoped Autorelease Pools for Metal Operations
**Goal**: Create tight autorelease pools around Metal operations

```cpp
// Add to metal_command_processor.cc
class ScopedAutoreleasePool {
public:
    ScopedAutoreleasePool(const char* name) : name_(name) {
        #if XE_PLATFORM_MAC
        pool_ = AutoreleasePoolTracker::Push(name);
        #endif
    }
    
    ~ScopedAutoreleasePool() {
        #if XE_PLATFORM_MAC
        AutoreleasePoolTracker::Pop(pool_, name_);
        #endif
    }
    
private:
    void* pool_ = nullptr;
    const char* name_;
};

// Usage in ExecutePacket:
void MetalCommandProcessor::ExecutePacket() {
    ScopedAutoreleasePool pool("ExecutePacket");
    // ... process packet ...
    // Pool automatically drained when scope exits
}
```

### Step 4: Fix Metal Object Ownership
**Goal**: Ensure Metal objects don't escape their autorelease pools

**Changes needed**:
1. In `metal_command_processor.cc`:
   - Add pools around BeginSubmission/EndSubmission
   - Add pools around shader compilation
   - Add pools around pipeline creation

2. In `metal_shader.cc`:
   - Add pool around DXIL conversion
   - Add pool around Metal library creation

3. In `metal_texture_cache.cc`:
   - Add pool around texture creation
   - Explicitly retain textures that need to persist

### Step 5: Add Pre-Exit Cleanup
**Goal**: Ensure all Metal resources are released before thread exit

```cpp
// In MetalCommandProcessor::ShutdownContext
void MetalCommandProcessor::ShutdownContext() {
    XELOGI("MetalCommandProcessor: Beginning clean shutdown");
    
    // 1. Stop accepting new work
    shutting_down_ = true;
    
    // 2. Drain any pending command buffers
    if (current_command_buffer_) {
        ScopedAutoreleasePool pool("FinalCommandBufferDrain");
        EndSubmission();
    }
    
    // 3. Clear all caches (with autorelease pool)
    {
        ScopedAutoreleasePool pool("CacheClear");
        texture_cache_->Shutdown();
        pipeline_cache_->Shutdown();
        buffer_cache_->Shutdown();
    }
    
    // 4. Release all retained Metal objects
    {
        ScopedAutoreleasePool pool("MetalObjectRelease");
        device_.reset();
        queue_.reset();
    }
    
    // 5. Verify no leaked pools
    AutoreleasePoolTracker::CheckLeaks();
    
    XELOGI("MetalCommandProcessor: Clean shutdown complete");
}
```

### Step 6: Update Thread Exit Path
**Goal**: Ensure threads exit cleanly without TLS cleanup issues

```cpp
// In CommandProcessor::WorkerThreadMain
void CommandProcessor::WorkerThreadMain() {
    // No thread-level autorelease pool here!
    
    while (worker_running_) {
        // Each iteration has its own pool
        ScopedAutoreleasePool pool("WorkerIteration");
        
        // Process commands...
    }
    
    // Clean shutdown
    if (IsMetalCommandProcessor()) {
        ShutdownContext();
    }
    
    // Verify no autoreleased objects remain
    AutoreleasePoolTracker::CheckLeaks();
    
    // Thread can now exit safely
}
```

### Step 7: Add Debug Verification
**Goal**: Detect problems early

```cpp
// Add debug checks
#ifdef DEBUG
    #define VERIFY_NO_AUTORELEASE_LEAKS() \
        AutoreleasePoolTracker::CheckLeaks()
#else
    #define VERIFY_NO_AUTORELEASE_LEAKS()
#endif

// Use in critical paths:
void MetalCommandProcessor::EndSubmission() {
    // ... end submission ...
    VERIFY_NO_AUTORELEASE_LEAKS();
}
```

## Testing Plan

### Test 1: Basic Trace Dump
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```
**Expected**: Clean exit, no hang

### Test 2: Pool Verification
```bash
OBJC_DEBUG_MISSING_POOLS=YES ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```
**Expected**: No warnings about missing pools

### Test 3: Memory Verification
```bash
MallocScribble=1 MallocGuardEdges=1 ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```
**Expected**: No memory errors

## Success Criteria

1. ✅ Trace dump completes without hanging
2. ✅ Clean thread shutdown (no TLS cleanup hang)
3. ✅ No autorelease pool warnings
4. ✅ No Metal object leaks (verified by MetalObjectTracker)
5. ✅ PNG output generated successfully

## Implementation Order

1. **First**: Add AutoreleasePoolTracker (Step 1)
2. **Second**: Add scoped pools to Metal operations (Step 3)
3. **Third**: Remove thread-level pools (Step 2)
4. **Fourth**: Fix Metal object ownership (Step 4)
5. **Fifth**: Add pre-exit cleanup (Step 5)
6. **Sixth**: Update thread exit paths (Step 6)
7. **Finally**: Add verification and test (Step 7)