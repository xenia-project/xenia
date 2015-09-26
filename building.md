# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### Windows

* Windows 8 or 8.1
* Visual Studio 2015
* [Python 2.7](http://www.python.org/download/releases/2.7.6/)
* If you are on Windows 8, you will also need the [Windows 8.1 SDK](http://msdn.microsoft.com/en-us/windows/desktop/bg162891)

Ensure Python is in your PATH (`C:\Python27\`).

I recommend using [Cmder](http://bliker.github.io/cmder/) for git and command
line usage.

#### Debugging

VS behaves oddly with the debug paths. Open the xenia project properties
and set the 'Command' to `$(SolutionDir)$(TargetPath)` and the
'Working Directory' to `$(SolutionDir)..\..`. You can specify flags and
the file to run in the 'Command Arguments' field (or use `--flagfile=flags.txt`).

By default logs are written to a file with the name of the executable. You can
override this with `--log_file=log.txt`.

If running under Visual Studio and you want to look at the JIT'ed code
(available around 0xA0000000) you should pass `--emit_source_annotations` to
get helpful spacers/movs in the disassembly.

### Linux

Linux support is extremely experimental and presently incomplete.

The build script uses LLVM/Clang 3.8. GCC should also work, but is not easily
swappable right now.

[CodeLite](http://codelite.org) is the IDE of choice and `xb premake` will spit
out files for that. Make also works via `xb build`.

## Running

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable.
