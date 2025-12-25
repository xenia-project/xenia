# NEXT_STEPS

This document tracks the current Metal backend recovery work after recent
lost progress. D3D12 is the source of truth for correctness. Use `scratch/`
for logs, repros, and temporary artifacts.

---

## Phase 1: Metal Texture Cache Parity with D3D12 (IN_PROGRESS)

### Problem Description
Metal texturing diverges from D3D12 in multiple places (swizzles, format
coverage, decompression policy), and the GPU texture load path is disabled by
default. As a result, Metal falls back to the CPU path for many formats and
renders incorrect UI/text in traces.

### Root Cause Analysis
- GPU load path is gated by `--metal_texture_gpu_load` (default false), so most
  textures use the CPU fallback.
- Per-binding swizzles (`binding.host_swizzle`) are not applied; only a base
  per-format swizzle is set at texture creation.
- Metal does not initialize all `texture_load_*` pipelines that exist in the
  bytecode set (R10G11B11/R11G11B10, UNORM/SNORM to float, DXT3A AS1111).
- BC decompression policy does not match D3D12 (always uses hardware BC).
- Host format mapping diverges for `k_8_8_8_8` (BGRA vs RGBA in D3D12).

### Implementation Checklist
- [x] Remove or default-enable the GPU load gate
      (`cvars::metal_texture_gpu_load`) so GPU path is attempted first.
- [x] Implement per-binding swizzles:
      - Add texture views with swizzle = `binding.host_swizzle`.
      - Cache views by `(texture, is_signed, host_swizzle, dimension)`.
      - Keep base texture swizzle identity (format only).
- [x] Expand `GetLoadShaderIndexForKey` to include:
      `k_10_11_11`, `k_11_11_10`, `k_DXT3A_AS_1_1_1_1`.
- [x] Implement UNORM/SNORM→float selection for 16-bit formats as needed.
- [x] Instrument norm16 float fallback logging for trace validation.
- [x] Initialize missing Metal `texture_load_*` pipelines that already exist in
      `src/xenia/gpu/shaders/bytecode/metal/`.
- [x] Ensure `./xb buildshaders --target=metal` generates all shaders in
      `src/xenia/gpu/shaders/bytecode/metal/*`, excluding the third-party
      upscaling shaders (unsupported).
- [x] Match D3D12 BC decompression rules (use decompress path on misalignment).
- [x] Align host format for `k_8_8_8_8` with D3D12 (RGBA8) and update CPU
      fallback swizzle/conversion to match.
- [ ] Verify texture formats and swizzles with trace instrumentation:
      - Dump a small, bounded set of textures to `scratch/gpu/` keyed by
        fetch constant + guest address.
      - Compare Metal vs Vulkan output for the same fetch constants.
- [ ] Decode missing binding fetch constants (e.g., fetch 21) to identify the
      guest format/swizzle/signedness being dropped.
- [ ] Log memexport usage per draw/shader to correlate warnings with visual
      artifacts (store shader IDs and target usage).
- [ ] Add resolve/transfer debug logging for UI render targets (format,
      MSAA, pixel format) to verify parity with D3D12/Vulkan.

### Reference Information
- `src/xenia/gpu/metal/metal_texture_cache.cc`
- `src/xenia/gpu/metal/metal_texture_cache.h`
- `src/xenia/gpu/d3d12/d3d12_texture_cache.cc`
- Metal shader bytecode: `src/xenia/gpu/shaders/bytecode/metal/texture_load_*`

---

## Phase 2: Geometry Shader Emulation Pipeline (BLOCKED - MSC BUG)

### Problem Description
Rectangle/point list expansion draws are skipped on Metal because the backend
lacks a geometry emulation pipeline. A Metal Shader Converter (MSC) bug blocks
mesh/geometry pipeline materialization for real traces (GTA IV).

### Root Cause Analysis
- Geometry emulation pipeline code exists in scratch snapshots but is not in
  the repo.
- MSC 3.0.6 emits malformed MetalLib for certain GS control flow patterns,
  causing `Failed to materializeAll` and `metal-objdump: Invalid cast`.

### Implementation Checklist
- [ ] Reintegrate geometry emulation pipeline from scratch:
      - `scratch/metal_command_processor.cc`
      - `scratch/metal_geometry_shader.cc`
      - `scratch/metal_shader.cc` (function_name_ assignment)
- [ ] Add new Metal geometry shader module (`metal_geometry_shader.{h,cc}`),
      wire into `premake5.lua`, and include in builds.
- [ ] Restore geometry pipeline caches and draw path integration in
      `MetalCommandProcessor`.
- [ ] Restore shader dump/probe helpers used in the scratch pipeline.
- [ ] Keep mesh shader capability gating (macOS 14+ / Apple GPU family).
- [ ] Integrate MSC conversion details from scratch:
      - Function-constant discovery and binding for GS stage.
      - Root signature visibility ALL for geometry pipelines.
      - Stage-in reflection path for vertex attributes.
- [ ] Validate draw-path coverage:
      - Indexed + non-indexed geometry emulation draws.
      - Null pixel shader cases (decide if dummy PS is required).
      - Geometry emulation path not taken for unsupported host VS types.

### Reproducer (Latest in `scratch/`)
- `scratch/27f5af3d-296b-468b-8ff4-552d0e66f700/GS_DEBUG.md`
- `scratch/27f5af3d-296b-468b-8ff4-552d0e66f700/repro_steps.txt`
- `scratch/27f5af3d-296b-468b-8ff4-552d0e66f700/repro_loader.mm`
- Note: `scratch/5a3bb38b-14fb-41e9-9a49-287c054383b0/` is an older variant.

### GS Repro Summary (Keep in scratch)
- Consolidated GS repro bundle: `scratch/gs_bug_repro`
- Minimal shareable bundle: `scratch/gs_bug_repro_min.zip`
- These contain the up-to-date repro, logs, and Apple sample notes. Avoid
  copying these details into NEXT_STEPS beyond this pointer.

### Reference Information
- D3D12 geometry shader generation:
  `src/xenia/gpu/d3d12/pipeline_cache.cc`
- MSC docs:
  `third_party/metal-shader-converter/docs/UserManual.md`

---

## Phase 3: Trace Validation + Remaining Render/Transfer Gaps (PENDING)

### Problem Description
After restoring texture cache parity and geometry emulation, trace outputs must
be validated, and any remaining transfer/resolve gaps addressed.

### Implementation Checklist
- [ ] Re-run reference traces (Halo 3 / GTA IV / Gears) with GPU load enabled.
- [ ] Confirm rectangle/point list draws are no longer skipped.
- [ ] Investigate any remaining MSAA color transfer gaps or resolve failures.
- [ ] Validate tiled `k_32_FLOAT` render target creation on Metal.

### Reference Information
- Trace inputs: `testdata/reference-gpu-traces/traces/`
- Metal RT cache: `src/xenia/gpu/metal/metal_render_target_cache.cc`
