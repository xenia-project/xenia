# Current Metal Backend Implementation Plan

## Immediate Goal: Enable Xcode Frame Capture
**Target**: Get Metal trace dump to produce capturable frames in Xcode's GPU debugger

## Recent Achievements (This Session)
1. ✅ **EDRAM Buffer** - Created 10MB buffer for Xbox 360's embedded DRAM
2. ✅ **Presentation Pipeline** - Implemented CAMetalLayer frame management
3. ✅ **Resolve Framework** - Added copy/resolve operation structure
4. ✅ **Platform Guards** - Cleaned up unnecessary conditional compilation

## Current State
The Metal backend successfully:
- Loads Xbox 360 GPU traces
- Converts shaders (Xbox 360 → DXBC → DXIL → Metal)
- Processes draw commands
- Creates command buffers

But it **doesn't yet**:
- Create MTLRenderPipelineState objects
- Encode actual draw calls
- Bind resources to the pipeline

## Next Implementation Steps (2-3 Sessions to Frame Capture)

### Session 1: Complete Pipeline State Creation
**Files to modify**: `metal_pipeline_cache.cc/h`

1. **Create MTLRenderPipelineDescriptor**
   ```cpp
   MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
   descriptor->setVertexFunction(vertex_shader->GetMetalFunction());
   descriptor->setFragmentFunction(pixel_shader->GetMetalFunction());
   ```

2. **Configure Vertex Descriptor**
   - Parse Xbox 360 vertex fetch constants
   - Map to Metal vertex attribute descriptors
   - Set up vertex buffer layouts

3. **Set Render State**
   - Configure color attachments from render target cache
   - Set blend state from Xbox 360 registers
   - Configure depth/stencil state

4. **Create Pipeline State Object**
   ```cpp
   NS::Error* error = nullptr;
   pipeline_state = device->newRenderPipelineState(descriptor, &error);
   ```

### Session 2: Implement Render Command Encoding
**Files to modify**: `metal_command_processor.cc`

1. **Create Render Command Encoder**
   ```cpp
   // In IssueDraw after pipeline state creation
   MTL::RenderPassDescriptor* render_pass = render_target_cache_->GetRenderPassDescriptor();
   MTL::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass);
   ```

2. **Bind Pipeline and Resources**
   ```cpp
   encoder->setRenderPipelineState(pipeline_state);
   encoder->setVertexBuffer(vertex_buffer, offset, 0);
   encoder->setFragmentTexture(texture, 0);
   ```

3. **Issue Draw Call**
   ```cpp
   if (indexed) {
     encoder->drawIndexedPrimitives(primitive_type, index_count, index_type, index_buffer, 0);
   } else {
     encoder->drawPrimitives(primitive_type, vertex_start, vertex_count);
   }
   encoder->endEncoding();
   ```

### Session 3: Wire Up Render Targets
**Files to modify**: `metal_render_target_cache.cc`

1. **Create Actual Render Target Textures**
   - When SetRenderTargets is called, create Metal textures
   - Configure texture descriptors with proper formats
   - Set up MSAA if needed

2. **Connect to Render Pass Descriptor**
   - Set color/depth attachments to actual textures
   - Configure load/store actions properly
   - Handle clear operations

3. **Implement Basic Resolve**
   - Copy from render target to EDRAM buffer
   - Basic format conversion if needed

## Testing Strategy

### Phase 1: Verify Pipeline Creation
1. Run trace dump with logging
2. Confirm pipeline states are created without errors
3. Check shader compilation succeeds

### Phase 2: Capture in Xcode
1. Run trace dump under Xcode
2. Use GPU Frame Capture
3. Verify command buffer structure
4. Check resource bindings

### Phase 3: Debug Rendering
1. Examine vertex data in capture
2. Verify shader execution
3. Check render target outputs
4. Debug any rendering issues

## Success Metrics

### Milestone 1: Pipeline State Creation ✓
- No Metal validation errors
- Pipeline states cached properly
- Shaders compile successfully

### Milestone 2: Command Encoding ✓
- Draw calls appear in Xcode capture
- Resources properly bound
- No encoder errors

### Milestone 3: Visual Output ✓
- Render targets contain data
- Basic geometry visible
- Can step through draw calls in debugger

## Known Challenges

1. **Vertex Format Mapping**
   - Xbox 360 formats don't map 1:1 to Metal
   - Need format conversion for some types

2. **Primitive Restart**
   - Metal always uses 0xFFFF/0xFFFFFFFF
   - Already handled by primitive processor

3. **Coordinate Systems**
   - Xbox 360 uses different conventions
   - May need viewport/projection adjustments

## File Organization

### Core Implementation Files
- `metal_command_processor.cc` - Main command processing and draw calls
- `metal_pipeline_cache.cc` - Pipeline state creation and caching
- `metal_render_target_cache.cc` - Render target and EDRAM management
- `metal_shader.cc` - Shader translation and compilation
- `metal_buffer_cache.cc` - Vertex/index buffer management
- `metal_texture_cache.cc` - Texture resource management

### Support Files
- `metal_presenter.cc` - CAMetalLayer presentation
- `metal_primitive_processor.cc` - Primitive conversion
- `dxbc_to_dxil_converter.cc` - Shader translation pipeline

## Development Environment

### Tools Required
- Xcode 15+ with Metal debugging tools
- Metal System Trace for performance analysis
- Metal Shader Converter (installed at `/opt/metal-shaderconverter/`)
- Wine for DXBC→DXIL conversion

### Debug Commands
```bash
# Run trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace

# Run with Metal validation
MTL_DEBUG_LAYER=1 ./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump [trace_file]

# Capture with Xcode
# 1. Open Xcode
# 2. Debug → Attach to Process → xenia-gpu-metal-trace-dump
# 3. Debug → Capture GPU Frame
```

## Timeline

- **Today**: Committed EDRAM, presentation, and resolve framework
- **Next Session**: Complete pipeline state creation (2-3 hours)
- **Following Session**: Implement command encoding (2-3 hours)
- **Final Session**: Wire up render targets and debug (2-3 hours)
- **Result**: Xcode frame capture working, basic rendering visible

## Notes

The architecture is solid and the hard parts (shader translation) are working. We just need to connect the final pieces of the rendering pipeline. Once we can capture frames in Xcode, debugging and optimization will be much easier.