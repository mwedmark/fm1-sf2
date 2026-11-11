# Packs the last firmware build and a bank into dist\install\ for install.html (node make_install.js).
# Run it after .\build_firmware.ps1: it packs the build in fm1_open\build\dx7, not a dist\*.fwsc of your choosing.
#   .\build_install.ps1                                  the packed bank (the default firmware, with the live cache, plays it)
#   .\build_install.ps1 -Bank dist\gm_32k_bfp8.fm1b      the BFP8 bank (either firmware plays it)
#   .\build_install.ps1 -Serve                           also starts the server and opens install.html (start_web.ps1)
param([string]$Bank = 'dist\gm_32k_packed.fm1b', [int]$Voices = 0, [switch]$Serve)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Push-Location $root
try {
    if (!(Test-Path $Bank)) { throw "No bank $Bank" }
    $nodeArgs = @('make_install.js', $Bank)
    if ($Voices -gt 0) { $nodeArgs += "--voices=$Voices" }
    node @nodeArgs
    if ($LASTEXITCODE -ne 0) { throw 'make_install.js failed' }
    $manifest = Get-Content 'dist\install\manifest.json' -Raw | ConvertFrom-Json
    if (!$manifest.name) { Write-Warning 'No dist\*.fwsc matches this build: it was not built with build_firmware.ps1 / build_fw.ps1' }
    Write-Host "install.html installs: $($manifest.name) with $($manifest.bank.source)"
} finally { Pop-Location }
if ($Serve) { & (Join-Path $root 'start_web.ps1') }
