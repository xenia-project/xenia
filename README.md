Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. It does not run games (yet),
and if you are unable to understand that please leave now.

Currently supported features:

* Nothing!

Coming soon (maybe):

* Everything!

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. <b>It is not for enabling
illegal activity</b>. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).

## Quickstart

    git clone https://github.com/benvanik/xenia.git
    cd xenia && source xeniarc
    xb setup
    xb build
    ./bin/xenia-run some.xex

## Requirements

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

#### Windows

Ensure you have the latest patched version of Visual Studio 2010, the
Windows SDK, the full DirectX SDK, and the Kinect SDK.

* Visual Studio 2010 (not sure Express will work)
  * [Visual Studio 2010 SP1](http://msdn.microsoft.com/en-us/vstudio/aa718359)
* [Windows SDK](http://www.microsoft.com/download/en/details.aspx?id=8279)
* [DirectX SDK](http://msdn.microsoft.com/en-us/directx/)
* [Kinect SDK](http://www.kinectforwindows.org/download/)
* [Python 2.7](http://www.python.org/download/releases/2.7.3/)

Make sure that Python is on your PATH.
Use the Visual Studio 2010 x64 command prompt.

There's a bug in VC++ that breaks with an internal error when building LLVM.
Change line 87 of include/llvm/ADT/StringExtras.h:
```
-static inline std::string utostr(uint64_t X, bool isNeg = false) {
+static __declspec(noinline) std::string utostr(uint64_t X, bool isNeg = false) {
```

#### OS X

Only tested on OS X 10.8 (Mountain Lion).

* [Xcode 4](http://developer.apple.com/xcode/) + command line tools
* [Homebrew](http://mxcl.github.com/homebrew/)

#### Linux

Only tested on Ubuntu 12.10.

* random other things (`sudo apt-get install make flex bison texinfo`)
* clang (`sudo apt-get install clang`)

Pretty much just install what's asked for as you try to `xb setup` or
`xb build`.

## Building

A simple build script is included to manage basic tasks such as building
dependencies.

    ./xenia-build.py --help

The `xeniarc` bash file has some aliases that save some keypresses.

    source xeniarc
    xb  -> python xenia-build.py
    xbb -> python xenia-build.py build
    xbc -> python xenia-build.py clean

### Commands

#### setup

Run this on initial checkout to pull down all dependencies and setup LLVM.

    xb setup

#### pull

Does a `git pull` in addition to updating submodules and rebuilding dependencies
and gyp outputs. Use this, if possible, instead of git pull.

    xb pull

#### gyp

Updates all of the supported gyp projects. If you're using Visual Studio or
Xcode to build or debug your projects you'll need to run this after you change
gyp/gypi files.

    xb gyp

#### xethunk

Updates the checked-in `src/cpu/xethunk/xethunk.bc` and `xethunk.ll` files.
This is only required if changes are made to the xethunk files. The results
should be checked in.

#### build

Builds all xenia targets using ninja. Release is built by default; specify
`--debug` to build the debug configuration.

    xb build
    xb build --debug

#### clean

Cleans just xenia outputs from the build/ directory. A following build will just
have the rebuild xenia and not all of the dependencies.

    xb clean

#### nuke

Cleans up xenia outputs as well as all dependencies. A full build will be
required after this - including LLVM - so only do this if you want to reclaim
your disk space or something is really wrong.

    xb nuke

## Running

Use the wrapper shell scripts under `bin/` to run tools. They will ensure the
tools are built (but not that they are up to date) before running and allow
switching between the debug and release variants with `--debug`.

### xenia-info

Dumps information about a xex file.

    ./bin/xenia-info some.xex

### xenia-run

Runs a xex.

    ./bin/xenia-run some.xex
