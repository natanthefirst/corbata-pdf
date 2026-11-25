# Builds Corbata PDF for Windows and packages it into dist\Corbata PDF\ (+ a .zip).
#
# Needs: Visual Studio 2022 or newer Build Tools (C++), CMake, Qt 6.8 msvc2022_64 with the
# Qt PDF module, and vcpkg with qpdf installed for x64-windows-static-md:
#   vcpkg install qpdf:x64-windows-static-md
param(
    [string]$QtDir = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.8.3\msvc2022_64' }),
    [string]$Vcpkg = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\vcpkg' })
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'

function Invoke-Step([string]$what, [scriptblock]$cmd) {
    Write-Host "==> $what" -ForegroundColor Cyan
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}

Invoke-Step 'Configure' {
    # No -G: CMake picks the newest installed Visual Studio (2022, 2026, ...).
    cmake -S $root -B $build -A x64 `
        "-DCMAKE_PREFIX_PATH=$QtDir" `
        "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
}
Invoke-Step 'Build' { cmake --build $build --config Release --parallel }
Invoke-Step 'Test' { ctest --test-dir $build -C Release --output-on-failure }

$dist = Join-Path $root 'dist\Corbata PDF'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $build 'Release\Corbata PDF.exe') $dist

Invoke-Step 'Bundle Qt' {
    & (Join-Path $QtDir 'bin\windeployqt.exe') --release --no-translations --no-system-d3d-compiler `
        --no-opengl-sw --no-compiler-runtime (Join-Path $dist 'Corbata PDF.exe')
}

# The MSVC runtime: ship it next to the exe so it runs on PCs without it installed.
$redist = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT", `
                              "$env:ProgramFiles\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" `
                        -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if ($redist) {
    Copy-Item (Join-Path $redist.FullName '*.dll') $dist
} else {
    Write-Warning 'MSVC runtime DLLs not found; the app needs the VC++ 2015-2022 redistributable on the target PC.'
}

$zip = Join-Path $root 'dist\Corbata-PDF-windows.zip'
Compress-Archive -Path $dist -DestinationPath $zip -Force
Write-Host "`nDone: $dist\Corbata PDF.exe`n      $zip" -ForegroundColor Green
