@echo off
rem ===========================================================================
rem  Mushihimesama Futari (recompiled) - asset extractor
rem
rem  Drag either of these onto this file:
rem    - your Xbox 360 disc ISO, or
rem    - the game folder copied off your console's storage (the "435A07D7"
rem      folder, or anything above it) - this also installs your DLC, and the
rem      copy is verified against the console's own integrity hashes first.
rem  Or just double-click it when an .iso is sitting in this same folder.
rem
rem  It extracts default.xex and the game modules into "assets\" next to
rem  futari.exe (and DLC packages into "dlc\"), which is where the game looks
rem  by default - after this, futari.exe runs on a plain double-click.
rem
rem  Requires Python 3 (https://www.python.org/downloads/). This is a thin
rem  wrapper around scripts\extract_iso.py.
rem ===========================================================================
setlocal enableextensions
title Mushihimesama Futari - Asset Extractor

rem Work from this script's own folder so "assets\" and ISO auto-detect land here.
cd /d "%~dp0"

rem Locate the extractor (repo layout vs flattened release-zip layout).
rem futari ships from a disc ISO, so this drives extract_iso.py.
set "EXTRACTOR="
if exist "%~dp0extract_iso.py" set "EXTRACTOR=%~dp0extract_iso.py"
if not defined EXTRACTOR if exist "%~dp0scripts\extract_iso.py" set "EXTRACTOR=%~dp0scripts\extract_iso.py"
if not defined EXTRACTOR (
    echo [ERROR] Could not find extract_iso.py next to this script.
    echo         Keep extract_assets.bat together with the extractor script.
    goto :end
)

rem Find a Python 3 interpreter: prefer the py launcher, then python on PATH.
rem Each one is actually run: on a fresh PC "python" on PATH can be the
rem Microsoft Store alias stub, which exists but exits 9009 and does nothing.
set "PY="
py -3 --version >nul 2>&1
if not errorlevel 1 set "PY=py -3"
if not defined PY (
    python --version >nul 2>&1
    if not errorlevel 1 set "PY=python"
)
if not defined PY (
    echo [ERROR] Python 3 was not found on this PC.
    echo         Install it from https://www.python.org/downloads/
    echo         ^(tick "Add python.exe to PATH" in the installer^), then re-run this.
    goto :end
)

echo.
if "%~1"=="" (
    echo Nothing was dragged on - looking for a .iso in this folder...
    echo ^(You can also drag on your disc ISO, or the game folder copied off
    echo your console - see docs\DUMPING.md.^)
)
rem Kept outside the if-block above: a dragged path containing ")" -- think
rem "Program Files (x86)" -- would otherwise close the block early.
if not "%~1"=="" echo Extracting from: %~1
echo Output folders:  %~dp0assets  and  %~dp0dlc
echo.

%PY% "%EXTRACTOR%" %1 --out "%~dp0assets" --dlc-out "%~dp0dlc"
set "RC=%errorlevel%"

echo.
if not "%RC%"=="0" (
    echo [FAILED] Extraction did not complete - see the messages above.
) else if exist "%~dp0futari.exe" (
    echo [DONE] See the summary above. You can now double-click futari.exe.
) else (
    echo [DONE] Game data is in place. Next, build the port: run build.bat, or
    echo        follow the Build section of README.md.
)

:end
echo.
pause
endlocal
