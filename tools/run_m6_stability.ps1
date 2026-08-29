param(
    [string]$Port = "COM3",
    [string]$FirmwareFile,
    [ValidateRange(1, 100)]
    [int]$Cycles = 10,
    [ValidateRange(1, 4294967295)]
    [uint32]$Version = 1,
    [switch]$ConfirmDestructive
)

$ErrorActionPreference = "Stop"

if (-not $ConfirmDestructive) {
    throw "This test repeatedly erases the APP partition. Re-run with -ConfirmDestructive."
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$upgradeTool = Join-Path $PSScriptRoot "serial_upgrade.py"

if ([string]::IsNullOrWhiteSpace($FirmwareFile)) {
    $FirmwareFile = Join-Path $repositoryRoot `
        "firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin"
}

$resolvedFirmware = (Resolve-Path -LiteralPath $FirmwareFile).Path
$pythonLauncher = Get-Command py.exe -ErrorAction Stop
$startedAt = Get-Date
$passed = 0

Write-Host "M6 stability test"
Write-Host "  Port     : $Port"
Write-Host "  Firmware : $resolvedFirmware"
Write-Host "  Version  : $Version"
Write-Host "  Cycles   : $Cycles"
Write-Host "  WARNING  : every cycle erases and rewrites the APP partition"

for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
    Write-Host ""
    Write-Host "========== M6 cycle $cycle/$Cycles =========="

    & $pythonLauncher.Source -3 $upgradeTool `
        --port $Port `
        --file $resolvedFirmware `
        --version $Version

    $upgradeExitCode = $LASTEXITCODE
    if ($upgradeExitCode -ne 0) {
        throw "Cycle $cycle upgrade failed with exit code $upgradeExitCode"
    }

    # ACTIVATE resets the target. Give the APP enough time to initialize its
    # USART service before the non-destructive post-upgrade probe.
    Start-Sleep -Milliseconds 1000

    & $pythonLauncher.Source -3 $upgradeTool `
        --port $Port `
        --probe-only

    $probeExitCode = $LASTEXITCODE
    if ($probeExitCode -ne 0) {
        throw "Cycle $cycle APP probe failed with exit code $probeExitCode"
    }

    $passed++
    Write-Host "[PASS] cycle $cycle/$Cycles"
}

$elapsed = (Get-Date) - $startedAt
Write-Host ""
Write-Host "[PASS] M6 stability test: $passed/$Cycles cycles"
Write-Host ("Elapsed: {0:hh\:mm\:ss}" -f $elapsed)
