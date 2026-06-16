# build.ps1 — Build midway-bddtool (Windows/SDL2 port) using Visual Studio
param(
    [string]$SourceDir  = $PSScriptRoot,
    [string]$BuildRoot  = "$env:LOCALAPPDATA\bddview-build",
    [string]$SharedDeps = "$env:LOCALAPPDATA\midway-build\deps",
    [string]$Sdl2Ver    = "",
    [string]$Arch       = "x64",
    [string]$ReleaseVersion = ""
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if ($Arch -eq "x86") {
    $cmakeArch = "Win32"
    $sdl2Lib   = "x86"
    $vcvarsArch = "x86"
} else {
    $Arch = "x64"
    $cmakeArch = "x64"
    $sdl2Lib   = "x64"
    $vcvarsArch = "x64"
}

# Locate Visual Studio with the C++ toolchain.
$vsRoot = $null
$vsInstallVersion = ""
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $instances = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $instance = $instances | Sort-Object -Property installationVersion -Descending | Select-Object -First 1
    if ($instance -and (Test-Path "$($instance.installationPath)\VC\Auxiliary\Build\vcvarsall.bat")) {
        $vsRoot = $instance.installationPath
        $vsInstallVersion = $instance.installationVersion
    }
}
$vsRoots = @(
    "C:\Program Files\Microsoft Visual Studio\2026\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2026\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2026\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2026\Professional",
    "C:\Program Files\Microsoft Visual Studio\2026\Enterprise",
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
)
if (-not $vsRoot) {
    $vsRoot = $vsRoots | Where-Object { Test-Path "$_\VC\Auxiliary\Build\vcvarsall.bat" } | Select-Object -First 1
}
if (-not $vsRoot) { Write-Error "Visual Studio C++ toolchain not found"; exit 1 }
if (-not $vsInstallVersion) {
    if ($vsRoot -match '\\2026\\') { $vsInstallVersion = "18.0" }
    elseif ($vsRoot -match '\\2022\\') { $vsInstallVersion = "17.0" }
}
$vsMajor = 17
if ($vsInstallVersion -match '^(\d+)\.') {
    $vsMajor = [int]$Matches[1]
}
$cmakeGenerator = if ($vsMajor -ge 18) { "Visual Studio 18 2026" } else { "Visual Studio 17 2022" }
$vcvarsall = "$vsRoot\VC\Auxiliary\Build\vcvarsall.bat"
$cmakeRel  = "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$cmakeRoots = @($vsRoot) + $vsRoots
$vsCmake   = ($cmakeRoots | ForEach-Object { "$_\$cmakeRel" } | Where-Object { Test-Path $_ } | Select-Object -First 1)
if ($vsCmake) { $vsCmake = Split-Path $vsCmake } else { $vsCmake = "" }

# Resolve SDL2 version
if (-not $Sdl2Ver) {
    Write-Host "[1/4] Querying GitHub for latest SDL2 2.x release..." -ForegroundColor Cyan
    try {
        $releases = Invoke-RestMethod "https://api.github.com/repos/libsdl-org/SDL/releases?per_page=20"
        $r = $releases | Where-Object { $_.tag_name -match '^release-2\.' } | Select-Object -First 1
        $Sdl2Ver = $r.tag_name -replace '^release-', ''
    } catch { $Sdl2Ver = "2.30.2" }
}
$sdl2Root  = "$SharedDeps\SDL2-$Sdl2Ver"
$sdl2Cmake = "$sdl2Root\cmake"
$buildDir  = "$BuildRoot\build"
$versionFile = Join-Path $SourceDir "VERSION"
$releaseVersion = $ReleaseVersion.Trim()
if (-not $releaseVersion -and (Test-Path $versionFile)) {
    $releaseVersion = (Get-Content -Path $versionFile -TotalCount 1).Trim()
}
New-Item -ItemType Directory -Force -Path $BuildRoot, $SharedDeps, $buildDir | Out-Null

if (-not (Test-Path $sdl2Root)) {
    $url = "https://github.com/libsdl-org/SDL/releases/download/release-$Sdl2Ver/SDL2-devel-$Sdl2Ver-VC.zip"
    $zip = "$SharedDeps\sdl2.zip"
    Write-Host "[2/4] Downloading SDL2-devel-$Sdl2Ver-VC.zip ..." -ForegroundColor Cyan
    (New-Object Net.WebClient).DownloadFile($url, $zip)
    Expand-Archive -Path $zip -DestinationPath $SharedDeps -Force
    Remove-Item $zip
}

# CMake configure + build
Write-Host "[3/4] Configuring..." -ForegroundColor Cyan
$cacheFile = Join-Path $buildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cachedGenerator = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' |
        Select-Object -First 1 |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
    if ($cachedGenerator -and $cachedGenerator -ne $cmakeGenerator) {
        Write-Host "CMake generator changed ($cachedGenerator -> $cmakeGenerator); clearing cache." -ForegroundColor Yellow
        Remove-Item -LiteralPath $cacheFile -Force
        $cmakeFiles = Join-Path $buildDir "CMakeFiles"
        if (Test-Path $cmakeFiles) {
            Remove-Item -LiteralPath $cmakeFiles -Recurse -Force
        }
    }
}
$cmakeConfigure = "cmake -B `"$buildDir`" -G `"$cmakeGenerator`" -A $cmakeArch -DSDL2_DIR=`"$sdl2Cmake`""
if ($releaseVersion) {
    $cmakeConfigure += " -DBDDVIEW_RELEASE_VERSION=`"$releaseVersion`""
}
$cmakeConfigure += " `"$SourceDir`""
$batLines = @(
    "@echo off",
    "call `"$vcvarsall`" $vcvarsArch",
    "if errorlevel 1 exit /b 1",
    "set PATH=$vsCmake;%PATH%",
    $cmakeConfigure,
    "if errorlevel 1 exit /b 1"
)
$batFile = "$env:TEMP\build_bddview.bat"
[System.IO.File]::WriteAllLines($batFile, $batLines, [System.Text.Encoding]::ASCII)
& cmd.exe /c $batFile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[4/4] Building ($Arch Release)..." -ForegroundColor Cyan
$buildLines = @(
    "@echo off",
    "call `"$vcvarsall`" $vcvarsArch",
    "if errorlevel 1 exit /b 1",
    "set PATH=$vsCmake;%PATH%",
    "cmake --build `"$buildDir`" --config Release",
    "if errorlevel 1 exit /b 1"
)
[System.IO.File]::WriteAllLines($batFile, $buildLines, [System.Text.Encoding]::ASCII)
& cmd.exe /c $batFile
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    $exe = "$buildDir\Release\bddview.exe"
    $toolExe = "$buildDir\Release\bddtool.exe"
    Write-Host "`n*** Build succeeded ***" -ForegroundColor Green
    Write-Host "EXE: $exe" -ForegroundColor Green
    if (Test-Path $toolExe) { Write-Host "TOOL: $toolExe" -ForegroundColor Green }
    $sdl2Dll = "$sdl2Root\lib\$sdl2Lib\SDL2.dll"
    if (Test-Path $sdl2Dll) { Copy-Item $sdl2Dll (Split-Path $exe) -Force }
    $compatDir = Join-Path $SourceDir "build-win\Release"
    $compatAssets = Join-Path $compatDir "assets"
    New-Item -ItemType Directory -Force -Path $compatDir, $compatAssets | Out-Null
    Copy-Item $exe $compatDir -Force
    if (Test-Path $toolExe) { Copy-Item $toolExe $compatDir -Force }
    if (Test-Path $sdl2Dll) { Copy-Item $sdl2Dll $compatDir -Force }
    if (Test-Path "$SourceDir\platform\assets\MaterialSymbolsSharp-Regular.ttf") {
        Copy-Item "$SourceDir\platform\assets\MaterialSymbolsSharp-Regular.ttf" $compatAssets -Force
    }
    if (Test-Path "$SourceDir\appicon.png") {
        Copy-Item "$SourceDir\appicon.png" (Join-Path $compatAssets "appicon.png") -Force
    }
    Write-Host "Compat EXE: $compatDir\bddview.exe" -ForegroundColor Green
    if (Test-Path "$compatDir\bddtool.exe") { Write-Host "Compat TOOL: $compatDir\bddtool.exe" -ForegroundColor Green }
} else {
    Write-Host "`n*** Build FAILED (exit $exitCode) ***" -ForegroundColor Red
}
exit $exitCode
