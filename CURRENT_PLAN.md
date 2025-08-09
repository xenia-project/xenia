# Metal Backend Debug Plan - Issues to Fix

## Current Status (2025-08-08)

### ✅ Major Achievements
- **Fixed texture dumping** - All textures are being captured correctly including the A-Train HX logo  
- **Fixed vertex shader texture binding** - Textures are now uploaded and bound for vertex shaders
- **Fixed command buffer commits** - Draws are being sent to GPU (though batching needs refinement)
- **Fixed render encoder lifecycle** - endEncoding is being called properly
- **Textures load correctly** - We can see the logo in texture dumps
- **MAJOR: Successfully refactored RenderTargetCache** - Complete architectural alignment with D3D12/Vulkan
  - MetalRenderTargetCache now inherits from base gpu::RenderTargetCache class
  - Base class properly manages EDRAM and creates render targets (verified in logs)
  - Successfully retrieving targets via `last_update_accumulated_render_targets()`
  - Architecture now matches functional D3D12/Vulkan backends pattern
- **Render target binding working** - Targets are created and bound to Metal render passes
- **Capture functioning** - Getting non-zero pixel data (pink output vs black)

### 🔍 Key Findings from Refactoring

1. **Architecture is correct**: The refactoring approach was validated - inheriting from base RenderTargetCache is the right pattern
2. **Base class is working**: Creating render targets with correct keys (0x00104000 for color, 0x003042E0 for depth)
3. **Dummy target behavior is expected**: When RB_COLOR_INFO registers are zero, no targets are bound (normal for many draws)
4. **Capture is working**: We're successfully reading pixel data from GPU (non-zero values confirmed)

### ✅ Latest Progress

- **Fixed capture to use real render targets** - Now tracking and capturing from last REAL render target
- **Confirmed real RT binding** - 320x720 (key 0x00104000) and 1280x720 (key 0x00008000) targets are bound
- **Capture working correctly** - Reading from actual game RT, not dummy target

### ✅ Fixed Issues

**ENCODER LIFECYCLE** - Fixed! Now properly reusing encoders across draws
- Encoder is created once and reused for multiple draws with same render state
- Only creates new encoder when render targets or command buffer changes
- Properly ends encoder before command buffer commit

### ❌ ROOT CAUSE IDENTIFIED

**MISSING NDC TRANSFORMATION** - Xbox 360 vertices not transformed to Metal NDC!
- Xbox 360 vertex shaders output in custom coordinate system (NOT standard NDC)
- D3D12 and Vulkan backends apply NDC scale/offset transformation
- **Metal backend is passing vertices through unchanged** - THIS IS THE BUG!
- Vertices appear to have swizzled coordinates: `[-0.5, -0.5, 1279.5]` where 1279.5 is actually screen X
- Need to apply same transformation as Vulkan: `position.xyz = position.xyz * ndc_scale + ndc_offset * position.w`

### ❌ Other Remaining Issues

1. **GPU capture file corruption** - Files generated but Xcode reports "index file does not exist"
2. **Inefficient command buffer batching** - Arbitrarily committing every 5 draws
3. **Need to implement ownership transfers** - Base class expects transfers for proper RT management

## Detailed Fix Plan

### Issue 1: GPU Capture File Corruption
**Problem**: GPU trace files report "The index file does not exist. The capture may be incomplete or corrupt."

**Root Cause**: Programmatic capture lifecycle issues - likely starting/stopping multiple times or not ending properly.

**Solution**:
```cpp
// In BeginSubmission() - only start capture ONCE at beginning
static bool capture_started = false;
if (is_trace_dump && !capture_started && first_submission) {
  debug_utils_->BeginProgrammaticCapture();
  capture_started = true;
}

// In ShutdownContext() - end capture ONCE at end
if (is_trace_dump && capture_started) {
  debug_utils_->EndProgrammaticCapture();
  capture_started = false;
}
```

**Files to modify**:
- `src/xenia/gpu/metal/metal_command_processor.mm`
- `src/xenia/gpu/metal/metal_debug_utils.cc`

### Issue 2: Command Buffer Batching Strategy  
**Problem**: Committing every 5 draws arbitrarily breaks GPU capture and is inefficient.

**Current broken code**:
```cpp
if (draws_in_current_batch_ >= 5) {
  EndSubmission(false);
  draws_in_current_batch_ = 0;
  BeginSubmission(true);
}
```

**Solution**: Accumulate ALL draws in single command buffer, commit once at end:
```cpp
// In IssueDraw() for trace dump mode:
if (is_trace_dump) {
  // Just accumulate draws, don't commit
  XELOGI("Trace dump: Accumulated draw {}", draws_in_current_batch_);
}

// In ShutdownContext() or after WaitOnPlayback():
if (is_trace_dump && submission_open_) {
  XELOGI("Trace dump: Committing all {} draws", draws_in_current_batch_);
  EndSubmission(false);
}
```

**Files to modify**:
- `src/xenia/gpu/metal/metal_command_processor.mm` - Remove 5-draw batching

### Issue 3: Redundant Default Render Target Creation
**Problem**: Creating new default RT for every draw with no RT specified (11 times).

**Current broken code**:
```cpp
if (rt_count == 0) {
  // Creating NEW default RT every time!
  color_targets[0] = 0x00001000 | 0x00000200 | 0x00000028;
  rt_count = 1;
}
```

**Solution**: Cache and reuse single default RT:
```cpp
// Class member:
uint32_t default_trace_rt_edram_base_ = 0;

// In SetupContext():
if (is_trace_dump) {
  default_trace_rt_edram_base_ = 0x00001000;
}

// In IssueDraw():
if (rt_count == 0 && default_trace_rt_edram_base_) {
  color_targets[0] = default_trace_rt_edram_base_ | 0x00000200 | 0x00000028;
  rt_count = 1;
}
```

**Files to modify**:
- `src/xenia/gpu/metal/metal_command_processor.h` - Add member variable
- `src/xenia/gpu/metal/metal_command_processor.mm` - Implement caching

### Issue 4: Remove Hardcoded Warning
**Problem**: "CRITICAL ISSUES DETECTED" always shown even with no issues.

**Solution**: Find and remove/conditionally show in script:
```bash
# Change from:
echo "CRITICAL ISSUES DETECTED: Textures not being uploaded"

# To:
if grep -q "Failed to upload.*texture" "$LOG_FILE"; then
  echo "CRITICAL: Texture upload failures detected"
fi
```

**Files to modify**:
- `tools/metal_debug/debug_metal.sh`

## Previous GPU Capture Analysis

1. **ResourceDescriptorHeap is COMPLETELY EMPTY** (all zeros)
   - Despite logs showing "setTexture 0x..." at heap slots
   - IRDescriptorTableSetTexture is being called but not writing data
   
2. **IR_TopLevelAB_PS is TOO SMALL** (only 24 bytes)
   - Should be 120 bytes for shader with T0, T1, CB0, CB1, S0
   - Currently only has space for 1 entry (CB0)
   
3. **GPU Capture shows ONLY ONE DRAW CALL**
   - 4 vertices at position (0,0,0,1) - likely a clear operation
   - Later textured draws aren't being captured
   - Command buffer might be committing too early

### ✅ What's Working
- Texture upload (logs show "Uploaded untiled texture data")
- Texture pointers are valid (0x000000010291B740 etc.)
- Heap indices parsed correctly from reflection ([65535, 0, 1] for textures, [4] for samplers)
- Shader compilation successful with texture sampling instructions

### ❌ What's Broken

#### 1. Descriptor Heap Population
```cpp
// PROBLEM: This is being called but heap remains empty
::IRDescriptorTableSetTexture(&res_entries[heap_slot], texture, /*minLODClamp*/0.0f, /*flags*/0);
```
**Possible causes:**
- IRDescriptorTableSetTexture might be a no-op or broken
- The heap buffer might be getting cleared after population
- The texture's gpuResourceID might be invalid

#### 2. Argument Buffer Size Calculation
```cpp
// WRONG: Only allocates 24 bytes (1 entry)
size_t ps_ab_size = kEntry; // Default size if no layout
if (!ps_layout.empty()) {
    ps_ab_size = ps_layout.back().elt_offset + kEntry;
}
```
**Issue:** When ps_layout is populated, the size should be calculated correctly, but it's not.

#### 3. Texture Binding Flow
- Heap indices `[65535, 0, 1]` mean:
  - 65535 = framebuffer fetch (skip)
  - 0 = first texture at heap slot 0
  - 1 = second texture at heap slot 1
- We only have 1 texture binding from shader analysis
- Using fallback to reuse texture at slot 0 for slot 1

### 📊 Texture Binding Architecture (MSC Dynamic Resources)

```
Shader → [[buffer(2)]] Top-Level AB → Contains pointers to heaps
                ↓
         [[buffer(0)]] Resource Heap (textures)
         [[buffer(1)]] Sampler Heap
```

**Top-Level AB Structure:**
- Each entry is 24 bytes (IRDescriptorTableEntry)
- For SRV/Sampler: Points to the heap buffer
- For CBV: Contains direct buffer pointer

**Resource/Sampler Heap Structure:**
- Array of IRDescriptorTableEntry (24 bytes each)
- Indexed by ShaderResourceViewIndices/SamplerIndices
- Must be sparse (gaps allowed)

## 🔧 DEBUGGING PLAN

### Phase 1: Fix Command Buffer Capture
1. **Find why only one draw is captured**
   - Check when command buffers are committed
   - Look for early EndSubmission calls
   - Verify trace playback isn't ending prematurely

2. **Add logging for command buffer lifecycle**
   ```cpp
   XELOGI("Creating command buffer");
   XELOGI("Committing command buffer");
   XELOGI("Starting new command buffer");
   ```

### Phase 2: Fix Descriptor Heap Population
1. **Verify IRDescriptorTableSetTexture works**
   - Log the descriptor contents after writing
   - Check texture->gpuResourceID() is valid
   - Verify heap buffer isn't being overwritten

2. **Debug heap population timing**
   - Ensure heaps are populated BEFORE draw
   - Check if heaps persist across draws
   - Verify heap buffers are marked resident

### Phase 3: Fix Argument Buffer Sizes
1. **Fix PS argument buffer allocation**
   ```cpp
   // Should be:
   if (!ps_layout.empty()) {
       ps_ab_size = ps_layout.size() * kEntry;
       // OR
       ps_ab_size = ps_layout.back().elt_offset + kEntry;
   }
   ```

2. **Verify reflection parsing**
   - Log all TopLevelAB entries
   - Ensure layout matches shader expectations

### Phase 4: Validate Texture Access
1. **Check shader is accessing textures correctly**
   - Decompile metallib to verify texture sampling code
   - Check if indices match heap slots
   - Verify sampler associations

2. **Test with simplified case**
   - Create a test with single texture
   - Verify it appears in heap
   - Check if shader can sample it

## 🐛 Immediate Actions

1. **Fix command buffer capture issue** - Most critical
2. **Add heap content verification** after population
3. **Fix PS argument buffer size calculation**
4. **Add texture validation** before writing to heap

## 📝 Code Locations

- Heap population: `metal_command_processor.mm:2218-2250`
- AB size calculation: `metal_command_processor.mm:2320-2330`
- Texture collection: `metal_command_processor.mm:1890-2045`
- Command buffer management: `metal_command_processor.mm` (BeginSubmission/EndSubmission)

## 🎯 Success Criteria

1. GPU capture shows multiple draw calls (not just one)
2. ResourceDescriptorHeap has non-zero texture descriptors at slots 0 and 1
3. IR_TopLevelAB_PS is correctly sized (120+ bytes for textured shaders)
4. PNG output shows actual game graphics instead of black
5. Metal debugger shows textures in argument buffers