<#
.SYNOPSIS
    Configure, build and test stocksim with MSVC from any PowerShell prompt.
.DESCRIPTION
    Enters the Visual Studio developer environment (so cl.exe, cmake and ninja are on PATH),
    then runs the given CMake preset: configure, build, and (unless -NoTest) ctest.
.EXAMPLE
    ./tools/build.ps1 -Preset debug
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'core-only')]
    [string]$Preset = 'debug',
    [switch]$NoTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found; install Visual Studio 2022 (Build Tools) with the C++ workload."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
    }
    # VsDevCmd shells out to vswhere itself, so it must be on PATH.
    $env:PATH = "$(Split-Path -Parent $vswhere);$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

Push-Location $repoRoot
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
    if (-not $NoTest -and $Preset -ne 'release') {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
