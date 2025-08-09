# Metal GPU Backend Implementation Status

## Executive Summary

The Metal GPU backend has achieved significant architectural improvements with proper RenderTargetCache inheritance and NDC viewport transformation. While rendering output remains pink/magenta (indicating shader execution but incorrect vertex transformation), the backend is now architecturally aligned with D3D12/Vulkan. The backend is approximately **30% complete** compared to working backends.

**Latest Status (2025-08-08)**: 
- Refactored to inherit from base gpu::RenderTargetCache class (major architectural fix)
- Implemented GetHostViewportInfo() for NDC transformation 
- Fixed render encoder lifecycle (proper reuse across draws)
- Rendering produces pink output (shaders executing but vertices in wrong coordinate space)

## Current Capabilities vs Reality

### ✅ What Actually Works
- Metal device and command queue initialization
- Xbox 360 shader → Metal shader translation pipeline (DXBC→DXIL→Metal)
- Pipeline state creation without validation errors
- **RenderTargetCache inheritance** - Properly inherits from base class like D3D12/Vulkan
- **Render target creation from EDRAM** - Base class creates targets with correct keys
- **Encoder lifecycle management** - Reuses encoders across draws (efficiency)
- **Texture untiling** - Converts Xbox 360 tiled textures to linear
- **Shader reflection integration** - Resources bind at correct offsets
- **Argument buffer creation** - Based on reflection data
- **Resource binding** - Textures, samplers, and CBVs placed correctly
- **Viewport calculation** - GetHostViewportInfo() provides NDC transformation values

### ❌ What Doesn't Work (Critical Issues)
- **NDC vertex transformation** - Vertices output in Xbox 360 space, not transformed to Metal NDC
  - Xbox 360: Custom coordinate system with screen-space output
  - Metal NDC: Y-up, (-1,-1) at bottom-left, needs transformation
  - Issue: Shaders compiled without NDC transformation injection
- **Shader constant application** - NDC scale/offset calculated but not applied to vertices
  - GetHostViewportInfo() provides correct values
  - Need to inject transformation: `position.xyz = position.xyz * ndc_scale + ndc_offset * position.w`
- **Fragment shader output** - Pink output indicates vertices outside viewport
- **GPU capture corruption** - Capture files report "index file does not exist"
- **Most Xbox 360 GPU features** - Blending, depth/stencil, tessellation, etc.

## Critical Next Steps

### 1. Fix NDC Transformation (HIGHEST PRIORITY)
The core issue is that Xbox 360 vertex shaders output positions in a custom coordinate system that needs transformation to Metal's NDC space. Options:
- **Option A**: Modify shader translation to inject NDC transformation
- **Option B**: Use compute shader for vertex post-processing
- **Option C**: Pass NDC values as push constants and modify in shader
- **Current approach**: Viewport set correctly but vertices not transformed

### 2. Debug Logging Improvements
Add to capture/debug output:
- NDC transformation values from GetHostViewportInfo()
- Actual vertex positions before/after transformation
- Render target dimensions and formats
- Pipeline state details

## File Structure

### Core Implementation Files
```
metal_command_processor.mm    - Draw commands, NDC viewport (PARTIAL)
metal_pipeline_cache.cc       - Pipeline state creation (WORKING)
metal_shader.cc               - Shader translation (NEEDS NDC INJECTION)
metal_render_target_cache.cc  - RT creation (WORKING - inherits from base)
metal_buffer_cache.cc         - Vertex/index buffers (WORKING)
metal_texture_cache.cc        - Textures (WORKING - untiling implemented!)
metal_shared_memory.cc        - Shared memory (BASIC)
metal_primitive_processor.cc  - Primitive conversion (BASIC)
metal_object_tracker.h        - Debug tracking (WORKING)
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

### 1. PNG Capture Still Black (Despite Correct Rendering)
```cpp
// PROBLEM:
// Metal frame capture shows textures correctly
// But PNG output is still black
// Issue: Capturing wrong buffer or at wrong time

// INVESTIGATION NEEDED:
// - Check if dummy_color_target_ is being captured correctly
// - Verify capture happens after all draws complete
// - May need explicit synchronization before capture
```

### 2. Fragment Shader Output
- Shaders may not be writing correctly to render targets
- Need to verify fragment shader [[color(0)]] output
- Check if shader constants are properly bound

### 3. EDRAM Integration (Required for Real Games)
- 10MB buffer created but never used
- Need Load/Store operations between EDRAM and textures
- Xbox 360 games expect EDRAM for all framebuffer operations

### 4. Extended Texture Format Support
- Currently supporting only ~15% of Xbox 360 formats
- Need DXN (BC5), CTX1, float formats, YUV formats
- Many games won't work without these

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