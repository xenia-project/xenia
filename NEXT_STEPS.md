# NEXT_STEPS

## Phase 0: Trace-Dump Stability (Metal Validation) (COMPLETED)

### Problem Description
Real-game traces (Halo 3 / Gears of War) hit Metal validation asserts when the
backend tried to use a pipeline with depth/stencil formats while the active
render pass had no depth/stencil attachments (or had a 1x1 dummy RT but a large
scissor).

### Root Cause Analysis
- `MetalCommandProcessor::BeginCommandBuffer` previously returned early if a
  command buffer existed, so it never restarted the `MTLRenderCommandEncoder`
  when `MetalRenderTargetCache` switched from dummy RT0 → real RTs (or added
  depth/stencil).
- `MetalRenderTargetCache::GetRenderPassDescriptor` only bound a stencil
  attachment for one Xenos depth enum (`kD24S8`), but Metal requires the stencil
  attachment to be bound whenever the depth texture’s `pixelFormat` includes
  stencil.
- The dummy RT0 was 1x1, but the command processor used a fallback 1280x720
  scissor when no real RT0 was bound, triggering Metal’s scissor bounds assert.

### Implemented (This Phase)
- [x] Restart the render encoder when the cache-provided render pass descriptor
      changes (dummy → real RTs, depth/stencil changes).
- [x] Bind the stencil attachment whenever the depth texture’s pixel format
      includes stencil.
- [x] Size the dummy RT0 to a non-1x1 size (prefer last real RT size) so
      viewport/scissor derived from RT dimensions stays valid.

### Reference Information
- `src/xenia/gpu/metal/metal_command_processor.cc`
- `src/xenia/gpu/metal/metal_render_target_cache.cc`

---

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

## Phase 2: Render Target Transfers / Clears Parity (Halo 3) (IN PROGRESS)

### Problem Description
Some Halo 3 traces show clear evidence of missing host render-target “ownership
transfer” and format conversion steps (logged as skipped transfers). This can
produce subtle diffs vs the D3D12 reference: stale data, unexpected overlays,
and background differences.

### Root Cause Analysis
- `MetalRenderTargetCache::PerformTransfersAndResolveClears` currently logs and
  skips key transfers:
  - depth transfers (`src_depth != dst_depth`)
  - format mismatch conversions (for specific color formats used in traces)
- Without these transfers, Metal can render using incomplete EDRAM restore /
  resolve state compared to D3D12.

### Implementation Checklist
- [ ] Implement transfer pipelines equivalent to
      `D3D12RenderTargetCache::GetOrCreateTransferPipelines` for the modes hit
      by real traces:
  - [ ] Color→Color (same format) copy/resolve
  - [ ] Color→Color (format mismatch) conversion for formats seen in Halo 3
  - [ ] Depth→Depth copy/resolve (including stencil where applicable)
  - [ ] Color/Depth ↔ stencil-bit transfer modes used by Xenos
- [ ] Add targeted per-trace instrumentation:
  - [ ] Log each skipped transfer with the Xenos format names (not just ids)
  - [ ] Emit a small “transfer summary” at end of trace playback

### Validation Traces
- Halo 3: `reference-gpu-traces/traces/4D5307E6_37345.xtr`
- Gears of War: `reference-gpu-traces/traces/4D5307D5_2072.xtr` (good for
  catching render-pass transition / validation issues)

---

## Phase 3: Reverse-Z + Depth/Stencil State Parity (PENDING)

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

## Phase 4: Blend / Alpha Semantics (PENDING)

### Problem Description
UI and overlays in some games depend on correct alpha semantics (premultiplied
vs straight alpha) and correct handling of single-channel textures used as alpha
masks.

### Root Cause Analysis
- Xenos content frequently uses alpha-only textures (`k_8` / A8-like), which
  must be swizzled correctly for Metal sampling.
- Some titles appear to rely on premultiplied alpha blend states.

### Implementation Checklist
- [ ] Ensure alpha-only textures are sampled as expected (alpha-from-R swizzle).
- [ ] Verify blend factor mapping (separate RGB/alpha ops, factors, masks)
      against D3D12 for traces that show overlay diffs.

---

## Phase 5: Trace Dump Diffing Workflow (PENDING)

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
