# Detailed Metal Backend Implementation Plan

## Current State Analysis

### Working Components ✅
1. **Shader Pipeline**: DXBC → DXIL → Metal compilation
2. **GPU State Reading**: Viewport, constants, registers
3. **Vertex Buffer Binding**: Guest memory mapping with sizes/strides
4. **Index Buffer Support**: Host-converted and guest DMA
5. **Primitive Processing**: Rectangle lists, primitive restart handling
6. **Basic Command Encoding**: Metal pipeline states and draw calls

### Placeholder/Broken Components ❌
1. **Render Target**: 256x256 blue dummy texture instead of EDRAM
2. **Vertex Data**: No endian conversion (likely showing garbage)
3. **Viewport Transform**: Missing NDC conversion and coordinate fixes
4. **Limited Primitives**: Only basic types supported

## Phase 1: Fix Vertex Data Endianness (1-2 days)

### Problem
Vertex data from Xbox 360 (big-endian) needs conversion to little-endian for Metal. Currently showing garbage geometry.

### Implementation Steps

#### 1.1 Add Vertex Endian Info to Pipeline
```cpp
// In metal_command_processor.cc, after line 370 (vertex buffer binding)
// Add endian info tracking
struct BoundVertexBuffer {
  MTL::Buffer* buffer;
  uint32_t stride;
  xenos::Endian endian;  // From vertex fetch constant
};
```

#### 1.2 Create Endian-Swapped Vertex Buffers
```cpp
// After mapping guest memory, before creating Metal buffer:
if (fetch.endian != xenos::Endian::kNone) {
  // Allocate temporary buffer for swapped data
  void* swapped_data = malloc(size);
  memcpy(swapped_data, guest_data, size);
  
  // Swap based on element size
  uint32_t element_size = GetVertexFormatSize(fetch.format);
  for (uint32_t i = 0; i < vertex_count; i++) {
    uint8_t* element = (uint8_t*)swapped_data + (i * stride);
    SwapVertexElement(element, fetch.format, fetch.endian);
  }
  
  // Create Metal buffer with swapped data
  vertex_buffer = device->newBuffer(swapped_data, size, 
                                   MTL::ResourceStorageModeShared);
  free(swapped_data);
}
```

#### 1.3 Implement Vertex Format Swapping
```cpp
void SwapVertexElement(uint8_t* data, xenos::VertexFormat format, 
                      xenos::Endian endian) {
  switch (format) {
    case xenos::VertexFormat::k_32_FLOAT:
      *(uint32_t*)data = xenos::GpuSwap(*(uint32_t*)data, endian);
      break;
    case xenos::VertexFormat::k_16_16_FLOAT:
      *(uint16_t*)(data + 0) = xenos::GpuSwap(*(uint16_t*)(data + 0), endian);
      *(uint16_t*)(data + 2) = xenos::GpuSwap(*(uint16_t*)(data + 2), endian);
      break;
    // ... handle all formats
  }
}
```

### Testing
- Should see recognizable geometry instead of random triangles
- Vertex positions should make sense (not huge/tiny values)

## Phase 2: EDRAM Integration (1-2 weeks)

### Problem
Currently rendering to dummy texture instead of Xbox 360's 10MB EDRAM.

### Implementation Steps

#### 2.1 Create EDRAM Buffer
```cpp
// In metal_render_target_cache.cc Initialize()
class MetalRenderTargetCache : public RenderTargetCache {
  MTL::Buffer* edram_buffer_;  // 10MB (or scaled)
  
  bool Initialize() {
    uint32_t edram_size = xenos::kEdramSizeBytes * 
        draw_resolution_scale_x() * draw_resolution_scale_y();
    
    edram_buffer_ = device_->newBuffer(
        edram_size, 
        MTL::ResourceStorageModePrivate | 
        MTL::ResourceHazardTrackingModeUntracked);
    edram_buffer_->setLabel(NS::String::string("Xbox360 EDRAM"));
  }
};
```

#### 2.2 Create Render Target Textures
```cpp
// Based on RB_SURFACE_INFO register
struct RenderTargetBinding {
  MTL::Texture* color_texture;
  MTL::Texture* depth_texture;
  uint32_t edram_base;
  xenos::ColorRenderTargetFormat color_format;
  xenos::DepthRenderTargetFormat depth_format;
};

MTL::Texture* CreateRenderTargetTexture(
    uint32_t pitch_tiles, uint32_t height_tiles,
    xenos::ColorRenderTargetFormat format, 
    xenos::MsaaSamples samples) {
  
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(pitch_tiles * 80);  // 80 pixels per tile
  desc->setHeight(height_tiles * 16); // 16 pixels per tile
  desc->setPixelFormat(XenosToMetalFormat(format));
  desc->setTextureType(samples > 1 ? 
      MTL::TextureType2DMultisample : MTL::TextureType2D);
  desc->setSampleCount(1 << samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | 
                 MTL::TextureUsageShaderRead);
  
  return device_->newTexture(desc);
}
```

#### 2.3 EDRAM Tile Mapping
```cpp
// Port from D3D12RenderTargetCache
void ResolveTilesToTexture(MTL::Texture* texture, 
                          uint32_t edram_base,
                          uint32_t pitch_tiles,
                          uint32_t height_tiles) {
  // Use compute shader to copy from EDRAM tiles to texture
  // Handle Xbox 360's tiled memory layout
}
```

#### 2.4 Update Command Processor
```cpp
// Replace dummy texture with real render targets
// In IssueDraw(), before creating render pass:
auto& rt_cache = *render_target_cache_;
RenderTargetBinding rt_binding;
if (rt_cache.GetCurrentBinding(rt_binding)) {
  render_pass->colorAttachments()->object(0)->setTexture(
      rt_binding.color_texture);
  render_pass->depthAttachment()->setTexture(
      rt_binding.depth_texture);
}
```

### Testing
- Should see actual rendering instead of blue screen
- Depth testing should work
- Multiple render targets should be supported

## Phase 3: Viewport & NDC Transform (2-3 days)

### Problem
Xbox 360 and Metal have different coordinate systems and viewport handling.

### Implementation Steps

#### 3.1 Get Viewport Info
```cpp
// Use draw_util helpers
draw_util::ViewportInfo viewport_info;
draw_util::GetHostViewportInfo(
    regs, 
    texture_cache_->draw_resolution_scale_x(),
    texture_cache_->draw_resolution_scale_y(), 
    false,  // origin_bottom_left (Metal uses top-left)
    render_target_width,
    render_target_height,
    true,   // allow_reverse_z
    normalized_depth_control,
    false,  // convert_z_to_float24
    false,  // full_float24_in_0_to_1
    pixel_shader && pixel_shader->writes_depth(),
    viewport_info);
```

#### 3.2 Set Metal Viewport
```cpp
MTL::Viewport metal_viewport;
metal_viewport.originX = viewport_info.left;
metal_viewport.originY = viewport_info.top;
metal_viewport.width = viewport_info.width;
metal_viewport.height = viewport_info.height;
metal_viewport.znear = viewport_info.z_min;
metal_viewport.zfar = viewport_info.z_max;
encoder->setViewport(metal_viewport);
```

#### 3.3 Pass NDC Scale/Offset to Shaders
```cpp
// In shader constants buffer
struct SystemConstants {
  float4 ndc_scale;   // viewport_info.ndc_scale + padding
  float4 ndc_offset;  // viewport_info.ndc_offset + padding
  // ... other constants
};
```

### Testing
- Geometry should appear at correct screen positions
- Aspect ratio should be correct
- Clipping should work properly

## Phase 4: Additional Primitive Types (1 week)

### Problem
Only basic primitive types supported, need triangle fans, line loops, etc.

### Implementation Steps

#### 4.1 Extend Primitive Processor
```cpp
// In metal_primitive_processor.cc
bool MetalPrimitiveProcessor::InitializeBuiltinIndexBuffer() {
  // Allocate buffer for:
  // - Triangle fan indices
  // - Line loop indices  
  // - Quad list indices
  // - Two-triangle strips (already done)
}
```

#### 4.2 Handle Each Type
```cpp
switch (guest_primitive_type) {
  case xenos::PrimitiveType::kTriangleFan:
    // Convert to triangle list
    // Use PrimitiveProcessor::TriangleFanToList
    break;
  case xenos::PrimitiveType::kLineLoop:
    // Convert to line strip with closing segment
    // Use PrimitiveProcessor::LineLoopToStrip
    break;
  case xenos::PrimitiveType::kQuadList:
    // Convert to triangle list
    // Use PrimitiveProcessor::QuadListToTriangleList
    break;
}
```

### Testing
- Each primitive type should render correctly
- Geometry should match reference images

## Phase 5: Optimization & Polish (1 week)

### Performance Optimizations
1. **Buffer Pooling**: Reuse vertex/index buffers
2. **Caching**: Cache endian-swapped vertex data
3. **Batch State Changes**: Minimize pipeline switches

### Quality Improvements
1. **Texture Support**: Basic texture binding
2. **Blend States**: Proper blending modes
3. **Stencil/Depth**: Full depth/stencil testing

## Timeline Summary

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| 1. Vertex Endianness | 1-2 days | Visible geometry |
| 2. EDRAM Integration | 1-2 weeks | Real render targets |
| 3. Viewport Transform | 2-3 days | Correct positioning |
| 4. Additional Primitives | 1 week | Full primitive support |
| 5. Polish | 1 week | Optimized rendering |
| **Total** | **3-4 weeks** | **Complete rendering** |

## Success Criteria

1. **Phase 1**: Can see recognizable 3D geometry (even if wrong position/color)
2. **Phase 2**: Renders to actual framebuffer, not blue dummy
3. **Phase 3**: Geometry appears at correct screen positions
4. **Phase 4**: All Xbox 360 primitive types work
5. **Phase 5**: Performance acceptable, quality matches reference

## Key Files to Modify

1. `metal_command_processor.cc` - Vertex endian swap, viewport
2. `metal_render_target_cache.cc/h` - EDRAM implementation
3. `metal_primitive_processor.cc` - Additional primitive types
4. `metal_buffer_cache.cc` - Buffer pooling/caching
5. `metal_shared_memory.cc` - Vertex data upload