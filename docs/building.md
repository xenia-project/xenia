# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### Windows

* Windows 8 or 8.1
* Visual Studio 2013
* [Python 2.7](http://www.python.org/download/releases/2.7.6/)
* If you are on Windows 8, you will also need the [Windows 8.1 SDK](http://msdn.microsoft.com/en-us/windows/desktop/bg162891)

Ensure Python is in your PATH (`C:\Python27\`).

#### Debugging

VS behaves oddly with the debug paths. Open the xenia-run project properties
and set the 'Command' to `$(SolutionDir)$(TargetPath)` and the
'Working Directory' to `$(SolutionDir)..\..`. You can specify flags and
the file to run in the 'Command Arguments' field (or use `--flagfile=flags.txt`).

## xenia-build

A simple build script is included to manage basic tasks such as building
dependencies.

    ./xenia-build.py --help

On Windows the `xb.bat` file enables you to issue all commands by just typing
`xb`. Note that you should run everything from the root `xenia\` folder.

On Linux/OSX the `xeniarc` bash file has some aliases that save some
keypresses:

    source xeniarc
    xb  -> python xenia-build.py
    xbb -> python xenia-build.py build
    xbc -> python xenia-build.py clean

### Commands

#### setup

Run this on initial checkout to pull down all dependencies and submodules.

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

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable.

### xenia-info

Dumps information about a xex file.

    ./bin/xenia-info some.xex

### xenia-run

Runs a xex.

    ./bin/xenia-run some.xex
