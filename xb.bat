@ECHO OFF
REM Copyright 2015 Ben Vanik. All Rights Reserved.

SET DIR=%~dp0

REM ============================================================================
REM Environment Validation
REM ============================================================================

CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO.
  ECHO Python 3.4+ must be installed and on PATH:
  ECHO https://www.python.org/
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
1>NUL 2>NUL CMD /c where python3
IF NOT ERRORLEVEL 1 (
  SET FOUND_PYTHON_EXE=python3
)
IF %FOUND_PYTHON_EXE% EQU "" (
  IF EXIST c:\\python34\\python.exe SET FOUND_PYTHON_EXE=C:\\python34\\python.exe
)
IF %FOUND_PYTHON_EXE% EQU "" (
  IF EXIST c:\\python35\\python.exe SET FOUND_PYTHON_EXE=C:\\python35\\python.exe
)
IF %FOUND_PYTHON_EXE% EQU "" (
  IF EXIST c:\\python36\\python.exe SET FOUND_PYTHON_EXE=C:\\python36\\python.exe
)
IF %FOUND_PYTHON_EXE% EQU "" (
  1>NUL 2>NUL CMD /c where python
  IF NOT ERRORLEVEL 1 (
    SET FOUND_PYTHON_EXE=python
  )
)
IF %FOUND_PYTHON_EXE% EQU "" (
  ECHO ERROR: no Python executable found on PATH.
  ECHO Make sure you can run 'python' or 'python3' in a Command Prompt.
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
CMD /C %FOUND_PYTHON_EXE% -c "import sys; sys.exit(1 if not sys.version_info[:2] >= (3, 4) else 0)"
IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR: Python version mismatch - not 3.4+
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)
ENDLOCAL & (
  SET _RESULT=0
  SET PYTHON_EXE=%FOUND_PYTHON_EXE%
)
GOTO :eof
