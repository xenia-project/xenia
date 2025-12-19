# NEXT_STEPS

## Phase 1: Texture Semantics Parity (Halo 3) (IN PROGRESS)

### Problem Description
Real-game traces (notably Halo 3) show deterministic visual corruption that
matches texture data interpretation bugs (pitch/format/endian/alpha/depth), not
geometry/rasterization problems.

### Root Cause Analysis
- The Metal CPU fallback texture path previously inferred row pitch and slice
  strides from width, which is incorrect for Xenos (guest pitch is explicit).
- Metal GPU texture loading previously only supported a narrow subset
  (32bpp base level), leaving most formats (DXT/DXN/packed 16bpp) to the CPU
  fallback (which does not decode/compress correctly).
- Host swizzles were treated as always RGBA (incorrect for single/dual-channel
  formats and DXN).

### Implemented (This Phase)
- [x] Honor `TextureGuestLayout` in CPU fallback loading (row pitch and
      array/Z strides) and fix slice addressing for arrays/cubes.
- [x] Initialize Metal `texture_load_*` compute pipelines beyond the generic
      8/16/32/64/128bpp loaders (DXT/DXN/depth and packed 16bpp transforms).
- [x] Add Metal format mapping helpers:
  - [x] Choose correct host pixel formats for common guest formats.
  - [x] Choose correct `LoadShaderIndex` per guest format (DXT1 64bpp vs
        DXT3/5 128bpp, DXN, depth, packed 16bpp).
  - [x] Choose decompression shaders when required by block alignment.
- [x] Expand Metal GPU texture loading to handle:
  - [x] 2D/stacked and cube (as array slices)
  - [x] Base + mips, including packed mip tails (using `GetPackedMipOffset`)
  - [x] Correct host pitch padding for shader write patterns
  - [x] Proper per-slice guest offset (array slice stride)
- [x] Release `load_pipelines_` and `load_pipelines_scaled_` in `Shutdown()`.

### Implementation Checklist
- [ ] Extend `GetPixelFormatForKey` / `GetLoadShaderIndexForKey` coverage to
      match D3D12 for formats seen in Halo 3 traces:
  - [ ] `k_16`, `k_16_16`, `k_16_16_16_16` (UNORM + FLOAT variants)
  - [ ] `k_16_16_EXPAND`, `k_16_16_16_16_EXPAND`
  - [ ] `k_10_11_11`, `k_11_11_10` (RGBA16 conversion)
  - [ ] `k_DXT3A`, `k_DXT5A` (alpha-only)
  - [ ] `k_CTX1` (if needed by traces)
- [ ] Validate BC texture creation + uploads on macOS Metal:
  - [ ] If `StorageModeShared` is rejected for BC formats, switch to a valid
        mode and upload via a staging path.
- [ ] Add a focused debug workflow for problematic textures:
  - [ ] Dump a small set of per-trace textures to `scratch/gpu/` keyed by
        fetch constant + guest address.
  - [ ] Compare Metal vs Vulkan dumps for the same fetches.

### Reference Information
- Metal implementation:
  - `src/xenia/gpu/metal/metal_texture_cache.cc`
- D3D12 reference for format mapping + load shader selection:
  - `src/xenia/gpu/d3d12/d3d12_texture_cache.cc`

---

## Phase 2: Reverse-Z + Depth/Stencil Parity (PENDING)

### Problem Description
Some real-game traces render HUD/UI but lose backgrounds/skyboxes, consistent
with reverse-Z mismatch and missing depth/stencil state parity.

### Root Cause Analysis
- Many 360 titles use reverse-Z (near = 1, far = 0) and rely on depth compare
  and clears matching Xenos semantics.

### Implementation Checklist
- [ ] Implement depth/stencil state mapping parity (match Vulkan/D3D12):
  - [ ] Depth compare, depth write enable, depth bias
  - [ ] Stencil enable, ops, masks, reference
- [ ] Reverse-Z detection:
  - [ ] Identify the same heuristic used by Vulkan/D3D12 (or replicate their
        decision from register state).
  - [ ] When reverse-Z is active: clear depth to 0 and use `GreaterEqual`.

---

## Phase 3: Fixed-Function Gaps (PENDING)

### Problem Description
Traces will hit additional host fixed-function state not yet mirrored in Metal.

### Root Cause Analysis
Metal still relies on defaults for some pipeline/dynamic state that must match
Xenos semantics.

### Implementation Checklist
- [ ] Rasterization state parity:
  - [ ] Cull mode and front-face winding
  - [ ] Depth bias (constant + slope)
- [ ] Blend coverage beyond current traces:
  - [ ] Verify separate alpha blend ops and masks in more traces

---

## Phase 4: Trace Dump Diffing Workflow (PENDING)

### Problem Description
Metal trace dump output often differs in resolution/aspect from the bundled
reference PNGs, so diffs must normalize output to avoid false positives.

### Root Cause Analysis
- Reference images may be window-sized captures (scaled/letterboxed), while
  Metal trace dump writes the raw guest output.

### Implementation Checklist
- [ ] Always compare Metal vs Vulkan trace dumps at the same output resolution.
- [ ] Add a small script/workflow for:
  - [ ] Cropping/letterboxing normalization
  - [ ] Heatmap diff generation for quick regression detection

### Reference Commands
Build:
```bash
./xb build --target=xenia-gpu-metal-trace-dump 2>&1 | tee scratch/logs/build.log
grep -nE "error:|fatal error:" scratch/logs/build.log
```

Run (Metal):
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace \
  --metal_texture_gpu_load=true \
  2>&1 | tee scratch/logs/trace_589.log
```
