# Metal Primitive Processor - Next Steps

## Current State
We have successfully implemented the Metal primitive processor infrastructure with:
- ✅ Base primitive processor integration
- ✅ Frame-based buffer management
- ✅ Primitive type conversion (rectangle list → triangle strip)
- ✅ Automatic index buffer generation
- ✅ Integration with Metal command processor

## Immediate Next Steps (Priority Order)

### 1. Guest DMA Index Buffer Support (2-3 days)
Currently we see: `Metal IssueDraw: Guest DMA index buffers not yet implemented`

**Implementation:**
```cpp
// In IssueDraw when primitive_processing_result.index_buffer_type == kGuestDMA
case ProcessedIndexBufferType::kGuestDMA:
  // Map guest memory for index buffer
  const void* guest_indices = memory_->TranslatePhysical(
      primitive_processing_result.guest_index_base);
  
  // Create Metal index buffer from guest data
  MTL::Buffer* index_buffer = device->newBuffer(
      guest_indices,
      primitive_processing_result.guest_index_buffer_size,
      MTL::ResourceStorageModeShared);
  
  // Bind and draw
  encoder->drawIndexedPrimitives(...);
```

### 2. Primitive Restart Pre-processing (3-4 days)
Metal always treats 0xFFFF/0xFFFFFFFF as restart indices. We need to handle when Xbox 360 uses these as real vertices.

**Detection:**
```cpp
// Check if primitive restart is disabled
if (!regs.PA_SU_SC_MODE_CNTL.multi_prim_ib_ena) {
  // Scan indices for problematic values
  // If found, process through ReplaceResetIndex16To24/32To24
}
```

**Implementation:**
- Use existing `ReplaceResetIndex16To24()` for 16-bit indices
- Use existing `ReplaceResetIndex32To24()` for 32-bit indices
- Cache processed buffers to avoid re-processing

### 3. Additional Primitive Type Support (2-3 days)
Currently only handling rectangle lists. Need to add:
- Triangle fans → Triangle list
- Quad lists → Triangle list  
- Line loops → Line strip with closing segment
- Polygon mode → Triangle fan

### 4. Performance Optimizations (2 days)
- Implement index buffer caching by content hash
- Pool Metal buffers by size buckets
- Add metrics for conversion overhead

## Testing Strategy

### Test Cases:
1. **Primitive Restart Tests**
   - Games that disable primitive restart
   - Games using 0xFFFF as vertex indices
   - Mixed restart/non-restart draws

2. **Primitive Type Tests**
   - Find traces with each primitive type
   - Verify correct vertex order after conversion
   - Check winding order preservation

3. **Performance Tests**
   - Measure overhead of index pre-processing
   - Profile buffer allocation patterns
   - Monitor memory usage

## Integration with EDRAM (Future)

Once primitive processing is complete, the next major step is EDRAM:
1. Create 10MB Metal buffer for Xbox 360 EDRAM
2. Map EDRAM tiles to render targets
3. Replace placeholder blue texture with real targets
4. Implement resolve operations

## Code Organization

```
metal_primitive_processor.cc
├── Buffer Management
│   ├── RequestHostConvertedIndexBufferForCurrentFrame()
│   ├── Frame cleanup in BeginFrame()
│   └── Buffer pooling (TODO)
├── Index Processing  
│   ├── Primitive restart handling (TODO)
│   ├── Endian conversion
│   └── Caching (TODO)
└── Integration
    ├── Process() called from IssueDraw
    └── Results used for Metal draws
```

## Success Metrics
- All primitive types render correctly
- No geometry corruption from restart indices
- Overhead < 1% of frame time
- Memory usage stable across frames