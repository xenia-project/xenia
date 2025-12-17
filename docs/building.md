# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
drivers.

## Quick Start

```bash
# Clone and setup
git clone <repository_url>
cd xenia
./xb setup

# Build a target
./xb build --target=<target_name>

# Build release configuration
./xb build --target=<target_name> --config=release
```

## Build System

Xenia uses a Python build script (`xb`) with Premake5 for project generation.

### Common Commands

| Command | Description |
|---------|-------------|
| `./xb setup` | Initialize submodules and generate project files |
| `./xb premake` | Regenerate project files |
| `./xb build --target=<name>` | Build a specific target |
| `./xb build --config=release` | Build release configuration |
| `./xb buildshaders --target=metal` | Compile Metal shaders |
| `./xb format` | Format code with clang-format |
| `./xb devenv` | Open IDE (Xcode on macOS, VS on Windows) |

### Build Configurations

| Configuration | Description |
|---------------|-------------|
| Checked | Debug runtime, no optimization, full runtime checks |
| Debug | Release runtime, no optimization, debug symbols (default) |
| Release | Full optimization, LTO enabled |

### Output Locations

- Project files: `build/`
- Binaries: `build/bin/<Platform>/<Config>/`
- Object files: `build/obj/`

## Platform-Specific Instructions

### macOS (ARM64 / Apple Silicon)

This is the primary development platform for the ARM64 macOS port.
Intel Mac (x86_64) is not supported.

#### Requirements

- macOS 13.0+ (Ventura or later)
- [Xcode 15+](https://developer.apple.com/xcode/) with command line tools
- [Python 3.6+](https://www.python.org/downloads/)
- [Homebrew](https://brew.sh) package manager

#### Dependencies

```bash
# Install build tools
brew install cmake ninja python3

# Wine is required for DXBC→DXIL shader conversion (Metal backend)
brew install wine-staging
```

#### Metal Backend Dependencies

For the Metal GPU backend, additional setup is required:

1. **Metal Shader Converter** (Apple)
   - Download from: https://developer.apple.com/metal/shader-converter/
   - The `metalirconverter` library is used for DXIL → Metal IR conversion

2. **dxbc2dxil.exe** (Microsoft)
   - Required for DXBC → DXIL conversion
   - Runs via Wine on macOS

#### Building

```bash
./xb setup
./xb build --target=xenia-gpu-metal-trace-dump
```

#### Test Targets

| Target | Purpose |
|--------|---------|
| `xenia-base-tests` | Platform/base utility tests |
| `xenia-cpu-ppc-tests` | PPC instruction tests (167 tests) |
| `xenia-cpu-tests` | HIR/CPU backend tests |
| `xenia-gpu-metal-trace-dump` | GPU trace replay (Metal backend) |

```bash
# Run tests
./build/bin/Mac/Checked/xenia-base-tests
./build/bin/Mac/Checked/xenia-cpu-ppc-tests
./build/bin/Mac/Checked/xenia-cpu-tests

# Run GPU trace dump
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump \
    reference-gpu-traces/traces/title_414B07D1_frame_589.xenia_gpu_trace
```

#### Current Status

- **A64 CPU backend**: Functional, all CPU tests pass
- **Metal GPU backend**: Work in progress, trace dump not yet rendering

#### Why Metal is Required

MoltenVK (Vulkan-on-Metal) is **not supported** due to:
- Primitive restart issues causing rendering corruption
- Other Vulkan feature gaps in the translation layer

A native Metal backend is required for macOS GPU emulation.

#### Debugging on macOS

**Xcode**:
```bash
# Generate and open Xcode project
./xb devenv
```

In Xcode, set the scheme to `xenia-gpu-metal-trace-dump` and configure:
- Arguments: path to trace file
- Working Directory: repository root

**Metal GPU Debugging**:
- Use Xcode's Metal Debugger (Product → Metal → Capture GPU Workload)
- Enable Metal validation in scheme settings for debug builds
- GPU Frame Capture requires running from Xcode

**Instruments**:
- Use the "Metal System Trace" template for GPU performance analysis
- "Time Profiler" for CPU-side performance

### Windows

#### Requirements

- Windows 10 or later
- [Visual Studio 2022 or 2019](https://www.visualstudio.com/downloads/)
  - For VS 2022, MSBuild `v142` must be used; see [#2003](https://github.com/xenia-project/xenia/issues/2003)
- [Python 3.6+](https://www.python.org/downloads/) (ensure in PATH)
- Windows 11 SDK 10.0.22000.0 or newer

#### Building

```bash
xb setup
xb build
```

#### IDE Setup

```bash
# Open Visual Studio with generated solution
xb devenv
```

#### Debugging in Visual Studio

VS behaves oddly with debug paths. In the 'xenia-app' project properties:
- Set 'Command' to `$(SolutionDir)$(TargetPath)`
- Set 'Working Directory' to `$(SolutionDir)..\..`
- Use 'Command Arguments' for flags or `--flagfile=flags.txt`

Logs are written to a file named after the executable by default.
Override with `--log_file=log.txt` or `--log_file=stdout`.

For JIT debugging, pass `--emit_source_annotations` to get helpful
spacers in the disassembly around 0xA0000000.

### Windows ARM64

ARM64 Windows support exists via the A64 backend (originally developed by
[wunkolo](https://github.com/Wunkolo/xenia/tree/arm64-windows)) but is
currently untested on this fork.

### Linux

Linux support is experimental and incomplete.

#### Requirements

- LLVM/Clang 9+ (GCC may work but is not tested)
- Development libraries:

```bash
# Ubuntu/Debian
sudo apt-get install libgtk-3-dev libpthread-stubs0-dev liblz4-dev \
    libx11-dev libx11-xcb-dev libvulkan-dev libsdl2-dev \
    libiberty-dev libunwind-dev libc++-dev libc++abi-dev
```

- Vulkan drivers for your hardware

#### Building

```bash
./xb setup
./xb build
```

#### IDE Support

- [CodeLite](https://codelite.org): `xb devenv` generates workspace
- [CLion](https://www.jetbrains.com/clion/): `xb premake --devenv=cmake`

## Shader Compilation

```bash
# Build shaders for a specific backend
./xb buildshaders --target=metal
./xb buildshaders --target=spirv
```

## Testing

```bash
# Build and run default tests
./xb test

# Run specific test target
./xb test --target=xenia-cpu-ppc-tests

# Generate PPC test binaries from assembly
./xb gentests
```

## Troubleshooting

### Build Errors

Redirect build output to a file and grep for errors:

```bash
./xb build --target=<name> 2>&1 | tee build.log
grep -E "error:" build.log
```

### Regenerating Projects

If build issues persist after pulling changes:

```bash
./xb premake
./xb build --target=<name>
```

### Clean Build

```bash
rm -rf build/
./xb setup
./xb build --target=<name>
```
