Xenia - Xbox 360 Emulator Research Project
==========================================

Xenia is an experimental emulator for the Xbox 360. It does not run games (yet).

Pull requests are welcome but the code is in a very high churn state and may not
be accepted, so ask in IRC before taking on anything big. Contributions are
awesome but the focus of the developers is on writing new code, not teaching
programming or answering questions. If you'd like to casually help out it may
be better to wait a bit until things calm down and more of the code is documented.

Come chat with us **about development topics** in [#xenia @ irc.freenode.net](http://webchat.freenode.net?channels=%23xenia&uio=MTE9NzIaa).

## NOTE

I'd much rather write code than entertain jerks. If you're ignored or treated with
curtness perhaps you should reflect upon what you did that would cause that to
happen. This is a project done for fun and random internet arguments diminish
that.

## Status

* Some code runs. [Insert any game name here] doesn't. Except Frogger 2. Yep.
* Asserts! Crashes! Hangs! Blank screens!

## Disclaimer

The goal of this project is to experiment, research, and educate on the topic
of emulation of modern devices and operating systems. **It is not for enabling
illegal activity**. All information is obtained via reverse engineering of
legally purchased devices and games and information made public on the internet
(you'd be surprised what's indexed on Google...).

## Quickstart

Windows 8.1+:

    # install python 2.7 and VS2013
    git clone https://github.com/benvanik/xenia.git
    cd xenia
    xb setup
    # open build\xenia\xenia.sln and start xenia-run

When fetching updates use `xb pull` to automatically fetch everything and
update gyp files/etc.

## Building

See [building](docs/building.md) for setup and information about the
`xenia-build` script. When writing code, check the [style guide](docs/style_guide.md)
and be sure to clang-format!

## Contributors Wanted!

Have some spare time, know advanced C++, and want to write an emulator?
Contribute! There's a ton of work that needs to be done, a lot of which
is wide open greenfield fun.

Fixes and optimizations are always welcome (please!), but in addition to
that there are some major work areas still untouched:

* Help work through missing functionality/bugs in game [compat](https://github.com/benvanik/xenia/issues?labels=compat)
* Add input drivers for [OSX](https://github.com/benvanik/xenia/issues/61) and [PS4 controllers](https://github.com/benvanik/xenia/issues/60) (or anything else)
* Start [hacking on audio](https://github.com/benvanik/xenia/issues/62)
* Build a [virtual LIVE service](https://github.com/benvanik/xenia/issues/64)

See more projects [good for contributors](https://github.com/benvanik/xenia/issues?labels=good+for+contributors&page=1&state=open). It's a good idea to ask on IRC/the bugs before beginning work
on something.

## FAQ

### Can I get an exe?

**NO**. I am not releasing binaries - at least not for awhile. Don't be an
idiot and download a binary claiming to be of this project. In fact, don't
be an idiot and download *any* binary claiming to be an Xbox 360 or PS3
emulator from *any* source, especially not YouTube videos and shady websites.
Come on people. Jeez.

### What kind of machine do I need to run this?

You'll need 64-bit Windows 8 with a processor supporting at least AVX2 - in
other words, a Haswell. In general if you have to ask if your machine is good
enough to run games at a decent speed the answer is no.

### What about Linux/OSX?

The project is designed to support non-Windows platforms but until it's running
games it's not worth the maintenance burden. If you're a really passionate
Linux/OSX-based developer and want to help out, run Bootcamp/VM and contribute
an OpenGL 4 driver - that'll be the most difficult part in porting to
non-Windows platforms.

### What kind of GPU do I need?

DirectX 11 support is required. To get full speed and compatibility Mantle may
be required in the future.

### Have you heard of LLVM/asmjit/jitasm/luajit/etc?

I get asked this about once a day. Yes, I have heard of them. In fact, I spent
a long time trying them out:
[LLVM](https://github.com/benvanik/xenia/tree/85bdbd24d1b5923cfb104f45194a96e7ac57026e/src/xenia/cpu/codegen),
[libjit](https://github.com/benvanik/xenia/tree/eee856be0499a4bc721b6097f5f2b9446929f2cc/src/xenia/cpu/libjit),
[asmjit](https://github.com/benvanik/xenia/tree/ca208fa60a0285d396409743064784cc2320c094/src/xenia/cpu/x64).
They don't work for this purpose. I understand if you disagree, but please
understand that I've spent a significant amount of time on this problem.

### Why did you do X? Why not just use Y? You should use Y. NIH NIH NIH!

Trust that I either have a good reason for what I did or have absolutely no
reason for what I did. This is a large project that I've been working on
for almost 4 years and in that time new compilers and language specs have
been released, libraries have been created and died, and I've learned a lot.
Insulting me will get you ignored.

### Hey I'm going to go modify every file in the project, ok?

I welcome contributions, but please try to understand that I cannot accept
changes that radically alter the structure or content of the code, especially
if they are aesthetic and even more so if they are from someone who has not
contributed before. This may seem like common sense, but apparently it isn't.
If a pull request of this nature is denied that doesn't necessarily mean your
help is not wanted, just that it may need to be more carefully applied.

### I have a copy of the XDK. Do you want it?

No.

### (some argument over an unimportant technical choice)

In general: 'I don't care.'
That means I either really don't care and something is they way it is because
that was convienient (such as GYP, which I am familiar with), or that
I don't care because it's not material to the goal of the project. There are
a million important things that need to be done to get games running and
going back and forth about unimportant orthogonal issues does not help.
If you really do have a better way of doing something and can show it, do so.

Here's a short list of common ones:

* 'Why Python 2.7? 3 is awesome!' -- agreed, but gyp needs 2.7.
* 'Why this GYP stuff?' -- CMake sucks, managing Xcode projects by hand sucks,
and for the large cross-platform project this will become I'm not interested
in keeping all the platforms building any other way.
* 'Why this xenia-build.py stuff?' -- I like it, it helps me. If you want to
manually execute commands have fun, nothing is stopping you.
* 'Why not just take the code from project X?' -- the point of this project
is to build something better than previous emulator projects, and learn while
doing it. The easy way is almost never the best way, and is never as fun.

## Known Issues

### Use of stdout

Currently everything is traced to stdout, which is slow and silly. A better
tracing format is being worked on.
