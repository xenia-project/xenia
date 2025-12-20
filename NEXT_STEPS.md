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

## Phase 1: Texture Semantics Parity (Halo 3) (COMPLETED)

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
- [x] Extend `GetPixelFormatForKey` / `GetLoadShaderIndexForKey` coverage for:
  - [x] `k_16_16` variants (UNORM/FLOAT/EXPAND)
  - [x] `k_DXT3A`, `k_DXT5A` (swizzled as 000R)
  - [x] `k_8` (swizzled as 000R) to fix text rendering

### Implementation Checklist
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

## Phase 2: Render Target Transfers / Clears Parity (Halo 3) (COMPLETED)

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
- `MetalRenderTargetCache::Resolve` previously could fail with:
  - `Resolve: no valid GPU resolve shader / pipeline for this configuration`
  This indicated the Metal backend was missing the compute pipelines for
  `draw_util::ResolveCopyShaderIndex` values beyond the initial 32bpp-only path.
  This is now resolved for the standard resolve shader set.

### Implemented (This Phase)
- [x] Implement transfer pipelines equivalent to
      `D3D12RenderTargetCache::GetOrCreateTransferPipelines` for the modes hit
      by real traces:
  - [x] Color→Color (same format) copy/resolve
  - [x] Color→Color (format mismatch) conversion for formats seen in Halo 3
  - [x] Depth→Depth copy/resolve (including stencil where applicable)
  - [x] Color/Depth ↔ stencil-bit transfer modes used by Xenos
- [x] Implement missing **EDRAM resolve** pipelines so `IssueCopy` can complete
      resolves in real traces:
  - [x] Create Metal compute pipelines for the full XeSL resolve shader set:
        `resolve_full_{8,16,32,64,128}bpp` and
        `resolve_fast_{32,64}bpp_{1x2x,4x}msaa`.
  - [x] Validate in Halo 3 trace: `MetalRenderTargetCache::Resolve: GPU path
        (shader=...) wrote ... bytes` appears and no "no valid GPU resolve"
        errors are emitted (see `scratch/logs/trace_halo3_postresolve.log`).
- [x] Implement transfer pipelines equivalent to D3D12 for the modes hit by
      real traces (these are still the primary cause of remaining diffs / black
      output in some titles):
  - [x] Color→Color transfers with **format conversion** for formats observed
        in logs:
    - [x] `k_32_FLOAT (14) → k_8_8_8_8 (0)`
    - [x] `k_32_FLOAT (14) → k_2_10_10_10 (2)`
    - [x] `k_2_10_10_10 (2) → k_8_8_8_8 (0)`
    - [x] `k_8_8_8_8 (0) → k_2_10_10_10_FLOAT (3)`
  - [x] Depth transfers (src_depth != dst_depth) needed by Halo 3 / Gears.
- [x] Add targeted per-trace instrumentation:
  - [x] Log each skipped transfer with the Xenos format names (not just ids)
  - [x] Emit a small “transfer summary” at end of trace playback

### Validation Traces
- Halo 3: `reference-gpu-traces/traces/4D5307E6_37345.xtr`
- Gears of War: `reference-gpu-traces/traces/4D5307D5_2072.xtr` (good for
  catching render-pass transition / validation issues)

---

## Phase 3: Reverse-Z + Depth/Stencil State Parity (COMPLETED)

### Problem Description
Some real-game traces render HUD/UI but lose backgrounds/skyboxes, consistent
with reverse-Z mismatch and missing depth/stencil state parity.

### Root Cause Analysis
- Many 360 titles use reverse-Z (near = 1, far = 0) and rely on depth compare
  and clears matching Xenos semantics.

### Implemented (This Phase)
- [x] Implement depth/stencil state mapping parity (match Vulkan/D3D12):
  - [x] Depth compare, depth write enable, depth bias
  - [x] Stencil enable, ops, masks, reference
- [x] Reverse-Z detection:
  - [x] Identify the same heuristic used by Vulkan/D3D12 (or replicate their
        decision from register state).
  - [x] When reverse-Z is active: clear depth to 0 and use `GreaterEqual`.
- [x] **New**: Enable `allow_reverse_z` in `GetHostViewportInfo` to correctly
      set Metal viewport depth range (0..1 vs 1..0).

---

## Phase 4: Blend / Alpha Semantics (COMPLETED)

### Problem Description
UI and overlays in some games depend on correct alpha semantics (premultiplied
vs straight alpha) and correct handling of single-channel textures used as alpha
masks.

### Root Cause Analysis
- Xenos content frequently uses alpha-only textures (`k_8` / A8-like), which
  must be swizzled correctly for Metal sampling.
- Some titles appear to rely on premultiplied alpha blend states.

### Implemented (This Phase)
- [x] Ensure alpha-only textures are sampled as expected (alpha-from-R swizzle).
- [x] Verify blend factor mapping (separate RGB/alpha ops, factors, masks)
      against D3D12 for traces that show overlay diffs.
- [x] **Fix**: Changed `k_8` swizzle to `000R` (Alpha only) to fix text rendering.
- [x] **Fix**: Changed `k_8_8_8_8` swizzle to Identity (RGBA) to fix "blue-ish" rendering.

---

## Phase 5: Trace Dump Diffing Workflow (COMPLETED)

### Problem Description
Metal trace dump output often differs in resolution/aspect from the bundled
reference PNGs, so diffs must normalize output to avoid false positives.

### Root Cause Analysis
- Reference images may be window-sized captures (scaled/letterboxed), while
  Metal trace dump writes the raw guest output.

### Implemented (This Phase)
- [x] Update `tools/run_metal_trace.sh` to handle directory inputs and improved grep patterns.

---

## Phase 6: Rectangle List / Primitive Expansion (Metal) (PENDING)

### Problem Description
Real traces log many skipped draws:
`Metal: Host vertex shader type 10 is not supported yet, skipping draw`.

This corresponds to `Shader::HostVertexShaderType::kRectangleListAsTriangleStrip`
(primitive processor VS-expansion fallback for `xenos::PrimitiveType::kRectangleList`).
Skipping these draws removes UI / full-screen quads in many titles.

### Root Cause Analysis
- The Metal backend currently uses the DXBC → DXIL → Metal path for guest
  shaders, but `DxbcShaderTranslator` does not implement the HostVertexShaderType
  fallbacks (`kPointListAsTriangleStrip`, `kRectangleListAsTriangleStrip`,
  `kMemExportCompute`).
- D3D12 does not need these fallbacks in the same way because it uses generated
  geometry shaders (`PipelineCache::CreateDxbcGeometryShader`) for point/rect
  expansion, while Metal has no geometry stage.

### Implementation Checklist
- [ ] Implement `kRectangleListAsTriangleStrip` for the Metal path without
      relying on geometry shaders:
  - [ ] Option A (preferred): extend `DxbcShaderTranslator` to support
        `Shader::HostVertexShaderType::kRectangleListAsTriangleStrip` by
        emulating the D3D12 geometry shader logic in the translated vertex
        shader (select longest diagonal and generate the 4th vertex as in
        `PipelineCache::CreateDxbcGeometryShader`, `kRectangleList` case).
  - [ ] Option B: implement a Metal-only mesh/GS-emulation pipeline (if mesh
        shaders are available on the target OS/device) and generate the same
        expansion logic there.
- [ ] Implement `kPointListAsTriangleStrip` (if it shows up in Metal traces):
  - [ ] Match D3D12 `kPointList` geometry shader logic for point expansion
        (radius derived from system constants / shader outputs).
- [ ] Implement `kMemExportCompute` (needed for VS-only memexport draws):
  - [ ] Add a Metal compute path that runs vertex-shader memexport only once
        per guest vertex and preserves memory export ordering semantics.

### Reference Information
- Primitive fallback selection:
  - `src/xenia/gpu/primitive_processor.cc`
  - `src/xenia/gpu/shader.h` (`Shader::HostVertexShaderType`)
- D3D12 expansion logic reference:
  - `src/xenia/gpu/d3d12/pipeline_cache.cc` (`PipelineCache::CreateDxbcGeometryShader`)
- Metal skip site:
  - `src/xenia/gpu/metal/metal_command_processor.cc` (`IssueDraw` early return)

---

## Phase 7: Robust Null Texture Binding / Invalid Fetch Constants (Metal) (COMPLETED)

### Problem Description
Real traces produce warnings and sometimes Metal validation issues when a shader
references a texture fetch constant that is invalid or not bound, for example:
- `Texture fetch constant (...) has "invalid" type!`
- `GetTextureForBinding: No valid binding for fetch constant ...`
- Metal diagnostic errors if the fallback texture type doesn’t match what the
  converted shader expects (notably cube vs cube-array).

### Root Cause Analysis
- The shared `TextureCache::BindingInfoFromFetchConstant` correctly refuses to
  treat `kInvalidTexture` as a valid texture binding unless
  `--gpu_allow_invalid_fetch_constants=true` is set.
- The Metal backend previously bound a single 2D `null_texture_` for all
  missing textures, which can mismatch the shader’s expected texture type
  (cube-array, 3D/stacked).

### Implemented (This Phase)
- [x] Bind dimension-compatible null textures for missing bindings:
  - [x] Use `MetalTextureCache::GetNullTexture2D/3D/Cube` when a fetch constant
        is missing or incompatible with the shader’s dimension.
- [x] Return dimension-compatible null textures from
      `MetalTextureCache::GetTextureForBinding` for missing/incompatible
      bindings to reduce repeated warnings and avoid null derefs.

### Implementation Checklist
- [x] Reduce redundant logging:
  - [x] Consider gating Metal-only “no binding” warnings behind a debug flag,
        since the shared texture cache already logs invalid fetch constants.
- [ ] Decide policy for invalid fetch constants in trace dumps:
  - [ ] If traces routinely contain `kInvalidTexture` fetches that are actually
        used, consider enabling `gpu_allow_invalid_fetch_constants` in the
        trace-dump tool only (not globally), but only after confirming Vulkan /
        D3D12 behavior on the same traces.

---

## Phase 8: Vertex Fetch & Endianness (PENDING)

### Problem Description
Geometry is often missing ("Blue Void") or corrupted. This is likely due to
mismatched endianness in manual vertex fetch logic used by the shaders.

### Root Cause Analysis
- Xenia shaders often use manual vertex fetch (calculating addresses from fetch
  constants) to support robust bounds checking and endian swapping.
- The shaders rely on `fetch_constant` bits to determine endianness.
- If `MetalCommandProcessor` uploads fetch constants incorrectly, or if the
  shader logic for swapping is incompatible with the raw `shared_memory` buffer
  bound by Metal, vertices will be garbage.

### Implementation Checklist
- [ ] Debug `Fetch Constant` uploads in `MetalCommandProcessor`.
- [ ] Verify `DxbcShaderTranslator` emits correct `bswap` logic for vertex
      fetches.
- [ ] Consider implementing a Compute Pass to pre-swap vertex buffers if shader
      emulation proves too fragile or slow.

---

## Phase 9: Metal Presentation & Blit Pipelines (Implemented)

### Problem Description
Presentation is currently stubbed in `MetalPresenter::CreateBlitPipeline`, meaning
no scaling, gamma correction, or Guest Output logic (FSR/CAS) is applied. This
likely results in incorrect final image output.

### Implemented (This Phase)
- [x] Implement `MetalPresenter::CreateBlitPipeline` using `guest_output_*.xesl`
      shaders (now built by `xb`).
- [x] Load `guest_output_bilinear.ps.metallib` and others.
- [x] Implement `MetalPresenter::BlitTexture` to use the pipeline for scaling.

---

## Phase 10: Immediate Drawer Refactor (Implemented)

### Problem Description
`MetalImmediateDrawer` currently uses hardcoded shader strings instead of the
precompiled `immediate.vs/ps.xesl` artifacts built by `xb`. This splits the
asset pipeline and makes maintaining UI shaders harder.

### Implemented (This Phase)
- [x] Load `immediate.vs.metallib` and `immediate.ps.metallib` in
      `MetalImmediateDrawer`.
- [x] Remove hardcoded MSL strings.
- [x] Implement `white_texture_` fallback for non-textured draws.
- [x] Fix Vertex Buffer index (1) vs Push Constants (0) conflict in MSL.

### Notes
- **Texture Swizzling:** `k_8` and `k_DXT3A` are mapped to `RRRR` (Intensity) to match D3D12 behavior.

---

## Phase 11: Regression Analysis & Texture Sampling (PENDING)

### Problem Description
1.  **Green Screen Regression:** Some scenes (e.g. Gears of War Title Menu?) now render as a solid green screen, whereas previously they rendered correctly (albeit with some artifacts). This suggests an overcorrection in texture format mapping or swizzling, or an issue with the new presentation pipeline.
2.  **Texture Sampling Artifacts:** In-game Gears of War looks mostly correct, but specific textures (e.g. columns) show sampling errors (likely address mode, mipmap, or format interpretation issues).

### Root Cause Theories
- **Green Screen:**
    - Could be `k_8` / `k_DXT3A` swizzle (`RRRR` vs `111R`). If the game relies on `111R` (White) for a modulation mask and `RRRR` gives Black/Red, it might fail. Or if it's a video texture format being mishandled.
    - Could be `MetalPresenter` blit pipeline. If the source texture is valid but the presentation shader fails (e.g. gamma shader), it might output green/debug color.
- **Sampling Artifacts:**
    - Mipmap transitions?
    - `TextureAddressMode` conversion? Metal has strict requirements.

### Implementation Checklist
- [ ] Investigate "Green Screen" trace. Dump the source texture before presentation.
- [ ] Experiment with `k_8` swizzle (`111R`) again if `RRRR` is confirmed problematic for this specific case, despite D3D12 parity.
- [ ] Verify `MetalPresenter` input texture state.

Run (Metal):
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace \
  --metal_texture_gpu_load=true \
  2>&1 | tee scratch/logs/trace_589.log
```
