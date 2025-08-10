# Xenia Metal Backend Debug Plan - FINAL DIAGNOSIS

## 🎯 ROOT CAUSE IDENTIFIED: Empty Descriptor Heaps

### The Complete Issue Chain:
1. ✅ Render targets are properly created and bound (320x720, 1280x720)
2. ✅ Shaders are compiled and execute correctly
3. ✅ Textures are found and validated (1280x720, 1024x256, etc.)
4. ❌ **Descriptor heaps are EMPTY** - textures never make it to GPU
5. ❌ Shaders sample from empty texture slots → Metal returns pink (255,0,255)
6. ✅ Pink color is captured correctly from render targets

### Critical Evidence:
```
❌ Empty descriptor heaps detected:
w> F8000004 Resource descriptor heap is empty!
w> F8000004 Sampler descriptor heap is empty!

✓ Textures ARE being found:
i> F8000004 Metal IssueDraw: Found valid texture at fetch constant 0

❌ Textures found but NOT uploaded:
i> F8000004 Metal IssueDraw: Prepared 0 textures and 0 samplers for argument buffer
```

## The Real Problem

The issue is NOT:
- ❌ Render target management (working correctly)
- ❌ Shader compilation (working correctly)
- ❌ Texture loading (textures are found)
- ❌ Hardcoded pink in shaders (not present)

The issue IS:
- ✅ **Descriptor heaps remain empty despite finding textures**
- ✅ **Textures never get uploaded to argument buffers**
- ✅ **Shaders sample from empty slots → pink default**

## Solution Plan

### Phase 1: Fix Descriptor Heap Population 🔧 CRITICAL

The problem is in `MetalCommandProcessor::PopulateDescriptorHeaps()`:
```cpp
// Current broken flow:
1. Find texture at fetch constant
2. Try to add to descriptor heap
3. FAIL: Heap remains empty
4. Pass empty heap to shader
```

#### Fix Implementation:
```cpp
// In MetalCommandProcessor::IssueDraw() around line 2900:
if (texture_for_slot) {
  // CURRENT CODE: Just logs but doesn't add to heap
  XELOGI("Found texture for slot {}", slot);
  
  // MISSING: Actually add to descriptor heap!
  IRDescriptorTableEntry entry;
  entry.gpuVA = texture_gpu_address;
  entry.textureViewID = texture_for_slot;
  entry.metadata = packed_metadata;
  
  // Write to heap at correct offset
  memcpy(res_heap_ab_ + slot * sizeof(IRDescriptorTableEntry), 
         &entry, sizeof(entry));
}
```

### Phase 2: Verify Argument Buffer Setup

1. **Check heap allocation**:
   - Ensure res_heap_ab_ is allocated
   - Verify size is sufficient for all texture slots
   - Confirm alignment requirements

2. **Fix useResource calls**:
   ```cpp
   // Must call for EVERY texture in heap
   encoder->useResource(texture_for_slot, MTL::ResourceUsageSample);
   ```

3. **Bind heaps to encoder**:
   ```cpp
   // Ensure heaps are bound at correct indices
   encoder->setFragmentBuffer(res_heap_ab_, 0, kIRArgumentBufferBindPoint);
   encoder->setVertexBuffer(res_heap_ab_, 0, kIRArgumentBufferBindPoint);
   ```

### Phase 3: Test and Validate

1. **Add validation logging**:
   ```cpp
   XELOGI("Heap contents after population:");
   for (int i = 0; i < num_entries; i++) {
     IRDescriptorTableEntry* entry = (IRDescriptorTableEntry*)(res_heap_ab_ + i * 24);
     XELOGI("  Slot {}: gpuVA={:016x}, textureViewID={:p}", 
            i, entry->gpuVA, entry->textureViewID);
   }
   ```

2. **Verify in shader**:
   - Check T0/T1 texture access returns valid data
   - Confirm sampling returns actual texture colors

## Success Criteria

- ✅ Descriptor heaps contain texture entries
- ✅ "Prepared N textures" shows N > 0
- ✅ No "heap is empty" warnings
- ✅ Shaders sample real texture data
- ✅ Output shows game graphics, not pink

## Implementation Checklist

- [ ] Fix PopulateDescriptorHeaps to actually write entries
- [ ] Ensure heap memory is properly allocated
- [ ] Add useResource for all textures
- [ ] Bind heaps at correct buffer indices
- [ ] Add comprehensive heap validation
- [ ] Test with trace dump
- [ ] Verify proper graphics output

## Code Locations

- **Descriptor heap population**: `metal_command_processor.mm:2800-3000`
- **Heap allocation**: `metal_command_processor.mm:BeginSubmission()`
- **Argument buffer binding**: `metal_command_processor.mm:IssueDraw()`
- **Texture lookup**: `metal_command_processor.mm:2400-2500`

## Final Notes

This is the last major blocker for basic rendering. Once descriptor heaps work:
1. Textures will reach shaders
2. Pink output will be replaced with actual game graphics
3. Can move on to fixing smaller issues (depth, blending, etc.)