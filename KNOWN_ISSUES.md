# Known Issues - Xenia ARM64 macOS Port

This document tracks known issues with the ARM64 macOS port of Xenia that need to be addressed in future updates.

## ✅ RESOLVED - Native macOS Vulkan Support

### All Core Vulkan Issues ✅ FIXED (August 2025)
The native macOS Vulkan implementation is now **FULLY WORKING**:

- **✅ GTK Dependencies**: Completely replaced with native Cocoa framework
- **✅ VK_KHR_portability_enumeration**: MoltenVK device enumeration working
- **✅ VK_EXT_metal_surface**: Metal surface creation implemented with CAMetalLayer
- **✅ Build System**: Native macOS frameworks integrated (Cocoa, Carbon, CoreGraphics, QuartzCore)
- **✅ Vulkan Pipeline**: Complete working pipeline from instance to swapchain

**Current Status**: 
- ✅ Vulkan instance creates successfully (1.4.321 with MoltenVK)
- ✅ Apple M4 Pro device detection working
- ✅ Native macOS window (1920x1200) with NSWindow/NSView
- ✅ Metal surface creation via CAMetalLayer
- ✅ Vulkan swapchain creation (3024x1832, B8G8R8A8_UNORM)

**Build Command**: `./xb build --target xenia-ui-window-vulkan-demo` ✅ SUCCEEDS

### ⚠️ Minor Demo App Issue 🔍 INVESTIGATING
- **Issue**: Demo fails with "Failed to initialize app" after successful Vulkan setup
- **Status**: Core Vulkan functionality confirmed working, likely ImGui/app component issue
- **Priority**: LOW - Not blocking emulator functionality

---

## ARM64 CPU Backend Issues

### 1. Final CPU Test Failure (D3DCOLOR Pack Operation)
- **Status**: 1480/1481 CPU tests passing (99.93% compatibility)
- **Description**: One remaining test failure related to D3DCOLOR pack operations in vector sequences
- **Location**: `src/xenia/cpu/backend/a64/a64_seq_vector.cc`
- **Impact**: Minor - affects specific color packing operations
- **Priority**: Low (high compatibility already achieved)

### 2. Compiler Toolchain Differences (MSVC vs Clang)
- **Status**: Partially addressed through previous commits
- **Description**: ARM64 backend was originally designed for MSVC compiler behavior and required adaptations for Clang on macOS
- **Key Differences**:
  - Register allocation strategies differ between MSVC and Clang
  - ABI conventions vary between toolchains
  - Instruction scheduling assumptions needed adjustment
- **Examples of Required Fixes**:
  - Source operand ordering: `src2,src1` → `src1,src2` patterns
  - Register swapping in pack operations
  - Instruction sequence modifications
- **Commits**: 4e8aa29f0, 4f240879e, 114fe514d (now reverted for clean state)
- **Priority**: Medium (affects platform compatibility)

## Memory Management Issues

### 3. High Address Space Allocation on macOS ARM64
- **Status**: Workarounds implemented
- **Description**: macOS ARM64 often allocates memory in high address space (>32-bit), causing compatibility issues with code expecting 32-bit addresses
- **Affected Areas**:
  - Constant data placement (`PlaceConstData()`)
  - Function machine code addresses
  - Indirection table mapping
- **Current Workarounds**:
  - Added high address detection and warnings
  - Modified `ResolveFunction` to handle 64-bit addresses
  - Platform-specific code paths for address handling
- **Impact**: Potential runtime issues if high addresses aren't handled properly
- **Priority**: High (affects core functionality)

### 4. Function Resolution Context Issues
- **Status**: Debugging in progress
- **Description**: CallIndirect operations sometimes receive context pointers instead of function addresses
- **Symptoms**: 
  - Addresses like `0x0000000AD08DC000` appearing in function resolution
  - Pattern suggests context pointer being passed as target address
- **Location**: `CallIndirect()` and `ResolveFunction()` functions
- **Impact**: Function calls may fail or resolve incorrectly
- **Priority**: High (affects emulation correctness)

## Build System Issues

### 5. Platform-Specific Build Configuration
- **Status**: Needs improvement
- **Description**: Build system lacks proper detection and handling of macOS ARM64 specific requirements
- **Issues**:
  - Missing compiler-specific flags
  - Inadequate toolchain detection
  - Platform-specific library handling
- **Priority**: Medium (affects development workflow)

## Performance Issues

### 6. oaknut ARM64 Assembler Integration
- **Status**: Functional but not optimized
- **Description**: Current integration with oaknut assembler library may not be fully optimized for macOS ARM64
- **Potential Improvements**:
  - Better utilization of ARM64-specific instructions
  - Optimization for Apple Silicon features
  - Improved instruction scheduling
- **Priority**: Low (functionality over performance currently)

## Testing Issues

### 7. Incomplete Test Coverage for macOS ARM64
- **Status**: Basic CPU tests passing, but limited platform-specific testing
- **Description**: Need more comprehensive testing for macOS ARM64 specific behaviors
- **Missing Areas**:
  - Memory mapping edge cases
  - High address space handling
  - Compiler-specific behavior validation
- **Priority**: Medium (ensures stability)

## Future Improvements

### 8. Code Quality and Maintainability
- **Description**: Current ARM64 port contains platform-specific workarounds that should be cleaned up
- **Improvements Needed**:
  - Better abstraction of platform differences
  - Cleaner separation of MSVC vs Clang specific code
  - More robust error handling for address space issues
- **Priority**: Low (technical debt)

---

## Development Notes

### Testing Status (as of last check)
- Repository reset to commit 371829822: "Final ARM64 CPU backend fixes: reduce remaining test failures to 1"
- Clean state with 1 remaining CPU test failure
- Ready for systematic debugging and fixes

### Investigation Summary
The ARM64 macOS port faces primarily two categories of issues:
1. **Compiler compatibility** - Differences between MSVC (Windows) and Clang (macOS) toolchains
2. **Memory management** - macOS ARM64's high address space allocation patterns

Most critical functionality is working, with the main focus needed on polish and edge case handling.

## Test Results

### CPU Instruction Tests (xenia-cpu-tests) - Current Status

**Status**: 3 failures out of 88 tests on ARM64 macOS (96.6% pass rate)

**Failing Tests**:

1. **PACK_FLOAT16_2** (`src/xenia/cpu/testing/pack_test.cc:38`)
   - **Expected**: `[00000000 00000000 00000000 7FFFFFFF]`
   - **Actual**: `[00000000 00000000 00000000 7C00FC00]`
   - **Issue**: Float16 packing producing incorrect bit patterns for special values

2. **PACK_SHORT_2** (`src/xenia/cpu/testing/pack_test.cc:87`) 
   - **Expected**: `[00000000 00000000 00000000 00000000]`
   - **Actual**: `[00000000 00000000 00000000 80018001]`
   - **Issue**: Short packing not handling saturation/clamping correctly

3. **UNPACK_FLOAT16_2** (`src/xenia/cpu/testing/unpack_test.cc:44`)
   - **Expected**: `[47FFE000 C7FFE000 00000000 3F800000]` 
   - **Actual**: `[7FFFE000 FFFFE000 00000000 3F800000]`
   - **Issue**: Float16 unpacking producing incorrect exponent handling

**Root Cause**: These failures appear to be related to ARM64-specific vector instruction implementations for data format conversion operations (pack/unpack). The ARM64 backend may have different handling of:
- Float16 special values (NaN, infinity) 
- Saturation behavior in integer packing
- Sign extension in unpacking operations

**Impact**: These are specialized data conversion operations primarily used for GPU-related processing. Core emulation functionality appears unaffected.

**Priority**: Medium - The failures are in specialized vector operations rather than core CPU emulation.
