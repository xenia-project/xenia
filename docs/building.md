# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### Windows

* Windows 8 or 8.1
* Visual Studio 2015
* [Python 2.7](https://www.python.org/downloads/release/python-2713/)
* If you are on Windows 8, you will also need the [Windows 8.1 SDK](http://msdn.microsoft.com/en-us/windows/desktop/bg162891)

Ensure Python is in your PATH (`C:\Python27\`).

I recommend using [Cmder](http://cmder.net/) for git and command
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

To get the latest Clang on an ubuntu system:

```
sudo -E apt-add-repository -y "ppa:ubuntu-toolchain-r/test"
curl -sSL "http://llvm.org/apt/llvm-snapshot.gpg.key" | sudo -E apt-key add -
echo "deb http://llvm.org/apt/precise/ llvm-toolchain-precise main" | sudo tee -a /etc/apt/sources.list > /dev/null
sudo -E apt-get -yq update &>> ~/apt-get-update.log
sudo -E apt-get -yq --no-install-suggests --no-install-recommends --force-yes install clang-3.8 clang-format-3.8
```

## Running

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable.
