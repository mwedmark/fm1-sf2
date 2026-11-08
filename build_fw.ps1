# Builds the SF2 player firmware with its name and build time, for the panel's info page (REC), and puts the package in dist\.
#   .\build_fw.ps1 -Name sf2player_32v_info           dist\sf2player_32v_info.fwsc, which the info page names
#   .\build_fw.ps1 -Name sf2player_32v_info -Bank dist\gm_32k_bfp8.fm1b   also print the bank's and the sound set's CRC32
#   .\build_fw.ps1 -Name sf2player_live -LiveCache [-CacheKB 96] [-CacheFrames 2]   with the live cache (RUNNING.md, 1b): plays banks made by pack_bank.js
# The name and time go to fm1_open\app\build_info.h for the build (git ignores it) and are removed after, so a plain
# sdk\Build.ps1 shows FM1_OFFICIAL_OPEN.fwsc and the compiler's clock. The CRC32s are written next to the package (.txt).
param([Parameter(Mandatory)][string]$Name, [string]$Bank = 'dist\gm_32k_bfp8.fm1b', [string]$Toolchain = 'D:\JL\pi32', [switch]$LiveCache, [int]$CacheKB = 96, [int]$CacheFrames = 2)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if ($Name -notmatch '^[A-Za-z0-9_.-]{1,27}$') { throw 'Name: letters, digits, _ . - only, at most 27 characters (the screen shows 40 with .fwsc)' }
$info = Join-Path $root 'fm1_open\app\build_info.h'
$date = Get-Date -Format 'yyyy-MM-dd HH:mm'
Set-Content $info "/* Written by build_fw.ps1 for one build; not committed. */`n#define FM1_BUILD_NAME `"$Name.fwsc`"`n#define FM1_BUILD_DATE `"$date`"`n" -NoNewline
try {
    Push-Location (Join-Path $root 'fm1_open')
    $env:FM1_PI32_TOOLCHAIN = $Toolchain
    $live = @{}
    if ($LiveCache) { $live = @{ LiveCache = $true; CacheKB = $CacheKB; CacheFrames = $CacheFrames } }
    ./sdk/Build.ps1 -FirmwareOnly -Toolchain $Toolchain @live
    if (!$?) { throw 'The build failed.' }
} finally {
    Pop-Location
    Remove-Item $info -ErrorAction SilentlyContinue
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
