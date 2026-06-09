@echo off
setlocal EnableDelayedExpansion

set "ARCH=%1"
if "%ARCH%"=="" set "ARCH=x64"
if /I "%ARCH%"=="x64" (
    set "CMAKE_ARCH=x64"
    set "SDL2_ARCH=x64"
    set "CMAKE_ARCH_DIR=windows-x86_64"
    set "VCVARS_ARCH=x64"
) else if /I "%ARCH%"=="x86" (
    set "CMAKE_ARCH=Win32"
    set "SDL2_ARCH=x86"
    set "CMAKE_ARCH_DIR=windows-i386"
    set "VCVARS_ARCH=x86"
) else (
    echo Usage: build.bat [x64^|x86]
    exit /b 1
)

set "VCVARSALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARSALL=%%i\VC\Auxiliary\Build\vcvarsall.bat"
            goto :vs_found
        )
    )
)

for %%V in (Enterprise Professional Community "BuildTools") do (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\%%~V\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\%%~V\VC\Auxiliary\Build\vcvarsall.bat"
        goto :vs_found
    )
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%~V\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%~V\VC\Auxiliary\Build\vcvarsall.bat"
        goto :vs_found
    )
)
if not defined VCVARSALL (
    echo ERROR: Visual Studio 2022 not found.
    exit /b 1
)
:vs_found

set "SOURCE=."
set "BUILDROOT=%LOCALAPPDATA%\bddview-build"
set "BUILDDIR=%BUILDROOT%\build"
set "DEPSDIR=%BUILDROOT%\deps"
set "RELEASE_VERSION="
set "VERSION_FLAG="
if defined BDDVIEW_RELEASE_VERSION set "RELEASE_VERSION=%BDDVIEW_RELEASE_VERSION%"
if not defined RELEASE_VERSION if exist "%SOURCE%\VERSION" set /p RELEASE_VERSION=<"%SOURCE%\VERSION"
if defined RELEASE_VERSION set "VERSION_FLAG=-DBDDVIEW_RELEASE_VERSION=%RELEASE_VERSION%"
set "SDL2VER=2.30.2"
set "SDL2DIR=%DEPSDIR%\SDL2-%SDL2VER%"
set "SDL2CMAKE=%SDL2DIR%\cmake"
set "CMAKEVER=3.29.3"
set "CMAKEDIR=%DEPSDIR%\cmake-%CMAKEVER%-%CMAKE_ARCH_DIR%"
set "CMAKE=%CMAKEDIR%\bin\cmake.exe"

echo [1/4] Setting up VS 2022 %VCVARS_ARCH% environment...
call "%VCVARSALL%" %VCVARS_ARCH%
if errorlevel 1 exit /b 1

echo [2/4] Creating build dirs...
if not exist "%DEPSDIR%" mkdir "%DEPSDIR%"
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"

echo [3/4] Getting cmake %CMAKEVER% + SDL2 %SDL2VER%...
if not exist "%CMAKE%" (
    powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; (New-Object Net.WebClient).DownloadFile('https://github.com/Kitware/CMake/releases/download/v%CMAKEVER%/cmake-%CMAKEVER%-%CMAKE_ARCH_DIR%.zip', '%DEPSDIR%\cmake.zip')"
    powershell -NoProfile -Command "Expand-Archive -Path '%DEPSDIR%\cmake.zip' -DestinationPath '%DEPSDIR%' -Force"
    del "%DEPSDIR%\cmake.zip"
)

if not exist "%SDL2DIR%" (
    powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; (New-Object Net.WebClient).DownloadFile('https://github.com/libsdl-org/SDL/releases/download/release-%SDL2VER%/SDL2-devel-%SDL2VER%-VC.zip', '%DEPSDIR%\sdl2.zip')"
    powershell -NoProfile -Command "Expand-Archive -Path '%DEPSDIR%\sdl2.zip' -DestinationPath '%DEPSDIR%' -Force"
    del "%DEPSDIR%\sdl2.zip"
)

echo [4/4] CMake configure + build...
"%CMAKE%" -B "%BUILDDIR%" -G "Visual Studio 17 2022" -A %CMAKE_ARCH% ^
    -DSDL2_DIR="%SDL2CMAKE%" ^
    %VERSION_FLAG% ^
    "%SOURCE%"
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILDDIR%" --config Release -- /v:normal
if errorlevel 1 exit /b 1

copy /Y "%SDL2DIR%\lib\%SDL2_ARCH%\SDL2.dll" "%BUILDDIR%\Release\"

echo.
echo *** Build succeeded! ***
echo EXE : %BUILDDIR%\Release\bddview.exe
exit /b 0
