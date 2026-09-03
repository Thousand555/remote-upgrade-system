[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ServerIp,

    [string]$ListenAddress = '0.0.0.0',

    [ValidateRange(1, 65535)]
    [int]$Port = 8444
)

$ErrorActionPreference = 'Stop'

$parsedAddress = $null
if ((-not [System.Net.IPAddress]::TryParse($ServerIp, [ref]$parsedAddress)) -or
    ($parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork)) {
    throw 'ServerIp must be an IPv4 address.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = Join-Path $repositoryRoot 'server'
$certificateDirectory = Join-Path $repositoryRoot 'build\m10_untrusted_tls'
$caKey = Join-Path $certificateDirectory 'untrusted_ca.key'
$caCertificate = Join-Path $certificateDirectory 'untrusted_ca.pem'
$caSerial = Join-Path $certificateDirectory 'untrusted_ca.srl'
$serverKey = Join-Path $certificateDirectory 'untrusted_server.key'
$serverRequest = Join-Path $certificateDirectory 'untrusted_server.csr'
$serverCertificate = Join-Path $certificateDirectory 'untrusted_server.pem'
$serverExtensions = Join-Path $certificateDirectory 'untrusted_server.ext'

$openssl = Get-Command openssl.exe -ErrorAction SilentlyContinue
if ($null -eq $openssl) {
    throw 'openssl.exe was not found. Install OpenSSL or Git for Windows and add it to PATH.'
}

function Invoke-OpenSsl {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    & $openssl.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OpenSSL failed with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Force -Path $certificateDirectory | Out-Null

Invoke-OpenSsl @(
    'req', '-x509', '-new', '-nodes', '-newkey', 'rsa:2048', '-sha256',
    '-days', '30', '-keyout', $caKey, '-out', $caCertificate,
    '-subj', '/CN=Remote Upgrade M10 Deliberately Untrusted CA',
    '-addext', 'basicConstraints=critical,CA:TRUE',
    '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
    '-addext', 'subjectKeyIdentifier=hash'
)

Invoke-OpenSsl @(
    'req', '-new', '-nodes', '-newkey', 'rsa:2048', '-sha256',
    '-keyout', $serverKey, '-out', $serverRequest,
    '-subj', '/CN=m10-untrusted.local'
)

$extensionText = @"
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=IP:$ServerIp,DNS:m10-untrusted.local,DNS:localhost
"@
[System.IO.File]::WriteAllText($serverExtensions, $extensionText,
                               [System.Text.UTF8Encoding]::new($false))

Invoke-OpenSsl @(
    'x509', '-req', '-in', $serverRequest, '-CA', $caCertificate,
    '-CAkey', $caKey, '-CAcreateserial', '-CAserial', $caSerial,
    '-out', $serverCertificate, '-days', '30', '-sha256',
    '-extfile', $serverExtensions
)
Invoke-OpenSsl @('verify', '-CAfile', $caCertificate, $serverCertificate)

$launcher = Get-Command py.exe -ErrorAction SilentlyContinue
if ($null -ne $launcher) {
    $python = $launcher.Source
    $pythonPrefix = @('-3')
} else {
    $interpreter = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $interpreter) {
        throw 'Python 3 was not found in PATH.'
    }
    $python = $interpreter.Source
    $pythonPrefix = @()
}

Write-Warning 'Starting a deliberately untrusted M10 endpoint. This CA is not embedded in the ESP32.'
Write-Output "Test URL: https://${ServerIp}:$Port"
Write-Output 'Expected result: ESP32 TLS verification fails before the valid firmware cache is erased.'

Push-Location $serverRoot
try {
    & $python @pythonPrefix -m uvicorn app.main:app `
        --host $ListenAddress `
        --port $Port `
        --ssl-certfile $serverCertificate `
        --ssl-keyfile $serverKey `
        --access-log
} finally {
    Pop-Location
}
