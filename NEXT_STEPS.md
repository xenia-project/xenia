# NEXT_STEPS

## Phase 1: First Non-Black Metal Trace Dump (IN PROGRESS)

### Problem Description

`xenia-gpu-metal-trace-dump` completes successfully and saves a PNG, but the
captured output is fully black. The same trace produces expected output on
Vulkan.

### Root Cause Analysis

1. The output is black because the color render target is still all zeros
   before resolve/copy:
   - `MetalRenderTargetCache::Resolve` logs:
     `MetalResolve SRC_RT before DumpRenderTargets ... first pixels: 00000000`
   - This readback uses a Metal blit into a CPU buffer, so it reflects the real
     render target contents, not the resolve shader.

2. The Metal draw path is issuing draws and building pipelines, but likely
   rasterizes everything outside the active scissor due to a Y-axis mismatch:
   - `MetalCommandProcessor::IssueDraw` computes `viewport_info` via
     `draw_util::GetHostViewportInfo(..., origin_bottom_left=...)`.
   - `draw_util::GetHostViewportInfo` flips `ndc_scale[1]`/`ndc_offset[1]` when
     `origin_bottom_left` is true.
   - Vulkan passes `origin_bottom_left=false` (Vulkan NDC Y is down).
   - D3D12 passes `origin_bottom_left=true` (D3D12 NDC Y is up, needs flip).
   - Metal clip-space matches D3D12 semantics (NDC Y is up), but the Metal
     backend currently passes `origin_bottom_left=false`, treating Metal like
     Vulkan.
   - With a 2048-tall RT and a 720-tall scissor, a Y inversion causes primitives
     intended for the top of the frame to land in the bottom region and be
     fully clipped, resulting in no fragment shader execution and no texture
     sampling ("indirect textures never accessed").

3. Secondary (but important for ARM64 reliability): trace parsing is doing
   misaligned `reinterpret_cast` loads:
   - `src/xenia/gpu/trace_reader.cc`, `src/xenia/gpu/trace_player.cc` and
     `src/xenia/gpu/trace_viewer.cc` cast `trace_ptr` to command structs even
     though compressed blobs make the stream byte-packed, not 4-byte aligned.
   - On ARM64 this is undefined behavior (UBSan reports it) and can lead to
     incorrect parsing under optimization.

### Implementation Checklist

- [ ] Validate capture path: run with `--metal_draw_debug_quad=true` and verify
      the PNG is non-black (confirms RT + presenter copy/capture are working).
- [ ] Fix Metal NDC Y orientation: in
      `src/xenia/gpu/metal/metal_command_processor.cc`, call
      `draw_util::GetHostViewportInfo` with `origin_bottom_left=true` (match
      D3D12 behavior for Metal).
- [ ] Re-run the trace and confirm `MetalResolve SRC_RT...` reports non-zero
      pixels and the PNG is no longer fully black.
- [ ] If rendering appears but is vertically flipped/clipped, compare the Metal
      viewport/scissor setup with `src/xenia/gpu/d3d12/d3d12_command_processor.cc`
      and apply the same mapping rules.
- [ ] Add one-shot diagnostics (then remove): after the first real draw, blit
      read back a few pixels inside the expected scissor region (not only the
      top-left) to confirm rasterization is happening where expected.
- [ ] Fix trace command parsing on ARM64: replace `reinterpret_cast` of
      `trace_ptr` with `std::memcpy` into a local command header, and advance
      by the serialized sizes (keep the file format byte-packed).
- [ ] Validate against Vulkan: run the same trace on Vulkan and compare the
      resulting PNGs visually (and optionally via a simple histogram diff).

### Reference Information

- Trace run (save logs to `scratch/logs/`):
  - `./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tee scratch/logs/trace_metal.log`
  - `./build/bin/Mac/Checked/xenia-gpu-vulkan-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace 2>&1 | tee scratch/logs/trace_vulkan.log`

- Key code locations:
  - `src/xenia/gpu/metal/metal_command_processor.cc` (viewport/scissor + system
    constants)
  - `src/xenia/gpu/metal/metal_render_target_cache.cc` (Resolve + RT readback)
  - `src/xenia/gpu/draw_util.cc` (`origin_bottom_left` flips NDC Y)
  - `src/xenia/gpu/trace_reader.cc`, `src/xenia/gpu/trace_player.cc`,
    `src/xenia/gpu/trace_viewer.cc` (unaligned trace parsing)
4. Emit first 3 vertices in correct order
5. Generate 4th vertex: `v3 = v0 + (v1 - v0) + (v2 - v0)` (mirror across diagonal)
6. Copy interpolators and clip distances using same formula

### Solution Options for Metal

#### Option A: Mesh Shader Emulation (M3+ Only)
- Use Apple's Metal Shader Converter `IRRuntimeNewGeometryEmulationPipeline`
- Converts D3D12-style geometry shaders to Metal mesh shaders
- **Limitation**: Only works on M3+ hardware (Apple GPU Family 9)

#### Option B: Compute Pre-pass Expansion (All Apple Silicon)
- Create Metal compute shader that reads 3 vertices, outputs 4 vertices
- Run compute pass before each rectangle list draw
- Vertex shader reads from expanded buffer
- **Drawback**: Extra GPU pass, buffer management complexity

#### Option C: Vertex Shader Expansion (Recommended)
- Modify shader translation to handle `kRectangleListAsTriangleStrip`
- Primitive processor already generates correct index buffer:
  - Indices: `[i*4+0, i*4+1, i*4+2, i*4+3, UINT32_MAX (restart), ...]`
- Vertex shader must:
  1. Extract guest primitive index: `primitive_idx = vertex_id >> 2`
  2. Extract corner index: `corner = vertex_id & 3`
  3. Fetch all 3 guest vertices for this primitive
  4. For corners 0-2: output corresponding guest vertex
  5. For corner 3: compute `position = -v0 + v1 + v2`

### Why Option C is Recommended

1. **Infrastructure exists**: `PrimitiveProcessor` already generates the index
   buffer with the correct encoding
2. **No extra GPU passes**: Single vertex shader invocation per output vertex
3. **Works on all Apple Silicon**: No mesh shader requirement
4. **Consistent with SPIRV path**: The SPIRV translator has a TODO for this
   same approach (line 1315-1316 in `spirv_shader_translator.cc`)

### Implementation Plan for Option C

#### Step 1: Understand Index Buffer Encoding
The builtin index buffer (`builtin_ib_offset_two_triangle_strips_`) contains:
```
For primitive i:
  indices[i*5+0] = (i << 2) | 0  // corner 0
  indices[i*5+1] = (i << 2) | 1  // corner 1
  indices[i*5+2] = (i << 2) | 2  // corner 2
  indices[i*5+3] = (i << 2) | 3  // corner 3
  indices[i*5+4] = UINT32_MAX    // primitive restart
```

#### Step 2: Shader Translation Changes
Location: Either in `DxbcShaderTranslator` or Metal-specific wrapper

When `host_vertex_shader_type == kRectangleListAsTriangleStrip`:
1. Generate prologue that extracts `primitive_idx` and `corner`
2. Fetch 3 guest vertices from shared memory using guest indices
3. Select output based on corner (0-2 direct, 3 computed)

#### Step 3: Remove Skip Logic
In `metal_command_processor.cc`, remove the early return for
`kRectangleListAsTriangleStrip` once shader support is complete.

### Files to Modify (Metal Backend Only)

| File | Changes |
|------|---------|
| `src/xenia/gpu/metal/metal_command_processor.cc` | Remove skip logic for kRectangleListAsTriangleStrip |
| `src/xenia/gpu/metal/metal_shader.cc` (or new file) | Implement VS expansion shader wrapper |

### Reference Code

**D3D12 Geometry Shader**: `src/xenia/gpu/d3d12/pipeline_cache.cc` lines 2547-2692
**Vulkan Geometry Shader**: `src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc` lines 1499-1764
**SPIRV TODO**: `src/xenia/gpu/spirv_shader_translator.cc` line 1315

---

## Phase 3: Additional Metal Backend Work (FUTURE)

These items are documented for future reference but are lower priority than
Phases 1 and 2.

### Fixed-Function State Parity
- Depth/stencil state and stencil reference
- Blending state and blend constants
- Cull mode / front-face winding
- Depth bias
- Color write masks

### Resource Binding Model
- Stage-separated descriptor tables (VS vs PS)
- Proper bindless or bindful implementation
- Ring buffer for descriptor lifetime

### Point Sprite Expansion
Similar to rectangle lists - may need vertex shader expansion path.

---

## Test Commands

```bash
# Build
./xb build --target=xenia-gpu-metal-trace-dump 2>&1 | tee scratch/logs/build.log
grep -E "error:" scratch/logs/build.log

# Run trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace \
    2>&1 | tee scratch/logs/trace.log

# Check for issues
grep -iE "(error|exception|crash|assert|hang)" scratch/logs/trace.log
```
