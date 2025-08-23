I’m thinking about how to toggle sRGB gamma. I should mention the cvar
--gamma_render_target_as_srgb=true if scenes appear too dark. Also, the
--readback_resolve=false option is important to bring up, as it’s false by
default. It might be a good idea to suggest capturing logs to confirm shader
selection and check for any failed memory region messages.

Alright, I’ll compile this into a concise checklist for clarity!

codex
Agreed — we’re in good shape to retest now. Here’s a quick checklist:

- Build:
    - ./xb build --config=Release
- D3D12 run (Windows):
    - xenia_canary.exe --gpu=d3d12 --log_resolves=true --d3d12_edram_rov=false
<path-to-415608B2.xex>
    - Then retry with --d3d12_edram_rov=true for comparison.
- Vulkan run:
    - xenia_canary.exe --gpu=vulkan --log_resolves=true <path-to-415608B2.xex>
- What to watch:
    - No “Failed to obtain the resolve destination memory region” errors.
    - Resolve logs show shader names and dispatch group sizes; gameplay no
longer white/blank; models/textures persist.
    - Lighting improves from BC5/BC4 SNORM mapping (normals look correct).
    - VRAM usage plateaus rather than steadily growing.
- If still dark/muted:
    - Try --gamma_render_target_as_srgb=true to test gamma targets as sRGB.
- Optional:
    - Add --trace_gpu to confirm resolve passes output valid color each
dispatch.

If you want, I can gate logs by title ID or add brief rate-limiting after you
validate.