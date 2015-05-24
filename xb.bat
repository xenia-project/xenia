@ECHO OFF
REM Copyright 2015 Ben Vanik. All Rights Reserved.

SET DIR=%~dp0

SET XENIA_SLN=xenia.sln

REM ============================================================================
REM Environment Validation
REM ============================================================================
REM To make life easier we just require everything before we try running.

CALL :check_git
IF %_RESULT% NEQ 0 (
  ECHO ERROR:
  ECHO git must be installed and on PATH.
  GOTO :exit_error
)

CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO ERROR:
  ECHO Python 2.7 must be installed and on PATH:
  ECHO https://www.python.org/ftp/python/2.7.9/python-2.7.9.msi
  GOTO :exit_error
)

CALL :check_msvc
IF %_RESULT% NEQ 0 (
  ECHO ERROR:
  ECHO Visual Studio 2015 must be installed.
  ECHO.
  ECHO The Community Edition is free and can be downloaded here:
  ECHO https://www.visualstudio.com/downloads/visual-studio-2015-downloads-vs
  ECHO.
  ECHO Once installed, launch the 'Developer Command Prompt for VS2015' and run
  ECHO this script again.
  GOTO :exit_error
)

REM ============================================================================
REM Command Parsing
REM ============================================================================
SET _RESULT=-2
REM Ensure a command has been passed.
IF -%1-==-- GOTO :show_help
REM Dispatch to handler (:perform_foo).
2>NUL CALL :perform_%1 %*
IF %_RESULT% EQU -2 GOTO :show_help
IF %_RESULT% EQU -1 GOTO :exit_nop
IF %_RESULT% EQU 0 GOTO :exit_success
GOTO :exit_error

:exit_success
ECHO.
ECHO OK
EXIT /b 0

:exit_error
ECHO.
ECHO Error: %_RESULT%
EXIT /b 1

:exit_nop
ECHO.
ECHO (no actions performed)
EXIT /b 0


REM ============================================================================
REM xb help
REM ============================================================================
:show_help
SETLOCAL
ECHO.
ECHO Usage: xb COMMAND [options]
ECHO.
ECHO Commands:
ECHO.
ECHO   xb setup
ECHO     Initializes dependencies and prepares build environment.
ECHO.
ECHO   xb pull [--rebase]
ECHO     Fetches latest changes from github and rebuilds dependencies.
ECHO.
ECHO   xb proto
ECHO     Regenerates protocol files (*.fbs).
ECHO.
ECHO   xb edit
ECHO     Opens Visual Studio with `xenia.sln`.
ECHO.
ECHO   xb build [--checked OR --debug OR --release] [--force]
ECHO     Initializes dependencies and prepares build environment.
ECHO.
ECHO   xb gentests
ECHO     Generates test binaries (under src/xenia/cpu/frontend/test/bin/).
ECHO     Run after modifying test .s files.
ECHO.
ECHO   xb test [--checked OR --debug OR --release] [--continue]
ECHO     Runs automated tests. Tests must have been built with `xb build`.
ECHO.
ECHO   xb clean
ECHO     Cleans normal build artifacts to force a rebuild.
ECHO.
ECHO   xb nuke
ECHO     Resets branch and build environment to defaults to unhose state.
ECHO.
ECHO   xb lint [--all]
ECHO     Runs linter on local changes (or entire codebase).
ECHO.
ECHO   xb format [--all]
ECHO     Runs linter/auto-formatter on local changes (or entire codebase).
ECHO.
ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb setup
REM ============================================================================
:perform_setup
SETLOCAL
ECHO Setting up the build environment...

ECHO.
ECHO ^> git submodule update --init
git submodule update --init
IF %ERRORLEVEL% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to initialize git submodules
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb pull
REM ============================================================================
:perform_pull
SETLOCAL
SET REBASE=0
SHIFT
:perform_pull_args
IF "%~1"=="" GOTO :perform_pull_parsed
IF "%~1"=="--" GOTO :perform_pull_parsed
IF "%~1"=="--rebase" (SET REBASE=1)
SHIFT
GOTO :perform_pull_args
:perform_pull_parsed
ECHO Pulling latest changes and rebuilding dependencies...

ECHO.
ECHO ^> git checkout master
git checkout master
IF %ERRORLEVEL% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to checkout master
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ECHO.
IF %REBASE% EQU 1 (
  ECHO ^> git pull --rebase
  git pull --rebase
) ELSE (
  ECHO ^> git pull
  git pull
)
IF %ERRORLEVEL% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to pull latest changes from git
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ECHO.
ECHO ^> git submodule update
git submodule update
IF %ERRORLEVEL% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to update git submodules
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb proto
REM ============================================================================
:perform_proto
SETLOCAL EnableDelayedExpansion
ECHO Generating proto files...

SET FLATC=build\bin\Debug\flatc.exe
IF NOT EXIST %FLATC% (
  SET FLATC=build\bin\Release\flatc.exe
  IF NOT EXIST %FLATC% (
    ECHO.
    ECHO ERROR: flatc not built - build before running this
    ENDLOCAL & SET _RESULT=1
    GOTO :eof
  )
)

SET FBS_SRCS=src\xenia\debug\proto\
SET CC_OUT=src\xenia\debug\proto\
SET CS_OUT=src\Xenia.Debug\Proto\

SET ANY_ERRORS=0
PUSHD %FBS_SRCS%
FOR %%G in (*.fbs) DO (
  ECHO ^> generating %%~nG...
  POPD
  SET SRC_FILE=%FBS_SRCS%\%%G
  CMD /c build\bin\Debug\flatc.exe -c -o %CC_OUT% !SRC_FILE! 2>&1
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  CMD /c build\bin\Debug\flatc.exe -n -o %CS_OUT% !SRC_FILE! 2>&1
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  PUSHD %TEST_SRC%
)
POPD
IF %ANY_ERRORS% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to build one or more tests
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb edit
REM ============================================================================
:perform_edit
SETLOCAL
ECHO Launching Visual Studio...

ECHO.
ECHO ^> devenv %XENIA_SLN%
START devenv %XENIA_SLN%

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb build
REM ============================================================================
:perform_build
SETLOCAL
SET CONFIG="debug"
SET FORCE=0
SHIFT
:perform_build_args
IF "%~1"=="" GOTO :perform_build_parsed
IF "%~1"=="--" GOTO :perform_build_parsed
IF "%~1"=="--checked" (SET CONFIG="checked")
IF "%~1"=="--debug" (SET CONFIG="debug")
IF "%~1"=="--release" (SET CONFIG="release")
IF "%~1"=="--force" (SET FORCE=1)
SHIFT
GOTO :perform_build_args
:perform_build_parsed
ECHO Building for config %CONFIG%...

IF %FORCE% EQU 1 (
  SET DEVENV_COMMAND=/rebuild
) ELSE (
  SET DEVENV_COMMAND=/build
)
ECHO.
ECHO ^> devenv %XENIA_SLN% %DEVENV_COMMAND% %CONFIG%
devenv %XENIA_SLN% /nologo %DEVENV_COMMAND% %CONFIG%

IF %ERRORLEVEL% NEQ 0 (
  ECHO.
  ECHO ERROR: build failed with errors
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb gentests
REM ============================================================================
:perform_gentests
SETLOCAL EnableDelayedExpansion
ECHO Generating test binaries...

SET BINUTILS=third_party\binutils-ppc-cygwin
SET PPC_AS=%BINUTILS%\powerpc-none-elf-as.exe
SET PPC_LD=%BINUTILS%\powerpc-none-elf-ld.exe
SET PPC_OBJDUMP=%BINUTILS%\powerpc-none-elf-objdump.exe
SET PPC_NM=%BINUTILS%\powerpc-none-elf-nm.exe

SET TEST_SRC=src/xenia/cpu/frontend/test
SET TEST_SRC_WIN=src\xenia\cpu\frontend\test
SET TEST_BIN=%TEST_SRC%/bin
SET TEST_BIN_WIN=%TEST_SRC_WIN%\bin
IF NOT EXIST %TEST_BIN_WIN% (mkdir %TEST_BIN_WIN%)

SET ANY_ERRORS=0
PUSHD %TEST_SRC_WIN%
FOR %%G in (*.s) DO (
  ECHO ^> generating %%~nG...
  POPD
  SET SRC_FILE=%TEST_SRC%/%%G
  SET SRC_NAME=%%~nG
  SET OBJ_FILE=%TEST_BIN%/!SRC_NAME!.o
  CMD /c %PPC_AS% -a64 -be -mregnames -mpower7 -maltivec -mvsx -mvmx128 -R -o !OBJ_FILE! !SRC_FILE! 2>&1
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  %PPC_OBJDUMP% --adjust-vma=0x100000 -Mpower7 -Mvmx128 -D -EB !OBJ_FILE! > %TEST_BIN%/!SRC_NAME!.dis.tmp
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  REM Eat the first 4 lines to kill the file path that'll differ across machines.
  MORE +4 %TEST_BIN_WIN%\!SRC_NAME!.dis.tmp > %TEST_BIN_WIN%\!SRC_NAME!.dis
  DEL %TEST_BIN_WIN%\!SRC_NAME!.dis.tmp
  %PPC_LD% -A powerpc:common64 -melf64ppc -EB -nostdlib --oformat binary -Ttext 0x80000000 -e 0x80000000 -o %TEST_BIN%/!SRC_NAME!.bin !OBJ_FILE!
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  %PPC_NM% --numeric-sort !OBJ_FILE! > %TEST_BIN%/!SRC_NAME!.map
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_ERRORS=1
  )
  PUSHD %TEST_SRC_WIN%
)
POPD
IF %ANY_ERRORS% NEQ 0 (
  ECHO.
  ECHO ERROR: failed to build one or more tests
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb test
REM ============================================================================
:perform_test
SETLOCAL EnableDelayedExpansion
SET BUILD=1
SET CONFIG="debug"
SET CONTINUE=0
SHIFT
:perform_test_args
IF "%~1"=="" GOTO :perform_test_parsed
IF "%~1"=="--" (
  SHIFT
  GOTO :perform_test_parsed
)
IF "%~1"=="--checked" (SET CONFIG="checked")
IF "%~1"=="--debug" (SET CONFIG="debug")
IF "%~1"=="--release" (SET CONFIG="release")
IF "%~1"=="--continue" (SET CONTINUE=1)
SHIFT
GOTO :perform_test_args
:perform_test_parsed
ECHO Running automated testing for config %CONFIG%...

SET TEST_NAMES=xe-cpu-hir-test xe-cpu-ppc-test
FOR %%G IN (%TEST_NAMES%) DO (
  IF NOT EXIST build\bin\%CONFIG%\%%G.exe (
    ECHO.
    ECHO ERROR: unable to find `%%G.exe` - ensure it is built.
    ENDLOCAL & SET _RESULT=1
    GOTO :eof
  )
)

SET ANY_FAILED=0
FOR %%G IN (%TEST_NAMES%) DO (
  ECHO.
  ECHO ^> build\bin\%CONFIG%\%%G.exe %1 %2 %3 %4 %5 %6 %7 %8 %9
  build\bin\%CONFIG%\%%G.exe %1 %2 %3 %4 %5 %6 %7 %8 %9
  IF !ERRORLEVEL! NEQ 0 (
    SET ANY_FAILED=1
    IF %CONTINUE% EQU 0 (
      ECHO.
      ECHO ERROR: test failed, aborting, use --continue to keep going
      ENDLOCAL & SET _RESULT=1
      GOTO :eof
    ) ELSE (
      ECHO.
      ECHO ERROR: test failed but continuing due to --continue
    )
  )
)
IF %ANY_FAILED% NEQ 0 (
  ECHO.
  ECHO ERROR: one or more tests failed
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb clean
REM ============================================================================
:perform_clean
SETLOCAL
ECHO Cleaning normal build outputs...
ECHO (use nuke to kill all artifacts)

SET CONFIG_NAMES=Checked Debug Release
FOR %%G IN (%CONFIG_NAMES%) DO (
  ECHO.
  ECHO ^> devenv %XENIA_SLN% /clean %%G
  devenv %XENIA_SLN% /nologo /clean %%G
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb nuke
REM ============================================================================
:perform_nuke
SETLOCAL
ECHO Nuking all local changes...
ECHO.

REM rmdir build/
REM git checkout --hard /etc
ECHO TODO(benvanik): blast away build/ for now

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb lint
REM ============================================================================
:perform_lint
SETLOCAL
SET ALL=0
SHIFT
:perform_lint_args
IF "%~1"=="" GOTO :perform_lint_parsed
IF "%~1"=="--" GOTO :perform_lint_parsed
IF "%~1"=="--all" (SET ALL=1)
SHIFT
GOTO :perform_lint_args
:perform_lint_parsed
IF %ALL% EQU 1 (
  ECHO Running code linter on all code...
) ELSE (
  ECHO Running code linter on code staged in git index...
)

ECHO.
REM --all
REM run lint
ECHO TODO(benvanik): just `xb format`

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM xb format
REM ============================================================================
:perform_format
SETLOCAL EnableDelayedExpansion
SET ALL=0
SHIFT
:perform_format_args
IF "%~1"=="" GOTO :perform_format_parsed
IF "%~1"=="--" GOTO :perform_format_parsed
IF "%~1"=="--all" (SET ALL=1)
SHIFT
GOTO :perform_format_args
:perform_format_parsed
IF %ALL% EQU 1 (
  ECHO Running code formatter on all code...
) ELSE (
  ECHO Running code formatter on code staged in git index...
)

SET CLANG_FORMAT=""
SET LLVM_CLANG_FORMAT="C:\Program Files (x86)\LLVM\bin\clang-format.exe"
IF EXIST %LLVM_CLANG_FORMAT% (
  SET CLANG_FORMAT=%LLVM_CLANG_FORMAT%
) ELSE (
  1>NUL 2>NUL CMD /c where clang-format
  IF %ERRORLEVEL% NEQ 0 (
    SET CLANG_FORMAT="clang-format"
  )
)
IF %CLANG_FORMAT%=="" (
  ECHO.
  ECHO ERROR: clang-format is not on PATH or the standard location.
  ECHO LLVM is available from http://llvm.org/releases/download.html
  ECHO See docs/style_guide.md for instructions on how to get it.
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

SET ANY_ERRORS=0
IF %ALL% NEQ 1 (
  ECHO.
  ECHO ^> git-clang-format
  CMD /c python third_party/clang-format/git-clang-format --binary=%CLANG_FORMAT% --commit=HEAD
  IF %ERRORLEVEL% NEQ 0 (
    SET ANY_ERRORS=1
  )
) ELSE (
  PUSHD src
  FOR /R %%G in (*.cc *.c *.h *.inl) DO (
    ECHO ^> clang-format %%G
    CMD /C %CLANG_FORMAT% -i -style=file %%G
    IF !ERRORLEVEL! NEQ 0 (
      SET ANY_ERRORS=1
    )
  )
  POPD
)
IF %ANY_ERRORS% NEQ 0 (
  ECHO.
  ECHO ERROR: 1+ clang-format calls failed - ensure all files are staged
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & SET _RESULT=0
GOTO :eof


REM ============================================================================
REM Utilities
REM ============================================================================

:check_python
SETLOCAL
1>NUL 2>NUL CMD /c where python
IF %ERRORLEVEL% NEQ 0 (
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
CMD /c python -c "import sys; sys.exit(1 if not sys.version_info[:2] == (2, 7) else 0)"
IF %ERRORLEVEL% NEQ 0 (
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
ENDLOCAL & SET _RESULT=0
GOTO :eof

:check_git
1>NUL 2>NUL CMD /c where git
SET _RESULT=%ERRORLEVEL%
GOTO :eof

:check_msvc
1>NUL 2>NUL CMD /c where devenv
SET _RESULT=%ERRORLEVEL%
IF %_RESULT% NEQ 0 GOTO :eof
SET _RESULT=1
IF "%VS140COMNTOOLS%" NEQ "" (
  IF EXIST "%VS140COMNTOOLS%" (
    REM VS2015 CTP/RC
    SET _RESULT=0
  )
)
IF "%VS150COMNTOOLS%" NEQ "" (
  IF EXIST "%VS150COMNTOOLS%" (
    REM VS2015
    SET _RESULT=0
  )
)
GOTO :eof
