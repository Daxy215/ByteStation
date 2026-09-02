param(
    [switch]$f
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "release_build"
$Exe       = Join-Path $BuildDir "src\Release\ByteStation.exe"

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Install vcpkg and set VCPKG_ROOT to its install path."
    exit 1
}

if ($f) {
    Write-Host "Full build"

    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    cmake -S $ScriptDir -B $BuildDir -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
}

cmake --build $BuildDir --config Release

Write-Host "Running ByteStation..."
& $Exe
