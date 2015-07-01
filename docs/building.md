# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
video drivers for your card.

## Setup

### Windows

* Windows 8 or 8.1
* Visual Studio 2015
* [Python 2.7](http://www.python.org/download/releases/2.7.6/)
* If you are on Windows 8, you will also need the [Windows 8.1 SDK](http://msdn.microsoft.com/en-us/windows/desktop/bg162891)

Ensure Python is in your PATH (`C:\Python27\`).

I recommend using [Cmder](http://bliker.github.io/cmder/) for git and command
line usage.

#### Debugging

VS behaves oddly with the debug paths. Open the xenia project properties
and set the 'Command' to `$(SolutionDir)$(TargetPath)` and the
'Working Directory' to `$(SolutionDir)..\..`. You can specify flags and
the file to run in the 'Command Arguments' field (or use `--flagfile=flags.txt`).

To redirect output, use the following command flags:
`--flagfile=$(SolutionDir)scratch\flags.txt 2>&1 1>$(SolutionDir)scratch\stdout.txt`

## Running

To make life easier you can use `--flagfile=myflags.txt` to specify all
arguments, including using `--target=my.xex` to pick an executable.
