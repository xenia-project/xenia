# Constant Buffer Staging Fix - CRITICAL Bug #2 RESOLVED

## Issue Summary
The constant buffer staging system was broken - `cb0_staging_` and `cb1_staging_` buffers were written to by `WriteRegister()` during trace replay but never copied to `uniforms_buffer_`, causing shader constants to be zero.

## Root Cause
During trace replay, PM4 `LOAD_ALU_CONSTANT` packets trigger `WriteRegister()` calls that update the staging buffers (`cb0_staging_` for VS, `cb1_staging_` for PS). However, in `IssueDraw()`, the code was reading constants directly from the register file instead of using the staging buffers, resulting in zero constants being sent to shaders.

## The Fix
Modified `metal_command_processor.mm` in `IssueDraw()` to:

1. **Check if staging buffers have non-zero data** (lines 2705-2714, 2753-2762)
   - Scan `cb0_staging_` for VS constants
   - Scan `cb1_staging_` for PS constants
   - Set flags if non-zero data is found

2. **Copy from staging buffers when available** (lines 2717-2737, 2765-2785)
   - If staging buffer has data, use `memcpy` to copy to uniforms buffer
   - Fall back to register file if staging is empty
   - Log the first few constants for debugging

## Memory Layout
The uniforms buffer layout (20KB total):
- **b0** (offset 0): System constants (4KB) - NDC transformation
- **b1** (offset 4KB): VS float constants (4KB) - 256 vec4s
- **b2** (offset 8KB): Bool/Loop constants (4KB) 
- **b3** (offset 12KB): Fetch constants (4KB)
- **b1_ps** (offset 16KB): PS float constants (4KB) - 256 vec4s

## Test Results
Testing with Halo 3 Main Menu trace confirmed:
1. ✅ `WriteRegister()` updates staging buffers with shader constants
2. ✅ `IssueDraw()` detects non-zero data in staging buffers
3. ✅ Staging buffers are copied to uniforms buffer at correct offsets
4. ✅ Shader constants are now properly available to Metal shaders

### Sample Output
```
WriteRegister: Shader constant c0[0] = 0.364 (0x3EBA48E8) - Updated staging
Metal IssueDraw: Using cb0_staging_ for VS constants (has non-zero data)
Metal IssueDraw: Copied VS constants from cb0_staging_ to b1
  VS C0: [0.364, -1.361, -0.053, -2.225]
  VS C1: [-0.581, -0.249, 2.426, 16.641]
Metal IssueDraw: Using cb1_staging_ for PS constants (has non-zero data)
Metal IssueDraw: Copied PS constants from cb1_staging_ to b1_ps
  PS C0: [1.000, 2.000, 1.000, 0.031]
```

## Impact
This fix enables:
- Proper shader constant values for vertex and pixel shaders
- Correct NDC transformation (screen space to clip space)
- Proper rendering of Xbox 360 games that rely on shader constants
- Matrix transformations and other constant-based calculations

## Files Modified
- `src/xenia/gpu/metal/metal_command_processor.mm` (lines 2693-2785)

## Next Steps
With shader constants now working, the Metal backend can properly:
1. Transform vertices from screen space to NDC
2. Apply game-specific transformations
3. Pass color/material constants to pixel shaders
4. Support more complex rendering scenarios

This was CRITICAL bug #2 and it is now RESOLVED.