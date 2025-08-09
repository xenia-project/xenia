# Vertex Format Analysis

## Current Vertex Data (Buffer 6)

**Raw data from GPU capture:**
```
Row 0: [-0.5, -0.5, 1.0, 0, 0, 0, 0, 319.5]     // 8 floats, but stride is 7
Row 1: [-0.5, 1.0, 0, 0, 0, 0, 319.5, 719.5]    // Pattern changes
Row 2: [1.0, 0, 0, 0, 0, ...]                    // Incomplete (buffer ends)
```

**Stride**: 28 bytes = 7 floats

## Root Cause: Xbox 360 Vertex Swizzling

### How Xbox 360 Handles Vertices
1. **Source**: Vertex data comes from Xbox 360 guest memory (big-endian format)
2. **Vertex Fetch Constants**: GPU registers describe buffer locations (address, size, stride, endian)
3. **Vertex Fetch Instructions**: Shader contains instructions that specify:
   - Attribute offset in dwords
   - **Component swizzle patterns** (XYZW reordering)
   - Format conversions

### The Problem
Xbox 360 uses non-standard vertex attribute layouts with **component swizzling**:
- Position components are scattered across the vertex (not at offset 0)
- Components are reordered via swizzle patterns in the vertex fetch instruction
- The GPU hardware performs swizzling during vertex fetch

### Current Metal Backend Status
- ✅ Correctly handles endian byte-swapping
- ❌ **NOT handling Xbox 360 vertex swizzle patterns**
- ❌ Components remain in wrong order after endian swap

## Observed Data Patterns

1. **Screen coordinates present**: 319.5, 719.5 are clearly screen space X,Y
2. **Data appears swizzled**: Position likely at indices [6,7,1] or similar pattern
3. **Vertex interpretation**:
   - Indices [6,7]: Screen position X,Y (319.5, 719.5)
   - Other indices: Z, W, texture coords, or padding

## Pipeline Constraint: DXBC → DXIL → Metal

Since we're using:
1. DXBC (original Xbox 360 shader) 
2. dxbc2dxil (Microsoft's converter)
3. metal-shader-converter (Apple's converter)

We **cannot modify the shader** to fix swizzling during compilation.

## Solution: CPU-Side Vertex Processing

The Metal backend must handle swizzling on the CPU before GPU submission:

1. **Parse vertex fetch instructions** to extract swizzle patterns
2. **Reorder vertex components** during buffer creation
3. **Create properly formatted buffers** that match what the Metal shader expects

This matches how D3D12/Vulkan backends handle it - they process swizzles during shader translation, but since we can't modify shaders in our pipeline, we must do it during vertex buffer preparation.

## IR_Uniforms Buffer Status

✅ **WORKING CORRECTLY**
- NDC scale: [0.00625, -0.002778, 1.0] at offset 128
- NDC offset: [-0.996875, 0.998611, 0.0] at offset 144
- These values are correct for 320x720 viewport

## Next Steps

1. Determine actual vertex format from Xbox 360 vertex fetch
2. Fix vertex attribute binding to match actual data layout
3. Verify position is read from correct offset in vertex buffer