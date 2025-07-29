@ECHO OFF
REM Copyright 2025 Ben Vanik. All Rights Reserved.

SET "DIR=%~dp0"

REM ============================================================================
REM Environment Validation
REM ============================================================================

CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO.
  ECHO Python 3.9+ must be installed and on PATH:
  ECHO https://www.python.org/
  GOTO :eof
)


REM ============================================================================
REM Trampoline into xenia-build.py
REM ============================================================================

"%PYTHON_EXE%" "%DIR%\xenia-build.py" %*
EXIT /b %ERRORLEVEL%


REM ============================================================================
REM Utilities
REM ============================================================================

:check_python
SETLOCAL ENABLEDELAYEDEXPANSION

SET FOUND_PATH=""

SET "CANDIDATE_PATHS[0]=%WINDIR%\py.exe"
SET OUTPUT_INDEX=1

FOR /F "usebackq delims=" %%L IN (`2^>NUL where python`) DO (
  IF %%~zL NEQ 0 (
    SET "CANDIDATE_PATHS[!OUTPUT_INDEX!]=%%L"
    SET /A OUTPUT_INDEX+=1
  )
)
FOR /F "usebackq delims=" %%L IN (`2^>NUL where python3`) DO (
  IF %%~zL NEQ 0 (
    SET "CANDIDATE_PATHS[!OUTPUT_INDEX!]=%%L"
    SET /A OUTPUT_INDEX+=1
  )
)

SET CANDIDATE_INDEX=0
:check_candidate_loop
IF NOT DEFINED CANDIDATE_PATHS[%CANDIDATE_INDEX%] (
  GOTO :found_python
)
CALL SET CANDIDATE_PATH=%%CANDIDATE_PATHS[%CANDIDATE_INDEX%]%%
IF NOT EXIST "%CANDIDATE_PATH%" (
  SET /A CANDIDATE_INDEX+=1
  GOTO :check_candidate_loop
)
SET "FOUND_PATH=%CANDIDATE_PATH%"

:found_python
IF "%FOUND_PATH%"=="" (
  ECHO ERROR: no Python executable found on PATH.
  ECHO Make sure you can run 'python' or 'python3' in a Command Prompt.
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & (
  SET _RESULT=0
  SET "PYTHON_EXE=%FOUND_PATH%"
)
GOTO :eof
