Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. For more information see the
[main xenia website](http://xenia.jp/).

Pull requests are welcome but the code is in a very high churn state and may not
be accepted, so ask in IRC before taking on anything big. Contributions are
awesome but the focus of the developers is on writing new code, not teaching
programming or answering questions. If you'd like to casually help out it may
be better to wait a bit until things calm down and more of the code is documented.

Come chat with us **about development topics** in
[#xenia @ irc.freenode.net](http://webchat.freenode.net?channels=%23xenia&uio=MTE9NzIaa).
Please check the [frequently asked questions](http://xenia.jp/faq/) page before
asking questions. We've got jobs/lives/etc, so don't expect instant answers.
Discussing illegal activities will get you banned. No warnings.

**Before contributing code or issues be sure to read [CONTRIBUTING.md](CONTRIBUTING.md).**

## Status

Buildbot:
[![Build status](http://build.xenia.jp/png?builder=auto-builds)](http://build.xenia.jp/waterfall)

Project tracker:
[![Stories in Ready](https://badge.waffle.io/benvanik/xenia.svg?label=ready&title=Ready)](http://waffle.io/benvanik/xenia)
[![Stories in In Progress](https://badge.waffle.io/benvanik/xenia.svg?label=in%20progress&title=In%20Progress)](http://waffle.io/benvanik/xenia)

Some real games run. Most don't. Don't ask if GTA or whatever runs.
See the [Game compatibility list](https://github.com/xenia-project/game-compatibility/issues)
for currently tracked games.

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).

## Quickstart

Windows 8.1+ with Python 2.7 and [Visual Studio 2015](https://www.visualstudio.com/downloads/download-visual-studio-vs) and the Windows SDKs installed:

    > git clone https://github.com/benvanik/xenia.git
    > cd xenia
    > xb setup

    # Pull latest changes, rebase, and update submodules and premake:
    > xb pull

    # Build on command line:
    > xb build

    # Run premake and open Visual Studio (run the 'xenia-app' project):
    > xb devenv

    # Run premake to update the sln/vcproj's:
    > xb premake

    # Format code to the style guide:
    > xb format

When fetching updates use `xb pull` to automatically fetch everything and
run premake for project files/etc.

## Building

See [building.md](building.md) for setup and information about the
`xb` script. When writing code, check the [style guide](style_guide.md)
and be sure to run clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

For general rules and guidelines please see [CONTRIBUTING.md](CONTRIBUTING.md).

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through missing functionality/bugs in game [compat](https://github.com/benvanik/xenia/issues?labels=compat)
* Add input drivers for [PS4 controllers](https://github.com/benvanik/xenia/issues/60) (or anything else)
* Skilled with Linux? A strong contributor is needed to [help with porting](https://github.com/benvanik/xenia/labels/cross%20platform)

See more projects [good for contributors](https://github.com/benvanik/xenia/issues?labels=good+for+contributors&page=1&state=open). It's a good idea to ask on IRC/the bugs before beginning work
on something.

## FAQ

For more see the main [frequently asked questions](http://xenia.jp/faq/) page.

### Can I get an exe?

Not yet.
