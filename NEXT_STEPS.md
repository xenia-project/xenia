# NEXT_STEPS (updated 2025-12-30)

This document tracks Metal backend correctness work against D3D12. Keep it
focused on current gaps, validated completions, and the next steps to reach
feature parity.

Assessment sources (current repo state):
- src/xenia/gpu/metal/metal_command_processor.{cc,h}
- src/xenia/gpu/metal/metal_render_target_cache.{cc,h}
- src/xenia/gpu/metal/metal_texture_cache.{cc,h}
- src/xenia/ui/metal/metal_presenter.mm
- src/xenia/gpu/d3d12/* (parity reference)

## Recent History (Rebuilt Commits Since Dec 26)

Base / tooling:
- Improved cvar bool parsing and macOS utilities (filesystem attrs, thread
  priority/nanosleep, emulator shutdown logging).
- Trace viewer path + fetch constant updates for D3D12/Vulkan.
- Added Metal trace viewer target and accessors for render target inspection.

Metal UI / presenter:
- Refined Metal immediate drawing and capture scaffolding.
- Added shader-based swap format conversion for present (BGRA/BGR10 swaps).
- Added trace-dump helper plumbing; forced swap for capture in Metal trace dump.

Metal GPU core:
- MemExport range tracking + shared memory invalidation.
- Rasterizer state parity (cull/winding/fill/depth bias/depth clip) and stencil
  reference selection parity.
- Primitive processor frame tracking uses per-instance current_frame_.
- Shader converter diagnostics expanded; metal_edram_rov cvar added.
- Render target cache expansion + ordered-blend support hooks.
- EDRAM blend compute bytecode + shader sources (32/64 bpp, 1x/2x/4x MSAA).
- Ordered-blend fallback keep-mask gating treats UINT32_MAX as RT disabled.

Docs:
- Removed local Metal PDF references (docs/metal/*.pdf) from repo.

## Current Priorities (Ranked)

1) Ordered blending parity
   - Compute fallback exists (ROV-like), but reload masking and channel/pack
     validation are still incomplete.
   - ROV path is blocked by MSC opcode 42; a Metal ROG-based path remains.
2) Presenter + trace dump output
   - Metal UI presenter does not draw guest output to the drawable.
   - Trace dump presenter capture path is stubbed.
3) Resolve validation
   - No CPU decode parity for Metal resolve/swap output.
4) Texture cache parity
   - key.scaled_resolve unsupported; no GPU vs CPU decode validation.
5) Format mapping parity
   - Resource/draw/transfer format splits still incomplete.
6) Tessellation + geometry shader validation
   - MSC pipeline materialization remains fragile in real traces.

## Phase 1: Command Processor + EDRAM Blending Parity (IN PROGRESS)

Goal: match D3D12 ordering and blend correctness for EDRAM paths.

Completed (current codebase):
- [x] MemExport range tracking with shared-memory invalidation.
- [x] Rasterizer state updates and stencil reference parity.
- [x] Ordered-blend eligibility gating uses keep_mask != 0 and treats
      UINT32_MAX as RT disabled.
- [x] EDRAM compute fallback pipelines (32/64 bpp, 1x/2x/4x MSAA) plus shader
      sources and bytecode.
- [x] Coverage texture path for compute fallback (SV_Target4).
- [x] EDRAM buffer scaling with resolution; 64bpp tile span uses 40x16.
- [x] ROV descriptor table split and metal_edram_rov gating with device
      capability checks.

Remaining:
- [ ] Ordered blending path selection:
  - [ ] Implement Metal ROG-based path when MSC ROV is blocked.
  - [ ] Or complete compute fallback ordering so it is safe for all draws.
- [ ] Compute fallback coherency:
  - [ ] Mask reload by coverage so untouched pixels are preserved.
  - [ ] Validate transfer pack/unpack + channel order for k_8_8_8_8 and
        k_2_10_10_10.
  - [ ] Investigate bright spots / channel swaps after fallback.
- [ ] Metal-side CPU decode parity for resolve/swap verification.

## Phase 2: Presenter + Trace Dump Parity (IN PROGRESS)

Goal: ensure Metal on-screen output and trace dumps match guest output.

Completed:
- [x] Shader-based swap format conversion for present (BGRA/BGR10 swaps).

Remaining:
- [ ] Add sRGB/linear conversion when the swap texture is sRGB.
- [ ] Render guest output texture to the drawable in MetalPresenter.
- [ ] Implement MetalTraceDumpPresenter::CaptureGuestOutput using the command
      processor guest output.
- [ ] Replace TryRefreshGuestOutputForTraceDump placeholder sizing with real
      RT/swap dimensions.

## Phase 3: Texture Cache Parity + Validation (IN PROGRESS)

Goal: match D3D12 texture cache behavior and validate on Metal.

Completed (current codebase):
- [x] Per-binding swizzled texture views (signed/unsigned variants).
- [x] GPU-only texture load pipelines with DXBC->DXIL->Metal conversion.

Remaining:
- [ ] Implement key.scaled_resolve or fail-fast with explicit logging.
- [ ] Add bounded GPU decode vs CPU decode validation (start with swap surface
      k_8_8_8_8).
- [ ] Border color parity (including YUV black variants).

## Phase 4: Host RT Transfers + Format Mapping (IN PROGRESS)

Goal: match D3D12 transfer, resolve-clear, and format view behavior.

Completed (current codebase):
- [x] Transfer scheduling on active command buffer (no per-transfer waits).
- [x] Transfer shader pipelines + per-bit stencil passes.
- [x] MSAA transfer fallback using explicit per-sample paths.

Remaining:
- [ ] Split resource vs draw vs transfer pixel formats (sRGB + integer views).
- [ ] Validate MSAA transfer correctness vs D3D12 for 32/64bpp and depth.

## Phase 5: Tessellation + Geometry Shaders (DEFERRED)

Goal: stabilize MSC mesh/GS paths and validate in real traces.

Remaining:
- [ ] Confirm tessellation-enabled draws produce IsTessellated() in practice.
- [ ] Log and fix tessellation early-out reasons.
- [ ] Track MSC geometry shader materialization failures in traces.

## Validation Strategy

- Primary: Metal trace replay with reference PNGs where available.
- When D3D12 output exists, compare Metal GPU decode vs CPU decode.
- Trace locations:
  - testdata/reference-gpu-traces/traces (canonical)
  - reference-gpu-traces/traces (local mirror)
