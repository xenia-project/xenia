@ECHO OFF
REM Copyright 2015 Ben Vanik. All Rights Reserved.

SET DIR=%~dp0

REM ============================================================================
REM Environment Validation
REM ============================================================================

CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO.
  ECHO Python 2.7 must be installed and on PATH:
  ECHO https://www.python.org/ftp/python/2.7.9/python-2.7.9.msi
  GOTO :exit_error
)


REM ============================================================================
REM Trampoline into xenia-build
REM ============================================================================

%PYTHON_EXE% xenia-build %*
EXIT /b %ERRORLEVEL%


REM ============================================================================
REM Utilities
REM ============================================================================

:check_python
SETLOCAL
SET FOUND_PYTHON_EXE=""
1>NUL 2>NUL CMD /c where python2
IF NOT ERRORLEVEL 1 (
  ECHO FOUND PYTHON 2
  SET FOUND_PYTHON_EXE=python2
)
IF %FOUND_PYTHON_EXE% EQU "" (
  1>NUL 2>NUL CMD /c where python
  IF NOT ERRORLEVEL 1 (
    SET FOUND_PYTHON_EXE=python
  )
)
IF %FOUND_PYTHON_EXE% EQU "" (
  ECHO ERROR: no Python executable found on PATH.
  ECHO Make sure you can run 'python' or 'python2' in a Command Prompt.
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
CMD /C %FOUND_PYTHON_EXE% -c "import sys; sys.exit(1 if not sys.version_info[:2] == (2, 7) else 0)"
IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR: Python version mismatch - not 2.7
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
ENDLOCAL & (
  SET _RESULT=0
  SET PYTHON_EXE=%FOUND_PYTHON_EXE%
)
GOTO :eof
