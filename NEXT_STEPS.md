# NEXT_STEPS

## Phase 1: Trace 589 Visual Parity (IN PROGRESS)

### Problem Description
`xenia-gpu-metal-trace-dump` now produces a non-black PNG for:
`testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace`,
but the output differs from the reference PNG (incorrect compositing / colors).

### Root Cause Analysis
- Metal needed D3D-style NDC/viewport handling (NDC Y up).
- Metal host render pipeline was missing fixed-function parity needed for UI
  compositing:
  - Per-RT blend factors/ops (`RB_BLENDCONTROL0..3`)
  - Per-RT color write masks (`RB_COLOR_MASK` via `GetNormalizedColorMask`)
  - Alpha-to-coverage (`RB_COLORCONTROL.alpha_to_mask_enable`)
  - Dynamic blend constant (`RB_BLEND_{RED,GREEN,BLUE,ALPHA}`)
- Trace command parsing used unaligned loads on ARM64 (UB).

### Implementation Checklist
- [x] Fix Metal NDC mapping (use `origin_bottom_left=true` for Metal).
- [x] Fix ARM64 trace parsing UB (use `memcpy` for command headers) in:
  - [x] `src/xenia/gpu/trace_reader.cc`
  - [x] `src/xenia/gpu/trace_player.cc`
  - [x] `src/xenia/gpu/trace_viewer.cc`
- [x] Make Metal texture PNG dumping opt-in (`--metal_texture_dump_png`) and
      write to `scratch/gpu/`.
- [x] Implement Metal pipeline blend state + write masks and include them in
      the pipeline cache key.
- [x] Set Metal encoder blend constant per draw (`setBlendColor`).
- [x] Fix Metal capture swizzle (BGRA → RGBA) for PNG output.
- [ ] Rebuild `xenia-gpu-metal-trace-dump` and re-run trace 589.
- [ ] Compare Metal output to the reference and confirm:
  - [ ] Logo is composited (no solid quad behind it).
  - [ ] “Please wait” bar matches (no channel swap / gamma mismatch).
  - [ ] Result is stable across repeated runs.
- [ ] If still mismatched, triage in this order:
  - [ ] Presenter readback swizzle (BGRA/RGBA) and alpha handling.
  - [ ] sRGB vs linear formats for the guest output and capture path.
  - [ ] Remaining fixed-function gaps (cull/front-face, depth/stencil).

### Known Remaining Differences (As Observed)
- The bundled reference image is `1920x1200`, while trace-dump output is the
  raw guest output `1280x720`.
  - Likely the reference is a window-sized capture (scaled + letterboxed), so
    pixel-perfect diff requires normalizing the resolution first.
- Small edge/AA differences may simply be resampling differences (scaled
  reference vs unscaled trace-dump output).
- If there are content differences (a texture drawn in Metal but not in the
  reference), validate against Vulkan trace-dump output at the same resolution
  before assuming Metal is wrong.

### Investigation Checklist
- [ ] Generate a Vulkan trace-dump PNG for the same trace and compare against
      Metal at `1280x720` (same capture path semantics).
- [ ] Normalize output resolution when diffing:
  - [ ] Scale `1280x720 → 1920x1080` and letterbox to `1920x1200` (60px bars).
  - [ ] Use a heatmap diff focused on the content bbox (ignore letterbox).
- [ ] If Metal draws extra content vs Vulkan at `1280x720`, capture a draw-by-
      draw diff:
  - [ ] Enable per-draw logging around RT changes and `RB_*` state deltas.
  - [ ] Compare the first draw that diverges (shader hash + RT key + blend
        control + viewport/scissor).
- [ ] Validate guest-output extraction region:
  - Metal currently blits the top-left `min(src,dst)` region (`origin=(0,0)`).
    If the game renders to a viewport/scissor offset within the EDRAM-sized
    render target, we may need to apply a source `origin` offset.

### Reference Information
- Build (capture output to `scratch/`):
```bash
./xb build --target=xenia-gpu-metal-trace-dump 2>&1 | tee scratch/logs/build_metal.log
grep -E "error:" scratch/logs/build_metal.log
```

- Run trace 589:
```bash
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
  testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace \
  2>&1 | tee scratch/logs/trace_metal_589.log
```

- Reference PNG:
  - `testdata/reference-gpu-traces/references/title_414B07D1_frame_589.xenia_gpu_trace.png`

---

## Phase 2: Fixed-Function State Parity (PENDING)

### Problem Description
More traces (and later frames) will require deeper parity with the D3D12/Vulkan
backends’ fixed-function state beyond blending.

### Root Cause Analysis
Metal currently relies on defaults for several pieces of fixed-function state
that are pipeline/dynamic state on the host, and are required for correctness.

### Implementation Checklist
- [ ] Depth/stencil state parity:
  - [ ] Depth test/write + compare func
  - [ ] Stencil enable + ops + masks + reference
- [ ] Rasterization state parity:
  - [ ] Cull mode and front-face winding
  - [ ] Depth bias (constant + slope)
  - [ ] Polygon fill mode / line/point rules (as needed)
- [ ] Add state to pipeline cache keys where applicable (pipeline state vs
      dynamic state).

### Reference Information
- Reference implementations:
  - `src/xenia/gpu/d3d12/pipeline_cache.cc`
  - `src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc`

---

## Phase 3: Unsupported Host VS Types (PENDING)

### Problem Description
Metal currently skips draws when `PrimitiveProcessor` requests host vertex shader
types that need additional translation/runtime support (rectangle lists, point
expansion, memexport compute, tessellation).

### Root Cause Analysis
The MSC (DXIL→Metal) path does not yet implement the translation/runtime pieces
needed for these host VS types.

### Implementation Checklist
- [ ] Prioritize based on what reference traces hit most:
  - [ ] `kRectangleListAsTriangleStrip`
  - [ ] `kPointListAsTriangleStrip`
  - [ ] `kMemExportCompute`
  - [ ] Tessellation
- [ ] Remove skip logic only once the full path works end-to-end.

---

## Phase 4: EDRAM Resolve + Format Coverage (PENDING)

### Problem Description
Current Metal resolve coverage is enough to get a first frame out, but more
formats/MSAA modes and depth resolves will be needed to progress beyond the
smallest traces.

### Root Cause Analysis
Resolve behavior is format-dependent (packing, endian, MSAA), and correctness
depends on matching Xenia’s established Vulkan/D3D12 behavior.

### Implementation Checklist
- [ ] Expand resolve compute coverage (formats + MSAA).
- [ ] Validate resolve addressing and endianness.
- [ ] Validate EDRAM tile ownership transitions and invalidation behavior.

---

## Phase 5: Resource Binding + Texture Cache Robustness (PENDING)

### Problem Description
As shader variety increases, Metal needs predictable and correct resource
binding across VS/PS stages, with robust descriptor lifetime management.

### Root Cause Analysis
Metal uses MSC’s IR runtime argument buffers and a ring-buffered descriptor
model; correctness depends on matching the expected layout and avoiding races.

### Implementation Checklist
- [ ] Validate argument buffer layouts vs MSC docs for VS/PS.
- [ ] Validate SRV/UAV selection for shared memory (memexport paths).
- [ ] Improve texture cache invalidation granularity and reduce debug noise.
