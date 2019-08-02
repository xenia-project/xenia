# Style Guide

The style guide can be summed up as 'clang-format with the Google style set'.
In addition, the [Google Style Guide](https://google.github.io/styleguide/cppguide.html)
is followed and cpplint is the source of truth. When in doubt, defer to what
code in the project already does.

Base rules:

* 80 column line length max
* LF (Unix-style) line endings
* 2-space soft tabs, no TABs!
* [Google Style Guide](https://google.github.io/styleguide/cppguide.html) for naming/casing/etc
* Sort includes according to the [style guide rules](https://google.github.io/styleguide/cppguide.html#Names_and_Order_of_Includes)
* Comments are properly punctuated (that means capitalization and periods, etc)
* TODO's must be attributed like `// TODO(yourgithubname): foo.`

Code that really breaks from the formatting rules will not be accepted, as then
no one else can use clang-format on the code without also touching all your
lines.

### Why?

To quote the [Google Style Guide](https://google.github.io/styleguide/cppguide.html):

```
One way in which we keep the code base manageable is by enforcing consistency.
It is very important that any programmer be able to look at another's code and
quickly understand it. Maintaining a uniform style and following conventions
means that we can more easily use "pattern-matching" to infer what various
symbols are and what invariants are true about them. Creating common, required
idioms and patterns makes code much easier to understand. In some cases there
might be good arguments for changing certain style rules, but we nonetheless
keep things as they are in order to preserve consistency.
```

## Buildbot Verification

The buildbot runs `xb lint --all` on the master branch, and will run
`xb lint --origin` on pull requests. Run `xb format` before you commit each
local change so that you are consistently clean, otherwise you may have to
rebase. If you forget, run `xb format --origin` and rebase your changes (so you
don't end up with 5 changes and then a 6th 'whoops' one - that's nasty).

The buildbot is running LLVM 3.8.0. If you are noticing style differences
between your local lint/format and the buildbot, ensure you are running that
version.

## Tools

### clang-format

clang-format with the Google style is used to format all files. I recommend
installing/wiring it up to your editor of choice so that you don't even have to
think about tabs and wrapping and such.

#### Command Line

To use the `xb format` auto-formatter, you need to have a `clang-format` on your
PATH. If you're on Windows you can do this by installing an LLVM binary package
from [the LLVM downloads page](https://llvm.org/releases/download.html). If you
install it to the default location the `xb format` command will find it
automatically even if you don't choose to put all of LLVM onto your PATH.

#### Visual Studio

Grab the official [experimental Visual Studio plugin](https://llvm.org/builds/).
To switch to the Google style go Tools -> Options -> LLVM/Clang -> ClangFormat
and set Style to Google. Then use ctrl-r/ctrl-f to trigger the formatting.
Unfortunately it only does the cursor by default, so you'll have to select the
whole doc and invoke it to get it all done.

If you have a better option, let me know!

#### Xcode

Install [Alcatraz](https://github.com/alcatraz/Alcatraz) to get the [ClangFormat](https://github.com/travisjeffery/ClangFormat-Xcode)
package. Set it to use the Google style and format on save. Never think about
tabs or linefeeds or whatever again.

### cpplint

TODO(benvanik): write a cool script to do this/editor plugins.
In the future, the linter will run as a git commit hook and on travis.
