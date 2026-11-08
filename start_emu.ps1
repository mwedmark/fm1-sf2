# Starts the FM-1 emulator with the SF2 player: copies the newest firmware build and the bank into emu/, serves it
# (test_emu/cors_server.py) and opens it. See RUNNING.md section 4.
#   .\start_emu.ps1                      the multitimbral check (test_emu/mt.script)
#   .\start_emu.ps1 -Script ''           no script: play it yourself
#   .\start_emu.ps1 -Tap                 the page that records the emulator's audio into window.__cap (test_emu/tap.js)
#   .\start_emu.ps1 -Bank dist\gm_32k_bfp8.fm1b   another bank (make_bank.js builds them): the same firmware plays every format
#   .\start_emu.ps1 -Lan                 reachable from other computers on the network too (they need a Chrome flag, RUNNING.md section 4)
#   .\start_emu.ps1 -Update              first fetch the emulator's newest files from its site (the old ones go to emu\previous\)
param([int]$Port = 8781, [string]$Script = 'mt.script', [switch]$Tap, [switch]$NoBrowser, [string]$Bank = 'dist\gm_32k.fm1b', [switch]$Update, [switch]$Lan)
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
if ($Update) {
    $previous = Join-Path $emu 'previous'
    New-Item -ItemType Directory -Force $previous | Out-Null
    foreach ($f in $site) {
        $here = Join-Path $emu $f
        if (Test-Path $here) { Copy-Item $here $previous -Force }
        Invoke-WebRequest "https://joakimeriksson.github.io/fm1-emu/$f" -OutFile $here -UseBasicParsing
    }
    Write-Host "Fetched the emulator's files from https://joakimeriksson.github.io/fm1-emu/ (the old ones are in $previous)"
}

# 1b. three local changes to the emulator's main.js, made once (again after -Update):
#  - a browser keeps the package it ran first under the id sf2 (IndexedDB) and boots those bytes until "Check for firmware
#    updates" is pressed; every build has its own address (?v= below), so take a kept copy only while the address is the same
#  - window.__lcd holds the latest screen, so a test can read it while the page is hidden (RUNNING.md section 4)
#  - window.__perf, the load and idle share a test reads (test_emu\measure_load.ps1)
#  - window.__midi(bytes) and window.__key(id, down): MIDI into the DIN jack (SysEx too: a song upload) and the panel's buttons, from a test
$mainJs = Join-Path $emu 'main.js'
$code = Get-Content $mainJs -Raw
$patched = $code -replace '(  const kept = await keptEntry\(f\.id\);\r?\n)  if \(kept\) \{', '$1  if (kept && (f.id !== "sf2" || kept.url === f.package)) {   // local: a new build of sf2 is fetched'
if ($patched -notmatch '__lcd') {
    $patched = $patched -replace '(  if \(!m\.lcd && frame\?\.lcd\) m\.lcd = frame\.lcd;)(\r?\n)', '$1$2  if (m.lcd) window.__lcd = m.lcd;   // local addition: lets a test read the screen while the page is hidden$2'
}
if ($patched -notmatch 'window\.__perf') { $patched = $patched.TrimEnd() + "`n`nwindow.__perf = perf;   // local addition: lets a test read load and idleShare`n" }
if ($patched -notmatch 'window\.__midi') { $patched = $patched.TrimEnd() + "`nwindow.__midi = (bytes) => send({ type: `"midi`", bytes: Array.from(bytes) });   // local addition: a test sends MIDI (SysEx too) into the DIN jack`n" }
if ($patched -notmatch 'window\.__key') { $patched = $patched.TrimEnd() + "`nwindow.__key = (id, down) => send({ type: `"key`", id, down });   // local addition: a test presses a panel button (ids as in scripts)`n" }
if ($patched -ne $code) { Set-Content $mainJs $patched -NoNewline }
if ($patched -notmatch 'a new build of sf2 is fetched') { Write-Warning "main.js changed upstream: a browser may boot an older sf2 build (press Check for firmware updates)" }

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
Copy-Item (Join-Path $root 'fm1_song.js') $emu -Force               # for test_emu\song_test.js
$page = Join-Path $emu 'index_tap.html'
(Get-Content (Join-Path $emu 'index.html') -Raw) -replace '<head>', '<head><script src="tap.js"></script>' | Set-Content $page -NoNewline

# 3. the firmware catalog entry "sf2" points at this server's port
$catalog = Join-Path $emu 'firmwares.json'
$json = Get-Content $catalog -Raw
if ($json -notmatch '"id":\s*"sf2"') {                     # the site's catalog (a new one after -Update) has no entry of ours
    $entry = '{"id": "sf2", "name": "sf2player", "version": "test", "author": "local", "license": "GPL-3.0", "about": "local test", ' +
             '"repo": "http://127.0.0.1", "package": "sf2player.fwsc"}'
    $json = $json -replace '("firmwares":\s*\[)', "`$1`n    $entry,"
    if ($json -notmatch '"id":\s*"sf2"') { throw "firmwares.json: no firmwares list to add the sf2 entry to" }
}
# ?v= the package's time: each build gets its own address, and main.js (1b) boots a kept copy only of the same address. The address
# is relative to the page, so the same catalog works at 127.0.0.1 and from another computer (-Lan).
$version = (Get-Item $package).LastWriteTimeUtc.Ticks
$json = $json -replace '"package":\s*"(http://127\.0\.0\.1:\d+/)?sf2player\.fwsc(\?v=\d+)?"', "`"package`": `"sf2player.fwsc?v=$version`""
if ($json -notmatch [regex]::Escape("sf2player.fwsc?v=$version")) { throw "firmwares.json: the sf2 package address was not updated (the page would boot a kept older build)" }
Set-Content $catalog $json -NoNewline

# 4. the server (127.0.0.1 only; -Lan: every address), unless one already answers on the port as asked
$listening = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)
if ($Lan -and $listening -and !($listening | Where-Object { $_.LocalAddress -in '::', '0.0.0.0' })) {
    $listening | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force }      # it answers on 127.0.0.1 only: start it again
    $listening = @()
    Start-Sleep -Milliseconds 500
}
if (!$listening) {
    $bind = if ($Lan) { '::' } else { '127.0.0.1' }
    Start-Process python -ArgumentList "`"$(Join-Path $root 'test_emu\cors_server.py')`" $Port `"$emu`" $bind" -WindowStyle Hidden
    Start-Sleep -Seconds 1
}

# 5. open it (127.0.0.1, not localhost: flash= and script= are fetched from the page's own origin)
$url = "http://127.0.0.1:$Port/" + $(if ($Tap) { 'index_tap.html' } else { '' }) + "?fw=sf2&flash=$flashName&mhz=240" + $(if ($Script) { "&script=$Script" } else { '' })
Write-Host $url
if (!$NoBrowser) { Start-Process $url }
# From another computer: Chrome runs the emulator's audio (AudioWorklet) only on localhost or HTTPS, so -Lan also serves HTTPS on
# port+1 with a self-signed certificate (test_emu\lan_cert.pem): click through Chrome's warning once, no flag needed.
if ($Lan) {
    . (Join-Path $root 'test_emu\lan_cert.ps1')
    $lanCert = Get-LanCertificate $root
    $ips = $lanCert.Ips; $pem = $lanCert.Pem
    if ($lanCert.Renewed) {                                   # a new certificate: a server with the old one stops
        Get-NetTCPConnection -State Listen -LocalPort ($Port + 1) -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force }
        Start-Sleep -Milliseconds 500
    }
    if (!(Get-NetTCPConnection -State Listen -LocalPort ($Port + 1) -ErrorAction SilentlyContinue)) {
        Start-Process python -ArgumentList "`"$(Join-Path $root 'test_emu\cors_server.py')`" $($Port + 1) `"$emu`" :: `"$pem`"" -WindowStyle Hidden
        Start-Sleep -Seconds 1
    }
    $rest = $url.Substring($url.IndexOf('/', 8))
    $ips | ForEach-Object { Write-Host ("From the network ($($_.InterfaceAlias)): https://$($_.IPAddress):$($Port + 1)$rest  (accept the certificate warning once)") }
}
