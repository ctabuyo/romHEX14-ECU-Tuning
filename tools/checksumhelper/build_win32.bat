@echo off
REM ─────────────────────────────────────────────────────────────────
REM build_win32.bat  —  Build checksumhelper.exe (32-bit) on Windows
REM
REM Requires: MinGW with 32-bit multilib support
REM If you have Qt's MinGW: C:\Qt\Tools\mingw1310_64\bin\gcc.exe -m32
REM fails if 32-bit libs missing — use winlibs-i686 instead:
REM   https://github.com/brechtsanders/winlibs_mingw/releases
REM   Download i686-mingw-w64-ucrt package and extract to C:\winlibs32
REM ─────────────────────────────────────────────────────────────────

set SCRIPT_DIR=%~dp0
set OUTPUT=%SCRIPT_DIR%\checksumhelper.exe

REM Try Qt MinGW 64 with -m32 first
set GCC_QT="C:\Qt\Tools\mingw1310_64\bin\gcc.exe"
if exist %GCC_QT% (
    echo Trying Qt MinGW with -m32...
    %GCC_QT% -m32 -O2 -static "%SCRIPT_DIR%\main.c" -o "%OUTPUT%" 2>nul
    if exist "%OUTPUT%" (
        echo Built: %OUTPUT%
        goto :done
    )
    echo Qt MinGW -m32 failed (missing 32-bit libs)
)

REM Try winlibs 32-bit
set GCC_WL="C:\winlibs32\bin\gcc.exe"
if exist %GCC_WL% (
    echo Using winlibs 32-bit gcc...
    %GCC_WL% -O2 -static "%SCRIPT_DIR%\main.c" -o "%OUTPUT%"
    if exist "%OUTPUT%" (
        echo Built: %OUTPUT%
        goto :done
    )
)

REM Try MSYS2 mingw32
set GCC_MSYS="C:\msys64\mingw32\bin\gcc.exe"
if exist %GCC_MSYS% (
    echo Using MSYS2 mingw32 gcc...
    %GCC_MSYS% -O2 -static "%SCRIPT_DIR%\main.c" -o "%OUTPUT%"
    if exist "%OUTPUT%" (
        echo Built: %OUTPUT%
        goto :done
    )
)

echo.
echo ERROR: No suitable 32-bit gcc found. Options:
echo   1. Install MSYS2 from https://www.msys2.org/ then: pacman -S mingw-w64-i686-gcc
echo   2. Download winlibs i686 from https://github.com/brechtsanders/winlibs_mingw/releases
echo      Extract to C:\winlibs32
echo.
exit /b 1

:done
echo.
echo Copy checksumhelper.exe next to rx14.exe when deploying.
echo.
