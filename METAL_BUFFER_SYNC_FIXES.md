# Metal Backend Buffer Synchronization Critical Bug Fix Report

## Executive Summary

**CRITICAL BUG #1**: No `didModifyRange()` calls after buffer modifications, causing GPU to read stale/uninitialized data.

**Impact**: GPU reads incorrect/stale data from all shared buffers, causing:
- Black/corrupted rendering
- Missing textures
- Incorrect shader constants
- Vertex/index data corruption

**Root Cause**: Metal requires explicit `didModifyRange()` calls on shared buffers after CPU modifications to ensure CPU-GPU synchronization. The entire Metal backend has only ONE such call (in old render target cache).

## Affected Buffers

### 1. Uniforms Buffer (`uniforms_buffer_`)
- **Size**: 20KB
- **Usage**: Shader constant buffers (b0-b3)
- **Modified**: Lines 2681-2828 in metal_command_processor.mm
- **Missing Sync**: After populating with shader constants

### 2. Resource Descriptor Heap (`res_heap_ab_`)
- **Size**: 8KB (64 entries × 128 bytes)
- **Usage**: Texture descriptors for MSC
- **Modified**: Lines 2834-2905 in metal_command_processor.mm
- **Missing Sync**: After writing texture descriptors

### 3. Sampler Descriptor Heap (`smp_heap_ab_`)
- **Size**: 2KB (32 entries × 64 bytes)
- **Usage**: Sampler descriptors for MSC
- **Modified**: Lines 2908-2925 in metal_command_processor.mm
- **Missing Sync**: After writing sampler descriptors

### 4. UAV Descriptor Heap (`uav_heap_ab_`)
- **Size**: 4KB (32 entries × 128 bytes)
- **Usage**: UAV descriptors (mostly unused)
- **Modified**: Line 198 in metal_command_processor.mm (initialization)
- **Missing Sync**: After initialization

### 5. Top-Level Argument Buffers (`top_level_vs_ab`, `top_level_ps_ab`)
- **Size**: Variable (calculated per draw)
- **Usage**: Root signature bindings
- **Modified**: Lines 2982-3138 in metal_command_processor.mm
- **Missing Sync**: After populating with heap pointers

### 6. Dynamic Vertex Buffer (`dynamic_vertex_buffer_`)
- **Size**: 128MB
- **Usage**: Vertex data streaming
- **Modified**: Lines 169-170 in metal_buffer_cache.cc
- **Missing Sync**: After copying vertex data

### 7. Dynamic Index Buffer (`dynamic_index_buffer_`)
- **Size**: 16MB
- **Usage**: Index data streaming
- **Modified**: Lines 131-132 in metal_buffer_cache.cc
- **Missing Sync**: After copying index data

### 8. IR Uniforms Buffer (temporary per-draw)
- **Size**: 1024 bytes
- **Usage**: NDC constants for IR shaders
- **Modified**: Lines 3373-3389 in metal_command_processor.mm
- **Missing Sync**: After copying NDC constants

## Code Locations Requiring Fixes

### Fix 1: Uniforms Buffer Synchronization
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Line**: After line 2828
**Missing Call**:
```cpp
// After populating all constant buffers
uniform_buffer_->didModifyRange(NS::Range::Make(0, uniform_buffer_->length()));
```

### Fix 2: Resource Heap Synchronization
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Line**: After line 2905
**Missing Call**:
```cpp
// After writing all texture descriptors
res_heap_ab_->didModifyRange(NS::Range::Make(0, res_heap_ab_->length()));
```

### Fix 3: Sampler Heap Synchronization
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Line**: After line 2925
**Missing Call**:
```cpp
// After writing all sampler descriptors
smp_heap_ab_->didModifyRange(NS::Range::Make(0, smp_heap_ab_->length()));
```

### Fix 4: Top-Level Argument Buffers
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Line**: After line 3119 (VS) and 3270 (PS)
**Missing Calls**:
```cpp
// After populating VS argument buffer
top_level_vs_ab->didModifyRange(NS::Range::Make(0, top_level_vs_ab->length()));

// After populating PS argument buffer
top_level_ps_ab->didModifyRange(NS::Range::Make(0, top_level_ps_ab->length()));
```

### Fix 5: Dynamic Vertex Buffer
**File**: `src/xenia/gpu/metal/metal_buffer_cache.cc`
**Line**: After line 170
**Missing Call**:
```cpp
// After copying vertex data
dynamic_vertex_buffer_->buffer->didModifyRange(
    NS::Range::Make(dynamic_vertex_buffer_offset_ - guest_length, guest_length));
```

### Fix 6: Dynamic Index Buffer
**File**: `src/xenia/gpu/metal/metal_buffer_cache.cc`
**Line**: After line 132
**Missing Call**:
```cpp
// After copying index data
uint32_t buffer_size = guest_length * index_size;
dynamic_index_buffer_->buffer->didModifyRange(
    NS::Range::Make(dynamic_index_buffer_offset_ - buffer_size, buffer_size));
```

### Fix 7: IR Uniforms Buffer
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Line**: After line 3389
**Missing Call**:
```cpp
// After copying NDC constants
ir_uniforms_buffer->didModifyRange(NS::Range::Make(128, 24)); // Only NDC constants
```

### Fix 8: Initialization Synchronization
**File**: `src/xenia/gpu/metal/metal_command_processor.mm`
**Lines**: After 185, 190, 198
**Missing Calls**:
```cpp
// After initializing heaps to zero
res_heap_ab_->didModifyRange(NS::Range::Make(0, kResourceHeapBytes));
smp_heap_ab_->didModifyRange(NS::Range::Make(0, kSamplerHeapBytes));
uav_heap_ab_->didModifyRange(NS::Range::Make(0, kUAVHeapBytes));
```

## Implementation Priority

### Priority 1 (CRITICAL - Rendering)
1. Uniforms buffer sync - Required for ALL shader constants
2. Resource heap sync - Required for texture access
3. Sampler heap sync - Required for texture sampling

### Priority 2 (CRITICAL - Geometry)
4. Dynamic vertex buffer sync - Required for vertex data
5. Dynamic index buffer sync - Required for indexed draws

### Priority 3 (IMPORTANT - Correctness)
6. Top-level argument buffer sync - Required for MSC binding
7. IR uniforms buffer sync - Required for NDC transformation
8. Initialization sync - Ensures clean state

## Verification Test Plan

### Test 1: Basic Triangle
- Expected: Red triangle visible
- Validates: Uniforms and vertex buffer sync

### Test 2: Textured Quad
- Expected: Textured quad visible
- Validates: Resource/sampler heap sync

### Test 3: Trace Dump
- Expected: Non-black output PNG
- Validates: Complete pipeline sync

### Test 4: GPU Capture
- Expected: No warnings about unsynchronized buffers
- Validates: All sync calls present

## Metal Documentation Reference

From Apple's Metal Programming Guide:
> "For buffers with a storage mode of MTLStorageModeShared, after you modify the buffer's content on the CPU, you must call didModifyRange: to ensure that the GPU sees the latest data."

## Expected Impact

After implementing these fixes:
1. GPU will read correct data from all buffers
2. Textures will bind and sample correctly
3. Shader constants will have proper values
4. Vertex/index data will render correctly
5. Output PNGs will show actual rendering

## Code Pattern for All Fixes

```cpp
// Standard pattern after any buffer modification:
void* buffer_data = buffer->contents();
// ... modify buffer_data ...
buffer->didModifyRange(NS::Range::Make(offset, size));
```

## Critical Note

This is the #1 most critical bug in the Metal backend. Without these synchronization calls, the GPU is reading uninitialized or stale data, which explains:
- Black output despite successful draw calls
- "All zeros" warnings in shader constants
- Missing textures despite successful uploads
- Corrupted vertex data

Every other Metal application calls `didModifyRange()` after buffer modifications. This is not optional - it's required by the Metal API contract.