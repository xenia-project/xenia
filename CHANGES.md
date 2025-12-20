# Changes

## Graphics (Metal)

### Fixes
- **Reverse-Z Depth Handling:** Implemented detection of Reverse-Z depth modes (Greater/GreaterEqual comparison) in `MetalCommandProcessor::BeginCommandBuffer`. Now correctly clears the depth attachment to `0.0` instead of `1.0` when Reverse-Z is active, fixing missing backgrounds and skyboxes in titles like Halo 3.
- **Texture Swizzling:** Added `ToMetalTextureSwizzle` helper and updated `CreateTexture` to apply component mapping. Specifically mapped Alpha-only formats (`k_8`, `k_8_A`, `DXT3A`) to `111R` (Red broadcast to Alpha, RGB=1) to fix "pink/peach" UI artifacts and text rendering.
- **BC Texture Handling:** Modified `MetalTextureCache::IsDecompressionNeededForKey` to always return `false` for compressed formats (DXT/DXN/CTX1), forcing the use of Metal's hardware BC support via the CPU upload path. Implemented specialized `SwapBlockBC1/2/3/5` helpers to correctly byte-swap the 16-bit color endpoints and 32-bit/64-bit lookup tables within BC blocks during CPU detiling. This resolves "rainbow/neon" noise artifacts and "static" caused by incorrect generic endian swapping.
- **Texture Format Corrections:**
    - Updated `k_8_8_8_8` and `k_8_8_8_8_A` handling to use `BGRA8Unorm` pixel format with `BGRA` swizzle (1546). This specifically addresses "Purple Ocean" artifacts by swapping Red and Blue channels to match Xenos expectations.
    - Added `BGRA` swizzle for `k_2_10_10_10` to fix channel ordering for 10-bit formats.
- **Vertex Buffer Validation:** Updated `MetalCommandProcessor` to only configure `MTLVertexDescriptor` layouts if attributes are actually bound to them, preventing Metal validation assertions for unused buffer slots.
- **Cube Map Validation:** Updated `MetalTextureCache::CreateTextureCube` to always create `MTLTextureTypeCubeArray` (even for single cubes), matching the expectations of the converted shaders and fixing "Invalid texture type" validation errors.
- **Mixed Depth/Color Transfers:** Enabled support for all combinations of depth/color surfaces in `PerformTransfersAndResolveClears`, ensuring proper render pass configuration for resolve operations.

### Theories on Remaining Issues
- **Missing Geometry ("Blue Void"):** While Reverse-Z fixes depth clearing, geometry might still be clipped or culled incorrectly.
    - *Theory 1 (Culling):* Incorrect front-face winding order (CW vs CCW) in `MetalCommandProcessor` state setup. Xenos and Metal might disagree on the default winding, or the `PA_SU_SC_MODE_CNTL` register is not being correctly translated.
    - *Theory 2 (Manual Fetch Endianness):* The shader uses manual vertex fetch (calculating addresses from fetch constants). If `DxbcShaderTranslator` emits logic that assumes Little Endian data, but the `shared_memory` buffer contains Big Endian data (raw Xbox RAM), vertices will be garbage. `CopySwapBlock` handles textures, but vertex buffers are accessed directly by the GPU. The fix would be to ensure `DxbcShaderTranslator` emits `bswap` instructions for vertex loads, or to implement a compute pass that pre-swaps vertex buffers.
    - *Theory 3 (Tessellation):* Halo 3 uses tessellation. If the Metal backend's tessellation emulation (via Mesh Shaders) is incomplete or incorrect, the pipeline will produce no geometry.
- **Striping/Noise:** If artifacts persist after the BC fix, it could be due to:
    - *Pitch Alignment:* Mismatches between Xenos padded pitch and Metal's linear texture pitch requirements. The CPU upload path handles this via `untiled_data` packing, but if the shader calculates UVs based on a different pitch assumption than what was uploaded, striping occurs.