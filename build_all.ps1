# Builds the firmware and packs it with the matching bank for install.html: build_firmware.ps1, then build_install.ps1.
#   .\build_all.ps1                              live cache (84 KB) and the packed bank
#   .\build_all.ps1 -Name sf2player_arm72        another firmware name
#   .\build_all.ps1 -NoLiveCache                 without the live cache (all formats, the MIDI file player), with the BFP8 bank
#   .\build_all.ps1 -Serve                       then starts the server and opens install.html
param([string]$Name = ('sf2player_' + (Get-Date -Format 'yyyyMMdd_HHmm')), [switch]$NoLiveCache, [int]$CacheKB = 84,
      [int]$CacheFrames = 2, [string]$Toolchain = 'D:\JL\pi32', [switch]$Serve)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
& (Join-Path $root 'build_firmware.ps1') -Name $Name -NoLiveCache:$NoLiveCache -CacheKB $CacheKB -CacheFrames $CacheFrames -Toolchain $Toolchain
$bank = if ($NoLiveCache) { 'dist\gm_32k_bfp8.fm1b' } else { 'dist\gm_32k_packed.fm1b' }
& (Join-Path $root 'build_install.ps1') -Bank $bank -Serve:$Serve
