$ErrorActionPreference = "Stop"

$protocolRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $protocolRoot
$outputDirectory = Join-Path $repositoryRoot "build\m5_protocol_tests"
$testExecutable = Join-Path $outputDirectory "protocol_tests.exe"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$sources = @(
    (Join-Path $protocolRoot "src\crc16_modbus.c"),
    (Join-Path $protocolRoot "src\protocol_byte_order.c"),
    (Join-Path $protocolRoot "src\modbus_rtu.c"),
    (Join-Path $protocolRoot "src\modbus_rtu_stream.c"),
    (Join-Path $protocolRoot "src\upgrade_protocol.c"),
    (Join-Path $PSScriptRoot "test_support.c"),
    (Join-Path $PSScriptRoot "test_crc16_modbus.c"),
    (Join-Path $PSScriptRoot "test_modbus_rtu.c"),
    (Join-Path $PSScriptRoot "test_upgrade_protocol.c"),
    (Join-Path $PSScriptRoot "test_modbus_rtu_stream.c"),
    (Join-Path $PSScriptRoot "test_main.c")
)

& gcc.exe `
    -std=c99 `
    -Wall `
    -Wextra `
    -Werror `
    -Wpedantic `
    "-I$(Join-Path $protocolRoot 'include')" `
    "-I$PSScriptRoot" `
    @sources `
    -o $testExecutable

if ($LASTEXITCODE -ne 0) {
    throw "GCC protocol test build failed with exit code $LASTEXITCODE"
}

& $testExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Protocol tests failed with exit code $LASTEXITCODE"
}
