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

Please read over [style_guide.md](../docs/style_guide.md) before sending pull requests
and ensure your code is clean as the buildbot (or I) will make you to fix it :)
[style_guide.md](../docs/style_guide.md) has information about using `xb format` and
various IDE auto formatting tools so that you can avoid having to clean things
up later, so be sure to check it out.

Basically: run `xb format` before you add a commit and you won't have a problem.

## Referencing Sources

In code interacting with guest interfaces or protocols, where applicable, please
leave comments describing how the information included in it was obtained. For
code based on analysis of the response of the original Xbox 360 hardware or
software, please provide reproduction information, such as an outline of the
algorithm executed, arguments and results of function calls or processor
instructions involved, GPU or XMA commands and state register changes. Having
traceable sources helps solve multiple issues:

* The legality of the submitted code can be verified easily.
* Additional analysis based on reproduction steps from prior research can be
  done to discover more details or to research the behavior of other related
  features.
* The accuracy and completeness of the information can be evaluated. Knowing
  whether something is ground truth about the console's behavior or an
  approximation (for example, based on similar functionality in Windows, the
  Qualcomm Adreno 200 GPU, AMD's desktop GPU architectures; the Direct3D 11.3
  functional specification, which may be used as a generic fallback when no
  information specific to the Xenos or Direct3D 9 is available) may help avoid
  redoing work that has already been done if the findings are accurate, or
  making potentially wrong conclusions about related functionality if there's no
  certainty about the correctness of the information. In addition, it's useful
  to describe how complete the implementation of a feature is — such as edge
  cases checked and covered. If you are unsure if your code accurately reflects
  the behavior of the console, or you have deliberately made deviations due to
  the lack of prerequisites for an accurate implementation or for performance
  reasons (in case of the latter, it's recommended to provide both options,
  selectable with a configuration variable), or you just want more research to
  be done in the future, please leave a TODO comment in the format provided in
  [style_guide.md](../docs/style_guide.md).

If you have verified your code by checking the correctness of the behavior of a
game, **do not** refer to it by its title trademark. To avoid unnecessary
dependencies on third parties, instead, use the hexadecimal title ID number
displayed in the title bar beside the name of the game. It's also recommended to
avoid using proper names of game content if they can be replaced with easily
understandable pointers not including them, such as "first mission",
"protagonist", "enemy aircraft".

Do not leave any hard-coded references to specific games, even in title ID form,
in any part of the user interface, including the configuration file. If you want
to provide an example of a game where changing a configuration variable may have
a noticeable effect, use a code comment near the declaration of the variable
rather than its description string. Any game identifiers referenced in the user
interface must be obtained only from information provided by the user such as
game data files.

Also, do not put any conditionals based on hard-coded identifiers of games — the
task of the project is researching the Xbox 360 console itself and documenting
its behavior by creating open implementations of its interfaces. Game-specific
hacks provide no help in achieving that, instead only complicating research by
introducing incorrect state and hiding the symptoms of actual issues. While
temporary workarounds, though discouraged, may be added in cases when progress
would be blocked otherwise in other areas, they must be expressed and reasoned
in terms of the common interface rather than logic internal to a specific game.

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
[LICENSE](../LICENSE). Code under `third_party/` is licensed under its original
license.

Incoming code in pull requests are subject to the xenia [LICENSE](../LICENSE).
Once code comes into the codebase it is very difficult to ever fully remove so
copyright is ascribed to the project to prevent future disputes such as what
occurred in [Dolphin](https://dolphin-emu.org/blog/2015/05/25/relicensing-dolphin/).
That said: xenia will never be sold, never be made closed source, and never
change to a fundamentally incompatible license.

Any `third_party/` code added will be reviewed for conformance with the license.
In general, GPL code is forbidden unless it is used exclusively for
development-time tooling (like compiling). LGPL code is strongly discouraged as
it complicates building.
