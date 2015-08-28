---
layout: page
title: Roadmap
icon: event
permalink: /roadmap/
---

* foo
{:toc}

## Goals

Normal emulator stuff. Games work, etc.

### Cross-platform Support

Linux support is slowly progressing, but really needs a strong contributor to
fully support. OS X support is highly unlikely unless Apple gets Vulkan
support.

## Non-Goals

There's a lot that *could* be done in the project, but there are many things
that *shouldn't* be.

### Pixel-perfect Accuracy

A lot of corners are cut for one reason or another: performance, lack of
understanding, lack of documentation, etc. Getting something that perfectly
matches the output on a real console isn't really possible with our approach.
Conversely, this allows much more freedom to provide higher-quality or faster
implementations.

### Xbox Live Connectivity

Xenia will never be able to connect to the real Xbox Live network. A simulated
Live-like network is possible, however anything that interfaces with the
official Microsoft services is not only not possible, but not something the
project seeks to enable.

### Game Servers

Some multiplayer games are peer-to-peer, however many have some server component
required for either matchmaking or actual session hosting. Though it'd be
possible to work with such homebrew servers if projects sprung up to support
them Xenia itself will not be attempting to do so.

### Original Xbox Backwards Compatibility

Microsoft released a compatibility layer that enabled original Xbox games to run
on the 360. Though likely feasible to get running under Xenia, it's not a goal.
There's likely to be a lot of trickery going on that most games don't do, and
distribution of the compatibility layer isn't possible.

## Dreams

### VR

Figure that out :)

### Simulated Xbox Live Network

Implement the system APIs for friend's list, leaderboards, etc. Reusing Steam or
some other service would be ideal.
