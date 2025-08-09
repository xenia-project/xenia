# GPU Capture Analysis Guide

## What to Look For in Xcode GPU Capture

### 1. Find the Render Pipeline States
- Look for "Render Pipeline State" groups in the capture
- We're seeing 5 pipelines (0-4) where only pipeline 0 has valid depth/stencil

### 2. Check the IR_Uniforms Buffer (Critical!)

**Location**: In any draw call, look at the bound buffers section
- Find "Buffer 5" (this is `kIRArgumentBufferUniformsBindPoint`)
- This should be labeled "IR_Uniforms"
- Export this buffer as CSV

**Expected Values at Specific Offsets**:
```
Offset 128 (float 32): 0.00078125    # ndc_scale_x
Offset 132 (float 33): -0.00078125   # ndc_scale_y (negative for Y-flip)
Offset 136 (float 34): 1.0           # ndc_scale_z
Offset 140 (float 35): 0.0           # padding
Offset 144 (float 36): -0.999609     # ndc_offset_x
Offset 148 (float 37): 0.999609      # ndc_offset_y
Offset 152 (float 38): 0.0           # ndc_offset_z
```

**What We're Seeing**: ALL ZEROS! This is the bug.

### 3. Check Vertex Input Buffer

**Location**: Usually Buffer 6 or Buffer 0
- Look for the vertex buffer with position data
- Export as CSV

**Current Issue**:
- Vertices show NDC coordinates: `[-1.003125, 1.001389, 1.0]`
- This suggests CPU pre-transformation is happening
- Should show screen space: `[0-1280, 0-720, z]` if CPU transform disabled

### 4. Check Shader Resources

**Vertex Shader AIR**:
- Look for loads from CB0 at offsets 128-152
- Should see `getelementptr i8, i8 addrspace(2)* %CB0..unpack, i64 136`
- Should see `air.fma` operations applying the transformation

### 5. Check Draw Arguments Buffer

**Location**: Buffer 4 (kIRArgumentBufferDrawArgumentsBindPoint)
- Should contain vertex count, instance count, etc.

## The Core Problem

**Issue 1**: IR_Uniforms buffer shows all zeros instead of NDC constants
- We're setting the values in code (confirmed by logs)
- But they're not reaching the GPU

**Issue 2**: CPU is pre-transforming vertices
- Located at lines 1890-2018 in metal_command_processor.mm
- This needs to be disabled (set `if (false)` condition)

**Issue 3**: Double transformation if both work
- CPU transforms screen→NDC
- Shader also transforms screen→NDC
- Results in incorrect vertices

## Quick Test

1. In Xcode GPU capture, find any draw call
2. Go to "Bound Resources" → "Vertex" → "Buffer 5"
3. Right-click → "View Buffer Contents"
4. Check bytes 128-152 (or floats 32-38)
5. Should see the NDC values above, but currently shows zeros

## Solution Approach

1. **First**: Verify buffer binding is correct (index 5)
2. **Second**: Check if buffer is being cleared after we set values
3. **Third**: Disable CPU viewport transform
4. **Fourth**: Ensure uniforms buffer is marked as resident/used

The key is figuring out why the IR_Uniforms buffer that we're filling with NDC constants is showing as all zeros in the GPU capture.