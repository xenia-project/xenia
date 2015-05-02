A lightweight cross-platform/compiler compatibility library.

This library presupposes C++11/14 support. As more compilers get C++14 it will
assume that.

Other parts of the project use this to avoid creating spaghetti linkage. Code
specific to the emulator should be kept out, as not all of the projects that
depend on this need it.

Where possible, C++11/14 STL should be used instead of adding any code in here,
and the code should be kept as small as possible (by reusing STL/etc). Third
party dependencies should be kept to a minimum.

Target compilers:
* MSVC++ 2013+
* Clang 3.4+
* GCC 4.8+.

Target platforms:
* Windows 8+ (`_win.cc` suffix)
* Mac OSX 10.9+ (`_mac.cc` suffix, falling back to `_posix.cc`)
* Linux ? (`_posix.cc` suffix)

Avoid the use of platform-specific #ifdefs and instead try to put all
platform-specific code in the appropriately suffixed cc files.
