$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $repositoryRoot "protocol\tests\run_tests.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "M5 C protocol tests failed with exit code $LASTEXITCODE"
}

$launcher = Get-Command py.exe -ErrorAction SilentlyContinue
if ($null -ne $launcher) {
    $python = $launcher.Source
    $pythonPrefix = @('-3')
} else {
    $interpreter = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $interpreter) {
        throw "Python 3 was not found in PATH"
    }
    $python = $interpreter.Source
    $pythonPrefix = @()
}

& $python @pythonPrefix -m unittest discover -s $PSScriptRoot -p "test_*.py" -v

if ($LASTEXITCODE -ne 0) {
    throw "M6 Python protocol tests failed with exit code $LASTEXITCODE"
}

$serverTests = Join-Path $repositoryRoot "server\tests"
if (Test-Path $serverTests) {
    & $python @pythonPrefix -m unittest discover -s $serverTests -p "test_*.py" -v

    if ($LASTEXITCODE -ne 0) {
        throw "M8 server tests failed with exit code $LASTEXITCODE"
    }
}
