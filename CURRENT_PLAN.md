# Current Plan - Metal Backend Implementation

## Executive Summary

We're debugging why the Metal backend renders pink (magenta) output instead of proper graphics. Through extensive investigation, we've identified that the issue stems from texture binding problems in the Metal Shader Converter pipeline.

## Key Observations So Far

### 1. Metal Shader Converter Architecture
- Uses programmatic vertex fetching (T0/U0 buffers instead of vertex attributes)
- Expects direct resource pointers in top-level argument buffers
- Uses automatic layout mode with IRDescriptorTableEntry structures (24 bytes each)
- Shaders access resources via indices into the top-level argument buffer

### 2. Resource Binding Model
```
Top-Level Argument Buffer (buffer[2]):
├── T0 (SRV) - offset 0, slot 1
├── T1 (SRV) - offset 24, slot 2  
├── CB0 (CBV) - offset 48
├── CB1 (CBV) - offset 72
└── S0 (Sampler) - offset 96, slot 0
```

### 3. Critical Findings

#### ✅ Fixed Issues:
1. **Vertex Buffer Binding** - T0/U0 now correctly bound for programmatic fetch
2. **Direct Resource Binding** - Changed from heap pointers to direct resource pointers
3. **Slot Mapping** - Fixed off-by-one issue (T0→slot 1, T1→slot 2)
4. **Resource Residency** - Added useResource calls for textures
5. **Texture Duplication** - Removed harmful texture reuse logic

#### ❌ Remaining Issues:
1. **Missing Texture Data** - Only 1 texture binding available, shader expects 2
2. **Pink Output** - RGB(255, 0, 255) indicates missing/invalid texture sampling
3. **Fetch Constant 1 Empty** - No texture data at fetch constant 1
4. **Depth/Stencil Issues** - Invalid attachments for some draws

### 4. Texture Binding Analysis

From our debugging:
- **Pixel Shader Expectations**: Needs T0 and T1 textures
- **Available Bindings**: Only 1 texture binding (fetch_constant=0)
- **Heap Slot 0**: Has valid texture from fetch_constant=0
- **Heap Slot 1**: No texture binding, fetch_constant=1 has no data
- **Current Behavior**: T1 falls back to using T0's texture

## Root Cause Hypothesis

The pink output occurs because:
1. Shader samples two textures (T0 and T1) and combines them
2. Both T0 and T1 are getting the same texture (or T1 is invalid)
3. The shader's texture combination logic produces magenta when textures are missing/invalid
4. This is likely a default error color in the shader

## Debug Plan

### Phase 1: Texture Data Investigation
- [ ] Check all 32 fetch constants to find any additional texture data
- [ ] Log texture addresses and formats for all fetch constants
- [ ] Verify if game actually provides 2 textures in different fetch constants
- [ ] Check if texture binding indices are being calculated correctly

### Phase 2: Shader Analysis
- [ ] Examine shader bytecode to understand texture sampling logic
- [ ] Check if shader has fallback paths for missing textures
- [ ] Verify texture coordinate generation
- [ ] Analyze how shader combines T0 and T1

### Phase 3: Texture Upload Verification
- [ ] Confirm texture data is correctly uploaded to GPU
- [ ] Verify texture format conversion (Xbox 360 → Metal)
- [ ] Check texture dimensions and mip levels
- [ ] Test with debug textures of known patterns

### Phase 4: Alternative Solutions
- [ ] Create a default/debug texture for missing slots
- [ ] Try binding different fetch constants to heap slot 1
- [ ] Implement texture aliasing if game reuses textures
- [ ] Check if texture should come from vertex shader output

## Implementation Strategy

### Immediate Actions:
1. **Comprehensive Fetch Constant Scan**
   ```cpp
   for (uint32_t i = 0; i < 32; i++) {
     auto fetch = register_file_->GetTextureFetch(i);
     if (fetch.base_address) {
       // Log this texture's details
     }
   }
   ```

2. **Debug Texture Creation**
   - Create a checkerboard pattern texture
   - Use for missing texture slots
   - Will help identify if issue is missing data vs bad sampling

3. **Enhanced Logging**
   - Log all texture fetch constants at draw time
   - Show texture format details
   - Track texture cache hits/misses

### Long-term Fixes:
1. Implement proper texture binding discovery
2. Handle missing textures gracefully
3. Add texture format validation
4. Improve shader reflection accuracy

## Test Cases

1. **Single Texture Test**: Force both T0 and T1 to use different textures
2. **Debug Texture Test**: Replace missing textures with debug patterns
3. **Format Test**: Verify texture format conversions
4. **Sampling Test**: Check sampler state configuration

## Success Criteria

- No pink/magenta output
- Proper texture rendering
- All textures correctly bound to their slots
- No shader sampling errors

## Next Steps

1. Implement comprehensive fetch constant scanning
2. Create debug texture system
3. Add detailed texture logging
4. Test with modified texture bindings
5. Analyze shader disassembly for texture usage patterns