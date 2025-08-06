# Metal GPU Backend Implementation Status & Roadmap

## Executive Summary

The Metal GPU backend is in **early development stage** with basic infrastructure and shader translation pipeline implemented, but **lacks critical Xbox 360 emulation features** required for functional game rendering. Current implementation is approximately **25% complete** compared to working Vulkan and D3D12 backends.

## Current Implementation Analysis

### ✅ What is Actually Implemented (25% Complete)

#### 1. **Basic Infrastructure**
- Metal device and command queue initialization
- Command processor architecture framework
- Cache system structure (buffer, texture, render target, pipeline caches)
- Vertex data endian swapping
- Basic vertex buffer binding from Xbox 360 guest memory

#### 2. **Shader Translation Pipeline** 
- **Xbox 360 → DXBC → DXIL → Metal** conversion chain implemented
- DXBC shader translator integration
- DXBC to DXIL converter with Wine integration  
- Metal Shader Converter integration for DXIL → Metal
- Metal library and function creation

#### 3. **Partial Pipeline Support**
- Pipeline cache with proper hashing
- Basic shader lookup and Metal pipeline creation
- Simplified blend state configuration
- Basic vertex descriptor framework

#### 4. **Basic Resource Management**
- Dynamic vertex/index buffer allocation (64MB vertex, 16MB index)
- Guest memory translation and copying
- Basic Metal texture creation
- Simple format conversion mapping

### ❌ Critical Missing Functionality (75% Incomplete)

#### 1. **EDRAM Simulation - MISSING ENTIRELY**
```cpp
// Current status: COMPLETELY MISSING
edram_rov_used = false  // Hardcoded to disabled
// TODO: Restore EDRAM state from snapshot (line 157)
```

**Required Implementation:**
- 10MB EDRAM buffer creation and management
- Tile addressing and ownership transfer system  
- Render target aliasing support
- EDRAM snapshot save/restore functionality
- Metal equivalent of Raster Order Groups for pixel-perfect accuracy

#### 2. **Swapchain/Presentation - STUB ONLY**
```cpp
void MetalCommandProcessor::IssueSwap(...) {
  // TODO: Implement Metal swapchain presentation
  XELOGW("Metal IssueSwap not implemented");  // STUB
}
```

**Required Implementation:**
- CAMetalLayer integration for presentation
- Drawable management and frame synchronization
- Blit operations for final framebuffer presentation
- Gamma correction pipeline

#### 3. **Copy Operations - STUB ONLY**
```cpp
bool MetalCommandProcessor::IssueCopy() {
  // TODO: Issue Metal copy commands  
  XELOGW("Metal IssueCopy not implemented");  // STUB
  return false;
}
```

**Required Implementation:**
- EDRAM resolve operations (render target → shared memory)
- Format conversion during copies
- Texture-to-texture copy operations
- Scaling and filtering during copies

#### 4. **Xbox 360 Format Support - SEVERELY LIMITED**

**Current Status:** Only 9/50+ Xbox 360 formats implemented
- Missing: Most compressed formats (DXT variants, 3Dc, CTX1)
- Missing: Xbox 360-specific formats (signed formats, gamma formats)
- Missing: Proper depth format handling (24-bit depth)
- Missing: Texture format conversion shaders

#### 5. **Advanced GPU Features - NOT IMPLEMENTED**
- **Compute pipelines**: Returns error immediately
- **MSAA support**: Framework exists but no implementation
- **Tessellation**: No tessellation shader support
- **Memory export**: No vertex shader memory writing
- **Predicated rendering**: No Xbox 360 predicate support

## Comparison with Working Backends

### Metal vs D3D12 (Reference Implementation)
| Feature | D3D12 | Metal | Gap |
|---------|-------|-------|-----|
| EDRAM Simulation | ✅ Dual-path (ROV + RTV) | ❌ Not implemented | **CRITICAL** |
| Xbox 360 Shader Support | ✅ Complete microcode translation | ⚠️ Basic translation only | **MAJOR** |
| Format Support | ✅ All 50+ Xbox 360 formats | ❌ Only 9 basic formats | **CRITICAL** |
| Copy Operations | ✅ Full resolve pipeline | ❌ Stub only | **CRITICAL** |
| Presentation | ✅ Full swapchain + gamma | ❌ Stub only | **CRITICAL** |
| MSAA | ✅ 2x/4x with proper patterns | ❌ Not implemented | **MAJOR** |
| Performance | ✅ Bindless + optimization | ❌ Basic resource management | **MAJOR** |

### Metal vs Vulkan (Production Ready)
| Feature | Vulkan | Metal | Gap |
|---------|--------|-------|-----|
| EDRAM Simulation | ✅ Fragment interlock + render targets | ❌ Not implemented | **CRITICAL** |
| Shared Memory | ✅ 512MB buffer with barriers | ⚠️ Basic translation only | **MAJOR** |
| Texture Cache | ✅ VMA + format conversion | ⚠️ Basic caching only | **MAJOR** |
| Pipeline Management | ✅ SPIR-V + caching | ⚠️ Basic MSL translation | **MAJOR** |
| Resource Barriers | ✅ Automatic state tracking | ❌ Not implemented | **MAJOR** |

## Why Current Implementation Can't Run Xbox 360 Games

### 1. **No EDRAM = No Xbox 360 Graphics**
Xbox 360 GPU architecture is fundamentally built around 10MB of embedded DRAM that serves as both framebuffer and intermediate storage. Without EDRAM simulation:
- Render targets cannot be properly aliased
- Resolve operations fail (render target → texture)
- Memory export operations don't work
- Tiled rendering patterns break

### 2. **No Presentation = No Visual Output**
Even if rendering worked, there's no way to display the final image without proper swapchain implementation.

### 3. **Limited Format Support = Texture Corruption**
Xbox 360 games rely heavily on compressed textures and specialized formats. With only 9/50+ formats supported, most textures will be corrupted or missing.

### 4. **No Copy Operations = Broken Post-Processing**
Xbox 360 games extensively use resolve and copy operations for post-processing effects, depth-of-field, bloom, etc. Without copy support, these effects fail completely.

## Realistic Implementation Roadmap

### Phase 1: Foundation (3-6 months) 
**Goal**: Get basic rendering working with simple test cases

#### 1.1 EDRAM Buffer Implementation
- [ ] Create 10MB Metal buffer for EDRAM simulation
- [ ] Implement basic tile addressing (80 tiles × 16 samples)
- [ ] Add EDRAM usage tracking and barriers
- [ ] Create basic render target aliasing system

#### 1.2 Presentation Pipeline  
- [ ] CAMetalLayer integration
- [ ] Basic drawable management
- [ ] Simple blit operations (EDRAM → swapchain)
- [ ] Frame synchronization

#### 1.3 Copy Operations Framework
- [ ] Basic resolve operations (render target → EDRAM)
- [ ] Simple format conversions
- [ ] Texture copy implementations

**Success Criteria**: Simple colored triangles render and display

### Phase 2: Format Support (2-4 months)
**Goal**: Support textures and render targets properly

#### 2.1 Texture Format Implementation
- [ ] DXT1/DXT3/DXT5 compressed texture support
- [ ] Xbox 360 signed/unsigned format variants
- [ ] Endian swapping for texture data
- [ ] Format conversion compute shaders

#### 2.2 Render Target Formats
- [ ] All Xbox 360 color render target formats
- [ ] 24-bit depth format handling  
- [ ] Gamma space conversion support
- [ ] MSAA format variants

**Success Criteria**: Textured geometry renders correctly

### Phase 3: Advanced Features (4-8 months)
**Goal**: Support complex Xbox 360 rendering techniques

#### 3.1 MSAA Implementation
- [ ] 2x MSAA support (emulated as 4x on Metal)
- [ ] Proper sample pattern generation
- [ ] MSAA resolve operations
- [ ] Sample mask and coverage support

#### 3.2 Advanced Rendering
- [ ] Memory export from vertex shaders
- [ ] Predicated rendering support
- [ ] Complex primitive types (rectangles, quads)
- [ ] Tessellation shader integration

#### 3.3 Performance Optimization
- [ ] Metal argument buffers for bindless-like behavior
- [ ] Persistent pipeline state caching
- [ ] Efficient memory management with heaps
- [ ] Multi-threaded command encoding

**Success Criteria**: Most Xbox 360 games achieve basic functionality

### Phase 4: Compatibility & Polish (2-4 months)
**Goal**: Production-ready Xbox 360 game compatibility

#### 4.1 Edge Case Handling
- [ ] Complex EDRAM aliasing patterns
- [ ] Advanced blend state support
- [ ] Depth/stencil operation accuracy
- [ ] Rare texture format support

#### 4.2 Performance Tuning
- [ ] GPU profiling and optimization
- [ ] Memory bandwidth optimization  
- [ ] Command submission optimization
- [ ] Resource pooling and recycling

**Success Criteria**: High compatibility with Xbox 360 game library

## Current Development Priority

### Immediate Priorities (Next 1-2 months)
1. **EDRAM Buffer Creation** - Critical foundation requirement
2. **Basic Presentation Pipeline** - Need to see visual output
3. **Copy Operations Framework** - Required for resolve operations
4. **DXT Texture Support** - Most common Xbox 360 texture format

### Dependencies
- Metal Shader Converter framework (already working)
- Wine + DXBC2DXIL pipeline (already working)
- Autorelease pool management (completed)
- Basic shader translation (completed)

## Status: Early Development

**Completion**: ~25% of required functionality
**Timeline to Basic Functionality**: 6-12 months of focused development
**Timeline to Production Ready**: 12-24 months of focused development

The Metal backend has solid architectural foundations and working shader translation, but requires substantial additional implementation to support Xbox 360 emulation. The critical blocker is EDRAM simulation - without this core Xbox 360 GPU feature, the backend cannot progress beyond basic shader compilation tests.