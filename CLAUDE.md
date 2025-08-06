# Xenia macOS ARM64 Port - Claude AI Assistant Reference

This document provides comprehensive information about the Xenia Xbox 360 emulator macOS ARM64 port for AI assistants working on this codebase.

## Project Overview

**Xenia** is an open-source Xbox 360 emulator. This fork implements a complete **native macOS ARM64 port** that runs Xbox 360 games directly on Apple Silicon without Rosetta 2 translation.

### Current Status
- **ARM64 CPU Backend**: ✅ ~Mostly complete (1 test failure remaining, pending upstream merge)
- **Metal Graphics**: 🔄 In development (25% complete, see `src/xenia/gpu/metal/METAL_BACKEND_IMPLEMENTATION_STATUS.md`)
- **Platform Integration**: ✅ Complete macOS support
- **Vulkan Backend**: ❌ Abandoned on macOS due to MoltenVK primitive restart limitations

## Repository Structure

### Core Source Code
```
src/xenia/
├── base/                    # Platform abstraction layer
│   ├── threading_mac.cc     # macOS-specific threading with pthread/mach
│   ├── clock_mac.cc         # High-precision timing using mach_absolute_time
│   └── memory_mac.cc        # Guest memory virtualization for ARM64
├── cpu/
│   └── backend/a64/         # ARM64 JIT backend (note: called "a64" not "arm64")
├── gpu/
│   ├── vulkan/             # Vulkan backend (not usable on macOS)
│   └── metal/              # Native Metal backend (in development)
├── kernel/                 # Xbox 360 kernel emulation
├── apu/                    # Audio processing unit
└── ui/                     # User interface (ImGui-based)
```

### Why Vulkan Was Abandoned on macOS

The Vulkan backend was abandoned on macOS because MoltenVK (the Vulkan-to-Metal translation layer) has a critical limitation: Metal **always** treats 0xFFFF/0xFFFFFFFF as primitive restart indices with no way to disable this behavior. Xbox 360 games sometimes use these values as legitimate vertex indices when primitive restart is disabled, causing geometry corruption. The native Metal backend solves this by pre-processing index buffers when necessary.

### macOS-Specific Implementation Files

#### Threading System (`src/xenia/base/threading_mac.cc`)
- **Purpose**: Native macOS threading using pthread + mach APIs  
- **Key Features**:
  - Objective-C autorelease pool management for all threads
  - ARM64 JIT write protection handling (`pthread_jit_write_protect_np`)
  - Mach thread port management for high-precision scheduling
  - Windows-compatible alertable wait implementation

#### Metal GPU Backend (`src/xenia/gpu/metal/`)
**Status**: Work in progress (25% complete) - see `METAL_BACKEND_IMPLEMENTATION_STATUS.md` for details
```
metal/
├── metal_command_processor.{cc,h}    # Central GPU command processing
├── metal_shader.{cc,h}              # Xbox 360 shader conversion (DXIL→MSL)
├── metal_pipeline_cache.{cc,h}      # Render pipeline state management
├── metal_buffer_cache.{cc,h}        # Vertex/index buffer management
├── metal_texture_cache.{cc,h}       # Texture format conversion & caching
├── metal_render_target_cache.{cc,h} # EDRAM simulation & render targets
├── metal_presenter.{cc,h}           # CAMetalLayer presentation
├── dxbc_to_dxil_converter.{cc,h}   # DXBC→DXIL via Wine integration
└── metal_primitive_processor.{cc,h} # Geometry processing
```

#### Platform Integration
- **Kernel**: `src/xenia/kernel/xobject.cc` - Fixed handle cleanup for pthread_exit
- **Memory**: Dynamic indirection tables for ARM64 48-bit address space
- **FFmpeg**: ARM64-native libraries for video/audio decoding

#### A64 Backend macOS Changes (`src/xenia/cpu/backend/a64/`)
**Important**: Several changes were made to get the a64 backend working on macOS with clang:
- **JIT Thread Cleanup**: Added thread-local cleanup for `pthread_jit_write_protect_np`
- **Clang Compatibility**: Fixed various compilation issues with Apple's clang
- **Memory Management**: ARM64-specific guest memory handling
- **Note**: Some changes may need `XE_PLATFORM_MAC` guards when upstreaming

## Third-Party Dependencies

### macOS-Specific Dependencies

#### 1. **metal-cpp** (`third_party/metal-cpp/`)
- **Purpose**: Apple's official C++ wrapper for Metal API
- **Integration**: Provides C++ interface to Metal without Objective-C
- **Status**: ✅ Fully integrated

#### 2. **metal-shader-converter** (`third_party/metal-shader-converter/`)
- **Purpose**: Apple's DXIL → Metal Shading Language converter
- **Location**: `/opt/metal-shaderconverter/` (system installation)
- **Integration**: `metal-shader-converter.lua` premake configuration
- **Requirements**: macOS 13+, Xcode 15+
- **Status**: ✅ Working shader conversion pipeline

#### 3. **dxbc2dxil** (`third_party/dxbc2dxil/`)
- **Purpose**: Microsoft's DXBC → DXIL conversion via Wine
- **Integration**: Wine subprocess execution for DirectX shader conversion
- **Dependencies**: Wine (via Homebrew), Microsoft DirectX Shader Compiler
- **Status**: ✅ Complete integration with caching

#### 4. **DirectXShaderCompiler** (`third_party/DirectXShaderCompiler/`)
- **Purpose**: Xbox 360 microcode → DXBC translation
- **Usage**: DxbcShaderTranslator for Xbox 360 shader translation
- **Status**: ✅ Inherited from upstream Xenia

### Standard Dependencies
- **SDL2**: Cross-platform window management and input
- **FFmpeg**: ARM64-native builds for media playback
- **ImGui**: Immediate mode GUI for debugging and tools

## Shader Translation Pipeline

### Complete Xbox 360 → Metal Conversion
```
Xbox 360 Microcode → DxbcShaderTranslator → DXBC → Wine/dxbc2dxil → DXIL → Metal Shader Converter → Metal Shading Language
```

**Implementation**:
1. **Xbox 360 Microcode**: Original console shader bytecode
2. **DXBC Translation**: `DxbcShaderTranslator` converts to DirectX bytecode  
3. **DXIL Conversion**: Wine subprocess calls Microsoft's `dxbc2dxil.exe`
4. **Metal Conversion**: Apple's Metal Shader Converter creates MSL
5. **Metal Compilation**: Native Metal library creation and caching

**Performance**: ~50-100ms per shader with caching to avoid recompilation

## Build System

### macOS Build Dependencies

#### Required Software
```bash
# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install Wine for DXBC→DXIL conversion
brew install wine-staging

# Install build tools
brew install cmake ninja python3

# Download Metal Shader Converter (manual)
# Visit: https://developer.apple.com/metal/shader-converter/
# Install to: /opt/metal-shaderconverter/

# Get dxbc2dxil.exe (via Wine)
# Download from: https://github.com/microsoft/DirectXShaderCompiler
# Place in: third_party/dxbc2dxil/bin/
```

#### System Requirements
- **macOS**: 13.0+ (Ventura or later)
- **Xcode**: 15.0+ (for Metal Shader Converter)
- **Architecture**: Apple Silicon (M1/M2/M3/M4) or Intel Mac

### Build Commands (`./xb` script)

#### Essential Commands
```bash
# Initial setup (run once)
./xb setup

# Build all targets (default: Checked configuration on Mac)
./xb build
./xb build --config=release

# Update all dependencies
./xb pull

# Code formatting
./xb format

# Generate development environment
./xb devenv
```

#### Testing Commands (macOS ARM64)

**Note**: Default build output is `./build/bin/Mac/Checked/` (not Debug)

```bash
# Generate test binaries  
./xb gentests

# Run all automated tests
./xb test

# Test targets in order of development:
# 1. Base library tests (all passing)
./build/bin/Mac/Checked/xenia-base-tests

# 2. PPC instruction tests
./build/bin/Mac/Checked/xenia-cpu-ppc-tests

# 3. CPU backend tests (1 failing test remaining)
./build/bin/Mac/Checked/xenia-cpu-tests

# 4. Vulkan UI demo (abandoned - primitive restart issue)
# ./build/bin/Mac/Checked/xenia-ui-window-vulkan-demo

# 5. Vulkan trace dump (abandoned - primitive restart issue) 
# ./build/bin/Mac/Checked/xenia-gpu-vulkan-trace-dump

# 6. Metal UI demo (fully working)
./build/bin/Mac/Checked/xenia-ui-window-metal-demo

# 7. Metal trace dump (current development target)
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace

# GPU regression tests (currently non-functional)
# Note: ./xb gputest doesn't recognize Metal trace dump yet
./xb gputest
```

### GPU Test Data

#### Test Traces (`testdata/reference-gpu-traces/`)
- **Purpose**: Regression testing for GPU backends
- **Format**: Binary Xbox 360 GPU command captures (.xenia_gpu_trace)
- **Reference Images**: Expected rendering output (.png)
- **Usage**: `./xb gputest` runs automated comparisons

**Available Traces**:
- `title_414B07D1_frame_589.xenia_gpu_trace` - A-Train HX sample frame
- `title_414B07D1_frame_6543.xenia_gpu_trace` - Additional A-Train HX frame

#### GPU Test Commands (Currently Non-Functional)
```bash
# ISSUE: ./xb gputest doesn't recognize xenia-gpu-metal-trace-dump
# TODO: Modify xb script to use Metal backend on macOS

# These commands will work once Metal backend is complete:
./xb gputest                                     # Run all GPU tests
./xb gputest --generate_missing_reference_files  # Generate missing references
./xb gputest --update_reference_files           # Update reference imagery
open build/gputest/results.html                 # View results

# Current workaround: manually run trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump testdata/reference-gpu-traces/traces/*.xenia_gpu_trace
```

## Documentation

### Available Documentation (`docs/`)
- **`building.md`**: Build system and platform setup
- **`cpu.md`**: CPU backend architecture and JIT compilation
- **`gpu.md`**: GPU backend architecture and graphics pipeline
- **`kernel.md`**: Xbox 360 kernel emulation details
- **`style_guide.md`**: Code formatting and style requirements
- **`instruction_tracing.md`**: Debugging and profiling tools

### Project-Specific Documentation
- **`MACOS_ARM64_PORT_STATUS.md`**: Complete macOS port status and roadmap
- **`src/xenia/gpu/metal/METAL_BACKEND_IMPLEMENTATION_STATUS.md`**: Comprehensive Metal backend analysis and roadmap
- **`src/xenia/gpu/vulkan/VULKAN_BACKEND_ANALYSIS.md`**: Vulkan backend architecture (reference only, not usable on macOS)
- **`src/xenia/gpu/d3d12/D3D12_BACKEND_ANALYSIS.md`**: Reference D3D12 implementation details

## Development History

### Key Contributors
- **Wunkolo**: ARM64 CPU backend implementation (April-June 2024)
- **Will**: macOS platform port and Metal GPU backend (November 2024-present)

### Major Milestones
1. **ARM64 CPU Backend** (Wunkolo, 2024): Complete PowerPC → ARM64 JIT translation
2. **macOS Platform Support** (November 2024): Threading, memory, build system
3. **Vulkan Attempt** (August 2025): Abandoned due to MoltenVK primitive restart limitations
4. **Metal Infrastructure** (August 2025): Native Metal backend development begun

### Current Branch: `metal-gpu-backend-implementation`
- **Base**: Latest upstream Xenia with ARM64 support
- **Changes**: 15+ commits implementing Metal GPU backend
- **Status**: Functional shader pipeline, missing EDRAM simulation

## Development Guidelines

### Code Style
- Follow existing Xenia style guide (`docs/style_guide.md`)
- Use `./xb format` before committing
- Maintain compatibility with upstream Xenia architecture

### Metal Backend Development
- **Priority 1**: EDRAM simulation (10MB Metal buffer)
- **Priority 2**: CAMetalLayer presentation pipeline  
- **Priority 3**: Copy operations and resolve pipeline
- **Priority 4**: Complete Xbox 360 format support

### Testing Requirements
- All base tests must pass: `./build/bin/Darwin/Debug/xenia-base-tests`
- CPU tests should have ≤1 failure: `./build/bin/Darwin/Debug/xenia-cpu-tests`
- GPU tests validate graphics backends: `./xb gputest`

### Platform-Specific Considerations
- **ARM64 Memory**: Use dynamic indirection tables for address space management
- **JIT Protection**: Handle `pthread_jit_write_protect_np` for thread safety
- **Autorelease Pools**: Required for all threads using Metal/Objective-C APIs
- **Mach Threads**: Use mach ports for precise thread scheduling

## Known Issues

### Metal Backend Development
- **GPU Tests**: `./xb gputest` doesn't recognize `xenia-gpu-metal-trace-dump` target
- **EDRAM**: Not yet implemented, preventing real Xbox 360 rendering
- **Formats**: Only 9/50+ Xbox 360 texture formats supported

## Common Issues and Solutions

### Build Issues
- **Missing Dependencies**: Run `./xb setup` to install required tools
- **Xcode Version**: Requires Xcode 15+ for Metal Shader Converter
- **Wine Installation**: Use Homebrew: `brew install wine-crossover`

### Runtime Issues
- **JIT Crashes**: Ensure proper JIT write protection handling in threading
- **Memory Errors**: Check guest memory mapping and indirection tables
- **Graphics Issues**: Verify MoltenVK installation for Vulkan backend

### Metal Backend Development
- **Shader Conversion Failures**: Check Wine and dxbc2dxil.exe availability
- **Validation Errors**: Use Metal validation layer for debugging
- **Performance Issues**: Profile using Xcode GPU debugger

This reference document should be updated as the macOS ARM64 port continues development, particularly as the Metal backend progresses toward production readiness.