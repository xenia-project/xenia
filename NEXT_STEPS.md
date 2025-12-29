# NEXT_STEPS (updated 2025-12-29)

This document tracks the remaining Metal backend correctness work.
D3D12 is the reference implementation.
Phase 1 now includes the MemExport + EDRAM blending / resolve parity work and
is the current top priority.

Assessment sources (current repo state):
- src/xenia/gpu/metal/metal_texture_cache.{h,cc}
- src/xenia/gpu/metal/metal_command_processor.cc
- src/xenia/gpu/metal/metal_render_target_cache.cc
- src/xenia/gpu/d3d12/d3d12_texture_cache.cc

## Phase 0: Commit History Reconstruction (COMPLETED)

### Problem Description
Large uncommitted Metal backend diff with overlapping Codex sessions and
untracked artifacts, making it hard to reconstruct commit boundaries.

### Root Cause Analysis
Edits were applied across multiple sessions without commits, and some changes
were made outside tracked apply_patch logs.

### Implementation Checklist
- [x] Map current diff files to Codex sessions (Dec 25-29)
- [x] Extract session request summaries and note missing provenance
- [x] Create scratch/GIT_COMMIT_HISTORY_REBUILD.md with timeline and file map
- [x] Finalize commit ordering and staging plan
- [x] Decide handling of untracked artifacts and deleted docs
- [x] Execute staging and commit sequence

### Reference Information
- scratch/GIT_COMMIT_HISTORY_REBUILD.md
- scratch/COMMIT_REBUILD_PROGRESS.md
- ~/.codex/sessions/2025/12/

## Current largest gaps vs D3D12 (prioritized)

1) Ordered EDRAM blending parity (ROV/interlock equivalent)
- Logs still show "EDRAM blend: ordered blending likely required" in traces.
- Impact: per-draw RMW operations are unordered, so resolved output can be
  fundamentally wrong even if dumps/transfers are correct.

2) Resolve/EDRAM dump parity validation (ownership-based dumps + depth layout)
- EDRAM dumps now populate non-zero data, but output is still broken; validate
  resolve copy and swap decode using CPU decode on Metal (D3D12 not available).
- Impact: swapped output can stay corrupt even with correct dumping.

3) Swap/present parity (format conversions)
- Logs show `MetalPresenter::CopyTextureToGuestOutput` refusing blit copies when
  swap texture format != guest output format (ex: RGB10A2 -> RGBA8), resulting
  in black output even if rendering is correct.

4) Submission ordering parity (single command buffer per submission)
- Metal performs dump/resolve/transfer work in separate command buffers with
  `waitUntilCompleted`, rather than encoding into the active submission.
- Impact: hides ordering bugs and causes performance cliffs.

5) Format mapping parity (resource vs draw vs transfer)
- Metal uses a single pixel format for all usages (no sRGB draw view for gamma,
  no integer views for transfers), unlike D3D12’s split resource/draw/transfer
  formats.
- Impact: gamma RTs, SNORM16 full-range, and ownership transfers diverge.

6) Primitive processing gaps: tessellation validation + point/rectangle parity
- Tessellated draws are now routed through MSC mesh emulation (HS/DS +
  tessellation VS) but still need validation in real traces.
- Current traces like `4D53085B_14820.xtr` show no tessellated draws (all
  `tessellated=false`), so we need explicit instrumentation for
  `VGT_OUTPUT_PATH_CNTL` and tessellation enable/disable to confirm coverage.
- Point/rectangle expansion draws are no longer skipped in Metal; confirm
  output parity in traces.
- Impact: any remaining tessellated surface draws may still diverge until the
  MSC tessellation path is validated.

7) Geometry shader emulation: pipeline materialization reliability (MSC)
- Geometry shader bytecode generation and the Metal pipeline path are present,
  but real traces still fail at pipeline creation when specializing functions
  with constant values (MSC output + Metal materialization failures).
- Impact: GS-heavy titles (ex: GTA IV traces) still drop draws or fall back.

8) Scaled resolve textures (TextureCache key.scaled_resolve)
- MetalTextureCache::TryGpuLoadTexture returns false if key.scaled_resolve is set,
  while D3D12 has a full scaled-resolve virtual-buffer system.
- Impact: workloads depending on scaled-resolve textures diverge.

## Additional parity gaps to track (from recent analyses)
1) Rasterizer state parity (MetalCommandProcessor)
- Missing fixed-function state updates for cull mode, front-face winding,
  fill mode, depth bias, and depth clip mode. These are correctness-critical
  even without EDRAM blending changes.
  - [x] Apply cull/winding/fill/depth bias/depth clip per draw in Metal.
2) Stencil reference parity (MetalCommandProcessor)
- Metal always uses the front-face reference; D3D12 picks back-face ref when
  only back faces are drawn (approximation required by Metal too).
  - [x] Match D3D12 stencil-ref selection when only back faces are drawn.
3) Texture cache GPU upload path (MetalTextureCache)
- Current GPU texture load path stalls and does CPU readback + replaceRegion.
  Replace with GPU-only buffer->texture blits and remove waits.
4) Scaled resolve key equality (MetalTextureCache)
- `key.scaled_resolve` is hard-disabled in key comparisons, blocking parity.
5) Border color parity (MetalTextureCache)
- Metal supports only a subset of D3D12 border colors; emulate missing ones
  (notably YUV black variants) in shader sampling if needed.
6) Primitive processor frame tracking (MetalPrimitiveProcessor)
- Per-frame buffer reuse uses a static counter that never increments in the
  request path, leading to churn and potential sync issues.
  - [x] Use the per-instance `current_frame_` for per-frame tracking.
7) Presenter parity (MetalPresenter)
- UI presenter does not draw the guest output texture in PaintAndPresentImpl,
  so on-screen output won’t match Vulkan/D3D12.
8) Trace dump presenter parity (MetalTraceDumpPresenter)
- Capture path is stubbed; implement guest output capture for Metal traces.
9) PSI path TODO coverage (MetalRenderTargetCache)
- Resolve clear / PSI-specific paths are still TODO and can diverge if PSI
  path is selected.
10) Excess `waitUntilCompleted` usage (Metal backend)
- Remove avoidable waits to match submission ordering and avoid shutdown hangs.

## Phase 1: Texture cache parity + MemExport/EDRAM parity (status: IN_PROGRESS)

### Problem Description
- Ordered EDRAM blending is still missing, so blend-heavy traces (Reach/GTA IV)
  diverge even after resolve/memexport parity fixes.
- Host depth-store transfers are not triggered in current traces, so the
  depth-store path does not affect Reach/GTA IV output yet.
- Texture cache still lacks scaled-resolve handling and bounded validation.
- Metal EDRAM dumps now populate non-zero data in traces, but output remains
  broken, so resolve copy and swap decode still need validation against D3D12.
- Swap/present output can be black if the swap texture format does not match
  the guest output texture format (MetalPresenter refuses the blit copy).
- Enabling `metal_edram_rov` in real traces crashes inside Metal Shader
  Converter (MSC) with `patchBinaryWith2Outs: need to add support for opcode: 42`.

### Root Cause Analysis
- Ordered EDRAM blending requires a Metal ROV-equivalent path and enabling the
  ROV shader path in Metal (SRV/UAV table splitting + EDRAM UAV binding is now
  in place, and shader translation is now gated by `metal_edram_rov`, but the
  ordered blending path itself is still missing).
- Metal fragment memory ops are unordered without `[[raster_order_group]]`,
  and texture write support is limited (multisample/depth restrictions),
  so we must gate raster_order_group usage and provide a compute fallback.
- MSC currently crashes when ROV is enabled (patchBinaryWith2Outs opcode 42)
  in real traces, so ROV path cannot be exercised until MSC is fixed or we
  implement a compute fallback that avoids MSC ROV emission.
- Host depth-store transfers are not emitted in the current traces, so the
  compute path never runs.
- MetalTextureCache refuses key.scaled_resolve and has no validation path to
  compare Metal GPU decoding vs CPU decoding for bounded dumps.
- Metal resolve previously skipped when copy_src_select had no bound RT;
  this is now fixed, so remaining mismatch is likely in resolve copy or
  texture decode rather than ownership dumping.
- The compute fallback currently derives tile spans using a fixed 80x16 tile
  width, which is incorrect for 64bpp color RTs (40x16 tiles). This can cause
  blending/reload to target the wrong tiles and corrupt output.
- MetalPresenter currently refuses format-mismatched blit copies, so swap
  textures in RGB10A2 (or other formats) never reach the guest output.
- The EDRAM compute buffer allocation is fixed at 10 MiB; with resolution
  scaling enabled the compute shaders index a scaled layout and can write
  out of bounds, corrupting output or turning frames black.

### Implementation Checklist
#### 0) Immediate correctness blockers (swap/present + EDRAM buffer + 64bpp tile span)
- [x] Swap/present format conversion:
  - Add a shader-based copy path in `MetalPresenter::CopyTextureToGuestOutput`
    when swap texture format != guest output format (sample + write RGBA8).
  - Keep the blit path for identical formats; log conversion use.
  - Ensure the conversion pipeline is created once and released on shutdown.
  - [x] Handle swap textures bound as `MTLTextureType2DArray` by using a
        dedicated conversion pipeline and selecting slice 0.
  - [x] Swap R/B when the source format is BGRA or BGR10A2.
  - [ ] If colors are still off, add sRGB/linear conversion based on source
        format (sRGB read/write parity with Vulkan/D3D12 presenter).
- [x] EDRAM buffer scaling:
  - Allocate EDRAM buffer size based on tile count * tile size * resolution
    scale (or clamp scale), not a fixed 10 MiB constant.
  - Log the computed buffer size at startup for validation.
- [x] 64bpp tile span math for compute fallback:
  - Use 40x16 tiles for 64bpp when converting scissor -> tile span.
  - Update `BlendRenderTargetToEdramRect` and
    `ReloadRenderTargetFromEdramTiles` (done).
- [ ] Compute fallback coherency:
  - [x] Fix ordered-blend eligibility keep-mask check:
        treat `UINT32_MAX` as RT disabled and only trigger on real keep bits.
  - [x] Ensure EDRAM is initialized (zero or snapshot) before first reload.
  - Avoid reloading stale EDRAM into untouched pixels:
    - [x] Write source RT values into EDRAM when coverage == 0 (compute blend).
    - [ ] Mask reload by coverage so only touched pixels are overwritten.
  - [x] Scale reload scissor by resolution scale so reload matches host RT size.
  - [ ] Investigate bright spots / channel swaps after compute fallback:
        validate transfer pack/unpack and channel ordering for k_8_8_8_8 /
        k_2_10_10_10, and confirm swap/resolve outputs are not reinterpreted.

#### A) MemExport parity (MetalCommandProcessor)
- [x] Add Metal-side memexport range tracking and populate it for VS/PS.
- [x] Request shared memory ranges for memexport streams before encoding draws.
- [x] Mark memexport ranges as GPU-written after draw submission to invalidate
      overlapping texture cache pages.

#### B) EDRAM resolve + blending parity (MetalRenderTargetCache)
- [x] Implement non-host-RT resolve path (EDRAM -> shared memory).
- [x] Implement depth/stencil EDRAM store path (or compute fallback).
- [x] Add logging to confirm host-depth-store eligibility in traces.
- [x] Split SRV/UAV descriptor tables and bind the EDRAM UAV slot for the
      Metal IR runtime.
- [x] Enable ROV shader translation when `metal_edram_rov` is set.
- [x] Gate ROV path on `MTLDevice::areRasterOrderGroupsSupported()` and log
      when unsupported.
- [x] Port D3D12/Vulkan ROV system-constant setup into Metal (flags, keep
      masks, clamp, EDRAM base/pitch, stencil params).
- [x] Add compute fallback staging: optional full-EDRAM dump from host RTs
      after command buffers to keep EDRAM in sync (debug/slow path; no ordered
      blending yet).
- [x] Add compute blend path for resolve rectangles (32bpp 1x/2x/4x MSAA,
      64bpp 1x/2x/4x MSAA) and integrate when
      `metal_edram_compute_fallback` is enabled.
- [ ] Add ordered blending path for EDRAM and integrate into pipeline
      selection (compute fallback is required while MSC crashes on ROV DXIL).
  - Metal docs: `docs/metal/Metal-Shading-Language-Specification.pdf` (raster
    order groups, atomics in fragment), `docs/metal/Metal-Feature-Set-Tables.pdf`
    (device feature support / storage texture writes).
  - Shader references: `src/xenia/gpu/shaders/edram_blend_*`,
    `src/xenia/gpu/shaders/edram.xesli`,
    `src/xenia/gpu/shaders/pixel_formats.xesli`,
    `src/xenia/gpu/shaders/xenos_draw.hlsli`.
  - Backend references: D3D12 ROV path, Vulkan fragment shader interlock.
  - Sequenced plan (compute fallback):
    - [x] Detect ordered blending per draw and per-RT:
      - Use `RB_BLENDCONTROL` + `GetPSIColorFormatInfo` keep mask to derive
        a RT mask that needs ordered blending.
      - Gate on `metal_edram_compute_fallback` + Host RT path.
    - [x] Pipeline variant for compute fallback:
      - Disable fixed-function blending for those draws.
      - Override color write masks to RGBA for active RTs so source alpha
        is preserved for blend factors (masked channels are handled by keep
        mask in the compute shader).
      - Keep A2C and depth/stencil state as-is.
    - [x] Per-draw source capture + dispatch ordering:
      - End render encoder after the draw, run compute blend, then re-open
        render encoder for subsequent draws (must stay inside the same
        command buffer).
      - Use the current scissor in guest pixels; map to EDRAM tile span using
        the RT key (base/pitch/msaa) and tile size from `edram.xesli`.
    - [x] Coverage correctness (must be exact, not scissor-only):
      - Implement a per-draw coverage attachment so compute blending only runs
        on samples written by the draw (includes alpha test + A2C).
      - Done:
        - Added a dedicated MSAA coverage texture (R8Unorm) sized to the bound
          render targets; cleared to 0 each draw when compute fallback is active.
        - Modified DXBC translation (Metal-only flag) to emit an extra
          SV_Target (index 4) that always outputs 1.0. Hardware coverage and
          A2C mask which samples are written into the coverage texture.
        - Extended Metal pipeline descriptors + render pass descriptors to bind
          the coverage attachment when compute fallback is enabled.
        - Updated edram_blend compute shaders to read the coverage texture and
          skip samples with coverage < 0.5.
    - [x] EDRAM/host RT synchronization:
      - Track which tiles were updated by compute blending (scissor → tile
        span) and reload those tiles into the host RT.
      - Implemented EDRAM→host RT reload via a render pass that reads the EDRAM
        buffer in a fragment shader and writes to the RT format.
      - MSAA reload uses per-sample draws with `sample_mask` output to target
        each sample; reload dispatches immediately after compute blending so
        host RTs stay authoritative for subsequent sampling/transfers.
    - [x] Fix 64bpp tile span math in compute fallback:
      - Use 40x16 tile width for 64bpp RTs when converting scissor → tile span.
      - Update `BlendRenderTargetToEdramRect` and
        `ReloadRenderTargetFromEdramTiles` to halve tile width for 64bpp.
      - Audit any other tile-span conversions in the compute blend path for
        implicit 32bpp tile assumptions.
    - [ ] Validation:
      - Add one-shot logs for ordered-blend draws (RT mask, scissor, tile span).
      - Verify swap-resolve output via CPU decode on Metal.
- [x] Add resolve source logging to validate `ResolveInfo` tile spans vs the
  actual source RT key (pitch tiles, width/height, msaa, depth) since D3D12
  logs are unavailable.
- [x] Remove copy_src_select early-out in Metal resolve; always dump by
      ownership (matches D3D12 resolve flow).
- [x] Apply depth half-tile swap in depth EDRAM dump shaders (1x/2x/4x MSAA).
- [ ] Validate EDRAM dump/resolve contents using CPU decode on Metal
      (spot-check EDRAM bytes and swap outputs without D3D12).
- [ ] Add a swap-resolve validation step: dump a small range of shared memory
      for `0x03044000` after resolve and compare to a CPU-decoded reference.

#### E) Swap/present parity (MetalTextureCache + MetalCommandProcessor)
- [x] Implement `RequestSwapTexture` for Metal using fetch constant 0 (like
      D3D12/Vulkan) and present from that texture rather than RT0 when valid.
- [ ] Handle swap-present format mismatches:
  - If swap texture format != guest output format, perform a shader-based copy
    (sample src, write dst) instead of refusing the blit.
  - Alternatively, force the swap texture to be created in the guest output
    format (RGBA8) for Metal.

#### G) EDRAM buffer sizing (resolution scale)
- [ ] Make EDRAM buffer allocation scale with resolution scale (or clamp scale):
  - Current buffer is fixed 10MB and will overflow if resolution scale > 1.
  - Allocate based on tile count * tile size * bytes per sample, or cap scale.

#### F) Viewport/scissor validation (MetalCommandProcessor)
- [x] Log scissor state when it deviates from full RT to catch stale or
      incorrectly converted scissor/viewport state.

#### C) Texture cache parity + validation (MetalTextureCache)
- [ ] Decide behavior for key.scaled_resolve (implement or fail-fast with logs).
- [ ] Add bounded texture-dump validation: GPU decode vs CPU decode for the
      same texture key, starting with the swap surface (k_8_8_8_8).

#### D) Tessellation activation instrumentation (MetalCommandProcessor)
- [x] Log `VGT_OUTPUT_PATH_CNTL::path_select` and `VGT_HOS_CNTL::tess_mode` in
      the early draw debug output.
- [x] Track and report counts for tessellation-enabled draws vs. actual
      `IsTessellated()` draws at shutdown to confirm trace coverage.

#### E) Tessellation emulation bring-up (MetalCommandProcessor + MSC)
- [ ] Confirm tessellation-enabled draws result in `IsTessellated()` and build
      the tessellation pipeline (mesh/emulation) rather than default VS/PS.
- [ ] When tessellation is enabled but not tessellated, log the exact
      early-out reason (path_select, domain/hull availability, stage source).
- [ ] Ensure MSC function-constant space and HS/DS reflection are stable for
      tessellation pipelines (no missing config fields).
- [ ] Validate draw parameter bindings (draw params buffer + vertex buffer
      bind point) for tessellation-enabled draws.

#### F) Resolve + viewport/scissor alignment (MetalCommandProcessor + MetalRenderTargetCache)
- [ ] Validate scissor conversion against window/screen scissor registers and
      window offsets (ensure Metal scissor reflects guest intent even with
      origin_bottom_left=true).
- [ ] Confirm viewport/scissor are applied consistently after register changes
      (no stale 16x16 scissor carried into full-screen passes).
- [ ] Check swap-resolve scissor/rect uses frontbuffer dimensions (1152x720)
      without unintended offsets or pitch-based shifts.
- [ ] Add a one-shot log of window/screen scissor + resulting Metal scissor
      when the first banded frame is captured, then disable the forced full
      scissor override to see if banding correlates with guest scissor state.
- [ ] Validate swap resolve destination pitch/offsets vs RB_COPY_DEST_PITCH and
      force-align to 32 if the pitch is smaller than the resolve width.
- [ ] Compute blend fallback must be applied per draw or per pass, not during
      resolve (resolve-time blend uses stale/incorrect blend state and
      corrupts output). Add a per-draw/per-pass EDRAM blend path that captures
      blend state and writes to EDRAM before resolves.
- [ ] If ownership tracking yields no rectangles for a resolve, dump directly
      from the selected source RT to avoid zeroed EDRAM/garbled swaps.

#### G) EDRAM dump packing parity (MetalRenderTargetCache)
- [x] Store `k_2_10_10_10_FLOAT` in RGBA16F and pack float10 on dump (D3D12
      parity).
- [x] Add dump format enum/constants and per-format packing for 32bpp/64bpp and
      depth (including D24FS8 float24).
- [x] Bind stencil plane views for depth dumps and pack stencil into low byte.
- [x] Compile missing 2x MSAA color dump pipelines (32bpp + 64bpp).

## Phase 2: Host RT parity (status: IN_PROGRESS)

### Problem Description
- Metal still diverges from D3D12 in the host-RT path for ownership transfers,
  resolve clears, and submission ordering. Format mapping is also unified
  rather than split by usage (resource vs draw vs transfer).
- MSAA ownership transfers in Metal currently rely on `[[sample_id]]` without
  explicit per-sample passes, so only sample 0 may be written in multisample
  destinations.

### Root Cause Analysis
- Metal performs transfers via simplified blits and ad-hoc pipelines, not the
  full D3D12/Vulkan transfer set.
- Dump/resolve/transfer work is encoded in separate command buffers and waits,
  breaking D3D12-style submission ordering.
- A single pixel format is used for resource, draw, and transfer, so gamma and
  integer/typeless views are not represented correctly.
- Metal fragment shaders are not guaranteed to run per-sample; relying on
  `[[sample_id]]` without sample shading yields undefined MSAA transfer results.

### Implementation Checklist
#### A) Ownership transfers + resolve clears (MetalRenderTargetCache)
- [ ] Step 1: Port D3D12/Vulkan transfer shader math (MSAA mapping, 32/64bpp
      packing, color↔depth reinterpretation, host-depth compare) into a Metal
      MSL transfer library.
  - [ ] Translate Vulkan MSAA remap logic (1x/2x/4x, 32/64bpp, column swap).
  - [ ] Implement packed color/depth conversions (RGBA8, RGB10A2, float10,
        16-bit, 32-bit, D24S8, D24FS8 float24).
  - [ ] Implement host depth comparison path (texture vs EDRAM copy).
  - [x] Add per-sample transfer fallback for MSAA (sample mask + explicit
        dest sample ID constant), matching Vulkan fallback behavior.
- [x] Step 2: Implement transfer/clear pipeline creation for all transfer
      modes (color/depth/stencil-bit + host depth), plus dummy resources and
      depth/stencil state variants needed for per-bit stencil passes.
  - [x] Build per-key Metal transfer shader generation (output type + bindings).
  - [x] Add transfer clear pipelines (float/uint color + depth) and cache.
  - [x] Add dummy textures/buffers for safe binding when required.
- [x] Step 3: Replace Metal PerformTransfersAndResolveClears with full D3D12
      scheduling (host depth store, transfers, stencil clear/bit passes, and
      resolve-clear draws) encoded into the active command buffer.
  - [x] Encode host depth store on active command buffer (no waits).
  - [x] Implement sorted transfer invocations + per-rectangle scissor draws.
  - [x] Implement stencil clear + per-bit stencil passes.
  - [x] Implement resolve clears via transfer clear pipelines.

#### B) Submission ordering parity (Metal CP/RT Cache)
- [x] Provide EnsureCommandBuffer/EndRenderEncoder to keep transfers and
      resolves in the active submission.
- [x] Encode all transfers/resolve clears into the active command buffer and
      avoid per-transfer command buffer commits.

#### C) Format mapping parity (MetalRenderTargetCache)
- [ ] Split resource vs draw vs transfer pixel formats
      (sRGB view for gamma, integer views for ownership transfer).
- [ ] Update render pass attachments and transfer SRVs to use the correct
      views.
  - [x] Align 8_8_8_8 host render targets to RGBA8 (not BGRA8), matching D3D12.

### What's now true in code
- Metal uses GPU-based texture loading (texture_load_* compute shaders) with no
  CPU untile fallback in LoadTextureDataFromResidentMemoryImpl.
- Per-binding swizzles are applied via texture views created/cached per:
  (texture, view_format, host_swizzle, dimension, signedness).
- GetLoadShaderIndexForKey and InitializeLoadPipelines cover:
  R10G11B11 / R11G11B10, DXT3A AS1111, and UNORM/SNORM to float selection.
- BC decompression happens on misalignment (and CTX1 is decompressed).
- Host format for k_8_8_8_8 is RGBA8 (matching D3D12).
- Host depth-store eligibility logging confirms traces like
  `scratch/4D53085B_14820.log` have `host_depth_source==dest=0`, so the host
  depth-store path is not exercised yet.
- Metal IR descriptor tables now provide separate SRV/UAV regions, with EDRAM
  bound in the UAV table (u1) when available.
- `metal_edram_rov` now toggles ROV shader translation and the Metal RT path.
- Metal ROV path now checks `areRasterOrderGroupsSupported()` and updates
  SystemConstants for ROV when enabled (matching D3D12/Vulkan behavior).
- Metal pipeline creation disables fixed-function color writes/blending when
  the ROV path is active, leaving blending to the shader path.
- Metal disables fixed-function depth/stencil state when ROV is active so the
  shader path can handle depth/stencil ordering.
- Metal now logs ROV path selection for the first few draws to confirm runtime
  activation.
- Metal pipeline creation logs when the ROV path is used (to correlate shader
  output and fixed-function blending disablement).
- Metal now logs DXBC SFI0 ROV feature flags for pixel shaders when
  `metal_edram_rov` is enabled as a proxy for ROV shader emission.
- Metal pipeline reflection now logs fragment arguments matching `xe_edram`
  and their access modes when ROV is enabled.
- Metal logs when blend state + keep mask indicate ordered blending is likely
  required (first 16 occurrences; triggers in `4D53085B_14820.xtr`).
- Metal has a `metal_edram_compute_fallback` debug cvar to blend supported
  host RTs into EDRAM after command buffers (store/pack for depth or
  unsupported formats).
- Metal compute blending supports 32bpp 1x/2x/4x MSAA (8_8_8_8,
  2_10_10_10, 2_10_10_10_FLOAT, 16_16, 16_16_FLOAT, 32_FLOAT) and
  64bpp 1x/2x/4x MSAA (16_16_16_16, 16_16_16_16_FLOAT, 32_32_FLOAT).
- Metal dump shaders now pack per-guest format (including float10, float24)
  and bind stencil plane views; 2x MSAA color dump pipelines are compiled.
- Metal render targets now create draw/transfer views (sRGB for gamma draw,
  integer views reserved for ownership transfers). IssueCopy encodes the
  resolve on the active command buffer without waiting; depth EDRAM stores use
  the dump pipelines in the compute fallback path.
- Metal no longer skips HostVertexShaderType::kPointListAsTriangleStrip or
  ::kRectangleListAsTriangleStrip draws; memexport compute and tessellation
  still skip.
- Tessellation activation instrumentation now logs `path_select` and
  `hos_tess_mode` in `DRAW_PROC_DEBUG`. `4D53085B_14820.xtr` shows
  tessellation enabled but only 1 tessellated draw (path_select=3664,
  tessellated=1), so tessellation is still mostly inactive in this trace.
- Metal resolve now logs source RT key/size/pitch and warns when `dump_pitch`
  disagrees with the source render target pitch.
- Metal swap now prefers the swap texture fetched via texture fetch 0 (like
  D3D12/Vulkan), falling back to RT0 only when the swap texture is invalid.
- Metal now logs `SCISSOR_DEBUG` entries when scissor changes (first 64
  occurrences); `scratch/4D53085B_18196.log` shows full-RT scissor while
  window_scissor is still 16x16 (likely due to the forced full-scissor
  override).
- `scratch/4D53085B_18196.log` shows `MetalResolve constants` for base=675/1350
  with pitch=15 and `MetalResolve swap dest` scissor=(0,0 1152x720), and
  `MetalSwap` uses fetch0 1152x720.
- Metal resolve now logs swap destination pitch/offset info and clamps the
  destination pitch to at least the resolve width (aligned to 32) if needed.
- Metal resolve now falls back to dumping directly from the selected source RT
  if ownership rectangles are empty (logs the fallback).
- Metal ownership transfers now use the transfer shader path (including
  stencil-bit passes and resolve clears) and encode into the active command
  buffer without per-transfer command buffer commits.

### Remaining work (texture)
- Decide what to do about key.scaled_resolve:
  - Implement it (preferred), or
  - Assert it cannot occur on Metal and fail fast with logging.
- Finish the bounded texture-dump validation loop.

### Validation strategy (no Vulkan required)
- Use existing reference PNGs in testdata/reference-gpu-traces where available.
- When a D3D12 run is available (Windows/other machine), dump the same bounded set
  (same fetch constants / addresses) and compare offline.
- As an intermediate sanity check, add a CPU decode path ONLY for the dumped subset
  (not for runtime rendering) and compare Metal GPU decode vs CPU decode for the
  same texture key.

## Phase 2: Geometry shader emulation (status: DEFERRED)

### Known blockers
- Geometry shader materialization failures are attributed to Metal Shader
  Converter (MSC) output and are blocked pending Apple fixes.
- Tessellation emulation is now wired through MSC mesh shaders, but still needs
  validation and compatibility fixes on real traces.

### Next actions
- Revisit once MSC fixes are available or an alternative conversion path exists.
- Keep the primitive expansion work queued, but do not prioritize ahead of Phase 3.

## Phase 3: Trace validation + remaining gaps (status: PENDING)

### Implementation Plan (priority)

#### A) MemExport parity with D3D12/Vulkan
1) **Introduce memexport range tracking in Metal**
   - Add a Metal-side `memexport_ranges_` vector similar to D3D12/Vulkan.
   - Populate it in `MetalCommandProcessor::IssueDraw` using
     `draw_util::AddMemExportRanges` for VS and PS when memexport is used.
2) **Request shared memory residency for memexport ranges**
   - For each range, call `shared_memory_->RequestRange` before encoding draws.
   - Match D3D12 behavior for error handling and logging.
3) **Mark GPU-written ranges and invalidate texture cache overlaps**
   - After submitting the draw (or at command buffer completion), call
     `shared_memory_->RangeWrittenByGpu` for each range.
   - Add a Metal-side equivalent of D3D12’s texture cache invalidation:
     invalidate textures overlapping memexport ranges and force reload on next
     use.
4) **Plumb memexport visibility into texture cache**
   - Ensure `TextureCache::MarkRangeAsResolved` or an equivalent memexport path
     is triggered so texture bindings see updated data.
5) **Evaluate kMemExportCompute host VS path necessity**
   - Determine if Metal traces hit `HostVertexShaderType::kMemExportCompute`.
   - If required, implement a compute-based host VS path or stub with explicit
     logging and fail-fast to avoid silent corruption.

#### B) EDRAM blending parity (ROV/interlock equivalent)
1) **Define a Metal-compatible ROV path**
   - Use `[[raster_order_group]]` when supported by `MTLDevice` and by the
     resource access model (MSL spec: unordered fragment loads/stores unless
     `raster_order_group` is used; multisampled textures are read-only, depth
     textures are sample/read-only, sparse textures are read-only).
   - Gate the ROV path on `areRasterOrderGroupsSupported()` and fall back to
     compute when unsupported or when texture access restrictions apply.
   - Because MSC crashes on ROV-enabled DXIL (opcode 42), the compute fallback
     must be the primary functional path for now.
2) **Match D3D12/Vulkan behavior**
   - D3D12 uses ROV; Vulkan uses fragment shader interlock.
   - Mirror their behavior in Metal by:
     - Binding EDRAM (or RT) as a writable resource in the fragment stage.
     - Implementing manual read/modify/write for blend ops when required.
3) **Integrate into pipeline cache**
   - Add a Metal pipeline path that toggles between fixed-function blending and
     ROV-style emulation based on render target state.
4) **Validate EDRAM-format coverage**
   - Ensure 7e3/packed formats and depth/stencil interlocks are handled (or
     explicitly blocked with logs).

### Implementation plan for compute fallback (new)
- Implement a compute EDRAM blending path that:
  - Runs after rasterization to host RTs, reading color/depth from host RTs
    and writing into the EDRAM buffer (u1).
  - Mirrors D3D12/Vulkan ROV math using the existing ROV system constants
    (keep mask, clamp, blend factors/ops, blend constants, base/pitch).
  - Starts with 32bpp color, 1x MSAA (extend to 2x/4x and 64bpp as followups).
  - Enforces per-tile ordering by dispatching tiles serially or using a
    conservative grid and no overlapping writes.
- Gate the compute path with a new cvar so we can A/B against host RTs while
  MSC remains blocked.
- Add minimal logging: number of tiles dispatched, format, MSAA, and RT index.

### Compute fallback detailed plan (Phase 1 focus)
1) **Reference math and constants**
   - Use `dxbc_shader_translator_om.cc` ROV functions as the source of truth for
     unpack/pack, clamp, and blend behavior.
   - Use `src/xenia/gpu/shaders/xenos_draw.hlsli` and
     `DxbcShaderTranslator::SystemConstants` fields:
     `edram_rt_base_dwords_scaled`, `edram_rt_format_flags`,
     `edram_rt_clamp`, `edram_rt_keep_mask`, `edram_rt_blend_factors_ops`,
     `edram_blend_constant`, `edram_32bpp_tile_pitch_dwords_scaled`.
   - Vulkan’s FSI path (`src/xenia/gpu/vulkan/vulkan_command_processor.cc`) and
     D3D12’s ROV path (`src/xenia/gpu/d3d12/d3d12_command_processor.cc`) are the
     behavioral references; no compute fallback exists today.

2) **Shader implementation (XeSL/MSL)**
   - Add a new compute shader in `src/xenia/gpu/shaders/`:
     - Inputs: host RT texture (SRV), EDRAM buffer (UAV), per-dispatch constants
       (EDRAM tile base/pitch, format flags, keep masks, blend ops).
     - Output: EDRAM buffer writes using the same packing rules as ROV.
   - Start with a single shader for 32bpp color, 1x MSAA.
   - Port the ROV math directly into the compute shader (unpack → clamp →
     blend → pack), preserving bit-exact behavior.
   - Add follow-up shaders for 2x/4x MSAA and 64bpp formats once 1x/32bpp is
     validated.

3) **Pipeline plumbing (MetalRenderTargetCache)**
   - Allocate Metal compute pipeline(s) for the new shader(s) alongside existing
     resolve/EDRAM pipelines in `MetalRenderTargetCache::InitializeEdramComputeShaders`.
   - Add a `DispatchEdramBlendCompute(...)` helper that:
     - Builds a tile grid for the active RT(s) and the current EDRAM base/pitch.
     - Binds host RT texture(s) as SRV and EDRAM buffer as UAV.
     - Dispatches a conservative grid (no overlapping writes) per RT.
   - Trigger this compute path at the end of a render pass (before resolves) when
     `metal_edram_rov` is enabled and MSC ROV is disabled.

4) **Ordering and synchronization**
   - Ensure render pass completion before compute dispatch (single command buffer
     ordering is enough for Metal).
   - Serialize per-RT dispatches to avoid overlapping EDRAM writes.

5) **Cvars and logging**
   - Add a dedicated cvar (ex: `metal_edram_rov_compute_fallback`) to force the
     compute path, and allow A/B with host RTs.
   - Log per-dispatch: RT index, format, MSAA, tile span, and bytes written.
   - Add a once-per-run log if the compute fallback is selected due to MSC crash.

6) **Validation strategy**
   - Run trace dumps with `--metal_edram_rov=1` and compute fallback enabled.
   - Verify logs show compute dispatch and that resolves reference updated EDRAM.
   - Compare outputs against D3D12/Vulkan reference PNGs where available.

#### C) Resolve and non-host-RT parity
1) **Implement non-host-RT resolve path**
   - Mirror D3D12/Vulkan resolve path for EDRAM -> shared memory.
   - Support non-host-RT resolves and ensure the resolve constants match shader
     packing.
2) **Implement depth/stencil store**
   - Remove the current "unsupported" early-out and provide a path to persist
     depth/stencil EDRAM (even via a compute fallback).
3) **Add ordered synchronization between render passes and resolves**
   - Ensure resolves occur after render writes are complete (single command
     buffer ordering or explicit synchronization if needed).

#### D) Validation plan for Phase 3
1) **Targeted trace coverage**
   - Prioritize Halo Reach traces and any black-output traces such as
     `reference-gpu-traces/traces/454108E6_9042.xtr`.
2) **Instrumentation**
   - Add logging for memexport range usage, resolve source/dest selection, and
     EDRAM blending path selection.
3) **Regression guardrails**
   - Keep per-draw ring allocator logging minimal but ensure it reports any
     unexpected reuse or missing range requests.

## Later TODOs (cleanup)
- Audit the Metal backend for non-functional compatibility shims (for example,
  `MetalCommandProcessor::CreateIRConverterBuffers`) and remove unused paths
  once parity work is complete.

## Evidence (Metal docs + code)
- MSL spec: "All OS: Metal 2 and later support raster order group attributes"
  and "Loads and stores to buffers (in device memory) and textures in a fragment
  function are unordered... [[raster_order_group(index)]] ... guarantees the
  order of accesses for overlapping fragments"
  (`docs/metal/Metal-Shading-Language-Specification.pdf`).
- MSL spec: "Multisampled textures only support the read attribute. Depth
  textures only support the sample and read attributes. Sparse textures do not
  support write or read_write"
  (`docs/metal/Metal-Shading-Language-Specification.pdf`).
- Feature sets: "Raster order groups" and "Maximum number of raster order
  groups" entries, with a note about Mac2 device support
  (`docs/metal/Metal-Feature-Set-Tables.pdf`).
- D3D12/Vulkan interlock setup already exists and is the reference for Metal
  parity (`src/xenia/gpu/d3d12/d3d12_command_processor.cc`,
  `src/xenia/gpu/vulkan/vulkan_command_processor.cc`).

### Render targets / EDRAM
- Implement depth/stencil store (or an equivalent path) to avoid skipping depth
  EDRAM stores.
- Implement the non-host-RT resolve path (or a reliable compute fallback).
- Implement programmable EDRAM blending parity:
  - D3D12: ROV
  - Vulkan: fragment shader interlock
  - Metal: raster_order_group / equivalent ordered blending path (or compute fallback).
