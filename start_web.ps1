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
