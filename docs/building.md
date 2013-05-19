# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### OS X

Only tested on OS X 10.8 (Mountain Lion).

* [Xcode 4](http://developer.apple.com/xcode/) + command line tools
* [Homebrew](http://mxcl.github.com/homebrew/)

### Linux

Only tested on Ubuntu 12.10.

* random things (`sudo apt-get install make flex bison texinfo`)
* clang (`sudo apt-get install clang`)

Pretty much just install what's asked for as you try to `xb setup` or
`xb build`.

### Windows

* [Python 2.7](http://www.python.org/download/releases/2.7.3/)

Install both and add Python to your PATH (`C:\Python27\`).
Depending on your Visual Studio version you'll need to use one of the provided
command prompts (until I write my own) to perform all `xenia-build` tasks.

#### Visual Studio 2010

The build has been most tested with 2010.
Use the `Visual Studio 2010 x64 Command Prompt` as your shell.

* Visual Studio 2010 (not sure Express will work)
  * [Visual Studio 2010 SP1](http://msdn.microsoft.com/en-us/vstudio/aa718359)
* [Windows SDK](http://www.microsoft.com/download/en/details.aspx?id=8279)
* [DirectX SDK](http://msdn.microsoft.com/en-us/directx/)

#### Visual Studio 2012 (Express)

Basic testing has been done with 2012 Express (all I have access to). Since it's
newer and shinier, I may end up deprecating the 2010 support.
Use the `VS2012 x64 Cross Tools Command Prompt` as your shell.

* [Windows 8 SDK](http://msdn.microsoft.com/en-us/windows/desktop/aa904949.aspx)
* [Visual Studio 2012 Express for Desktop](http://go.microsoft.com/?linkid=9816758)

## xenia-build

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
required after this so only do this if you want to reclaim your disk space or
something is really wrong.

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
