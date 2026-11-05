# Starts the web server for the SF2 converter (index.html) and the install page (install.html) and opens both.
# Both pages are served by the same server, from this folder. See RUNNING.md sections 3 and 3b.
#   .\start_web.ps1              port 8765, opens both pages
#   .\start_web.ps1 -NoBrowser   only starts the server
# Stop it by its port: Get-NetTCPConnection -State Listen -LocalPort 8765 | ForEach-Object { Stop-Process -Id $_.OwningProcess }
param([int]$Port = 8765, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# the install page needs dist/install/ (node make_install.js); the converter works without it
if (!(Test-Path (Join-Path $root 'dist\install\manifest.json'))) {
    Write-Warning "No dist\install\manifest.json: run 'node make_install.js' after the firmware build, or the install page has nothing to install."
}

# the server, unless one already answers on the port
if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
    Write-Host "A server already listens on port $Port, using it"
} else {
    Start-Process python -ArgumentList "-m http.server $Port --directory `"$root`"" -WindowStyle Hidden
    for ($i = 0; $i -lt 20 -and !(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
    if (!(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue)) { throw "The server did not start on port $Port" }
    Write-Host "Started the server on port $Port"
}

# localhost: Web MIDI needs a secure origin, and localhost counts as one
$pages = "http://localhost:$Port/index.html", "http://localhost:$Port/install.html"
$pages | ForEach-Object { Write-Host $_ }
if (!$NoBrowser) { $pages | ForEach-Object { Start-Process $_ } }
Write-Host "http://localhost:$Port/song.html   (a MIDI file to the FM-1's MIDI file player, RUNNING.md section 3c)"

# from another computer on the network: the server answers on every address (python -m http.server), but Chrome gives Web
# MIDI only to a secure origin, which http://<this address> is not. On that computer, once: chrome://flags, "Insecure origins
# treated as secure", add the address below, Relaunch. (Or fetch the .fwsc from /dist/ and flash it with M-VAVE's workbench.)
$lan = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notmatch '^(127|169\.254)\.' -and $_.InterfaceAlias -notmatch '^vEthernet' -and $_.PrefixOrigin -ne 'WellKnown' }
foreach ($a in $lan) { Write-Host "From the network ($($a.InterfaceAlias)): http://$($a.IPAddress):$Port/install.html  (Chrome flag needed, see start_web.ps1)" }
