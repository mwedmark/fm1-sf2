# Builds the SF2 player firmware with its name and build time, for the panel's info page (REC), and puts the package in dist\.
#   .\build_fw.ps1 -Name sf2player_32v_info           dist\sf2player_32v_info.fwsc, which the info page names
#   .\build_fw.ps1 -Name sf2player_32v_info -Bank dist\gm_32k_bfp8.fm1b   also print the bank's and the sound set's CRC32
#   The live cache is on by default (RUNNING.md, 1b; [-CacheKB 84] [-CacheFrames 2]): the firmware plays BFP8 banks and those made by pack_bank.js.
#   .\build_fw.ps1 -Name sf2player_plain -LiveCache:$false   without it: all bank formats and the MIDI file player, no MP3 banks
#   .\build_fw.ps1 -Name sf2player_44k -Rate 44100      the player at 44.1 kHz (RUNNING.md, 2f): plays banks made at 44.1 kHz; pool 81 KB unless -CacheKB
# The name and time go to fm1_open\app\build_info.h for the build (git ignores it) and are removed after, so a plain
# sdk\Build.ps1 shows FM1_OFFICIAL_OPEN.fwsc and the compiler's clock. The CRC32s are written next to the package (.txt).
# -Rate 44100 writes fm1_open\app\fm1b_rate.h (FM1B_RATE, read by fm1b.h and live_cache\mp3_sound.h) the same way, and removes it after.
# -FlashProbe (a measurement build) adds FM1B_FLASH_PROBE 1 to it: ARP on the info page switches the voices between flash and SRAM (RUNNING.md, 2g).
# -Cpu1Test (a test build) adds FM1_CPU1_TEST 1: ARP on the info page starts the second core once, SEQ chooses the interrupts off meanwhile
# (app\cpu1_test.c; PLAN_DUALCORE_CLOCK.md on branch sf2-dualcore, section 7). Never started at power-on.
param([Parameter(Mandatory)][string]$Name, [string]$Bank = 'dist\gm_32k_packed.fm1b', [string]$Toolchain = 'D:\JL\pi32', [switch]$LiveCache = $true, [int]$CacheKB = 84, [int]$CacheFrames = 2,
      [ValidateSet(32000, 44100)][int]$Rate = 32000, [switch]$FlashProbe, [switch]$Cpu1Test)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ($Name -notmatch '^[A-Za-z0-9_.-]{1,27}$') { throw 'Name: letters, digits, _ . - only, at most 27 characters (the screen shows 40 with .fwsc)' }
$info = Join-Path $root 'fm1_open\app\build_info.h'
$rateFile = Join-Path $root 'fm1_open\app\fm1b_rate.h'
# at 44.1 kHz the heap is 2 KB smaller, so the 84 KB pool does not leave the 111 468 bytes the build asks for: 81 KB does
if ($Rate -eq 44100 -and !$PSBoundParameters.ContainsKey('CacheKB')) { $CacheKB = 81 }
if (Test-Path $rateFile) { throw "$rateFile is left from another build: remove it (-Rate writes it for one build)" }
$date = Get-Date -Format 'yyyy-MM-dd HH:mm'
Set-Content $info "/* Written by build_fw.ps1 for one build; not committed. */`n#define FM1_BUILD_NAME `"$Name.fwsc`"`n#define FM1_BUILD_DATE `"$date`"`n" -NoNewline
if ($Rate -ne 32000 -or $FlashProbe -or $Cpu1Test) {
    $lines = @('/* Written by build_fw.ps1 -Rate / -FlashProbe / -Cpu1Test for one build; not committed. */')
    if ($Rate -ne 32000) { $lines += "#define FM1B_RATE ${Rate}U" }
    if ($FlashProbe) { $lines += '#define FM1B_FLASH_PROBE 1' }
    if ($Cpu1Test) { $lines += '#define FM1_CPU1_TEST 1' }
    Set-Content $rateFile (($lines -join "`n") + "`n") -NoNewline
}
try {
    Push-Location (Join-Path $root 'fm1_open')
    $env:FM1_PI32_TOOLCHAIN = $Toolchain
    $live = @{ LiveCache = [bool]$LiveCache; CacheKB = $CacheKB; CacheFrames = $CacheFrames }
    ./sdk/Build.ps1 -FirmwareOnly -Toolchain $Toolchain @live
    if (!$?) { throw 'The build failed.' }
} finally {
    Pop-Location
    Remove-Item $info -ErrorAction SilentlyContinue
    Remove-Item $rateFile -ErrorAction SilentlyContinue
}
$package = Join-Path $root "dist\$Name.fwsc"
Copy-Item (Join-Path $root 'fm1_open\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc') $package -Force
Push-Location $root
try {
    $report = node fm1_info.js fm1_open\build\dx7\sdk.elf $Bank
    $text = @("$Name.fwsc built $date") + $report
    Set-Content (Join-Path $root "dist\$Name.txt") ($text -join "`n")
    $text | ForEach-Object { Write-Host $_ }
} finally { Pop-Location }
