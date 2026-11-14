# Builds the SF2 player firmware into dist\<Name>.fwsc (with a .txt of CRC32s next to it). A wrapper around build_fw.ps1.
# Default: the live cache with an 84 KB pool (the one firmware since 2026-11-11: BFP8 and MP3 banks; 90 KB and more did not boot), for the packed bank.
#   .\build_firmware.ps1                         dist\sf2player_YYYYMMDD_HHMM.fwsc, 44.1 kHz, live cache (the pool from what the heap allows: 77 KB)
#   .\build_firmware.ps1 -Name sf2player_arm72   another name (letters, digits, _ . -, at most 27 characters)
#   .\build_firmware.ps1 -NoLiveCache            without the live cache: all bank formats and the MIDI file player, no MP3 banks
#   .\build_firmware.ps1 -CacheKB 64             another pool size (do not go over 84)
#   .\build_firmware.ps1 -Rate 44100             the player at 44.1 kHz, for a bank made at 44.1 kHz (RUNNING.md, 2f); the voices of each block shared between
#                                                the two cores (-DualCore auto: on at 44.1 kHz; RUNNING.md, 2h); the pool from what the heap allows unless -CacheKB
#   .\build_firmware.ps1 -DualCore off           one core, as before 2026-11-14
#   .\build_firmware.ps1 -Linear                 linear instead of four-point interpolation
#   .\build_firmware.ps1 -Rate 32000             the old 32 kHz build (linear, one core, packed bank)
# Default since 2026-11-14: 44.1 kHz, both cores, four-point interpolation, bank dist\gm_44k_fill.fm1b.
# Then .\build_install.ps1 packs it for install.html, or .\build_all.ps1 does both.
param([string]$Name = ('sf2player_' + (Get-Date -Format 'yyyyMMdd_HHmm')), [switch]$NoLiveCache, [int]$CacheKB = 84,
      [int]$CacheFrames = 2, [string]$Toolchain = 'D:\JL\pi32', [ValidateSet(32000, 44100)][int]$Rate = 44100,
      [ValidateSet('auto', 'on', 'off')][string]$DualCore = 'auto', [switch]$Interp4, [switch]$Linear)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (!$NoLiveCache -and $CacheKB -gt 84) { Write-Warning "-CacheKB ${CacheKB}: 90 KB and more did not boot in fm1-emu, 96 KB not on the FM-1 (HANDOVER_LIVE_CACHE.md)" }

# the bank build_fw.ps1 writes the CRC32 of: the one this firmware plays
$bank = if ($Rate -eq 44100) { 'dist\gm_44k_fill.fm1b' } elseif ($NoLiveCache) { 'dist\gm_32k_bfp8.fm1b' } else { 'dist\gm_32k_packed.fm1b' }
$opts = @{ Name = $Name; Bank = $bank; Toolchain = $Toolchain; Rate = $Rate; DualCore = $DualCore }
if ($Interp4) { $opts.Interp4 = $true }
if ($Linear) { $opts.Linear = $true }
if ($Rate -eq 44100 -and $NoLiveCache) { throw '44.1 kHz needs the live cache (packed bank); for the old build without it: -Rate 32000 -NoLiveCache' }
$opts += @{ LiveCache = !$NoLiveCache; CacheFrames = $CacheFrames }
if ($PSBoundParameters.ContainsKey('CacheKB')) { $opts.CacheKB = $CacheKB }   # else build_fw.ps1's: 84 KB, 81 KB at 44.1 kHz

& (Join-Path $root 'build_fw.ps1') @opts
Write-Host "Firmware: dist\$Name.fwsc"
