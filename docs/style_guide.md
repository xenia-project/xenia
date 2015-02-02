# Style Guide

The style guide can be summed up as 'clang-format with the Google style set'.
In addition, the [Google Style Guide](http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml)
is followed and cpplint is the source of truth.

Base rules:

* 80 column line length max
* LF (Unix-style) line endings
* 2-space soft tabs, no TABs!
* [Google Style Guide](http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml) for naming/casing/etc

Code that really breaks from the formatting rules will not be accepted, as then
no one else can use clang-format on the code without also touching all your
lines.

## Tools

### clang-format

clang-format with the Google style is used to format all files. I recommend
installing/wiring it up to your editor of choice so that you don't even have to
think about tabs and wrapping and such.

#### Visual Studio

Grab the official [experimental Visual Studio plugin](http://llvm.org/builds/).
To switch to the Google style go Tools -> Options -> LLVM/Clang -> ClangFormat
and set Style to Google. Then use ctrl-r/ctrl-f to trigger the formatting.
Unfortunately it only does the cursor by default, so you'll have to select the
whole doc and invoke it to get it all done.

If you have a better option, let me know!

#### Xcode

Install [Alcatraz](http://alcatraz.io/) to get the [ClangFormat](https://github.com/travisjeffery/ClangFormat-Xcode)
package. Set it to use the Google style and format on save. Never think about
tabs or linefeeds or whatever again.

### cpplint

TODO(benvanik): write a cool script to do this/editor plugins.
In the future, the linter will run as a git commit hook and on travis.
