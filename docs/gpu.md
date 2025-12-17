# GPU Documentation

## The Xenos Chip

The [Xenos](https://en.wikipedia.org/wiki/Xenos_\(graphics_chip\)) is a graphics
chip designed by AMD based off of the R5xx architecture.

### Command Processing

The Xenos runs commands supplied to it directly by the DirectX bare-bones driver
via a ringbuffer located in system memory.

The bulk of the command processing code is located at
[src/xenia/gpu/command_processor.cc](../src/xenia/gpu/command_processor.cc)

### EDRAM

The Xenos uses special high-speed memory located on the same die as the chip to 
store framebuffers/render targets.

TODO: More documentation

## GPU Backends

Xenia supports multiple GPU backends for different platforms:

| Backend | Platform | Status |
|---------|----------|--------|
| D3D12 | Windows | Primary, most complete |
| Vulkan | Windows, Linux | Functional |
| Metal | macOS | Work in progress (required for macOS) |

### Why Metal is Required on macOS

**MoltenVK (Vulkan on Metal) is NOT supported** for Xenia on macOS due to:
- Primitive restart functionality issues that cause rendering corruption
- Other Vulkan feature gaps in the MoltenVK translation layer
- Performance overhead of the Vulkan-to-Metal translation

A native Metal backend is required for proper Xbox 360 GPU emulation on macOS.

### Metal Backend

The Metal backend (`src/xenia/gpu/metal/`) targets macOS with native Metal API support.
This is currently the primary development focus for ARM64 macOS.

**Status**: Early work in progress. Basic infrastructure is in place but trace dump
rendering is not yet functional.

#### Key Files

| File | Purpose |
|------|---------|
| `metal_command_processor.cc` | Core GPU command processing, draw calls |
| `metal_graphics_system.cc` | Metal graphics system initialization |
| `metal_shader.cc` | Metal shader wrapper |
| `metal_shader_converter.cc` | DXIL to Metal IR conversion |
| `metal_shader_cache.cc` | Compiled shader caching |
| `dxbc_to_dxil_converter.cc` | DXBC to DXIL conversion (via Wine) |
| `metal_render_target_cache.cc` | Render target/EDRAM management |
| `metal_texture_cache.cc` | Texture loading and format conversion |
| `metal_shared_memory.cc` | Xbox 360 memory buffer access |
| `metal_pipeline_cache.cc` | Pipeline state object caching |
| `metal_buffer_cache.cc` | Vertex/index buffer management |
| `metal_primitive_processor.cc` | Primitive type conversion |
| `metal_presenter.cc` | Display output presentation |
| `metal_trace_dump_main.cc` | GPU trace replay utility |

#### Shader Translation Pipeline

The Metal backend translates Xbox 360 shader microcode through multiple stages:

```
Xbox 360 Microcode (ucode)
    ↓ DxbcShaderTranslator (shared with D3D12)
DXBC (DirectX Bytecode)
    ↓ dxbc_to_dxil_converter (Wine + dxbc2dxil.exe)
DXIL (DirectX Intermediate Language)
    ↓ metal_shader_converter (Apple Metal Shader Converter)
Metal IR
    ↓ MTLDevice newLibraryWithData
MTLLibrary (GPU-executable)
```

This pipeline leverages:
- **DxbcShaderTranslator**: Existing Xenia infrastructure for microcode → DXBC
- **dxbc2dxil.exe**: Microsoft's shader compiler (run via Wine on macOS)
- **Metal Shader Converter**: Apple's `metalirconverter` library for DXIL → Metal IR

#### Dependencies

- `third_party/metal-cpp/` - C++ bindings for Metal framework
- `third_party/metal-shader-converter/` - Apple's DXIL to Metal IR converter
- Wine (runtime) - For running dxbc2dxil.exe

#### Building Metal Shaders

```bash
./xb buildshaders --target=metal
```

### Vulkan Backend

The Vulkan backend (`src/xenia/gpu/vulkan/`) provides cross-platform support.
On macOS, it runs via MoltenVK but has known issues with primitive restart.

### D3D12 Backend

The D3D12 backend (`src/xenia/gpu/d3d12/`) is the most complete implementation
and serves as the reference for other backends.

## Options

### General

See the top of [src/xenia/gpu/gpu_flags.cc](../src/xenia/gpu/gpu_flags.cc).

`--vsync=false` will attempt to render the game as fast as possible instead of
waiting for a fixed 60hz timer.

### Vulkan

See the top of [src/xenia/gpu/vulkan/vulkan_gpu_flags.cc](../src/xenia/gpu/vulkan/vulkan_gpu_flags.cc).

`vulkan_dump_disasm=true` "Dump shader disassembly. NVIDIA only supported."

## Tools

### GPU Trace Dump

The `xenia-gpu-metal-trace-dump` target replays captured GPU traces for testing
and debugging the Metal backend without requiring a full game to run.

```bash
# Build
./xb build --target=xenia-gpu-metal-trace-dump

# Run
./build/bin/Mac/Checked/xenia-gpu-metal-trace-dump <trace_file>
```

Reference traces are available in `reference-gpu-traces/traces/`:
- `title_414B07D1_frame_589.xenia_gpu_trace` (small, good for testing)
- `title_414B07D1_frame_6543.xenia_gpu_trace` (large)

Reference output images are in `reference-gpu-traces/references/`.

### Shaders

#### Shader Dumps

Adding `--dump_shaders=path/` will write all translated shaders to the given
path with names based on input hash (so they'll be stable across runs).
Binaries containing the original microcode will be placed side-by-side with
the dumped output to make it easy to pipe to `xe-gpu-shader-compiler`.

#### xe-gpu-shader-compiler

A standalone shader compiler exists to allow for quick shader translation
testing. You can pass a binary ucode shader in and get either disassembled
ucode or translated source out. This is best used through the Shader
Playground tool.

```
  xe-gpu-shader-compiler \
      --shader_input=input_file.bin.vs (or .fs)
      --shader_output=output_file.txt
      --shader_output_type=ucode (or spirvtext)
```

#### Shader Playground

Built separately (for now) under [tools/shader-playground/](../tools/shader-playground/)
is a GUI for interactive shader assembly, disassembly, validation, and
translation.

![Shader Playground Screenshot](images/shader_playground.png?raw=true)

Entering shader microcode on the left will invoke the XNA Game Studio
D3D compiler to translate the ucode to binary. The D3D compiler is then
used to disassemble the binary and display the optimized form. If
`xe-gpu-shader-compiler` has been built the ucode will be passed to that
for disassembly and that will then be passed through D3D compiler. If
the output of D3D compiler on the xenia disassembly doesn't match the
original D3D compiler output the box will turn red, indicating that the
disassembly is broken. Finally, the right most box will show the
translated shader in the desired format.

For more information and setup instructions see
[tools/shader-playground/README.md](../tools/shader-playground/README.md).

### xe-gpu-trace-viewer

To quickly iterate on graphical issues, xenia can dump frames (or sequences of
frames) while running that can be opened and inspected in a separate app.

The basic workflow is:

1. Capture the frame in game (using F4) or a stream of frames.
2. Add the file path to the xe-gpu-trace-viewer Debugging command line in
Visual Studio.
3. Launch xe-gpu-trace-viewer.
4. Poke around, find issues, etc.
5. Modify code.
6. Build and relaunch.
7. Goto 4.

#### Capturing Frames

First, specify a path to capture traces to with
`--trace_gpu_prefix=path/file_prefix_`. All files will have a randomish name
based on that.

When running xenia.exe you can hit F4 at any time to capture the next frame the
game tries to draw (up until a VdSwap call). The file can be used immediately.

#### Capturing Sequences

Passing `--trace_gpu_stream` will write all frames rendered to a file, allowing
you to seek through them in the trace viewer. These files will get large.

## References

### Command Buffer/Registers

Registers documented at [src/xenia/gpu/register_table.inc](../src/xenia/gpu/register_table.inc).

PM4 commands documented at [src/xenia/gpu/xenos.h](../src/xenia/gpu/xenos.h#L521).

#### Performance Counters that may be read back by D3D

They are 64-bit values and have a high and low 32-bit register as well as a `SELECT` register each:

- CP_PERFCOUNTER0

- RBBM_PERFCOUNTER0
- RBBM_PERFCOUNTER1

- SQ_PERFCOUNTER0
- SQ_PERFCOUNTER1
- SQ_PERFCOUNTER2
- SQ_PERFCOUNTER3

- VGT_PERFCOUNTER0
- VGT_PERFCOUNTER1
- VGT_PERFCOUNTER2
- VGT_PERFCOUNTER3

- VC_PERFCOUNTER0
- VC_PERFCOUNTER1
- VC_PERFCOUNTER2
- VC_PERFCOUNTER3

- PA_SU_PERFCOUNTER0
- PA_SU_PERFCOUNTER1
- PA_SU_PERFCOUNTER2
- PA_SU_PERFCOUNTER3

- PA_SC_PERFCOUNTER0
- PA_SC_PERFCOUNTER1
- PA_SC_PERFCOUNTER2
- PA_SC_PERFCOUNTER3

- HZ_PERFCOUNTER0
- HZ_PERFCOUNTER1

- TCR_PERFCOUNTER0
- TCR_PERFCOUNTER1

- TCM_PERFCOUNTER0
- TCM_PERFCOUNTER1

- TCF_PERFCOUNTER0
- TCF_PERFCOUNTER1
- TCF_PERFCOUNTER2
- TCF_PERFCOUNTER3
- TCF_PERFCOUNTER4
- TCF_PERFCOUNTER5
- TCF_PERFCOUNTER6
- TCF_PERFCOUNTER7
- TCF_PERFCOUNTER8
- TCF_PERFCOUNTER9
- TCF_PERFCOUNTER10
- TCF_PERFCOUNTER11

- TP0_PERFCOUNTER0
- TP0_PERFCOUNTER1
- TP1_PERFCOUNTER0
- TP1_PERFCOUNTER1
- TP2_PERFCOUNTER0
- TP2_PERFCOUNTER1
- TP3_PERFCOUNTER0
- TP3_PERFCOUNTER1

- SX_PERFCOUNTER0

- BC_PERFCOUNTER0
- BC_PERFCOUNTER1
- BC_PERFCOUNTER2
- BC_PERFCOUNTER3

- MC0_PERFCOUNTER0
- MC1_PERFCOUNTER0

- MH_PERFCOUNTER0
- MH_PERFCOUNTER1
- MH_PERFCOUNTER2

- BIF_PERFCOUNTER0

### Shaders

* [LLVM R600 Tables](https://llvm.org/viewvc/llvm-project/llvm/trunk/lib/Target/AMDGPU/R600Instructions.td)
** The opcode formats don't match, but the name->psuedo code is correct.
* [xemit](https://github.com/gligli/libxemit/blob/master/xemitops.c)

### Metal

* [Metal Best Practices Guide](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/)
* [Metal Shading Language Specification](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf)
* [Metal Shader Converter Documentation](https://developer.apple.com/documentation/metal/shader_conversion)
