# Builds the SF2 player firmware with its name and build time, for the panel's info page (REC), and puts the package in dist\.
#   .\build_fw.ps1 -Name sf2player_32v_info           dist\sf2player_32v_info.fwsc, which the info page names
#   .\build_fw.ps1 -Name sf2player_32v_info -Bank dist\gm_32k_bfp8.fm1b   also print the bank's and the sound set's CRC32
#   The live cache is on by default (RUNNING.md, 1b; [-CacheKB 84] [-CacheFrames 2]): the firmware plays BFP8 banks and those made by pack_bank.js.
#   .\build_fw.ps1 -Name sf2player_plain -LiveCache:$false   without it: all bank formats and the MIDI file player, no MP3 banks
#   .\build_fw.ps1 -Name sf2player_44k -Rate 44100      the player at 44.1 kHz (RUNNING.md, 2f): plays banks made at 44.1 kHz
#   -DualCore auto|on|off (default auto): the voices of each block shared between the two cores (RUNNING.md, 2h). auto is on at -Rate 44100 and off at
#     32 kHz, where the image has 165 bytes to spare and the code takes about 2.8 KB. It borrows the SDK's own CPU1 through the SDK's hand-over
#     (fm1b.c, app\cpu1_split.c); CPU1 is never started or held by us. The info page (REC) has the line SPLIT: ARP on/off, SEQ the share of the voices
#     for CPU1 (30 to 70 %, 50 at start); CC 102 on MIDI channel 16 sets it too (0: off).
#   -Interp4: four-point interpolation (FM1B_INTERP4 1); about 18 points of CPU more at 44.1 kHz on one core (RUNNING.md, 2g).
#   -SplitCheck: a verification build of -DualCore: CPU0 also renders CPU1's voices from copies of their state and compares (info page: CHK checked/differed).
#   -FlashProbe: a measurement build (RUNNING.md, 2g): ARP on the info page switches the voices between flash and SRAM; one core only (-DualCore off).
# The pool is taken from what the heap allows: 84 KB at 32 kHz, 81 at 44.1 kHz, less for the dual-core buffers and four-point interpolation; the build
# asks for 111 468 bytes of heap, and if it comes out short build_fw.ps1 lowers the pool by what is missing and links again (unless -CacheKB is given).
# The name and time go to fm1_open\app\build_info.h for the build (git ignores it) and are removed after, so a plain
# sdk\Build.ps1 shows FM1_OFFICIAL_OPEN.fwsc and the compiler's clock. The CRC32s are written next to the package (.txt).
# -Rate, -DualCore, -Interp4 and the like write fm1_open\app\fm1b_rate.h (read by fm1b.h and live_cache\mp3_sound.h) the same way, and remove it after.
# The dual-core code calls two functions of the SDK's library that are local symbols: the build links, reads their addresses with llvm-nm into
# fm1_open\app\cpu1_sdk_addr.h (not committed) and links again until the addresses it was built with are the ones in the image (two or three links).
param([Parameter(Mandatory)][string]$Name, [string]$Bank = 'dist\gm_32k_packed.fm1b', [string]$Toolchain = 'D:\JL\pi32', [switch]$LiveCache = $true, [int]$CacheKB = 84, [int]$CacheFrames = 2,
      [ValidateSet(32000, 44100)][int]$Rate = 32000, [ValidateSet('auto', 'on', 'off')][string]$DualCore = 'auto', [switch]$Interp4, [switch]$SplitCheck, [switch]$FlashProbe)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ($Name -notmatch '^[A-Za-z0-9_.-]{1,27}$') { throw 'Name: letters, digits, _ . - only, at most 27 characters (the screen shows 40 with .fwsc)' }
$info = Join-Path $root 'fm1_open\app\build_info.h'
$rateFile = Join-Path $root 'fm1_open\app\fm1b_rate.h'
$addrFile = Join-Path $root 'fm1_open\app\cpu1_sdk_addr.h'
if (Test-Path $rateFile) { throw "$rateFile is left from another build: remove it (-Rate and the like write it for one build)" }

# which build this is
$split = $DualCore -eq 'on' -or ($DualCore -eq 'auto' -and $Rate -eq 44100 -and !$FlashProbe)
if ($SplitCheck -and !$split) { throw '-SplitCheck needs -DualCore on (or auto at -Rate 44100)' }
if ($FlashProbe -and $split) { throw '-FlashProbe measures one core: add -DualCore off' }
if ($FlashProbe -and $Interp4) { throw '-FlashProbe is for the linear voice loop: not with -Interp4' }
if ($split -and !$LiveCache) { throw '-DualCore: the dual-core build is the live-cache build' }
# the pool the heap allows (KB): at 44.1 kHz the heap is 2 KB smaller; the dual-core buffers take 4 KB, four-point interpolation 3, the check build's copies of the voices 18
if (!$PSBoundParameters.ContainsKey('CacheKB')) {
    $CacheKB = ($(if ($Rate -eq 44100) { 81 } else { 84 })) - ($(if ($split) { 4 } else { 0 })) - ($(if ($Interp4) { 3 } else { 0 })) - ($(if ($SplitCheck) { 18 } else { 0 }))
}
$explicitKB = $PSBoundParameters.ContainsKey('CacheKB')

$date = Get-Date -Format 'yyyy-MM-dd HH:mm'
Set-Content $info "/* Written by build_fw.ps1 for one build; not committed. */`n#define FM1_BUILD_NAME `"$Name.fwsc`"`n#define FM1_BUILD_DATE `"$date`"`n" -NoNewline
if ($Rate -ne 32000 -or $split -or $Interp4 -or $FlashProbe) {
    $lines = @('/* Written by build_fw.ps1 (-Rate, -DualCore, -SplitCheck, -Interp4, -FlashProbe) for one build; not committed. */')
    if ($Rate -ne 32000) { $lines += "#define FM1B_RATE ${Rate}U" }
    if ($split) { $lines += '#define FM1B_SPLIT 1' }
    if ($SplitCheck) { $lines += '#define FM1B_SPLIT_CHECK 1' }
    if ($Interp4) { $lines += '#define FM1B_INTERP4 1' }
    if ($FlashProbe) { $lines += '#define FM1B_FLASH_PROBE 1' }
    Set-Content $rateFile (($lines -join "`n") + "`n") -NoNewline
}

# the SDK library's local symbols the dual-core code calls (cpu1_split.c), found again after each link
$sdkSymbols = 'is4byte_mode', 'cpu_wait_flag', 'cpu_enter_sfc_critical', 'cpu_exit_sfc_critical'
function Write-SdkAddr($map) {
    $l = @('/* Written by build_fw.ps1 for the dual-core build: the SDK library''s local symbols, read off the last link with llvm-nm. Not committed. */')
    foreach ($s in $sdkSymbols) { $l += '#define FM1_SDK_' + $s.ToUpper() + (' 0x{0:X8}U' -f $map[$s]) }
    Set-Content $addrFile (($l -join "`n") + "`n") -NoNewline
}
function Read-SdkAddr {
    $m = @{}
    foreach ($line in & (Join-Path $Toolchain 'bin\llvm-nm.exe') (Join-Path $root 'fm1_open\build\dx7\sdk.elf')) {
        $p = $line -split ' '
        if ($p.Count -eq 3 -and $sdkSymbols -contains $p[2]) {
            $v = [Convert]::ToUInt32($p[0], 16)
            if ($m.ContainsKey($p[2]) -and $m[$p[2]] -ne $v) { throw "two $($p[2]) in sdk.elf" }
            $m[$p[2]] = $v
        }
    }
    foreach ($s in $sdkSymbols) { if (!$m.ContainsKey($s)) { throw "no $s in sdk.elf" } }
    $m
}
function Invoke-Link([int]$kb) {
    $live = @{ LiveCache = [bool]$LiveCache; CacheKB = $kb; CacheFrames = $CacheFrames }
    if ($split) { $script:written = @{}; foreach ($s in $sdkSymbols) { $script:written[$s] = 0x01C40000 }; Write-SdkAddr $script:written }   # placeholders of the same size
    ./sdk/Build.ps1 -FirmwareOnly -Toolchain $Toolchain @live
    if (!$?) { throw 'The build failed.' }
    for ($pass = 2; $split; $pass++) {                      # link again until the image has the addresses it was built with
        $found = Read-SdkAddr
        if (!($sdkSymbols | Where-Object { $found[$_] -ne $script:written[$_] })) { Write-Host "FM1: SDK addresses settled after $($pass - 1) links"; break }
        if ($pass -gt 4) { throw 'The SDK addresses did not settle in 4 links' }
        $script:written = $found
        Write-SdkAddr $script:written
        ./sdk/Build.ps1 -FirmwareOnly -Toolchain $Toolchain @live
        if (!$?) { throw 'The build failed.' }
    }
}
try {
    Push-Location (Join-Path $root 'fm1_open')
    $env:FM1_PI32_TOOLCHAIN = $Toolchain
    for ($attempt = 1; ; $attempt++) {
        try { Invoke-Link $CacheKB; break }
        catch {
            if (!$explicitKB -and $attempt -lt 3 -and $_.Exception.Message -match 'heap is (\d+) bytes; need at least (\d+)') {
                $less = [int][math]::Ceiling(([int]$Matches[2] - [int]$Matches[1]) / 1024)
                $CacheKB -= $less
                Write-Host "FM1: the heap was short by $($Matches[2] - $Matches[1]) bytes: the pool is $CacheKB KB, linking again"
            } else { throw }
        }
    }
} finally {
    Pop-Location
    Remove-Item $info -ErrorAction SilentlyContinue
    Remove-Item $rateFile -ErrorAction SilentlyContinue
    Remove-Item $addrFile -ErrorAction SilentlyContinue
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
