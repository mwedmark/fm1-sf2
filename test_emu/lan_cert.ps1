# Makes (or reuses) a self-signed certificate for this computer's network addresses and returns the PEM file (certificate and key)
# and the addresses: test_emu\lan_cert.pem. Used by start_emu.ps1 and start_web.ps1 for -Lan (HTTPS: Chrome gives AudioWorklet and
# Web MIDI to a secure origin only; the browser asks once to accept this certificate). Dot-source it: . test_emu\lan_cert.ps1
function Get-LanCertificate([string]$Root) {
    $ips = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notmatch '^(127|169\.254)\.' -and $_.InterfaceAlias -notmatch '^vEthernet' -and $_.PrefixOrigin -ne 'WellKnown' })
    $pem = Join-Path $Root 'test_emu\lan_cert.pem'
    $ipFile = "$pem.ips"
    $want = ($ips.IPAddress | Sort-Object) -join ','
    if (!(Test-Path $pem) -or !(Test-Path $ipFile) -or (Get-Content $ipFile -Raw).Trim() -ne $want) {
        $rsa = [System.Security.Cryptography.RSA]::Create(2048)
        $req = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=fm1-emu', $rsa, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
        $san = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
        $san.AddDnsName('localhost'); $san.AddIpAddress([System.Net.IPAddress]::Loopback)
        $ips | ForEach-Object { $san.AddIpAddress([System.Net.IPAddress]::Parse($_.IPAddress)) }
        $req.CertificateExtensions.Add($san.Build())
        $cert = $req.CreateSelfSigned([DateTimeOffset]::UtcNow.AddDays(-1), [DateTimeOffset]::UtcNow.AddYears(1))
        Set-Content $pem ($cert.ExportCertificatePem() + "`n" + $rsa.ExportPkcs8PrivateKeyPem()) -NoNewline
        Set-Content $ipFile $want
        $renewed = $true
    }
    [pscustomobject]@{ Pem = $pem; Ips = $ips; Renewed = [bool]$renewed }
}
