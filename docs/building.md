# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
drivers.

## Setup

### Windows

* Windows 7 or later
* [Visual Studio 2019 or Visual Studio 2017](https://www.visualstudio.com/downloads/)
* [Python 3.4+](https://www.python.org/downloads/)
  * Ensure Python is in PATH.
* Windows 10 SDK

```
git clone https://github.com/xenia-project/xenia.git
cd xenia
xb setup

# Build on command line (add --config=release for release):
xb build


# Pull latest changes, rebase, update submodules, and run premake:
xb pull

# Run premake and open Visual Studio (run the 'xenia-app' project):
xb devenv

# Run premake to update the sln/vcproj's:
xb premake

# Format code to the style guide:
xb format
```
<!--
# Remove intermediate files and build outputs (doesn't work on Linux):
xb clean

# Check for lint errors with clang-format:
xb lint

# Run the style checker on all code:
xb style

# Remove all build/ output and do a hard git reset:
xb nuke

# Runs the clang-tidy checker on all code:
xb tidy


## Testing:

# Generate tests:
xb gentests

# Run tests:
xb test

# Run GPU tests:
xb gputest


## Other:

# Generate SPIR-V binaries and header files:
xb genspirv
-->

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

To get the latest Clang on an Ubuntu system:
```
sudo -E apt-add-repository -y "ppa:ubuntu-toolchain-r/test"
curl -sSL "http://llvm.org/apt/llvm-snapshot.gpg.key" | sudo -E apt-key add -
echo "deb http://llvm.org/apt/precise/ llvm-toolchain-precise main" | sudo tee -a /etc/apt/sources.list > /dev/null
sudo -E apt-get -yq update &>> ~/apt-get-update.log
sudo -E apt-get -yq --no-install-suggests --no-install-recommends --force-yes install clang-4.0 clang-format-4.0
```

You will also need some development libraries. To get them on an Ubuntu system:
```
sudo apt-get install libgtk-3-dev libpthread-stubs0-dev liblz4-dev libglew-dev libx11-dev libvulkan-dev libc++-dev libc++abi-dev
```

In addition, you will need the latest Vulkan libraries and drivers for your hardware.

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

Either restart the computer, or inject the NVIDIA drivers:
```
sudo rmmod nouveau
sudo modprobe nvidia
```

## Running

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable. You
can also specify `--log_file=stdout` to log to stdout rather than a file.
