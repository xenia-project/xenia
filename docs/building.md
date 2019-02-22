# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### Windows

* Windows 8 or later
* Visual Studio 2017 
* Windows Universal CRT SDK
* [Python 3.4+](https://www.python.org/downloads/)
* You will also need the [Windows 8.1 SDK](https://msdn.microsoft.com/en-us/windows/desktop/bg162891)
  * (for VS2017 just click the Windows 8.1 SDK option in the Individual Components section in the Visual Studio Installer)

Ensure Python is in your PATH.

I recommend using [Cmder](https://cmder.net/) for git and command
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

[CodeLite](https://codelite.org) is the IDE of choice and `xb premake` will spit
out files for that. Make also works via `xb build`.

To get the latest Clang on an ubuntu system:

```
sudo -E apt-add-repository -y "ppa:ubuntu-toolchain-r/test"
curl -sSL "http://llvm.org/apt/llvm-snapshot.gpg.key" | sudo -E apt-key add -
echo "deb http://llvm.org/apt/precise/ llvm-toolchain-precise main" | sudo tee -a /etc/apt/sources.list > /dev/null
sudo -E apt-get -yq update &>> ~/apt-get-update.log
sudo -E apt-get -yq --no-install-suggests --no-install-recommends --force-yes install clang-4.0 clang-format-4.0
```

You will also need some development libraries. To get them on an ubuntu system:
```
sudo apt-get install libgtk-3-dev libpthread-stubs0-dev liblz4-dev libglew-dev libx11-dev libvulkan-dev libc++-dev libc++abi-dev
```

In addition, you will need the latest OpenGL libraries and drivers for your hardware. Intel and the open source
drivers are not supported as they do not yet support OpenGL 4.5.

#### Linux NVIDIA Vulkan Drivers

You'll need to install the latest NVIDIA drivers to enable Vulkan support on Linux.

First, remove all existing NVIDIA drivers:

```
sudo apt-get purge nvidia*
```

Add the graphics-drivers PPA to your system:

```
sudo add-apt-repository ppa:graphics-drivers
sudo apt update
```

Install the NVIDIA drivers (newer ones may be released after 387; check online):

```
sudo apt install nvidia-387
```

Either reboot the computer, or inject the NVIDIA drivers:

```
sudo rmmod nouveau
sudo modprobe nvidia
```

## Running

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable. You
can also specify `--log_file=stdout` to log to stdout rather than a file.
