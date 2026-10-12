@echo off
rem Builds Skyrok.dll (release).
rem Needs Visual Studio or its Build Tools with the C++ workload, CMake, and vcpkg with VCPKG_ROOT set.
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer not found: install Visual Studio or its Build Tools.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio installation with the C++ tools was found.
    exit /b 1
)
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT is not set: point it to your vcpkg folder.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
cmake --preset release || exit /b 1
cmake --build build\release || exit /b 1
echo built %~dp0build\release\Skyrok.dll
