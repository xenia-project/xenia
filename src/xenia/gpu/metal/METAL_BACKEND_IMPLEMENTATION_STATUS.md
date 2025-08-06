# Metal GPU Backend Implementation Status

## Executive Summary

The Metal GPU backend has reached a **major milestone** with working PNG output generation from trace dumps. The implementation can execute draw commands, manage Metal resources, and capture rendered frames. However, we're currently only capturing depth buffers (gray placeholders) instead of actual color render targets. Current implementation is approximately **35% complete** compared to working Vulkan and D3D12 backends.

**Latest Status (2025-01-06)**: PNG output pipeline fully working! Successfully generating PNG files from trace dumps, though currently showing depth buffer placeholders instead of actual rendered content. Some autorelease pool crashes remain during shutdown.

## Current Capabilities vs Reality

### ✅ What Actually Works
- Metal device and command queue initialization
- Xbox 360 shader → Metal shader translation pipeline (DXBC→DXIL→Metal)
- Pipeline state creation without validation errors
- Vertex/index buffer binding from guest memory
- Draw command encoding and execution
- Render target creation with format/MSAA support
- Endian conversion for vertex data
- Metal object lifecycle (no leaks with proper tracking)
- Command buffer submission flow with synchronization
- **PNG output generation from trace dumps** ✅ NEW!
- Frame capture during draw operations
- Depth buffer capture as placeholder images

### ❌ What Doesn't Work
- **No color output** - Only capturing depth buffer (gray images)
- **Autorelease pool crashes** - Still occurring during shutdown
- **No EDRAM integration** - Buffer exists but not connected
- **No texture sampling** - Textures not implemented (critical for actual games)
- **No color render targets** - Need to capture color buffer instead of depth
- **Missing most Xbox 360 features** - Blending, proper depth/stencil, etc.

## File Structure

### Core Implementation Files
```
metal_command_processor.mm    - Draw commands, submission flow (WORKING)
metal_pipeline_cache.cc       - Pipeline state creation (WORKING)
metal_shader.cc               - Shader translation (WORKING)
metal_render_target_cache.cc  - RT creation (PARTIAL - no readback)
metal_buffer_cache.cc         - Vertex/index buffers (WORKING)
metal_texture_cache.cc        - Textures (STUB)
metal_shared_memory.cc        - Shared memory (BASIC)
metal_primitive_processor.cc  - Primitive conversion (BASIC)
metal_object_tracker.h        - Debug tracking (NEW - WORKING)
```

### Integration Files
```
metal_presenter.mm            - UI integration (PARTIAL)
metal_trace_dump_main.cc     - Trace dump entry (WORKING)
dxbc_to_dxil_converter.cc    - Shader conversion (WORKING)
```

## Recent Changes (2025-01-06)

### 1. PNG Output Pipeline Implementation ✅
**Problem**: No way to verify rendering output
**Solution**: 
- Created `MetalTraceDumpPresenter` dummy presenter class
- Made `GraphicsSystem::presenter()` virtual for override
- Implemented frame capture in `MetalCommandProcessor`
- Added `GetLastCapturedFrame()` for post-execution retrieval
**Result**: Successfully generating PNG files from trace dumps!

### 2. Fixed Command Buffer Synchronization ✅
**Problem**: Crashes from async command buffer completion
**Solution**:
- Added `waitUntilCompleted()` for trace dump mode
- Proper fence at shutdown to wait for all GPU work
- Synchronous completion in headless mode
**Result**: No more command buffer related crashes

### 3. Frame Capture Implementation ✅
**Problem**: No way to capture rendered frames
**Solution**:
- Capture every 10th draw for debugging
- Store last captured frame in command processor
- Direct Metal texture → CPU buffer copy
**Result**: Capturing 1280x720 frames successfully

### 4. Depth Buffer Placeholder ✅
**Problem**: Depth+stencil textures can't be directly read
**Solution**: Generate gray placeholder images for now
**Result**: PNG output works, showing gray placeholders

## Critical Missing Pieces

### 1. Color Buffer Capture (URGENT - Currently Only Depth)
```cpp
// CURRENT:
CaptureColorTarget(0, ...) {
  // Falls back to depth buffer when no color target
  // Results in gray placeholder images
}

// NEEDED:
CaptureColorTarget(0, ...) {
  // Actually capture color render target
  // Show real rendered content
}
```

### 2. Autorelease Pool Crash Fix
- Still crashing in `objc_autoreleasePoolPop()` during shutdown
- Happens in `MetalCommandProcessor::ShutdownContext`
- Need to investigate Metal-cpp object lifecycle

### 3. EDRAM Integration (Required for Real Rendering)
- 10MB buffer created but never used
- Need Load/Store operations between EDRAM and textures
- Xbox 360 games expect EDRAM for all framebuffer operations

### 4. Texture Support (Critical for Games)
- Currently NO texture support at all
- A-Train HX logo is a texture - won't appear without this
- Need format conversion, sampling, caching

## Test Results

### Working Test Case
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```

### What Happens
1. ✅ Loads trace successfully
2. ✅ Translates shaders to Metal
3. ✅ Creates pipeline states
4. ✅ Executes 11 draw calls
5. ✅ No Metal validation errors
6. ✅ PNG output generated successfully
7. ⚠️ PNG shows gray placeholder (depth buffer, not color)
8. ❌ Autorelease pool crash on shutdown

### Metal Object Tracker Output
```
====== Metal Object Tracker Report ======
Total created: 63
Currently alive: 0
=== No Leaked Objects ===
```

## Realistic Assessment

### What's Done (~35%)
- Core rendering pipeline structure ✅
- Shader translation working ✅
- Draw command execution ✅
- Resource management basics ✅
- PNG output generation ✅
- Frame capture pipeline ✅

### What's Not Done (~65%)
- Color render target capture (currently depth only)
- Texture support (0% done)
- EDRAM operations (buffer exists, not connected)
- Most GPU features (blending, proper depth/stencil, etc.)
- Clean shutdown (autorelease crashes)
- Performance optimization

### Distance from Reference Image
**Current**: Gray placeholder (depth buffer)
**Target**: A-Train HX title screen with logo and UI
**Gap**: Need color buffers, textures, EDRAM, proper render states

### Time to Functional
Based on current progress:
- **See actual rendered content**: 1-2 weeks (color buffer + EDRAM basics)
- **Minimum viable** (simple games work): 2-3 months
- **Feature complete** (most games work): 6-12 months

## Next Priority Tasks (Updated 2025-01-06)

### Phase 1: Fix Immediate Issues (1-2 days)
1. **Fix Autorelease Pool Crash**
   - Investigate Metal-cpp object lifecycle in ShutdownContext
   - May need to restructure cleanup order
   - Consider using NS::AutoreleasePool directly

2. **Capture Color Buffer Instead of Depth**
   - Modify CaptureColorTarget to actually get color RT
   - Stop falling back to depth buffer
   - Should immediately show more realistic output

### Phase 2: Enable Basic Rendering (3-5 days)
3. **Implement EDRAM Load/Store**
   - Connect 10MB EDRAM buffer to render targets
   - Implement LoadRenderTargetsFromEDRAM
   - Implement StoreRenderTargetsToEDRAM
   - Handle resolve operations

4. **Fix SetRenderTargets**
   - Ensure color targets are properly bound
   - Verify render pass descriptors are correct
   - Check MSAA settings match

### Phase 3: Texture Support (1 week)
5. **Basic Texture Implementation**
   - Start with common formats (DXT1, DXT5, RGBA8)
   - Implement texture cache
   - Add sampler state management
   - Wire up texture binding in shaders

### Phase 4: Render States (3-4 days)
6. **Implement Blend States**
   - Alpha blending for UI
   - Color write masks
   - Blend equations

7. **Depth/Stencil States**
   - Proper depth testing
   - Stencil operations
   - Clear operations

## Known Issues

### Critical
- Thread shutdown crash prevents clean exit
- No rendered output visible (test pattern only)

### Major  
- EDRAM not functional
- No texture support
- RefreshGuestOutput is a stub

### Minor
- Some vertex formats not handled
- Limited primitive types
- No performance optimization

## Summary

The Metal backend has a solid foundation with working shader translation and command execution, but lacks the critical final steps to actually see rendered output. The architecture is sound, but significant work remains to reach usability. Current state is best described as "proof of concept" rather than functional.