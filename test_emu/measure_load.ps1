# Serves one emulator setup for a load measurement (RUNNING.md section 4): a script, a bank and a firmware package, and prints
# the silent page's address. Open it and read "load n%" from the page; the held part of a load script is from about 5 s on.
#   .\test_emu\measure_load.ps1 load32_filter.script dist\gm_32k.fm1b                       the newest build
#   .\test_emu\measure_load.ps1 load32_filter.script dist\gm_32k.fm1b dist\other.fwsc       another package
param([Parameter(Mandatory)][string]$Script, [string]$Bank = 'dist\gm_32k.fm1b', [string]$Firmware = '')
$root = Split-Path $PSScriptRoot
Set-Location $root
$url = (.\start_emu.ps1 -Tap -Script $Script -NoBrowser -Bank $Bank 6>&1 | Select-String '^http').Line
if ($Firmware) { Copy-Item $Firmware (Join-Path $root 'emu\sf2player.fwsc') -Force }
"$url&mute=1&t=$([DateTime]::Now.Ticks)"
