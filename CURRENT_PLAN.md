# Xenia Metal Backend Debug Plan

## LATEST DISCOVERY: Pink Output is Uninitialized Dummy Target

### The Real Issue Chain:
1. **Render targets ARE being preserved** - logs show targets stay at 0xb9ad1d400, 0xb9ad1d3e0
2. **Dummy target is created** when no real targets exist (as fallback)
3. **Pink color (255,0,255)** is Metal's default uninitialized texture color
4. **Xbox 360 shaders never render to dummy** - they expect real targets
5. **CaptureColorTarget captures dummy** instead of real rendered content

### Evidence:
- Render target preservation fix IS working (targets no longer null)
- GetRenderPassDescriptor creates dummy when has_any_render_target=false
- CaptureColorTarget falls back to dummy, logs: "Using render cache dummy target"
- Pink is not hardcoded anywhere - it's uninitialized Metal texture

### Root Problems:
1. **Real render targets not properly bound** to render pass descriptor
2. **Shaders render to wrong location** or not at all
3. **Capture system gets dummy** instead of actual rendered content

## Comprehensive Solution Plan

### Phase 1: Fix Render Target Persistence ✅ COMPLETED
- Modified base class to preserve targets when none requested
- Added logic to keep `are_accumulated_render_targets_valid_ = true`
- Result: Targets ARE preserved (0xb9ad1d400, 0xb9ad1d3e0 stay valid)

### Phase 2: Fix Shader Rendering to Real Targets 🔄 IN PROGRESS

#### Problem: Shaders Not Writing to Bound Targets
1. **Verify render pass setup**:
   - Check if real targets properly attached to MTLRenderPassDescriptor
   - Ensure loadAction/storeAction correct (Load/Store not Clear/DontCare)
   - Validate texture formats match pipeline expectations

2. **Debug shader execution**:
   - Add GPU counter to verify fragment shader runs
   - Check if discard_fragment being called
   - Verify viewport/scissor not culling everything

3. **Fix render target binding**:
   - Ensure targets from base class properly cast to MetalRenderTarget*
   - Check texture() returns valid MTLTexture
   - Verify sample count matches between targets and pipeline

## Testing Status

### Current Behavior
- Pink screen from uninitialized dummy render target
- Real targets ARE preserved (0xb9ad1d400, 0xb9ad1d3e0) 
- Shaders execute (AIR shows texture sampling, calculations)
- But output goes to wrong place or gets discarded

### Key Observations
- GetRenderPassDescriptor logs: "No render targets bound, creating dummy"
- This means current_color_targets_[i] or texture() returning null
- Yet base class has valid accumulated_render_targets_
- **Gap between base class state and Metal binding**

## Next Steps

### Immediate Actions:

1. **Fix GetRenderPassDescriptor logic** ✨ PRIORITY
   ```cpp
   // In MetalRenderTargetCache::Update():
   // After getting accumulated_targets from base class
   // Properly store them as current_color_targets_[]
   // So GetRenderPassDescriptor finds them
   ```

2. **Add validation in Update()**:
   - Log texture() for each MetalRenderTarget
   - Verify cast from RenderTarget* to MetalRenderTarget* works
   - Check if textures are actually created

3. **Debug shader output**:
   - Add color write validation
   - Check blend state not discarding
   - Verify fragment shader return values

4. **Fix capture path**:
   - Ensure CaptureColorTarget gets real rendered target
   - Not dummy fallback

### Phase 3: Validate Rendering Pipeline
- Confirm vertices transformed correctly
- Check primitive assembly
- Verify rasterization produces fragments

## Implementation Details

### The Missing Link: MetalRenderTarget Texture Creation
Looking at logs, the issue is likely:
1. Base class creates RenderTarget objects
2. MetalRenderTargetCache::CreateRenderTarget called
3. But texture() might return null if creation failed
4. GetRenderPassDescriptor sees null texture → creates dummy

### Fix Strategy:
```cpp
// In MetalRenderTargetCache::Update()
RenderTarget* const* accumulated_targets = last_update_accumulated_render_targets();

// THIS IS THE KEY FIX:
for (int i = 0; i < 5; i++) {
  if (accumulated_targets[i]) {
    MetalRenderTarget* metal_rt = static_cast<MetalRenderTarget*>(accumulated_targets[i]);
    if (!metal_rt->texture()) {
      // Texture was never created! Create it now
      CreateTextureForRenderTarget(metal_rt);
    }
  }
}
```

## Success Criteria
- Real render targets stay bound throughout frame
- No dummy target creation
- Shaders write to real targets
- CaptureColorTarget gets actual rendered content
- Output shows game graphics, not pink