Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. It does not run games (yet),
and if you are unable to understand that please leave now.

Come chat with us **about development topics** in [#xenia @ irc.freenode.net](http://webchat.freenode.net?channels=%23xenia&uio=MTE9NzIaa).

Currently supported features:

* Nothing!

Coming soon (maybe):

* Everything!

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).

## Quickstart

Windows:

    # install python 2.7 and VS2013
    git clone https://github.com/benvanik/xenia.git
    cd xenia
    xb setup
    # open build\xenia\xenia.sln and start xenia-run

When fetching updates use `xb pull` to automatically fetch everything and
update gyp files/etc.

## Building

See [building](docs/building.md) for setup and information about the
`xenia-build` script.

## Contributors Wanted!

[![Stories in Ready](https://badge.waffle.io/benvanik/xenia.png?label=ready)](https://waffle.io/benvanik/xenia)

Have some spare time, know C++, and want to write an emulator? Contribute!
There's a ton of work that needs to be done, a lot of which is wide open
greenfield fun. Fixes and optimizations are always welcome (please!), but
in addition to that there are some major work areas still untouched:

* Write an [OpenGL driver](https://github.com/benvanik/xenia/issues/59)
* Add input drivers for [OSX](https://github.com/benvanik/xenia/issues/61) and [PS4 controllers](https://github.com/benvanik/xenia/issues/60) (or anything else)
* Start [hacking on audio](https://github.com/benvanik/xenia/issues/62)
* Support [loading of PIRS files](https://github.com/benvanik/xenia/issues/63)
* Build a [virtual LIVE service](https://github.com/benvanik/xenia/issues/64)
 
See more projects [good for contributors](https://github.com/benvanik/xenia/issues?labels=good+for+contributors&page=1&state=open).

## FAQ

### Can I get an exe?

**NO**. I am not releasing binaries - at least not for awhile. Don't be an
idiot and download a binary claiming to be of this project. In fact, don't
be an idiot and download *any* binary claiming to be an Xbox 360 or PS3
emulator from *any* source, especially not YouTube videos and shady websites.
Come on people. Jeez.

### What kind of machine do I need to run this?

You'll need 64-bit Windows 7 with a processor supporting at least SSE4.
It's only tested on Windows 8 and that may become a requirement as several of
the APIs exposed there are beneficial to emulation. In general if you have to
ask if your machine is good enough to run games at a decent speed the answer is
no.

### What about Linux/OSX?

The project is designed to support non-Windows platforms but until it's running
games it's not worth the maintenance burden. If you're a really passionate
Linux/OSX-based developer and want to help out, run Bootcamp/VM and contribute
an OpenGL 4 driver - that'll be the most difficult part in porting to
non-Windows platforms.

### What kind of GPU do I need?

DirectX 11 support is required. To get full speed and compatibility Mantle may
be required in the future (which means R9 AMD cards and up).

### (some argument over an unimportant technical choice)

In general: 'I don't care.'

Here's a short list of common ones:

* 'Why Python 2.7? 3 is awesome!' -- agreed, but gyp needs 2.7.
* 'Why this GYP stuff?' -- CMake sucks, managing Xcode projects by hand sucks,
and for the large cross-platform project this will become I'm not interested
in keeping all the platforms building any other way.
* 'Why this xenia-build.py stuff?' -- I like it, it helps me. If you want to
manually execute commands have fun, nothing is stopping you.

## Known Issues

### Use of stdout

Currently everything is traced to stdout, which is slow and silly. A better
tracing format is being worked on.
