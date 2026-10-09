@echo off
rem ---------------------------------------------------------------------------
rem  Verify the Esc fix in a REAL console.
rem
rem  Why a script instead of a test case: the fix only runs when stdin is an
rem  actual console (see src/Terminal.cpp, `stdin_is_tty_ && !consoleInputPending`).
rem  Every automated test drives kalo through a pipe, where stdin_is_tty_ is
rem  false and the new branch is never reached. So this path can only be
rem  exercised by a human in front of a real console window.
rem
rem  Double-click this file. A new console window opens and kalo takes it over.
rem  All text here is ASCII on purpose: cmd.exe reads batch files using the
rem  current code page, so non-ASCII bytes would turn into mojibake.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
cd /d "%~dp0.."

set "KALO="
for %%P in (
    "out\build\msvc-check\kalo.exe"
    "out\build\x64-Debug\kalo.exe"
    "out\build\x64-Release\kalo.exe"
    "build\kalo.exe"
    "build\Release\kalo.exe"
    "build\Debug\kalo.exe"
    "kalo.exe"
) do (
    if not defined KALO (
        if exist "%%~P" set "KALO=%%~P"
    )
)

if not defined KALO (
    echo [ERROR] kalo.exe not found in this directory.
    echo Build the project first, then run this again.
    echo Looked in: out\build\msvc-check, out\build\x64-Debug, build, .
    echo.
    pause
    exit /b 1
)

set "SAMPLE=%TEMP%\kalo_esc_sample.txt"
>  "%SAMPLE%" echo line one: press Ctrl-S, then Esc once
>> "%SAMPLE%" echo line two: then try the arrow keys
>> "%SAMPLE%" echo line three: Ctrl-Q to quit

echo ============================================================
echo  Esc verification  -  REAL CONSOLE REQUIRED
echo ============================================================
echo.
echo  Binary : %KALO%
echo  File   : %SAMPLE%
echo.
echo  STEP 1   Ctrl-S        "Save as:" prompt appears
echo  STEP 2   Esc  (once)   prompt must cancel IMMEDIATELY
echo                         if nothing happens, the fix did not take
echo  STEP 3   Arrow keys    cursor must move normally
echo                         if an arrow cancels the prompt instead,
echo                         Esc is swallowing it - raise the timeout
echo                         (kEscTimeoutMs in src/Terminal.cpp)
echo  STEP 4   Ctrl-Q        quit
echo.
pause

"%KALO%" "%SAMPLE%"

echo.
echo kalo exited with code %errorlevel%
del "%SAMPLE%" >nul 2>&1
pause
