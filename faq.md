---
layout: page
title: FAQ
icon: assignment
permalink: /faq/
---

* foo
{:toc}

## Can I get an exe?

**NO**, not yet. Xenia is in its extremely early phases and not ready for use by
non-developers.

Don't be an idiot and download a binary claiming to be of this project. In fact,
don't be an idiot and download *any* binary claiming to be an Xbox 360 or PS3
emulator from *any* source, especially not YouTube videos and shady websites.
Come on people. Jeez.

## I have a copy of the XDK. Do you want it?

No. Do not post links or downloads to such resources or you will be banned.

## System Requirements

* Windows 8.1 or Windows 10
* Intel Sandy Bridge or Haswell processor (supporting AVX or AVX2)
* An OpenGL 4.5-compatible GPU (NVIDIA preferred)
* An XInput-compatible controller

### Can you add support for Windows Me? How about my Pentium II?

Support for older operating systems and processors will not be added unless a
contributor steps up to build and maintain them. All active contributors are on
modern systems and busy with higher priority tasks. Whether the emulator runs on
Windows Vista or not doesn't matter if it can't run games.

### Does Xenia run on Linux or OSX?

The project is designed to support non-Windows platforms but until it's running
games it's not worth the maintenance burden. OSX will likely remain unsupported
until Apple supports Vulkan.

### What kind of GPU do I need?

OpenGL 4.5 support and drivers are required. This includes NVIDIA's GeForce 400
series and above, and AMD's 5000 series and above. AMD cards/drivers often have
issues and you'll have better luck with NVIDIA.

To get full speed and compatibility the project will be adopting Vulkan and
Direct3D 12 so plan accordingly.

## Why did you do X? Why not just use Y? You should use Y. NIH NIH NIH!

Trust that I either have a good reason for what I did or have absolutely no
reason for what I did. This is a large project that I've been working on
for almost 5 years and in that time new compilers and language specs have
been released, libraries have been created and died, and I've learned a lot.
Constructive contributions and improvements are welcome.

### Have you heard of LLVM/asmjit/jitasm/luajit/etc?

Yes, I have heard of them. In fact, I spent a long time trying them out:
[LLVM](https://github.com/benvanik/xenia/tree/85bdbd24d1b5923cfb104f45194a96e7ac57026e/src/xenia/cpu/codegen),
[libjit](https://github.com/benvanik/xenia/tree/eee856be0499a4bc721b6097f5f2b9446929f2cc/src/xenia/cpu/libjit),
[asmjit](https://github.com/benvanik/xenia/tree/ca208fa60a0285d396409743064784cc2320c094/src/xenia/cpu/x64).
I did not find them acceptable for use in this project for various reasons. If
for some reason you feel strongly otherwise, feel free to either contribute a
[new CPU backend](https://github.com/benvanik/xenia/tree/master/src/xenia/cpu/backend).

### (some argument over an unimportant technical choice)

In general: *I don't care*.
That means I either really don't care and something is they way it is because
that was convienient or that I don't care because it's not material to the goal
of the project. There are a million important things that need to be done to get
games running and going back and forth about unimportant orthogonal issues does
not help. If you really do have a better way of doing something and can show it,
contributions are welcome.

Here's a short list of common ones:

* 'Why Python 2.7? 3 is awesome!' -- agreed, but git-clang-format needs 2.7.
* 'Why this xb stuff?' -- I like it, it helps me. If you want to
manually execute commands have fun, nothing is stopping you.
* 'Why not just take the code from project X?' -- the point of this project
is to build something different than previous emulator projects and learn while
doing it. The easy way is almost never the best way and most certainly isn't as
fun.

## Hey I'm going to go modify every file in the project, ok?

We welcome contributions, but please try to understand that we cannot accept
changes that radically alter the structure or content of the code, especially
if they are aesthetic and even more so if they are from someone who has not
contributed before. If a pull request of this nature is denied that doesn't
necessarily mean your help is not wanted, just that it may need to be more
carefully applied.

Contributions adding large untested pieces of functionality may take some time
to land, so engage early in [IRC]({{ site.baseurl }}/development/irc/) to ensure
someone else is not already working on it or that things can be done to ensure
smooth sailing.
