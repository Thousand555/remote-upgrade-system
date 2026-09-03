[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ServerIp,

    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{0,126}[A-Za-z0-9]$')]
    [string]$ServerDnsName = 'm10.local',

    [ValidateRange(1, 3650)]
    [int]$ServerCertificateDays = 825,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$parsedAddress = $null
if ((-not [System.Net.IPAddress]::TryParse($ServerIp, [ref]$parsedAddress)) -or
    ($parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork)) {
    throw "ServerIp must be an IPv4 address"
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$certificateDirectory = Join-Path $repositoryRoot 'server\certs'
$embeddedCertificateDirectory = Join-Path $repositoryRoot 'firmware\esp32_gateway\components\firmware_downloader\certs'
$caKey = Join-Path $certificateDirectory 'm10_dev_ca.key'
$caCertificate = Join-Path $certificateDirectory 'm10_dev_ca.pem'
$caSerial = Join-Path $certificateDirectory 'm10_dev_ca.srl'
$serverKey = Join-Path $certificateDirectory 'm10_server.key'
$serverRequest = Join-Path $certificateDirectory 'm10_server.csr'
$serverCertificate = Join-Path $certificateDirectory 'm10_server.pem'
$serverExtensions = Join-Path $certificateDirectory 'm10_server.ext'
$embeddedCa = Join-Path $embeddedCertificateDirectory 'm10_ca.pem'

$openssl = Get-Command openssl.exe -ErrorAction SilentlyContinue
if ($null -eq $openssl) {
    throw 'openssl.exe was not found. Install OpenSSL or Git for Windows and add it to PATH.'
}

$generatedFiles = @($caKey, $caCertificate, $caSerial, $serverKey,
                    $serverRequest, $serverCertificate, $serverExtensions)
if ((-not $Force) -and ($generatedFiles | Where-Object { Test-Path -LiteralPath $_ })) {
    throw 'M10 TLS material already exists. Use -Force only when intentionally rotating the development CA and server certificate.'
}

New-Item -ItemType Directory -Force -Path $certificateDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $embeddedCertificateDirectory | Out-Null

function Invoke-OpenSsl {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    & $openssl.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OpenSSL failed with exit code $LASTEXITCODE"
    }
}

Invoke-OpenSsl @(
    'req', '-x509', '-new', '-nodes', '-newkey', 'rsa:2048', '-sha256',
    '-days', '3650', '-keyout', $caKey, '-out', $caCertificate,
    '-subj', '/CN=Remote Upgrade M10 Development CA',
    '-addext', 'basicConstraints=critical,CA:TRUE',
    '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
    '-addext', 'subjectKeyIdentifier=hash'
)

Invoke-OpenSsl @(
    'req', '-new', '-nodes', '-newkey', 'rsa:2048', '-sha256',
    '-keyout', $serverKey, '-out', $serverRequest,
    '-subj', "/CN=$ServerDnsName"
)

$extensionText = @"
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=IP:$ServerIp,DNS:$ServerDnsName,DNS:localhost
"@
[System.IO.File]::WriteAllText($serverExtensions, $extensionText,
                               [System.Text.UTF8Encoding]::new($false))

Invoke-OpenSsl @(
    'x509', '-req', '-in', $serverRequest, '-CA', $caCertificate,
    '-CAkey', $caKey, '-CAcreateserial', '-CAserial', $caSerial,
    '-out', $serverCertificate, '-days', $ServerCertificateDays.ToString(),
    '-sha256', '-extfile', $serverExtensions
)

Invoke-OpenSsl @('verify', '-CAfile', $caCertificate, $serverCertificate)
Copy-Item -LiteralPath $caCertificate -Destination $embeddedCa -Force

Write-Output 'M10 development TLS material is ready.'
Write-Output "Server certificate: $serverCertificate"
Write-Output "Server private key: $serverKey"
Write-Output "ESP32 embedded CA: $embeddedCa"
Write-Output "Configure the gateway URL as: https://${ServerIp}:8443"
Write-Warning 'The private keys under server\certs are development-only and are ignored by Git.'
