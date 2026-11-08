@echo off
setlocal
cd /d "%~dp0"

rem ---------------------------------------------------------------------------
rem Local build for BrightTray-Win32.
rem Output goes to build\BrightTray.exe - this script NEVER overwrites an exe
rem that is currently running, and NEVER modifies the compiler installation.
rem ---------------------------------------------------------------------------

set "TCC_EXE="

if defined TCC if exist "%TCC%\tcc.exe" set "TCC_EXE=%TCC%\tcc.exe"
if not defined TCC_EXE if exist "%~dp0..\tools\tcc\tcc.exe" set "TCC_EXE=%~dp0..\tools\tcc\tcc.exe"
if not defined TCC_EXE (
    for %%I in (tcc.exe) do if exist "%%~$PATH:I" set "TCC_EXE=%%~$PATH:I"
)

if not defined TCC_EXE (
    echo [ERROR] TinyCC not found.
    echo Set TCC to the folder containing tcc.exe, or put tcc.exe on PATH.
    echo Download: https://download.savannah.gnu.org/releases/tinycc/
    exit /b 1
)

echo Compiler: %TCC_EXE%
echo.
echo NOTE: TCC's bundled import libraries lack a few APIs used here.
echo       If the link fails with undefined symbols, copy lib\*.def into
echo       the lib\ folder of your TCC installation (CI does this automatically).
echo       This script will not modify your compiler installation.
echo.

if not exist build mkdir build

"%TCC_EXE%" "%~dp0bright.c" -o "%~dp0build\BrightTray.exe" -lshell32 -ladvapi32 -Wl,-subsystem=windows
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo.
echo Build OK: build\BrightTray.exe
for %%A in ("%~dp0build\BrightTray.exe") do echo Size: %%~zA bytes
powershell -NoProfile -Command "$h = (Get-FileHash '%~dp0build\BrightTray.exe' -Algorithm SHA256).Hash.ToLower(); Write-Host ('SHA256 : ' + $h); Set-Content -Path '%~dp0build\BrightTray.exe.sha256' -Value ($h + '  BrightTray.exe') -Encoding ascii"
endlocal
