# Metal Backend Implementation Plan for Xenia GPU

## Overview
Replace MoltenVK with native Metal backend using Apple's Metal Shader Converter to solve fundamental compatibility issues on Apple Silicon.

## Architecture
```
Xbox 360 Microcode → DxbcShaderTranslator → DXIL → Metal Shader Converter → Metal Library
```

## Implementation Phases

### Phase 1: Metal Shader Converter Integration (4-6 weeks)

#### 1.1 Setup Metal Shader Converter
- [x] Download from https://developer.apple.com/metal/shader-converter/
- [ ] Install to `/opt/metal-shaderconverter/`
- [ ] Link libmetalirconverter.dylib
- [ ] Include metal_irconverter.h and metal_irconverter_runtime.h

#### 1.2 Create Metal Graphics System Structure
```
src/xenia/gpu/metal/
├── metal_graphics_system.h/cc         # Main graphics system
├── metal_command_processor.h/cc       # Command processing
├── metal_shader.h/cc                  # Shader management
├── metal_pipeline_cache.h/cc          # Pipeline state objects
├── metal_buffer_cache.h/cc            # Buffer management
├── metal_texture_cache.h/cc           # Texture management
├── metal_render_target_cache.h/cc     # Render targets
└── metal_presenter.h/cc               # Presentation
```

#### 1.3 DXIL to Metal Translation Infrastructure
```cpp
class MetalShaderTranslator {
  // Convert DXIL bytecode to Metal Library using Metal Shader Converter
  IRObject* ConvertDxilToMetalIR(const std::vector<uint8_t>& dxil_bytecode);
  id<MTLLibrary> CreateMetalLibrary(IRObject* metal_ir);
  
  // Handle Xbox 360 specific features
  void ConfigurePrimitiveRestart();
  void ConfigureVertexFetch();
  void ConfigureResourceBinding();
};
```

### Phase 2: Core Metal Backend (6-8 weeks)

#### 2.1 Metal Graphics System Implementation
```cpp
class MetalGraphicsSystem : public GraphicsSystem {
  id<MTLDevice> device_;
  id<MTLCommandQueue> command_queue_;
  std::unique_ptr<MetalCommandProcessor> command_processor_;
  std::unique_ptr<MetalShaderTranslator> shader_translator_;
};
```

#### 2.2 Primitive Restart Solution
Unlike MoltenVK's always-enabled primitive restart, implement D3D12-style approach:
```cpp
enum class PipelineStripCutIndex {
  kNone,      // No primitive restart
  kFFFF,      // 16-bit index restart value
  kFFFFFFFF   // 32-bit index restart value  
};

// Map to Metal's native draw commands with proper index handling
void DrawIndexedPrimitives(MTLPrimitiveType primitive_type,
                          NSUInteger index_count,
                          MTLIndexType index_type,
                          id<MTLBuffer> index_buffer,
                          NSUInteger index_buffer_offset,
                          PipelineStripCutIndex restart_mode);
```

#### 2.3 Resource Binding Model
Follow Metal Shader Converter's top-level Argument Buffer approach:
```cpp
struct MetalArgumentBuffer {
  // Xbox 360 constant buffers → Metal buffers
  uint64_t constant_buffer_addresses[16];
  
  // Xbox 360 textures → Metal texture handles  
  IRDescriptorTableEntry texture_descriptors[32];
  
  // Xbox 360 samplers → Metal sampler states
  IRDescriptorTableEntry sampler_descriptors[16];
};
```

### Phase 3: Shader Pipeline (4-5 weeks)

#### 3.1 Enhanced DxbcShaderTranslator Integration
Leverage existing DXBC infrastructure:
```cpp
class MetalShader : public DxbcShader {
  class MetalTranslation : public DxbcTranslation {
    // DXIL bytecode from DxbcShaderTranslator
    std::vector<uint8_t> dxil_bytecode_;
    
    // Metal Shader Converter output
    id<MTLLibrary> metal_library_;
    id<MTLFunction> metal_function_;
    
    // Convert DXIL to Metal using Metal Shader Converter
    bool CompileToMetal(IRCompiler* converter);
  };
};
```

#### 3.2 Pipeline State Management
```cpp
class MetalPipelineCache {
  // Metal render pipeline states
  std::unordered_map<uint64_t, id<MTLRenderPipelineState>> render_pipelines_;
  
  // Metal compute pipeline states  
  std::unordered_map<uint64_t, id<MTLComputePipelineState>> compute_pipelines_;
  
  // Handle Xbox 360 pipeline features
  id<MTLRenderPipelineState> GetOrCreateRenderPipeline(
      const RenderPipelineDescription& desc);
};
```

### Phase 4: Memory Management (3-4 weeks)

#### 4.1 Unified Memory Architecture
Leverage Apple Silicon's unified memory:
```cpp
class MetalBufferCache {
  // Shared memory pools for optimal performance
  id<MTLHeap> shared_heap_;
  
  // Xbox 360 GPU memory mapping
  std::unordered_map<uint32_t, id<MTLBuffer>> gpu_buffers_;
  
  // Efficient memory allocation using Metal's placement heaps
  id<MTLBuffer> AllocateBuffer(size_t size, MTLResourceOptions options);
};
```

#### 4.2 Texture Management
```cpp
class MetalTextureCache {
  // Handle Xbox 360 texture formats → Metal pixel formats
  MTLPixelFormat ConvertXenosFormat(uint32_t format);
  
  // Texture array requirements from Metal Shader Converter docs
  id<MTLTexture> CreateTextureArray(const TextureInfo& info);
};
```

### Phase 5: Performance Optimization (3-4 weeks)

#### 5.1 Apple Silicon Features
```cpp
// Tile-based deferred rendering optimization
void OptimizeForTBDR();

// MetalPerformanceShaders integration
void UseMetalPerformanceShadersWhenPossible();

// GPU command buffer optimization
void OptimizeCommandBufferEncoding();
```

#### 5.2 Metal Debugging and Profiling
```cpp
// Metal debug layer integration
void EnableMetalValidation();

// Xcode GPU debugger support
void ConfigureGPUCapture();

// Performance counters
void SetupMetalPerformanceCounters();
```

### Phase 6: Integration and Testing (4-5 weeks)

#### 6.1 Build System Integration
```cmake
# CMakeLists.txt additions
if(APPLE)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
  
  # Link Metal Shader Converter
  target_link_libraries(xenia-gpu 
    ${METAL_FRAMEWORK}
    ${METALKIT_FRAMEWORK}
    /opt/metal-shaderconverter/lib/libmetalirconverter.dylib
  )
endif()
```

#### 6.2 Configuration System
```cpp
// Enable Metal backend for macOS
DEFINE_bool(metal, true, "Use Metal backend on macOS", "GPU");
DEFINE_bool(metal_validation, false, "Enable Metal validation layer", "GPU");
DEFINE_bool(metal_capture, false, "Enable Metal GPU capture", "GPU");
```

## Key Benefits Over MoltenVK

### 1. **Primitive Restart Solved**
- Metal native draw commands with proper index handling
- D3D12-style PipelineStripCutIndex enum approach
- No more PM4_DRAW_INDX backend failures

### 2. **Performance Improvements**
- No SPIR-V → MSL translation overhead
- Direct DXIL → Metal conversion
- Apple Silicon unified memory optimization
- Tile-based deferred rendering utilization

### 3. **Feature Completeness**
- Xbox 360 pointPolygons support through proper Metal primitive types
- All Xbox 360 GPU features mapped to Metal equivalents
- No VK_KHR_portability_subset limitations

### 4. **Development Experience**
- Xcode GPU debugger support
- Metal validation layer integration
- Comprehensive profiling tools

## Implementation Timeline

| Phase | Duration | Key Deliverables |
|-------|----------|------------------|
| 1 | 4-6 weeks | Metal Shader Converter integration |
| 2 | 6-8 weeks | Core Metal backend systems |
| 3 | 4-5 weeks | Shader pipeline implementation |
| 4 | 3-4 weeks | Memory management optimization |
| 5 | 3-4 weeks | Performance tuning |
| 6 | 4-5 weeks | Integration and testing |
| **Total** | **24-32 weeks** | **Complete Metal backend** |

## Technical Requirements

### Dependencies
- macOS 13+ (Metal Shader Converter requirement)
- Xcode 15+ (Metal Shader Converter requirement)
- Apple Silicon or Intel Mac with Metal support
- Metal Shader Converter 2.0+ (for debug information support)

### Performance Targets
- 90%+ compatibility with existing Vulkan backend
- 15-25% performance improvement on Apple Silicon
- Sub-100ms shader compilation times
- Zero primitive restart related crashes

## Risk Mitigation

### Technical Risks
1. **Metal Shader Converter limitations** - Mitigated by comprehensive feature matrix analysis
2. **Xbox 360 GPU compatibility** - Leveraging proven D3D12 backend patterns
3. **Performance regressions** - Extensive benchmarking against Vulkan backend

### Development Risks
1. **Resource allocation** - Phased approach allows for incremental progress
2. **Testing complexity** - Automated testing framework with GPU trace validation
3. **Integration challenges** - Close collaboration with existing GPU team

## Success Metrics

### Functionality
- [ ] xenia-gpu-vulkan-trace-dump executes without crashes
- [ ] All Xbox 360 games that work on D3D12 backend work on Metal backend
- [ ] Complete Xbox 360 GPU feature support

### Performance  
- [ ] Frame times within 5% of D3D12 backend performance
- [ ] Shader compilation under 100ms average
- [ ] Memory usage optimized for Apple Silicon

### Quality
- [ ] Zero known primitive restart issues
- [ ] Metal validation layer clean
- [ ] Xcode GPU debugger functional

This implementation leverages Apple's official Metal Shader Converter to create a robust, high-performance Metal backend that solves all MoltenVK compatibility issues while providing superior performance on Apple Silicon.
