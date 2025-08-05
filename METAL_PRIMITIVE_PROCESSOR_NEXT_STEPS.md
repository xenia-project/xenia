# Metal Primitive Processor - Next Steps

## Current State
We have successfully implemented the Metal primitive processor infrastructure with:
- ✅ Base primitive processor integration
- ✅ Frame-based buffer management
- ✅ Primitive type conversion (rectangle list → triangle strip)
- ✅ Automatic index buffer generation
- ✅ Integration with Metal command processor

## Immediate Next Steps (Priority Order)

### 1. ✅ Guest DMA Index Buffer Support (COMPLETED)
**Status**: Implemented and tested successfully!

**What was implemented:**
- Guest memory mapping via `TranslatePhysical()`
- Metal buffer creation from guest index data
- Proper index type handling (16-bit vs 32-bit)
- Debug labeling for buffer tracking
- Cleanup and memory management

**Results:**
- Successfully processing guest DMA index buffers
- Creating Metal buffers directly from Xbox 360 memory
- Drawing indexed primitives with guest data
- No crashes or memory leaks

### 2. ✅ Primitive Restart Pre-processing (COMPLETED)
Metal always treats 0xFFFF/0xFFFFFFFF as restart indices. We need to handle when Xbox 360 uses these as real vertices.

**Status**: Completed! The primitive processor already handles this automatically.

**How it works:**
- The base `PrimitiveProcessor::Process()` method detects when primitive restart is disabled but indices contain restart values
- It automatically converts indices via `ProcessedIndexBufferType::kHostConverted` when needed
- For guest DMA buffers (type `kGuestDMA`), the processor has already verified they don't need conversion
- Metal command processor can use guest DMA buffers directly without additional checks

**Key insight:**
The primitive processor's result type tells us everything:
- `kHostConverted`: Already pre-processed to avoid Metal's hardcoded restart
- `kGuestDMA`: Safe to use directly, no problematic indices
- `kHostBuiltinForAuto`/`kHostBuiltinForDMA`: Generated indices, no restart issues

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