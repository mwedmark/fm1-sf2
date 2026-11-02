# Starts the FM-1 emulator with the SF2 player: copies the newest firmware build and the bank into emu/, serves it
# (test_emu/cors_server.py) and opens it. See RUNNING.md section 4.
#   .\start_emu.ps1                      the multitimbral check (test_emu/mt.script)
#   .\start_emu.ps1 -Script ''           no script: play it yourself
#   .\start_emu.ps1 -Tap                 the page that records the emulator's audio into window.__cap (test_emu/tap.js)
#   .\start_emu.ps1 -Bank dist\gm_32k_bfp8.fm1b   another bank (make_bank.js builds them): the same firmware plays every format
param([int]$Port = 8781, [string]$Script = 'mt.script', [switch]$Tap, [switch]$NoBrowser, [string]$Bank = 'dist\gm_32k.fm1b')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$emu = Join-Path $root 'emu'
$site = 'main.js', 'index.html', 'worklet.js', 'persistence.js', 'fwsc.js', 'tryout.js', 'style.css', 'firmwares.json', 'fm1_web.wasm'

# 1. the emulator's own files (MIT, from github.com/joakimeriksson/fm1-emu): kept in emu/, which git ignores
if (!(Test-Path (Join-Path $emu 'main.js'))) {
    $old = Get-ChildItem "$env:LOCALAPPDATA\Temp\claude\*\*\scratchpad\emu\main.js" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$old) { throw "No emulator files in $emu. Put these there (the site's files, e.g. downloaded from https://joakimeriksson.github.io/fm1-emu/): $($site -join ', ')" }
    New-Item -ItemType Directory -Force $emu | Out-Null
    foreach ($f in $site) { Copy-Item (Join-Path $old.DirectoryName $f) $emu -Force }
    Write-Host "Copied the emulator's files from $($old.DirectoryName)"
}

# 2. the firmware (the newest build), the bank as a flash overlay, and our scripts and tap
$build = Join-Path $root 'fm1_open\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc'
$package = Join-Path $emu 'sf2player.fwsc'
if (Test-Path $build) { Copy-Item $build $package -Force }
elseif (!(Test-Path $package)) { throw "No firmware: build it first (RUNNING.md section 1)." }
$bank = if ([IO.Path]::IsPathRooted($Bank)) { $Bank } else { Join-Path $root $Bank }
$flashName = if ($Bank -eq 'dist\gm_32k.fm1b') { 'gm.flash' } else { [IO.Path]::GetFileNameWithoutExtension($bank) + '.flash' }
$flash = Join-Path $emu $flashName
$stale = !(Test-Path $flash)
if (!$stale -and (Test-Path $bank)) { $stale = (Get-Item $bank).LastWriteTime -gt (Get-Item $flash).LastWriteTime }
if ($stale) {
    if (!(Test-Path $bank)) { throw "No bank at $bank" }
    node (Join-Path $root 'make_flash_overlay.js') $bank $flash
}
Copy-Item (Join-Path $root 'test_emu\*') $emu -Force -Exclude 'cors_server.py'
$page = Join-Path $emu 'index_tap.html'
(Get-Content (Join-Path $emu 'index.html') -Raw) -replace '<head>', '<head><script src="tap.js"></script>' | Set-Content $page -NoNewline

# 3. the firmware catalog entry "sf2" points at this server's port
$catalog = Join-Path $emu 'firmwares.json'
$json = Get-Content $catalog -Raw
if ($json -notmatch '"id":\s*"sf2"') { throw "firmwares.json has no sf2 entry: add one with id sf2 and package http://127.0.0.1:$Port/sf2player.fwsc" }
$json = $json -replace 'http://127\.0\.0\.1:\d+/sf2player\.fwsc', "http://127.0.0.1:$Port/sf2player.fwsc"
Set-Content $catalog $json -NoNewline

# 4. the server (127.0.0.1 only), unless one already answers on the port
if (!(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)) {
    Start-Process python -ArgumentList "`"$(Join-Path $root 'test_emu\cors_server.py')`" $Port `"$emu`"" -WindowStyle Hidden
    Start-Sleep -Seconds 1
}

# 5. open it (127.0.0.1, not localhost: flash= and script= are fetched from the page's own origin)
$url = "http://127.0.0.1:$Port/" + $(if ($Tap) { 'index_tap.html' } else { '' }) + "?fw=sf2&flash=$flashName&mhz=240" + $(if ($Script) { "&script=$Script" } else { '' })
Write-Host $url
if (!$NoBrowser) { Start-Process $url }
