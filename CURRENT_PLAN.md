# Current Plan: Metal Backend Implementation

## Current Status (90% Complete - TEXTURES WORKING WITH UNTILING! 🎉)

### ✅ Achievements (Session 11 - 2025-08-07)
1. **PNG Output Pipeline**: Successfully generating PNG files from trace dumps
2. **Program Stability**: Fixed all crashes and hangs - program runs to completion
3. **Thread Management**: Resolved autorelease pool crashes and thread shutdown issues
4. **Metal Pipeline**: Basic rendering pipeline executing (11 successful draws)
5. **Texture Loading**: Successfully loading and binding textures to shaders
6. **Basic Format Support**: k_8_8_8_8 (format 6) textures working
7. **Xbox 360 Dimension Fix**: Fixed texture dimensions (Xbox stores size-1)
8. **Dummy Target Persistence**: Fixed dummy color target to persist through render passes
9. **Format Support Expanded**: Added k_16_16_16_16 and other missing formats
10. **Correct Render Target Size**: Fixed dummy target to use actual 1280x720 resolution
11. **Viewport/Scissor Setup**: Added proper viewport and scissor configuration
12. **Single Dummy Texture**: Fixed to use one consistent dummy texture throughout frame
13. **Found Rendering Issue**: Draw commands encoded but not committed before capture
14. **FIXED COMMAND BUFFER**: Implemented end-of-frame commit
15. **🎉 FIRST RENDER OUTPUT**: Got cyan output instead of magenta - clear is working!
16. **FIXED SAMPLE COUNT MISMATCH**: Pipeline and render targets now use matching sample counts (Session 10)
17. **FIXED SCISSOR RECT VALIDATION**: Scissor rect now properly clamped to actual render target dimensions
18. **FIXED INVALID DEVICE LOAD ERRORS**: Vertex buffers now bound to correct Metal buffer index 0
19. **FIXED VERTEX DESCRIPTOR**: Made more flexible to handle varying Xbox 360 vertex formats
20. **FIXED BUFFER INDEX MAPPING**: Vertex buffers now bound to index 30+ where Metal shaders expect them
21. **🎉 TEXTURE UNTILING WORKING**: Implemented Xbox 360 texture untiling using texture_conversion::Untile()
22. **🎉 TEXTURES VISIBLE IN METAL CAPTURE**: Japanese text "起動中、しばらくお待ちください" now readable!
23. **FIXED COLOR FORMAT**: Changed k_8_8_8_8 from RGBA to BGRA to match Xbox 360 format
24. **FIXED RENDER TARGET PRESERVATION**: Dummy color target now preserves contents across render passes

### ⚠️ Known Issues & Workarounds
1. **PNG Output Still Black**: Despite textures rendering correctly in Metal capture
   - Textures ARE loading and untiling correctly
   - Metal capture shows correct Japanese text
   - But PNG capture gets black image
   - Issue: May be capturing at wrong time or wrong target
2. **Shader Output Issues**: Fragment shaders may not be outputting correctly
   - Draw calls execute but may not produce correct pixels
4. **Metal Device Crash**: Bad access on shutdown when releasing device
5. **BaseHeap Errors**: Memory release failures during shutdown
6. **Metal Validation Layer Crash**: METAL_DEVICE_WRAPPER_TYPE=1 causes bad access in objc_opt_respondsToSelector
   - Crash in GTMTLGuestAppClient when deallocating command queue
   - Workaround: Run without validation layer for now

## Texture Analysis from A-Train HX Trace

### Currently Loading Textures:
- **1279x719 @ 0x1BFD5000**: Format 6 (k_8_8_8_8) - Likely the main title screen
- **1023x15 @ 0x1B270000**: Format 6 - UI element (progress bar?)
- **1023x255 @ 0x1B170000**: Format 6 - UI element
- **1023x63 @ 0x1B130000**: Format 6 - UI element
- **1023x31 @ 0x1B110000**: Format 6 - UI element
- **1x1 @ 0x10000000**: Format 26 (k_16_16_16_16) - NOW WORKING (was 0x0, fixed dimension calculation)

### Missing Xbox 360 Texture Format Support:
Currently only supporting 9 formats out of 60+:
- ✅ k_8_8_8_8 (format 6)
- ✅ k_1_5_5_5, k_5_6_5, k_8, k_8_8
- ✅ k_DXT1, k_DXT2_3, k_DXT4_5 (BC1-3)
- ❌ k_16_16_16_16 (format 26) - Needed by trace
- ❌ k_2_10_10_10 (format 7)
- ❌ k_DXN (format 49) - BC5
- ❌ k_CTX1 (format 22)
- ❌ k_16/32 float formats
- ❌ YUV formats (k_Y1_Cr_Y0_Cb, etc.)
- ❌ Depth formats (k_24_8, k_24_8_FLOAT)

### Core Issues Preventing Correct Rendering:
1. **Depth-Only Passes**: Trace contains depth-only render passes
   - Color buffers disabled (`RB_COLOR_INFO[0-3] = 0x00000000`)
   - Only depth buffer active at EDRAM 0x2E0
2. **Missing Title Screen Rendering**: The actual A-Train logo rendering happened BEFORE this trace
3. **No Xbox 360 Tiling**: Textures use special tiling/swizzling not implemented

## Detailed Implementation Plan

### Phase 1: Add Missing Texture Format Support (PRIORITY)

**Task 1.1: Add k_16_16_16_16 Support**
```cpp
// In ConvertXenosFormat()
case xenos::TextureFormat::k_16_16_16_16:
  return MTL::PixelFormatRGBA16Unorm;  // Or RGBA16Snorm if signed
```

**Task 1.2: Add Common Formats Used by Games**
```cpp
case xenos::TextureFormat::k_2_10_10_10:
  return MTL::PixelFormatRGB10A2Unorm;
case xenos::TextureFormat::k_DXN:  // BC5
  return MTL::PixelFormatBC5_RGUnorm;
case xenos::TextureFormat::k_16_FLOAT:
  return MTL::PixelFormatR16Float;
case xenos::TextureFormat::k_16_16_FLOAT:
  return MTL::PixelFormatRG16Float;
case xenos::TextureFormat::k_32_FLOAT:
  return MTL::PixelFormatR32Float;
```

**Task 1.3: Fix Texture Size Calculation**
The 0x0 texture suggests we're not calculating size correctly for k_16_16_16_16:
```cpp
// In UpdateTexture2D, handle different bytes per pixel
uint32_t GetBytesPerPixel(TextureFormat format) {
  switch (format) {
    case k_16_16_16_16: return 8;  // 4 * 16-bit
    case k_8_8_8_8: return 4;       // 4 * 8-bit
    // etc.
  }
}
```

### Phase 2: Xbox 360 Texture Tiling/Swizzling

**The Problem**: Xbox 360 uses special memory layouts for textures:
- **2D Tiled**: 32x32 pixel tiles arranged in Morton order
- **3D Tiled**: 32x32x4 voxel tiles
- **Linear**: Standard row-major order (rare)

**Task 2.1: Implement Texture Untiling**
```cpp
void UntileTexture(const uint8_t* tiled_data, uint8_t* linear_data,
                   uint32_t width, uint32_t height, uint32_t bytes_per_pixel) {
  // Xbox 360 uses 32x32 tiles in Morton/Z-order
  const uint32_t tile_size = 32;
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint32_t tile_x = x / tile_size;
      uint32_t tile_y = y / tile_size;
      uint32_t tile_idx = GetMortonIndex(tile_x, tile_y);
      uint32_t in_tile_x = x % tile_size;
      uint32_t in_tile_y = y % tile_size;
      uint32_t in_tile_offset = GetMortonIndex(in_tile_x, in_tile_y);
      // Copy pixel from tiled to linear
    }
  }
}
```

**Task 2.2: Check if Texture is Tiled**
```cpp
// In TextureInfo, check the tiled flag
if (texture_info.is_tiled) {
  // Untile before uploading to Metal
  UntileTexture(guest_data, untiled_data, width, height, bpp);
  texture->replaceRegion(region, 0, untiled_data, bytes_per_row);
}
```

### Phase 3: Fix Color Buffer Capture

**The Core Problem**: We're capturing depth buffer instead of color
```cpp
// Current code in CaptureColorTarget:
if (depth_stencil_texture_) {
  // We're incorrectly using depth texture!
}
```

**Solution**: Track and capture actual color render targets
```cpp
// Need to:
1. Track color attachments during render pass setup
2. Store references to color textures
3. Read from color texture, not depth
```

### Phase 4: Handle Shader Translation Issues

**Problem**: Shaders need texture sampling support
- Vertex shaders may use textures (vertex texture fetch)
- Pixel shaders definitely use textures (we see 1 texture binding)
- Need proper texture coordinate handling

**Task 4.1: Ensure Texture Samplers are Created**
```cpp
// In pipeline creation, add sampler states
MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
sampler_desc->setAddressModeS(MTL::SamplerAddressModeRepeat);
MTL::SamplerState* sampler = device->newSamplerState(sampler_desc);
encoder->setFragmentSamplerState(sampler, slot);
```

## PROBLEMS IDENTIFIED

### 1. ✅ FIXED - Texture Pointer Mismatch
- Was creating multiple dummy textures with different sizes
- Fixed by using consistent 1280x720 and removing sample count checks
- Now using single texture throughout: verified with pointer logging

### 2. ✅ FIXED - Command Buffer Not Executed Before Capture

**The draw commands are encoded but NOT executed when we capture!**

Sequence of events:
1. Create render encoder with dummy target
2. Set viewport, scissor, pipeline state ✓
3. Bind vertex/index buffers ✓
4. Issue drawIndexedPrimitives ✓
5. End encoding ✓
6. **Try to capture** ❌ Command buffer not committed!
7. Capture gets uninitialized texture = MAGENTA

### Successful Fix
Implemented end-of-frame commit:
- Removed mid-frame captures from IssueDraw
- Added CommitPendingCommandBuffer method
- Called at end of trace playback before capture
- **Result: CYAN output instead of magenta!**

## CURRENT STATUS - Pipeline Working, Shaders Not Outputting

### What's Working
1. **Clear operations** - Black clear is working correctly
2. **Command buffer execution** - Draw commands are running
3. **Texture capture** - Capturing the correct render target
4. **Shader conversion** - Xbox 360 → DXBC → DXIL → Metal succeeds
5. **Draw calls executing** - 7 indexed draw calls are made

### DIAGNOSIS RESULTS
- With yellow clear: Got cyan output (yellow + something blue)
- With black clear: Got pure black (draws produce NO output)
- **Conclusion**: Draw calls execute but produce no visible pixels

### Root Cause Analysis

**The shaders are NOT producing output!**

Possible reasons:
1. **Shader translation broken** - Metal shaders compile but don't work
   - Xbox 360 uses different coordinate systems
   - DXIL→Metal conversion might lose semantics
   - Vertex output might not match fragment input

2. **Vertex data issues**
   - Vertices might be off-screen
   - Vertex format mismatch
   - No vertex buffers actually bound

3. **Render state problems**
   - Depth test rejecting all fragments
   - Backface culling removing all triangles
   - Viewport/scissor clipping everything

4. **Pipeline configuration**
   - Vertex attributes not properly configured
   - Missing vertex-fragment linkage

## DEBUGGING PLAN - Why Cyan Instead of Game Content?

### The Symptom
- Output is solid cyan/turquoise color
- Cyan = Green + Blue (no red)
- We're clearing to yellow (Red + Green, no blue)
- So where is the blue coming from?

### Hypothesis 1: Draw Calls Are Outputting Blue
**Test**: Change clear color to black (0,0,0,1)
- If still cyan: draws are producing cyan
- If black: draws aren't executing
- If other color: that's what draws produce

### Hypothesis 2: Shaders Aren't Working
**Possible issues**:
1. **Shader translation failed** - DXIL→Metal produced invalid shaders
2. **Shader output mismatch** - Fragment shader not writing to color(0)
3. **Shader constants wrong** - Uniforms/constants not bound correctly

**Tests**:
- Add logging to verify shader compilation succeeded
- Check if vertex shader is producing vertices
- Check if fragment shader is executing

### Hypothesis 3: Vertex Data Problems
**Possible issues**:
1. **Wrong vertex format** - Position/color attributes misaligned
2. **Endianness issues** - Xbox 360 is big-endian, we might not be swapping
3. **No vertices** - Vertex buffer empty or wrong

**Tests**:
- Log vertex buffer contents
- Verify vertex count > 0
- Check vertex attribute bindings

### Hypothesis 4: Render State Issues
**Possible issues**:
1. **Blend mode** - Might be blending with blue
2. **Color mask** - Might only write certain channels
3. **Depth test** - All fragments might be rejected

**Tests**:
- Disable depth test
- Set blend mode to replace
- Check color write mask

## SESSION 10 FIXES SUMMARY

### Critical Issues Fixed ✅
1. **Sample Count Mismatch**: Pipeline expected 4 samples, dummy target had 1
   - Fixed by passing expected_sample_count from pipeline to render target cache
   - Always set sample count in pipeline descriptor, even when 1

2. **Scissor Rect Validation Error**: Scissor rect (1280) exceeded render pass width (320)
   - Fixed by matching dummy target dimensions to actual depth target size
   - Dynamically clamp scissor rect to actual render target dimensions

3. **Metal Validation Layer Crash**: Bad access in objc_opt_respondsToSelector
   - Identified as issue in Metal's capture framework during cleanup
   - Workaround: Run without METAL_DEVICE_WRAPPER_TYPE=1 for now

### Remaining Critical Issue ❌
- **No Visible Rendering**: Draw calls execute but output is pure black
  - ✅ Fixed: ALL "Invalid device load" errors eliminated
  - ✅ Fixed: Vertex buffers correctly bound to index 30+ where shaders expect them
  - ✅ Fixed: Vertex descriptor supports multiple attributes
  - ✅ Fixed: No GPU validation errors remaining
  - ❌ Still broken: Fragment shaders produce no visible pixels

### Root Cause Analysis - Why Still Black?
We've fixed all GPU errors but still get black output. The problem has moved from the vertex stage to:

1. **Shader Constants Not Bound** (MOST LIKELY)
   - Xbox 360 shaders need constant buffers for transforms, colors, etc.
   - We bind some constants but maybe not at the right indices

2. **Fragment Shader Issues**
   - Shader might be outputting wrong color channel
   - Alpha might be 0 (transparent black)
   - Color write mask might be wrong

3. **Coordinate System Mismatch**
   - Xbox 360 uses different coordinate system than Metal
   - Vertices might be outside clip space

4. **Texture Sampling**
   - Fragment shader might be sampling from unbound textures
   - Returning black when texture is missing

## VERTEX ATTRIBUTE BINDING ISSUE ANALYSIS - FIXED ✅

### The Problem
Vertex shaders are crashing with "Invalid device load" errors because:
1. Vertex descriptor is hardcoded to expect only position (float3) at attribute 0
2. Actual Xbox 360 data has varying formats (3 attributes/20 bytes, 1 attribute/8 bytes)
3. Shader tries to read from nil buffers at huge offsets (506099730477023245)

### Xbox 360 Vertex Format Details
From the logs:
- **Draw 1**: 3 attributes, 20 byte stride, data: 0x43CE6000 0x43DC8000 0x1ED2AF20 0x00000000
- **Draw 2**: 1 attribute, 8 byte stride, data: 0xBF000000 0xBF000000 0x449FF000 0xBF000000

### Why It's Failing
1. **Vertex Descriptor Mismatch**: Metal expects float3 position only, Xbox provides varying formats
2. **Buffer Index Mismatch**: Shaders expect buffers at specific indices we're not providing
3. **Attribute Location Mismatch**: Xbox shaders use semantic-based binding, not location-based

### The Fix Applied ✅
1. ✅ Made vertex descriptor more flexible (3 attributes, 20 byte stride)
2. ✅ Fixed buffer binding to use Metal index 30+ where shaders expect vertex data
3. ✅ Result: ALL "Invalid device load" errors eliminated!

### Still TODO
1. ❌ Dynamic vertex descriptor based on shader's vertex_bindings()
2. ❌ Proper semantic mapping (POSITION, TEXCOORD, etc.)

## CRITICAL DISCOVERY - Buffer Index 30+

The DXIL→Metal shader converter maps buffers as follows:
- **Index 0-29**: Constant buffers and resources
- **Index 30+**: Vertex buffers (v0, v1, v2...)
- **Index 30**: Primary vertex stream
- **Index 31**: Secondary vertex stream

This is why we got "Invalid device load" errors - the shader was looking for vertex data at index 30, not index 0!

## METAL FRAME CAPTURE - WORKING! ✅

Successfully captured Metal frame in Debug mode! The capture reveals:

### Textures ARE Loading But Corrupted
1. **Multiple textures visible** - Various sizes from 80KB to 4.81MB
2. **Gradient texture (0x1027eb0e0)** - Shows yellow gradient (mostly correct)
3. **UI texture (0x1027eb360)** - Shows scrambled text/UI elements
4. **Depth buffer** - White depth values visible

### ROOT CAUSE: Missing Xbox 360 Texture Untiling
The corrupted appearance is EXACTLY what Xbox 360 textures look like when read linearly without untiling:
- Horizontal bands of data in wrong positions
- Text appears scrambled but recognizable
- Xbox 360 uses 32x32 pixel tiles in Morton order (Z-order)
- We're copying texture data directly without the untiling transformation

### The Fix Required
Use the existing `texture_conversion::Untile()` function when loading textures from guest memory.

## METAL FRAME CAPTURE ISSUE

### Solution: Run in Debug Mode
Metal capture works when running in Debug mode (not Release mode)!

## NEXT PRIORITY: Fix PNG Capture (Black Output Despite Correct Rendering)

### The Symptom
- Metal frame capture shows textures rendering correctly (Japanese text visible)
- But PNG output is completely black
- This suggests we're capturing the wrong buffer or at the wrong time

### Possible Causes
1. **Timing Issue**: Capture happens before draws complete
   - Need to ensure command buffer is committed and completed
   - May need explicit GPU synchronization
   
2. **Wrong Buffer**: Capturing a different buffer than what's rendered to
   - dummy_color_target_ might be getting cleared after capture
   - Capture code might be looking at wrong texture
   
3. **Render Target State**: Dummy target might not persist correctly
   - Check if texture is being recreated between render and capture
   - Verify texture pointer consistency

### Investigation Plan
1. Add extensive logging to track dummy_color_target_ pointer
2. Verify capture happens AFTER command buffer completion
3. Check if we need waitUntilCompleted() before capture
4. Test capturing immediately after draw vs after swap

### Understanding the Untile Function

The `texture_conversion::Untile()` function requires:
1. **Width/Height in blocks** - For uncompressed formats, this is pixels. For compressed (DXT/BC), it's 4x4 blocks
2. **Input/Output pitch** - Number of blocks per row
3. **Format info** - Must be non-null for both input and output
4. **Copy callback** - Function to copy each block (handles endian swapping)

### Key Insights from Other Backends
- D3D12/Vulkan use compute shaders for untiling in GPU
- For CPU untiling, they use the same `texture_conversion::Untile()` function
- The copy callback is crucial - it handles endian swapping during the copy
- Width/height parameters are in blocks, not bytes

### Correct Implementation Plan for Texture Untiling

#### Step 1: Fix the UpdateTexture2D Function

```cpp
bool MetalTextureCache::UpdateTexture2D(MTL::Texture* texture, const TextureInfo& texture_info) {
  // Get guest data
  const uint8_t* guest_data = static_cast<const uint8_t*>(
      memory_->TranslatePhysical(texture_info.memory.base_address));
  if (!guest_data) {
    XELOGE("Metal texture cache: Invalid guest texture address 0x{:08X}", 
           texture_info.memory.base_address);
    return false;
  }

  const FormatInfo* format_info = texture_info.format_info();
  if (!format_info) {
    XELOGE("Metal texture cache: Invalid texture format");
    return false;
  }

  // Calculate texture data layout
  // Xbox 360 stores dimensions as (actual_size - 1)
  uint32_t width = texture_info.width + 1;
  uint32_t height = texture_info.height + 1;
  
  // For uncompressed formats, blocks are pixels
  // For compressed formats (DXT/BC), blocks are 4x4 pixels
  uint32_t block_width = format_info->block_width;
  uint32_t block_height = format_info->block_height;
  uint32_t blocks_per_row = (width + block_width - 1) / block_width;
  uint32_t blocks_per_column = (height + block_height - 1) / block_height;
  uint32_t bytes_per_block = format_info->bytes_per_block();
  uint32_t bytes_per_row = blocks_per_row * bytes_per_block;
  uint32_t texture_size = bytes_per_row * blocks_per_column;

  XELOGI("Metal texture cache: Uploading {}x{} texture, format {}, tiled={}, {} bytes",
         width, height, static_cast<uint32_t>(format_info->format), 
         texture_info.is_tiled, texture_size);

  MTL::Region region = MTL::Region(0, 0, 0, width, height, 1);
  
  // Check if texture is tiled (Xbox 360 uses 32x32 pixel tiles in Morton order)
  if (texture_info.is_tiled) {
    // Allocate buffer for untiled data
    std::vector<uint8_t> untiled_data(texture_size);
    
    // Setup untiling parameters
    texture_conversion::UntileInfo untile_info = {};
    untile_info.offset_x = 0;
    untile_info.offset_y = 0;
    
    // Width and height are in blocks (pixels for uncompressed, 4x4 blocks for compressed)
    untile_info.width = blocks_per_row;
    untile_info.height = blocks_per_column;
    
    // Pitch is also in blocks
    // For tiled textures, the pitch field contains the pitch of the tiled layout
    untile_info.input_pitch = texture_info.pitch;
    untile_info.output_pitch = blocks_per_row;
    
    // Set format info for proper block handling
    untile_info.input_format_info = format_info;
    untile_info.output_format_info = format_info;
    
    // Use CopySwapBlock callback to handle endian swapping during untiling
    // This is critical - the callback both copies AND swaps endianness
    untile_info.copy_callback = [texture_info](void* dst, const void* src, size_t size) {
      texture_conversion::CopySwapBlock(texture_info.endianness, dst, src, size);
    };
    
    // Perform untiling with endian swap
    texture_conversion::Untile(untiled_data.data(), guest_data, &untile_info);
    
    // Upload the untiled data (already endian-swapped by callback)
    texture->replaceRegion(region, 0, untiled_data.data(), bytes_per_row);
    
  } else {
    // Linear texture - handle endianness then upload directly
    if (texture_info.endianness != xenos::Endian::kNone && 
        texture_info.endianness != xenos::Endian::k8in16) {
      // Need to swap endianness - create a temporary buffer
      std::vector<uint8_t> swapped_data(texture_size);
      std::memcpy(swapped_data.data(), guest_data, texture_size);
      
      // Perform endian swap based on format
      SwapTextureEndian(swapped_data.data(), texture_size, format_info->format);
      
      texture->replaceRegion(region, 0, swapped_data.data(), bytes_per_row);
    } else {
      // No endian swap needed - upload directly
      texture->replaceRegion(region, 0, guest_data, bytes_per_row);
    }
  }
  
  return true;
}
```

#### Step 3: Handle Endian Swapping After Untiling

For textures that need both untiling AND endian swapping:
```cpp
if (texture_info.is_tiled) {
  // First untile
  std::vector<uint8_t> untiled_data(...);
  texture_conversion::Untile(...);
  
  // Then endian swap if needed
  if (needs_endian_swap) {
    // Swap the untiled data
    SwapTextureData(untiled_data.data(), ...);
  }
  
  texture->replaceRegion(region, 0, untiled_data.data(), bytes_per_row);
}
```

#### Step 4: Calculate Correct Pitch

Xbox 360 pitch handling:
```cpp
// Xbox 360 pitch is in blocks for compressed formats
uint32_t GetActualPitch(const TextureInfo& info) {
  if (IsCompressedFormat(info.format)) {
    // For DXT/BC formats, pitch is in blocks
    return info.pitch * GetBlockSize(info.format);
  } else {
    // For uncompressed formats, pitch is in pixels
    return info.pitch * GetBytesPerPixel(info.format);
  }
}
```

#### Step 5: Test with Specific Textures

From the Metal capture, focus on fixing:
1. **Texture 0x1027eb360** - UI text (most visibly corrupted)
2. **Texture 0x1027ea360** - Larger texture with banding
3. **Gradient texture** - Already mostly correct (likely linear)

### Expected Results After Fix

- Text should be readable instead of scrambled
- UI elements should appear in correct positions
- No more horizontal banding artifacts
- Game content should finally be visible!

### Debugging Tips

1. Start with a simple test - disable tiling for all textures to see if they load
2. Log which textures are tiled vs linear
3. Compare before/after untiling in memory dumps
4. The gradient texture that looks correct is probably linear (not tiled)

### Verification Steps

1. **Check TextureInfo tiling flag**:
```cpp
XELOGI("Texture {} {}x{}: format={}, tiled={}, pitch={}",
       texture_hash, texture_info.width, texture_info.height,
       texture_info.format, texture_info.is_tiled, texture_info.pitch);
```

2. **Verify untiling is working**:
- Before: Scrambled text with horizontal bands
- After: Readable text and proper UI layout

3. **Common Xbox 360 Tiling Patterns**:
- **2D Tiled**: Most render targets and large textures
- **1D Tiled**: Smaller textures and some UI elements
- **Linear**: Rare, mostly very small textures (< 32x32)

## Additional Implementation Notes

### Key Files to Modify
- `src/xenia/gpu/metal/metal_texture_cache.cc` - Add untiling logic
- `src/xenia/gpu/texture_info.h` - Already has `is_tiled` flag
- `src/xenia/gpu/texture_conversion.h/cc` - Already has `Untile()` function

### Why This Will Fix The Black Screen
Currently we see black output because:
1. Textures are loaded but corrupted due to tiling
2. Fragment shaders likely sample from these corrupted textures
3. The corruption makes textures unusable, resulting in black output

After implementing untiling:
1. Textures will display correctly
2. Fragment shaders will sample proper texture data
3. We should finally see the game's actual rendered content!


## CRITICAL DEBUGGING PLAN - Magenta Output Mystery

### The Symptom
- Output is solid magenta (RGB 255,0,255)
- This is NOT our clear color (we set 0.1,0.2,0.3)
- Draw calls ARE executing (7 indexed draws)
- Textures ARE loading
- Viewport/scissor ARE set correctly

### What Magenta Means
Magenta (1.0, 0, 1.0) in graphics typically indicates:
1. **Uninitialized texture memory** - Most likely!
2. **Debug color for missing textures**
3. **BGRA/RGBA channel swap issue**
4. **Metal's default clear color for unbound attachments**

### The Core Issue
**The render target we're capturing was NEVER rendered to!**

Evidence:
- Clear color changes don't affect output
- Draw calls execute but don't affect output
- Output is consistent magenta (uninitialized)

## DEBUGGING STRATEGY

**Draw calls are executing but producing NO visible output!**

What's Working:
1. ✅ Draw calls are being issued (drawIndexedPrimitives)
2. ✅ Vertex and index buffers are bound
3. ✅ Textures are loaded and bound (1280x720 main texture + UI elements)
4. ✅ Viewport and scissor are configured
5. ✅ Pipeline state is created with vertex/pixel shaders
6. ✅ Render target is correct size (1280x720)

What's NOT Working:
1. ❌ No pixels are being rendered (just seeing cleared buffer)
2. ❌ Pink output suggests pixel format or alpha issues
3. ❌ Shaders may not be executing correctly
4. ❌ Vertex data may be malformed

### Step 1: Verify Render Target Binding
**Problem**: The dummy color target might not be properly bound
- Check if render pass descriptor actually has our dummy target
- Verify the texture in the render pass is the same one we capture
- Log texture pointers to ensure they match

### Step 2: Test with Simple Clear
**Problem**: Even clearing isn't working
- Try LoadActionClear with bright color (1,1,0 - yellow)
- Don't do any draws, just clear and capture
- If still magenta, the target isn't bound correctly

### Step 3: Check Render Pass Creation
**Problem**: We might be creating multiple render passes
- The cache might return a different render pass
- The dummy target might be created after the pass
- Need to ensure single render pass with correct target

### Step 4: Verify Shader Output
**Problem**: Shaders might not be writing to color attachment 0
- Check if fragment shader has [[color(0)]] output
- Verify the Metal shader translation includes color output
- The shader might be writing to wrong attachment index

### Step 5: Metal Validation
**Problem**: Metal might be silently failing
- Enable Metal validation layer
- Check for validation errors during rendering
- Look for attachment compatibility issues

## IMMEDIATE ACTIONS

1. **Add logging to track texture pointers**
   - Log dummy_color_target_ pointer when created
   - Log texture pointer in render pass descriptor
   - Log texture pointer during capture
   - Verify they're all the same!

2. **Test minimal clear-only path**
   - Skip all draws
   - Just clear to yellow (1,1,0)
   - Capture and check if yellow or still magenta

3. **Check render pass descriptor**
   - Log colorAttachments()->object(0)->texture() pointer
   - Verify it matches our dummy target
   - Check if texture is nil

## Immediate Actions (TODAY)

### Action 1: Stop Creating Gray Placeholder
Instead of creating gray data, we should:
1. Create our own color render target
2. Actually render the draw calls with textures
3. Capture that rendered output

### Action 2: Create Color Render Target
```cpp
// In IssueDraw, before render pass:
if (!color_targets_[0]) {
  // Create a color target to render into
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setTextureType(MTL::TextureType2D);
  desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  desc->setWidth(1280);
  desc->setHeight(720);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  color_targets_[0] = device->newTexture(desc);
}
```

### Action 3: Fix Texture Size Validation
The 0x0 texture (format k_16_16_16_16) has invalid dimensions:
```cpp
// In IssueDraw texture binding:
if (texture_info.width == 0 || texture_info.height == 0) {
  XELOGW("Invalid texture dimensions {}x{}", texture_info.width, texture_info.height);
  continue;  // Skip invalid textures
}
```

### Action 4: Add Texture Samplers
```cpp
// After binding texture:
MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
MTL::SamplerState* sampler = device->newSamplerState(sampler_desc);
encoder->setFragmentSamplerState(sampler, fetch_slot);
```

## Expected Outcome

With proper implementation, we should see:
1. **The A-Train HX logo** - Large text in the center
2. **Japanese subtitle text** - Below the logo
3. **Blue loading bar** - At the bottom showing "Loading..." in Japanese
4. **Black background** - The rest of the screen

## Current Rendering Pipeline Status

### What's Working:
- ✅ Basic Metal pipeline setup
- ✅ Vertex/pixel shader loading and translation
- ✅ Vertex buffer binding
- ✅ Texture loading from guest memory
- ✅ PNG output generation

### What's Missing:
- ❌ Color buffer capture (getting depth instead)
- ❌ Many texture formats (only 9/60+ supported)
- ❌ Xbox 360 texture tiling/swizzling
- ❌ Texture sampler states
- ❌ EDRAM operations
- ❌ Proper shader texture sampling

## Quick Wins for Next Session

1. **Add k_16_16_16_16 format** - 5 minutes, trace needs it
2. **Fix 0x0 texture size bug** - 10 minutes, validation issue
3. **Add basic sampler states** - 15 minutes, needed for filtering
4. **Try capturing color buffer** - 30 minutes, might show something

## Summary

We've made significant progress with texture loading working, but we're missing:
1. **Texture format support** - Only supporting 15% of Xbox 360 formats
2. **Tiling/swizzling** - Xbox 360 uses special memory layouts
3. **Color buffer capture** - Still getting depth buffer
4. **EDRAM implementation** - Need the 10MB embedded memory simulation

The A-Train HX trace shows textures being loaded (1279x719 main screen, various UI elements) but we can't render them properly yet due to the missing pieces above.