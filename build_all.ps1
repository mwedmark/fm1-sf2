# Builds the firmware and packs it with the matching bank for install.html: build_firmware.ps1, then build_install.ps1.
#   .\build_all.ps1                              default (2026-11-14): 44.1 kHz, voices shared between the two cores (RUNNING.md, 2h), four-point interpolation,
#                                                and dist\gm_44k_fill.fm1b (make it first: RUNNING.md, 2f)
#   .\build_all.ps1 -Name sf2player_arm72        another firmware name
#   .\build_all.ps1 -Linear                      linear instead of four-point interpolation
#   .\build_all.ps1 -DualCore off                the same on one core
#   .\build_all.ps1 -Rate 32000                  the old 32 kHz build: live cache (84 KB), the packed bank, linear, one core
#   .\build_all.ps1 -Rate 32000 -NoLiveCache     without the live cache (all formats, the MIDI file player), with the BFP8 bank
#   .\build_all.ps1 -Serve                       then starts the server and opens install.html
param([string]$Name = ('sf2player_' + (Get-Date -Format 'yyyyMMdd_HHmm')), [switch]$NoLiveCache, [int]$CacheKB = 84,
      [int]$CacheFrames = 2, [string]$Toolchain = 'D:\JL\pi32', [switch]$Serve,
      [ValidateSet(32000, 44100)][int]$Rate = 44100, [ValidateSet('auto', 'on', 'off')][string]$DualCore = 'auto', [switch]$Interp4, [switch]$Linear)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ($Rate -eq 44100 -and $NoLiveCache) { throw '44.1 kHz needs the live cache: its bank is packed (for the old build without it: -Rate 32000 -NoLiveCache)' }
$bank = if ($Rate -eq 44100) { 'dist\gm_44k_fill.fm1b' } elseif ($NoLiveCache) { 'dist\gm_32k_bfp8.fm1b' } else { 'dist\gm_32k_packed.fm1b' }
if (!(Test-Path (Join-Path $root $bank))) { throw "No $bank to install with it" }
$fw = @{ Name = $Name; NoLiveCache = $NoLiveCache; CacheFrames = $CacheFrames; Toolchain = $Toolchain; Rate = $Rate; DualCore = $DualCore }
if ($Interp4) { $fw.Interp4 = $true }
if ($Linear) { $fw.Linear = $true }
if ($PSBoundParameters.ContainsKey('CacheKB')) { $fw.CacheKB = $CacheKB }
& (Join-Path $root 'build_firmware.ps1') @fw
& (Join-Path $root 'build_install.ps1') -Bank $bank -Serve:$Serve
