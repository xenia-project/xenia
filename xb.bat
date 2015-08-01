@ECHO OFF
REM Copyright 2015 Ben Vanik. All Rights Reserved.

SET DIR=%~dp0

REM ============================================================================
REM Environment Validation
REM ============================================================================

CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO ERROR:
  ECHO Python 2.7 must be installed and on PATH:
  ECHO https://www.python.org/ftp/python/2.7.9/python-2.7.9.msi
  GOTO :exit_error
)


REM ============================================================================
REM Trampoline into xenia-build
REM ============================================================================

python xenia-build %*
EXIT /b %ERRORLEVEL%


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
