# Builds the SF2 player firmware into dist\<Name>.fwsc (with a .txt of CRC32s next to it). A wrapper around build_fw.ps1.
# Default: the live cache with an 84 KB pool (the one firmware since 2026-11-11: BFP8 and MP3 banks; 90 KB and more did not boot), for the packed bank.
#   .\build_firmware.ps1                         dist\sf2player_YYYYMMDD_HHMM.fwsc, live cache, 84 KB
#   .\build_firmware.ps1 -Name sf2player_arm72   another name (letters, digits, _ . -, at most 27 characters)
#   .\build_firmware.ps1 -NoLiveCache            without the live cache: all bank formats and the MIDI file player, no MP3 banks
#   .\build_firmware.ps1 -CacheKB 64             another pool size (do not go over 84)
# Then .\build_install.ps1 packs it for install.html, or .\build_all.ps1 does both.
param([string]$Name = ('sf2player_' + (Get-Date -Format 'yyyyMMdd_HHmm')), [switch]$NoLiveCache, [int]$CacheKB = 84,
      [int]$CacheFrames = 2, [string]$Toolchain = 'D:\JL\pi32')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (!$NoLiveCache -and $CacheKB -gt 84) { Write-Warning "-CacheKB ${CacheKB}: 90 KB and more did not boot in fm1-emu, 96 KB not on the FM-1 (HANDOVER_LIVE_CACHE.md)" }

# the bank build_fw.ps1 writes the CRC32 of: the one this firmware plays
$bank = if ($NoLiveCache) { 'dist\gm_32k_bfp8.fm1b' } else { 'dist\gm_32k_packed.fm1b' }
$opts = @{ Name = $Name; Bank = $bank; Toolchain = $Toolchain }
$opts += @{ LiveCache = !$NoLiveCache; CacheKB = $CacheKB; CacheFrames = $CacheFrames }

& (Join-Path $root 'build_fw.ps1') @opts
Write-Host "Firmware: dist\$Name.fwsc"
