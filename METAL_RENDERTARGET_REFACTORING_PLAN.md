# Metal RenderTargetCache Refactoring Plan

## Executive Summary
The Metal backend currently fails to render trace dumps correctly because it doesn't use the shared `RenderTargetCache` base class infrastructure. This causes it to miss critical render target reconstruction from EDRAM snapshots when register values are zero.

## Current Architecture Problems

### 1. Metal's Isolated Implementation
- `MetalRenderTargetCache` is a standalone class, not inheriting from `gpu::RenderTargetCache`
- Only reads RB_COLOR_INFO registers directly (which are 0x00000000 in traces)
- Creates dummy render targets as fallback
- Never reconstructs render targets from EDRAM snapshot data

### 2. Missing Base Class Integration
- No `RenderTargetCache::Update()` call which handles:
  - EDRAM snapshot interpretation
  - Normalized color mask calculation
  - Complex render target resolution
  - Format determination from EDRAM contents

### 3. Incomplete EDRAM Handling
- `RestoreEdram()` only copies data to buffer
- Doesn't trigger render target reconstruction
- No integration with base class EDRAM resolution logic

## Refactoring Phases

### Phase 1: Analyze Base RenderTargetCache Interface (2 hours)
**Goal**: Understand all virtual methods and requirements

#### Tasks:
1. Document all virtual methods in `gpu::RenderTargetCache`:
   - [ ] Path GetPath() const
   - [ ] uint32_t GetResolutionScale() const  
   - [ ] bool IsResolutionScaled() const
   - [ ] RenderTargetKey GetRenderTargetKey()
   - [ ] void RequestViewportReconfiguration()
   - [ ] void GetGuestRenderTargets()
   - [ ] uint32_t GetLastUpdateBounds()
   - [ ] void ResetAccumulatedRenderTargets()

2. Analyze base class data structures:
   - [ ] RenderTargetKey structure
   - [ ] RenderTarget base class
   - [ ] Transfer/resolve infrastructure
   - [ ] EDRAM range tracking

3. Study D3D12 implementation as reference:
   - [ ] How D3D12RenderTargetCache implements virtuals
   - [ ] Resource creation patterns
   - [ ] EDRAM to texture mapping

### Phase 2: Refactor MetalRenderTargetCache Class Hierarchy (4 hours)
**Goal**: Make MetalRenderTargetCache inherit from base class

#### Tasks:
1. Update class declaration:
   ```cpp
   class MetalRenderTargetCache final : public gpu::RenderTargetCache {
   ```

2. Refactor constructor to match base class signature:
   - [ ] Pass RegisterFile, Memory, TraceWriter
   - [ ] Initialize base class properly
   - [ ] Maintain Metal-specific members

3. Create MetalRenderTarget class:
   ```cpp
   class MetalRenderTarget : public RenderTarget {
     MTL::Texture* texture_;
     MTL::Texture* msaa_texture_;  // If MSAA
   };
   ```

4. Update existing methods to work with inheritance:
   - [ ] Move Metal-specific logic to overrides
   - [ ] Preserve existing functionality
   - [ ] Add base class calls where needed

### Phase 3: Implement Virtual Methods (6 hours)
**Goal**: Implement all required virtual methods for Metal

#### Critical Methods:
1. **GetPath()**
   ```cpp
   Path GetPath() const override { 
     return Path::kHostRenderTargets; 
   }
   ```

2. **GetResolutionScale()**
   ```cpp
   uint32_t GetResolutionScale() const override { 
     return 1; // No scaling for now
   }
   ```

3. **GetRenderTargetKey()**
   - Extract from current render state
   - Include EDRAM base, format, dimensions

4. **InitializeTraceSubmitDownloads()**
   - Prepare for trace dump capture
   - Set up readback resources

5. **RestoreEdramSnapshot()** 
   - Copy EDRAM data
   - Trigger render target reconstruction
   - Call base class resolution logic

6. **ResolveFromGuest()**
   - Convert EDRAM tiles to textures
   - Handle format conversions
   - Apply to Metal textures

### Phase 4: Integrate Draw Utility Functions (3 hours)
**Goal**: Add normalized color mask and depth control

#### Tasks:
1. Add includes:
   ```cpp
   #include "xenia/gpu/draw_util.h"
   #include "xenia/gpu/render_target_cache.h"
   ```

2. Update IssueDraw():
   ```cpp
   // Calculate normalized values
   uint32_t normalized_color_mask = pixel_shader ? 
       draw_util::GetNormalizedColorMask(regs, 
           pixel_shader->writes_color_targets()) : 0;
   
   reg::RB_DEPTHCONTROL normalized_depth_control = 
       draw_util::GetNormalizedDepthControl(regs);
   
   // Call base Update
   if (!render_target_cache_->Update(is_rasterization_done,
                                     normalized_depth_control,
                                     normalized_color_mask, 
                                     *vertex_shader)) {
     return false;
   }
   ```

3. Remove manual RB_COLOR_INFO reading:
   - Delete existing rt extraction code
   - Let base class handle it

### Phase 5: EDRAM Buffer Integration (4 hours)
**Goal**: Connect Metal's EDRAM buffer with base class

#### Tasks:
1. Implement EDRAM access methods:
   - [ ] GetEDRAMBuffer() for GPU access
   - [ ] MapEDRAM() for CPU access
   - [ ] UnmapEDRAM() after CPU access

2. Tile/untile operations:
   - [ ] ResolveTiledTexture()
   - [ ] LoadTiledTexture()
   - [ ] Use existing untiling code

3. Format conversion:
   - [ ] Map Xbox 360 formats to Metal
   - [ ] Handle compressed formats (DXT)
   - [ ] Color space conversions

### Phase 6: Render Pass Descriptor Generation (3 hours)
**Goal**: Generate MTL::RenderPassDescriptor from base class state

#### Tasks:
1. Query base class for current targets:
   ```cpp
   void GetCurrentRenderTargets(
       RenderTarget* out_color[4],
       RenderTarget* out_depth);
   ```

2. Convert to Metal descriptors:
   - Map RenderTarget to MTL::Texture
   - Set load/store actions
   - Configure clear colors

3. Handle special cases:
   - No render targets → use dummy
   - MSAA resolution
   - MRT setup

### Phase 7: Testing and Validation (4 hours)
**Goal**: Verify refactoring works correctly

#### Test Cases:
1. **Basic Rendering**:
   - [ ] Simple triangle renders
   - [ ] Texture sampling works
   - [ ] Depth testing works

2. **Trace Dumps**:
   - [ ] title_414B07D1_frame_589 (simple)
   - [ ] title_414B07D1_frame_6543 (complex)
   - [ ] Verify non-black output

3. **EDRAM Snapshot**:
   - [ ] RestoreEdramSnapshot creates targets
   - [ ] Render targets persist across draws
   - [ ] Correct format detection

4. **Performance**:
   - [ ] No regression in FPS
   - [ ] Memory usage acceptable
   - [ ] GPU capture still works

## Implementation Order

### Stage 1: Minimal Integration (Day 1)
1. Make MetalRenderTargetCache inherit from base
2. Implement required virtual methods (stubs)
3. Ensure compilation succeeds
4. Test: Should still work as before (black output)

### Stage 2: Base Update() Integration (Day 2)
1. Add normalized color mask calculation
2. Call base Update() in IssueDraw
3. Remove manual register reading
4. Test: May crash, debug issues

### Stage 3: EDRAM Resolution (Day 3)
1. Implement EDRAM buffer access
2. Add render target reconstruction
3. Connect to base class resolution
4. Test: Should see some rendering

### Stage 4: Full Integration (Day 4)
1. Complete all virtual methods
2. Format conversions
3. MSAA support
4. Test: Full trace dump support

## Risk Mitigation

### Potential Issues:
1. **Memory Management**
   - Base class expects specific lifetime
   - Solution: Use shared_ptr/unique_ptr consistently

2. **Threading**
   - Base class may not be thread-safe
   - Solution: Add mutex protection

3. **Performance**
   - Extra abstraction overhead
   - Solution: Profile and optimize hot paths

4. **Format Mismatches**
   - Xbox 360 vs Metal formats
   - Solution: Comprehensive format mapping table

## Success Criteria

1. **Functional**:
   - ✓ Trace dumps render correctly (not black)
   - ✓ RB_COLOR_INFO=0 cases work
   - ✓ EDRAM snapshot restoration works

2. **Quality**:
   - ✓ No memory leaks
   - ✓ No performance regression
   - ✓ Clean code architecture

3. **Compatibility**:
   - ✓ Existing games still work
   - ✓ GPU capture/debugging works
   - ✓ All test traces pass

## File Changes Overview

### Files to Modify:
1. `metal_render_target_cache.h` - Add inheritance, virtual methods
2. `metal_render_target_cache.cc` - Implement virtual methods
3. `metal_command_processor.mm` - Update IssueDraw, add draw_util
4. `metal_command_processor.h` - Update includes

### Files to Add:
1. `metal_render_target.h` - Metal-specific RenderTarget subclass
2. `metal_render_target.cc` - Implementation

### Files to Study:
1. `gpu/render_target_cache.h` - Base class interface
2. `gpu/render_target_cache.cc` - Base implementation
3. `d3d12/d3d12_render_target_cache.*` - Reference implementation
4. `gpu/draw_util.h` - Normalized mask calculations

## Timeline Estimate
- **Total Duration**: 4-5 days
- **Coding Time**: ~20 hours
- **Testing Time**: ~8 hours
- **Debugging Buffer**: +50%

## Next Steps
1. Begin Phase 1: Analyze base class interface
2. Create feature branch: `metal-rt-cache-refactor`
3. Set up incremental testing framework
4. Start implementation with Stage 1