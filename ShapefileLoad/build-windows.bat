@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if not defined QTDIR set "QTDIR=C:\Qt\6.11.0\msvc2022_64"
set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "NINJA_DIR=C:\Qt\Tools\Ninja"
set "BUILD_DIR=build\windows-ninja"

if not exist "%QTDIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ERROR: Qt MSVC kit could not be found:
    echo   %QTDIR%
    echo Set QTDIR before running this script if Qt is installed elsewhere.
    exit /b 1
)

if not exist "%CMAKE_EXE%" (
    echo ERROR: CMake could not be found:
    echo   %CMAKE_EXE%
    exit /b 1
)

if not exist "%NINJA_DIR%\ninja.exe" (
    echo ERROR: Ninja could not be found:
    echo   %NINJA_DIR%\ninja.exe
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer vswhere.exe could not be found.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"

if not defined VSINSTALL (
    echo ERROR: A Visual Studio installation with the MSVC x64 toolchain could not be found.
    exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat could not be found:
    echo   %VCVARS%
    exit /b 1
)

call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%

set "PATH=%NINJA_DIR%;%QTDIR%\bin;%PATH%"
set "CC=cl.exe"
set "CXX=cl.exe"

echo.
echo Configuring %CD%
echo Qt: %QTDIR%
echo Visual Studio: %VSINSTALL%
echo.

"%CMAKE_EXE%" --fresh ^
    -S . ^
    -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER=cl.exe ^
    -DCMAKE_PREFIX_PATH="%QTDIR%"

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed.
    exit /b %errorlevel%
)

"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    exit /b %errorlevel%
)

echo.
echo Build completed successfully.
echo Output directory: %CD%\outputs\windows\Release\bin
exit /b 0
