# Fix for MSC Descriptor Heap Texture Duplication Issue

## Problem
The Metal backend was producing pink/magenta output because it was duplicating the same texture across multiple descriptor heap slots when the shader expected different textures.

## Root Cause
In `metal_command_processor.mm` around lines 2444-2454, when fetch constants 1 and 2 had no texture data, the code was reusing texture from slot 0. This caused:
- Heap slot 0: Texture A
- Heap slot 1: Texture A (duplicate)

But the shader expected:
- T0 to read from heap slot 0
- T1 to read from heap slot 1 with a DIFFERENT texture

## Solution
Instead of duplicating texture 0, we now create unique debug textures for missing slots:

```cpp
// OLD: Duplicating texture
if (argument_slot > 0 && bound_textures_by_heap_slot.find(0) != bound_textures_by_heap_slot.end()) {
    MTL::Texture* texture_0 = bound_textures_by_heap_slot[0];
    bound_textures_by_heap_slot[argument_slot] = texture_0;  // WRONG: Same texture
}

// NEW: Creating unique textures
if (argument_slot > 0) {
    uint32_t debug_size = 64 * (argument_slot + 1);
    MTL::Texture* default_texture = texture_cache_->CreateDebugTexture(debug_size, debug_size);
    bound_textures_by_heap_slot[argument_slot] = default_texture;  // CORRECT: Unique texture
}
```

## Files Modified
1. `src/xenia/gpu/metal/metal_command_processor.mm`:
   - Lines 2444-2464: Fixed texture duplication for missing fetch constants
   - Lines 3025-3035: Removed fallback duplication in pixel shader binding

## Testing
After the fix:
- Descriptor heap has unique textures at each slot
- Shader can correctly sample from different textures
- Output should no longer be pink/magenta

## Verification
Check logs for:
```
Metal IssueDraw: No texture data for argument slot 1, using debug texture 128x128
MSC res-heap: setTexture 0xDIFFERENT at heap slot 1
Resource descriptor heap has 2 valid entries
```

The key indicator of success is seeing DIFFERENT texture addresses at heap slots 0 and 1, not the same address duplicated.