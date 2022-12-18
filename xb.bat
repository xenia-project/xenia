@ECHO OFF
REM Copyright 2022 Ben Vanik. All Rights Reserved.

SET "DIR=%~dp0"

REM ============================================================================
REM Environment Validation
REM ============================================================================

SET "PYTHON_MINIMUM_VERSION[0]=3"
SET "PYTHON_MINIMUM_VERSION[1]=6"
CALL :check_python
IF %_RESULT% NEQ 0 (
  ECHO.
  ECHO Python %PYTHON_MINIMUM_VERSION[0]%.%PYTHON_MINIMUM_VERSION[1]%+ must be installed and on PATH:
  ECHO https://www.python.org/
  GOTO :eof
)


REM ============================================================================
REM Trampoline into xenia-build
REM ============================================================================

"%PYTHON_EXE%" "%DIR%\xenia-build" %*
EXIT /b %ERRORLEVEL%


REM ============================================================================
REM Utilities
REM ============================================================================

:check_python
SETLOCAL ENABLEDELAYEDEXPANSION

SET FOUND_PATH=""

SET "CANDIDATE_PATHS[0]=C:\python310\python.exe"
SET "CANDIDATE_PATHS[1]=C:\python39\python.exe"
SET "CANDIDATE_PATHS[2]=C:\python38\python.exe"
SET "CANDIDATE_PATHS[3]=C:\python37\python.exe"
SET "CANDIDATE_PATHS[4]=C:\python%PYTHON_MINIMUM_VERSION[0]%%PYTHON_MINIMUM_VERSION[1]%\python.exe"
SET "CANDIDATE_PATHS[5]=C:\Windows\py.exe"
SET OUTPUT_INDEX=6

FOR /F "usebackq delims=" %%L IN (`2^>NUL where python3`) DO (
  IF %%~zL NEQ 0 (
    SET "CANDIDATE_PATHS[!OUTPUT_INDEX!]=%%L"
    SET /A OUTPUT_INDEX+=1
  )
)
FOR /F "usebackq delims=" %%L IN (`2^>NUL where python`) DO (
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

CMD /C ""%FOUND_PATH%" -c "import sys; sys.exit(1 if not sys.version_info[:2] ^>= (%PYTHON_MINIMUM_VERSION[0]%, %PYTHON_MINIMUM_VERSION[1]%) else 0)"
IF %ERRORLEVEL% NEQ 0 (
  ECHO ERROR: Python version mismatch, not at least %PYTHON_MINIMUM_VERSION[0]%.%PYTHON_MINIMUM_VERSION[1]%.
  ECHO Found Python executable was "%FOUND_PATH%".
  ENDLOCAL & SET _RESULT=1
  GOTO :eof
)

ENDLOCAL & (
  SET _RESULT=0
  SET "PYTHON_EXE=%FOUND_PATH%"
)
GOTO :eof
