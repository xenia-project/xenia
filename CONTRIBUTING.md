# Content Guidelines

The issue tracker is exclusively for filing and discussing bugs, feature
requests, and tracking work items. It is not for technical support or general
discussion. Avoid discussing any illegal activity, such as downloading games.

**Repeated misuses will result in a permanent project ban.**

## Information Sourcing

All information in xenia has been derived from reverse engineering legally-owned
games, hardware, and tools made public by Microsoft (such as the XNA Game Studio
tooling), scouring documentation made public by Microsoft (such as slide decks
and other presentations at conferences), and information from code made public
by 3rd party companies (like the Valve SDKs).

The official Microsoft Xbox Development Kits (XDKs) are not to be used for any
information added to the project. The contributors do not want the XDKs, nor do
they want any information derived from them. The challenge of the project is
what makes it fun! Poisoning the codebase with code obtained by shady means
could result in the project being terminated, so just don't do it.

**Posting any information directly from an XDK will result in a project ban.**

# Contributing Code

## Style Guide

Please read over [style_guide.md](style_guide.md) before sending pull requests
and ensure your code is clean as the buildbot (or I) will make you to fix it :)
[style_guide.md](style_guide.md) has information about using `xb format` and
various IDE auto formatting tools so that you can avoid having to clean things
up later, so be sure to check it out.

Basically: run `xb format` before you add a commit and you won't have a problem.

## Clean Git History

Tools such as `git bisect` are used on the repository regularly to check for and
identify regressions. Such tools require a clean git history to function
properly. Incoming pull requests must follow good git rules, the most basic of
which is that individual commits add functionality in somewhat working form and
fully compile and run on their own. Small pull requests with a single commit are
best and multiple commits in a pull request are allowed only if they are
kept clean. If not clean, you will be asked to rebase your pulls (and if
you don't know what that means, avoid getting into that situation ;).

Example of a bad commit history:

* Adding audio callback, random file loading, networking, etc. (+2000 lines)
* Whoops.
* Fixing build break.
* Fixing lint errors.
* Adding audio callback, second attempt.
* ...

Histories like this make it extremely difficult to check out any individual
commit and know that the repository is in a good state. Rebasing,
cherry-picking, or splitting your commits into separate branches will help keep
things clean and easy.

# License

All xenia code is licensed under the 3-clause BSD license as detailed in
[LICENSE](LICENSE). Code under `third_party/` is licensed under its original
license.

Incoming code in pull requests are subject to the xenia [LICENSE](LICENSE).
Once code comes into the codebase it is very difficult to ever fully remove so
copyright is ascribed to the project to prevent future disputes such as what
occurred in [Dolphin](https://dolphin-emu.org/blog/2015/05/25/relicensing-dolphin/).
That said: xenia will never be sold, never be made closed source, and never
change to a fundamentally incompatible license.

Any `third_party/` code added will be reviewed for conformance with the license.
In general, GPL code is forbidden unless it is used exclusively for
development-time tooling (like compiling). LGPL code is strongly discouraged as
it complicates building.
