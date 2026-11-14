# Builds the firmware and packs it with the matching bank for install.html: build_firmware.ps1, then build_install.ps1.
#   .\build_all.ps1                              live cache (84 KB) and the packed bank
#   .\build_all.ps1 -Name sf2player_arm72        another firmware name
#   .\build_all.ps1 -NoLiveCache                 without the live cache (all formats, the MIDI file player), with the BFP8 bank
#   .\build_all.ps1 -Rate 44100                  the player at 44.1 kHz with the voices shared between the two cores (RUNNING.md, 2h), and dist\gm_44k_fill.fm1b (make it first: RUNNING.md, 2f)
#   .\build_all.ps1 -Rate 44100 -DualCore off    the same on one core; -Interp4: four-point interpolation
#   .\build_all.ps1 -Serve                       then starts the server and opens install.html
param([string]$Name = ('sf2player_' + (Get-Date -Format 'yyyyMMdd_HHmm')), [switch]$NoLiveCache, [int]$CacheKB = 84,
      [int]$CacheFrames = 2, [string]$Toolchain = 'D:\JL\pi32', [switch]$Serve,
      [ValidateSet(32000, 44100)][int]$Rate = 32000, [ValidateSet('auto', 'on', 'off')][string]$DualCore = 'auto', [switch]$Interp4)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ($Rate -eq 44100 -and $NoLiveCache) { throw '-Rate 44100 needs the live cache: its bank is packed' }
$bank = if ($Rate -eq 44100) { 'dist\gm_44k_fill.fm1b' } elseif ($NoLiveCache) { 'dist\gm_32k_bfp8.fm1b' } else { 'dist\gm_32k_packed.fm1b' }
if (!(Test-Path (Join-Path $root $bank))) { throw "No $bank to install with it" }
$fw = @{ Name = $Name; NoLiveCache = $NoLiveCache; CacheFrames = $CacheFrames; Toolchain = $Toolchain; Rate = $Rate; DualCore = $DualCore }
if ($Interp4) { $fw.Interp4 = $true }
if ($PSBoundParameters.ContainsKey('CacheKB')) { $fw.CacheKB = $CacheKB }
& (Join-Path $root 'build_firmware.ps1') @fw
& (Join-Path $root 'build_install.ps1') -Bank $bank -Serve:$Serve
