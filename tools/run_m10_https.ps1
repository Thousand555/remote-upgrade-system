[CmdletBinding()]
param(
    [string]$ListenAddress = '0.0.0.0',

    [ValidateRange(1, 65535)]
    [int]$Port = 8443
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = Join-Path $repositoryRoot 'server'
$certificate = Join-Path $serverRoot 'certs\m10_server.pem'
$privateKey = Join-Path $serverRoot 'certs\m10_server.key'

if ((-not (Test-Path -LiteralPath $certificate)) -or
    (-not (Test-Path -LiteralPath $privateKey))) {
    throw 'M10 server certificate is missing. Run tools\setup_m10_tls.ps1 first.'
}

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

Push-Location $serverRoot
try {
    & $python @pythonPrefix -m uvicorn app.main:app `
        --host $ListenAddress `
        --port $Port `
        --ssl-certfile $certificate `
        --ssl-keyfile $privateKey `
        --access-log
} finally {
    Pop-Location
}
