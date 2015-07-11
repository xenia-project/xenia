---
layout: page
title: Quickstart
icon: build
permalink: /development/quickstart/
---

* foo
{:toc}

## Getting Started

Have Windows 8.1+ with Python 2.7 and Visual Studio 2015 installed:

    > git clone --recursive https://github.com/benvanik/xenia.git
    > start xenia\xenia.sln

That's mostly it. Run the `xenia` project. For debugging setup instructions see
[the github docs](https://github.com/benvanik/xenia/blob/master/docs/building.md).
Piping stdout to a file is strongly recommended as there's were most of the
interesting information goes.

### It doesn't build!

In general, if you're unable to get the instructions as above to work you either
don't meet the OS requirements, don't have Visual Studio or the Windows SDK
installed properly, or have no idea what you're doing. The
[buildbot](http://build.xenia.jp/waterfall) verifies that what's checked in does
build and is rarely wrong, so if that's green it means the problem is on your
end.

Improvements to the project configuration and instructions are welcome. Issues
filed along the lines of 'it won't build' will be closed as invalid.

### It doesn't run!

Ensure you meet the system requirements. No, really, go check your video driver
with something like [GPU Caps Viewer](http://www.ozone3d.net/gpu_caps_viewer/)
and ensure you have OpenGL 4.5. If there are errors related to missing CPU
features use something like [CPU-Z](http://www.cpuid.com/softwares/cpu-z.html)
and ensure your processor supports AVX or AVX2.

## Contributing

Pull requests are accepted, however must be of a certain level of quality.
This means that they must adhere to [the style guide](https://github.com/benvanik/xenia/blob/master/docs/style_guide.md),
not pollute the git history (you may be asked to rebase if you've been bad),
and build clean. Changes that are obviously untested or unsafe will not be
accepted without additional verification.
