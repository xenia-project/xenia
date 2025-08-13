# Texture Binding Index Alignment Fix

## Problem
The vertex shader texture binding logic was using a sequential counter (`vs_argument_position`) instead of using the actual heap slot values from the Metal Shader Converter (MSC) reflection data. This caused textures to be bound at wrong descriptor heap slots, leading to incorrect rendering.

## Root Cause
When processing vertex shader textures, the code:
1. Used a sequential counter that incremented for each non-sentinel texture
2. Skipped sentinel values (0xFFFF) which represent framebuffer fetch operations
3. This caused the counter to get out of sync with actual heap slot indices
4. Example: If heap_texture_indices = [0xFFFF, 2, 3], the code would bind textures at slots 0, 1 instead of 2, 3

## Solution
Fixed the vertex shader texture and sampler binding logic to:
1. **Use heap slot values directly** from `vs_heap_texture_indices` array instead of sequential counters
2. **Properly handle sentinel values** (0xFFFF) by skipping them without affecting slot alignment
3. **Added proper vertex shader sampler support** to match the pixel shader implementation

## Code Changes

### 1. Vertex Shader Texture Processing (lines 2412-2427)
**Before:**
```cpp
size_t vs_argument_position = 0;  // Sequential counter
for (size_t i = 0; i < vs_heap_texture_indices.size(); ++i) {
    uint32_t heap_value = vs_heap_texture_indices[i];
    if (heap_value == 0xFFFF) {
        continue;  // Skip sentinel
    }
    uint32_t argument_slot = vs_argument_position;
    vs_argument_position++;  // Increment counter
    // Use argument_slot for binding...
}
```

**After:**
```cpp
for (size_t i = 0; i < vs_heap_texture_indices.size(); ++i) {
    uint32_t heap_slot = vs_heap_texture_indices[i];  // This IS the heap slot
    if (heap_slot == 0xFFFF) {
        continue;  // Skip sentinel
    }
    // Use heap_slot directly for binding...
    bound_textures_by_heap_slot[heap_slot] = metal_texture;
}
```

### 2. Vertex Shader Sampler Processing (lines 2494-2539)
**Before:**
```cpp
// Process vertex shader samplers - for now just skip
// TODO: Implement proper sampler support
XELOGI("Metal IssueDraw: Skipping VS sampler processing for now");
```

**After:**
```cpp
// Process vertex shader samplers - use actual heap slot values from reflection
size_t vs_sampler_binding_index = 0;
for (size_t i = 0; i < vs_heap_sampler_indices.size(); ++i) {
    uint32_t heap_slot = vs_heap_sampler_indices[i];  // This IS the heap slot
    if (heap_slot == 0xFFFF) {
        continue;  // Skip sentinel
    }
    // Create and bind sampler at heap_slot...
    bound_samplers_by_heap_slot[heap_slot] = sampler;
}
```

### 3. Updated All References
Changed all references from `argument_slot` to `heap_slot` in:
- Texture upload error messages
- Texture binding to `bound_textures_by_heap_slot` map
- Debug logging statements

## Impact
This fix ensures that:
1. **Textures are bound at correct heap slots** matching what the shader expects
2. **Sentinel values don't break alignment** between binding indices and heap slots
3. **Vertex shader samplers are properly supported** instead of being skipped
4. **GPU debugger will show textures as "Accessed"** at the correct slots

## Testing
To verify the fix:
1. Run a trace dump with vertex shader textures
2. Check logs for "placing texture at heap slot X" messages
3. Verify heap slot values match the reflection data
4. Use GPU debugger to confirm textures are accessed at correct slots
5. Check output PNG for correct rendering (no pink/magenta areas from missing textures)

## Files Modified
- `/Users/admin/Documents/xenia_arm64_mac/src/xenia/gpu/metal/metal_command_processor.mm`
  - Lines 2412-2491: Vertex shader texture processing
  - Lines 2494-2539: Vertex shader sampler processing (new implementation)

## Related Issues
This was bug #5 in the texture binding system, causing textures to appear at wrong slots in the GPU debugger and potentially causing incorrect rendering with pink/magenta artifacts.