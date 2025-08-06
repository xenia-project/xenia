# Metal Backend - Detailed Implementation Plan

## Current State (2025-01-06)
- ✅ PNG output working (gray placeholders from depth buffer)
- ❌ No color output (need to capture color render targets)
- ❌ Autorelease pool crashes during shutdown
- ❌ No texture support
- ❌ EDRAM not connected

## Goal: Match A-Train HX Reference Image
The reference shows:
- Black background
- "A-Train HX" logo (texture)
- "High Definition 720P" subtitle
- Game description text
- Blue loading bar with Japanese text
- All UI elements require textures and proper blending

## PHASE 1: Fix Color Buffer Capture (1-2 days)

### Investigation Steps
```bash
# Add debug logging to trace render target creation
1. In SetRenderTargets():
   - Log when color targets are created
   - Log EDRAM addresses
   - Log formats and dimensions

2. In CaptureColorTarget():
   - Log why color_targets_[0] is null
   - Check if render targets are being cleared prematurely
   - Verify timing of capture vs render target lifetime
```

### Code Changes Needed
```cpp
// metal_command_processor.mm
bool MetalCommandProcessor::CaptureColorTarget(uint32_t index, ...) {
  // CURRENT PROBLEM:
  if (!color_targets_[index]) {
    // This always fails, falls back to depth
  }
  
  // INVESTIGATION:
  // 1. Are color targets created in SetRenderTargets?
  // 2. Are they destroyed before capture?
  // 3. Is index 0 the right target?
  
  // POTENTIAL FIX:
  // May need to capture from render_targets_ in cache
  // Or capture directly from current render pass
}
```

### Expected Outcome
- Should see black background with some rendered content
- Even without textures, should see colored geometry

## PHASE 2: Fix Autorelease Pool Crash (1 day)

### Root Cause Analysis
```cpp
// The crash happens here:
MetalCommandProcessor::ShutdownContext() {
  XE_SCOPED_AUTORELEASE_POOL("ShutdownContext");
  // ... cleanup code ...
} // <- CRASH on pool destruction
```

### Potential Solutions
1. **Remove the scoped pool**
   - Test if cleanup works without it
   - Metal-cpp may handle its own pools

2. **Restructure cleanup order**
   - Release Metal objects before pool destruction
   - Clear all caches first

3. **Use NS::AutoreleasePool directly**
   - More control over lifetime
   - Can drain() instead of destroy

### Testing
```bash
# Run with debug flags
OBJC_DEBUG_MISSING_POOLS=YES ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump ...
OBJC_PRINT_POOL_HIGHWATER=YES ...
```

## PHASE 3: EDRAM Implementation (3-5 days)

### Architecture
```
Xbox 360 Rendering Flow:
1. Game draws to EDRAM (10MB on-chip memory)
2. EDRAM tiles are 80x16 pixels
3. Resolve from EDRAM to main memory textures
4. Present from resolved texture

Current Metal Implementation:
- edram_buffer_ exists (10MB)
- Render targets created but not connected to EDRAM
- Need Load/Store operations
```

### Implementation Steps

#### Step 1: LoadRenderTargetsFromEDRAM
```cpp
void MetalRenderTargetCache::LoadRenderTargetsFromEDRAM(
    MTL::CommandBuffer* command_buffer,
    uint32_t edram_base, 
    ColorRenderTarget* color_rt,
    DepthRenderTarget* depth_rt) {
    
  // 1. Calculate EDRAM offset
  size_t offset = edram_base * 5120; // 5KB tiles
  
  // 2. Create blit encoder
  auto* blit = command_buffer->blitCommandEncoder();
  
  // 3. Copy from EDRAM to texture
  if (color_rt) {
    // Handle tiling during copy
    CopyEDRAMToTexture(blit, offset, color_rt->texture);
  }
  
  blit->endEncoding();
}
```

#### Step 2: StoreRenderTargetsToEDRAM
```cpp
void MetalRenderTargetCache::StoreRenderTargetsToEDRAM(
    MTL::CommandBuffer* command_buffer,
    uint32_t edram_base,
    ColorRenderTarget* color_rt,
    DepthRenderTarget* depth_rt) {
    
  auto* blit = command_buffer->blitCommandEncoder();
  
  if (color_rt) {
    // Copy texture back to EDRAM
    CopyTextureToEDRAM(blit, color_rt->texture, offset);
  }
  
  blit->endEncoding();
}
```

#### Step 3: Integration Points
- Call Load before render pass begins
- Call Store after render pass ends
- Handle MSAA resolve during Store

## PHASE 4: Basic Texture Support (1 week)

### Priority Formats
1. **k_DXT1 (BC1)** - Most common, 4bpp
2. **k_DXT5 (BC3)** - UI with alpha, 8bpp  
3. **k_8_8_8_8** - Uncompressed RGBA

### Implementation Plan

#### Step 1: Texture Cache Structure
```cpp
class MetalTextureCache {
  struct TextureEntry {
    MTL::Texture* texture;
    uint32_t guest_address;
    TextureFormat format;
    uint32_t width, height;
    uint64_t hash;  // For invalidation
  };
  
  std::unordered_map<uint64_t, TextureEntry> cache_;
};
```

#### Step 2: Format Conversion
```cpp
MTL::Texture* CreateMetalTexture(const TextureInfo& info) {
  switch (info.format) {
    case k_DXT1:
      // BC1 -> MTLPixelFormatBC1_RGBA
      return CreateCompressedTexture(info, MTL::PixelFormatBC1_RGBA);
      
    case k_DXT5:
      // BC3 -> MTLPixelFormatBC3_RGBA
      return CreateCompressedTexture(info, MTL::PixelFormatBC3_RGBA);
      
    case k_8_8_8_8:
      // RGBA8 -> MTLPixelFormatRGBA8Unorm
      return CreateUncompressedTexture(info, MTL::PixelFormatRGBA8Unorm);
  }
}
```

#### Step 3: Binding in Shaders
```cpp
void BindTextures(MTL::RenderCommandEncoder* encoder) {
  for (int i = 0; i < 32; i++) {
    if (bound_textures_[i]) {
      encoder->setFragmentTexture(bound_textures_[i], i);
    }
  }
}
```

## Testing Strategy

### Incremental Testing
1. **Color Buffer Test**
   - Should immediately show different output
   - Even solid colors would be progress

2. **EDRAM Test**
   - Use RenderDoc/Metal GPU Capture
   - Verify data flows through EDRAM

3. **Texture Test**
   - A-Train logo should appear
   - Start with uncompressed formats

### Debug Commands
```bash
# Enable all Metal validation
METAL_DEVICE_WRAPPER_TYPE=1 \
METAL_DEBUG_ERROR_MODE=5 \
METAL_SHADER_VALIDATION=1 \
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace

# Capture with Xcode
xcrun metal-capture --scope xenia-gpu-metal-trace-dump
```

## Success Metrics

### Phase 1 Success (Color Buffer)
- PNG shows black or colored output (not gray)
- Can distinguish between background and geometry

### Phase 2 Success (Crash Fix)
- Trace dump exits cleanly
- No autorelease pool errors

### Phase 3 Success (EDRAM)
- Render targets properly loaded/stored
- MSAA resolve working

### Phase 4 Success (Textures)
- A-Train HX logo visible
- UI text readable
- Loading bar rendered

## Risk Factors

1. **EDRAM Tiling Complexity**
   - Xbox 360 uses complex tiling scheme
   - May need compute shaders for untiling

2. **Texture Formats**
   - Xbox 360 has many proprietary formats
   - Some may not map directly to Metal

3. **Performance**
   - EDRAM copies add overhead
   - May need optimization later

## Timeline Estimate

- **Week 1**: Color buffer + crash fix
- **Week 2**: EDRAM implementation
- **Week 3**: Basic texture support
- **Total**: 3 weeks to see A-Train HX title screen

This would bring Metal backend to ~50% complete with basic games potentially playable.