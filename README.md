Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. It does not run games (yet),
and if you are unable to understand that please leave now.

Currently supported features:

* Nothing!

Coming soon (maybe):

* Everything!

## Quickstart

    git clone https://github.com/benvanik/xenia.git
    ./xenia-build.sh setup
    ./xenia-build.sh build
    ./bin/xenia some.xex

## Building

### Requirements

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
* [Doxygen](http://www.stack.nl/~dimitri/doxygen/download.html#latestsrc)
* [Python 2.6](http://www.python.org/getit/releases/2.6/)
* [CMake](http://www.cmake.org/cmake/resources/software.html)

#### OS X

* [Xcode 4](http://developer.apple.com/xcode/)

### Dependencies

TODO
