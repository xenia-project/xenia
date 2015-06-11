# GPU Documentation

## Options

### General

See the top of [src/xenia/gpu/gpu.cc](https://github.com/benvanik/xenia/blob/master/src/xenia/gpu/gpu.cc).

`--vsync=false` will attempt to render the game as fast as possible instead of
waiting for a fixed 60hz timer.

### OpenGL

See the top of [src/xenia/gpu/gl4/gl4_gpu.cc](https://github.com/benvanik/xenia/blob/master/src/xenia/gpu/gl4/gl4_gpu.cc).

Buggy GL implementations can benefit from `--thread_safe_gl`.

## Tools

### Shader Dumps

Adding `--dump_shaders=path/` will write all translated shaders to the given
path with names based on input hash (so they'll be stable across runs).

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

### Shaders

* [LLVM R600 Tables](https://llvm.org/viewvc/llvm-project/llvm/trunk/lib/Target/R600/R600Instructions.td)
** The opcode formats don't match, but the name->psuedo code is correct.
* [xemit](https://github.com/gligli/libxemit/blob/master/xemitops.c)

## Tools

### apitrace

[apitrace](http://apitrace.github.io/) can be used to capture and replay D3D11
call streams. To disable stdout spew first set `XE_OPTION_ENABLE_LOGGING` to 0
in `logging.h`.
