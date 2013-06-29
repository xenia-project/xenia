Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. It does not run games (yet),
and if you are unable to understand that please leave now.

Come chat with us in #xenia on freenode.

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

## Building

See [building](docs/building.md) for setup and information about the
`xenia-build` script.

## Known Issues

### asmjit bug

asmjit has an issue with removing unreachable code that will cause assertion
failures/exiting when running. Until it is patched you must go and modify
a file after checking out the project.

```
--- a/third_party/asmjit/src/asmjit/x86/x86compileritem.cpp
+++ b/third_party/asmjit/src/asmjit/x86/x86compileritem.cpp
@@ -114,7 +114,7 @@ CompilerItem* X86CompilerTarget::translate(CompilerContext& cc)
     return NULL;
   }

-  if (x86Context._isUnreachable)
+  if (0)//x86Context._isUnreachable)
   {
     // If the context has "isUnreachable" flag set and there is no state then
     // it means that this code will be never called. This is a problem, because
```
