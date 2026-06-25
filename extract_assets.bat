@echo off
rem ===========================================================================
rem  Mushihimesama Futari (recompiled) - asset extractor
rem
rem  Drag your Xbox 360 disc ISO onto this file, or just double-click it when an
rem  .iso is sitting in this same folder. It extracts default.xex and the game
rem  modules into "assets\" next to futari.exe, which is where the game looks by
rem  default - after this, futari.exe runs on a plain double-click.
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
set "PY="
where py >nul 2>&1
if %errorlevel%==0 set "PY=py -3"
where python >nul 2>&1
if %errorlevel%==0 if not defined PY set "PY=python"
if not defined PY (
    echo [ERROR] Python 3 was not found on this PC.
    echo         Install it from https://www.python.org/downloads/
    echo         ^(tick "Add python.exe to PATH" in the installer^), then re-run this.
    goto :end
)

echo.
if "%~1"=="" (
    echo No ISO was dragged on - looking for a .iso in this folder...
) else (
    echo Extracting from: %~1
)
echo Output folder:   %~dp0assets
echo.

%PY% "%EXTRACTOR%" %1 --out "%~dp0assets"
set "RC=%errorlevel%"

echo.
if not "%RC%"=="0" (
    echo [FAILED] Extraction did not complete - see the messages above.
) else (
    echo [DONE] Assets are in the "assets" folder. You can now run futari.exe.
)

:end
echo.
pause
endlocal
