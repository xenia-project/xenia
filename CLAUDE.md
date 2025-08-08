# Xenia macOS ARM64 Port - Claude AI Assistant Reference

This document provides comprehensive information about the Xenia Xbox 360 emulator macOS ARM64 port for AI assistants working on this codebase.

## Project Overview

**Xenia** is an open-source Xbox 360 emulator. This fork implements a complete **native macOS ARM64 port** that runs Xbox 360 games directly on Apple Silicon without Rosetta 2 translation.

### Current Status
- **ARM64 CPU Backend**: ✅ ~Mostly complete (1 test failure remaining, pending upstream merge)
- **Metal Graphics**: 🔄 In development (25% complete, see `src/xenia/gpu/metal/METAL_BACKEND_IMPLEMENTATION_STATUS.md`)
- **Platform Integration**: ✅ Complete macOS support
- **Vulkan Backend**: ❌ Abandoned on macOS due to MoltenVK primitive restart limitations

## Development Memories

- Remember to re-build our current target, xenia-gpu-metal-trace-dump before running.
- Stop creating git commits that are over-exaggerated, like with the word "feat". They should be simple, reflect accurate progress and be professional sounding.

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
│   ├── d3d12/              # Direct3D12 fully functional reference/baseline implementation. (Only supported on Windows)
│   ├── vulkan/             # Vulkan backend (not usable on macOS)
│   └── metal/              # Native Metal backend (in development)
├── kernel/                 # Xbox 360 kernel emulation
├── apu/                    # Audio processing unit
└── ui/                     # User interface (ImGui-based)
```

[... rest of the existing content remains unchanged ...]