# Metal Backend Implementation Plan

## Current Status (2025-08-07)

Shader reflection is now fully integrated and resources are binding at correct offsets in the argument buffer. However, rendering still produces black output because we're using dummy triangles instead of real Xbox 360 vertex data.

## Immediate Next Steps

### 1. Replace Dummy Triangles with Real Vertex Data

**Problem**: Currently using hardcoded dummy triangles that don't match what the Xbox 360 game is trying to render.

**Solution**: Use the actual vertex data from Xbox 360 shared memory.

**Implementation**:
1. **Fetch vertex buffers from shared memory**
   - Read vertex fetch constants from GPU registers
   - Get vertex buffer addresses and strides
   - Access actual vertex data from shared memory

2. **Parse vertex fetch descriptors**
   - Extract vertex attribute formats
   - Determine vertex layout and stride
   - Handle endian conversion for vertex data

3. **Create Metal vertex buffers with real data**
   - Allocate Metal buffers with correct size
   - Copy and convert vertex data
   - Handle Xbox 360 vertex format conversions

4. **Bind real vertex buffers**
   - Replace dummy triangle binding
   - Use correct vertex buffer offsets
   - Set proper vertex strides

### 2. Implement Proper Index Buffer Handling

**Problem**: Not using actual index data from Xbox 360.

**Implementation**:
1. Read index buffer info from GPU registers
2. Fetch index data from shared memory  
3. Handle 16-bit vs 32-bit indices
4. Apply endian conversion
5. Create and bind Metal index buffers

### 3. Fix Vertex Attribute Descriptors

**Problem**: Metal vertex descriptor doesn't match Xbox 360 vertex format.

**Implementation**:
1. Parse vertex shader's vertex_bindings() data
2. Build MTLVertexDescriptor from actual format
3. Map Xbox 360 formats to Metal formats
4. Handle format conversions as needed

### 4. Verify Render Target Setup

**Problem**: May be rendering to wrong targets or not preserving content.

**Implementation**:
1. Check render target bindings match shader outputs
2. Ensure load/store actions preserve content
3. Verify render pass descriptors are correct
4. Check if we need to resolve MSAA targets

## Code Locations

- Vertex fetching: `src/xenia/gpu/metal/metal_command_processor.mm` (IssueDraw)
- Shared memory: `src/xenia/gpu/metal/metal_shared_memory.cc`
- Vertex formats: `src/xenia/gpu/xenos.h` (VertexFormat enum)
- Register reading: `register_file_->values[XE_GPU_REG_*]`

## Expected Outcome

Once real vertex data is used instead of dummy triangles, we should see:
1. Actual geometry being rendered
2. Correct vertex positions and attributes
3. Visible output in PNG (not black)
4. Progress toward rendering actual game content

## Technical Details

### Xbox 360 Vertex Fetch

The Xbox 360 uses vertex fetch constants to describe vertex buffers:
- `VGT_DMA_*` registers control vertex fetching
- `FETCH_*` constants describe vertex attributes
- Vertex data is stored in big-endian format
- Multiple vertex streams can be interleaved

### Metal Requirements

Metal needs:
- Vertex buffers bound at specific indices
- Vertex descriptor matching shader expectations
- Proper stride and offset calculations
- Format conversions for unsupported Xbox 360 formats

## Testing

Test with trace file: `testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace`

Expected to see loading screen text instead of black output.