<#
.SYNOPSIS
  Configure and build the Project Mordor networking prototype (MSVC via CMake).
.EXAMPLE
  ./scripts/build.ps1            # Release
  ./scripts/build.ps1 -Config Debug
#>
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')][string]$Config = 'Release',
    [string]$BuildDir = 'build'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    cmake -S . -B $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
    cmake --build $BuildDir --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
    Write-Host "Build OK: $BuildDir/$Config"
} finally {
    Pop-Location
}
