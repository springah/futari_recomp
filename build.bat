@echo off
rem ===========================================================================
rem  Mushihimesama Futari (recompiled) - one-shot Windows build
rem
rem  Double-click, or run from a terminal. Arguments pass through to build.ps1:
rem    build.bat -GameSource "D:\Mushihimesama Futari Ver 1.5.iso" -Run
rem  See build.ps1 for everything it does; it installs missing tools via winget.
rem ===========================================================================
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "RC=%errorlevel%"
rem Keep the window open when launched from Explorer so the result can be read.
echo %cmdcmdline% | find /i "%~0" >nul && pause
exit /b %RC%
