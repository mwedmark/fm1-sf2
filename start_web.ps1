# Starts the web server for the SF2 converter (index.html) and the install page (install.html) and opens both.
# Both pages are served by the same server, from this folder. See RUNNING.md sections 3 and 3b.
#   .\start_web.ps1              port 8765, opens both pages
#   .\start_web.ps1 -NoBrowser   only starts the server
#   .\start_web.ps1 -Lan         also an HTTPS server on port+1 (self-signed certificate), to install from another computer
# Stop it by its port: Get-NetTCPConnection -State Listen -LocalPort 8765 | ForEach-Object { Stop-Process -Id $_.OwningProcess }
param([int]$Port = 8765, [switch]$NoBrowser, [switch]$Lan)
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

# from another computer on the network. http://<this address> is not a secure origin, so Chrome gives that page no Web MIDI: -Lan
# also serves this folder over HTTPS on port+1 with a self-signed certificate (test_emu\lan_cert.pem). On that computer, accept
# the certificate warning once ("Advanced", "Continue to ..."). No Chrome flag is needed. (Or fetch the .fwsc from /dist/ and flash
# it with M-VAVE's workbench.)
if ($Lan) {
    . (Join-Path $root 'test_emu\lan_cert.ps1')
    $lanCert = Get-LanCertificate $root
    $tls = $Port + 1
    if ($lanCert.Renewed) {                                   # a new certificate: a server with the old one stops
        Get-NetTCPConnection -State Listen -LocalPort $tls -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force }
        Start-Sleep -Milliseconds 500
    }
    if (!(Get-NetTCPConnection -State Listen -LocalPort $tls -ErrorAction SilentlyContinue)) {
        Start-Process python -ArgumentList "`"$(Join-Path $root 'test_emu\cors_server.py')`" $tls `"$root`" :: `"$($lanCert.Pem)`"" -WindowStyle Hidden
        for ($i = 0; $i -lt 20 -and !(Get-NetTCPConnection -State Listen -LocalPort $tls -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
    }
    foreach ($a in $lanCert.Ips) {
        Write-Host "From the network ($($a.InterfaceAlias)): https://$($a.IPAddress):$tls/install.html   https://$($a.IPAddress):$tls/song.html   (accept the certificate warning once)"
    }
} else {
    Write-Host "From another computer: .\start_web.ps1 -Lan (HTTPS; plain http://<address>:$Port gives no Web MIDI)"
}
