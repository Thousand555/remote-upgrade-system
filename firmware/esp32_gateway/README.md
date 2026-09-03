# ESP32 Gateway

This ESP-IDF 5.5.4 project targets the ESP32-S3-WROOM-1-N16R8 module. M7 makes
the ESP32 a local UART upgrade host: it validates an STM32 package stored in
the `stm_fw` partition, discovers the STM32 APP/Bootloader, transfers the image,
verifies it, activates it, and probes the restarted APP.

M9 adds Wi-Fi/HTTP download and resumable cache writes. M10 defaults network
downloads to HTTPS, validates the server certificate against an embedded
development CA, synchronizes wall-clock time through SNTP before certificate
validation, and keeps the M9 SHA-256/CRC32/valid-marker checks.

M11 waits for the STM32 APP to change `PENDING_BOOT` to `CONFIRMED` before an
upgrade can succeed. Repeated unconfirmed boots end in Bootloader recovery mode;
the cached package can then be applied again with another explicit
`upgrade start`.

No upgrade starts automatically. The destructive STM32 operations require an
explicit `upgrade start` console command.

## Hardware connection

The development-board pinout exposes GPIO17 and GPIO18 as free pins. M7 uses
UART1 with these defaults:

```text
ESP32 GPIO17 (TX) -> STM32 PA10 / USART1 RX
ESP32 GPIO18 (RX) <- STM32 PA9  / USART1 TX
ESP32 GND          - STM32 GND
```

Use 3.3 V TTL, 115200 baud, 8N1, and no flow control. Do not connect a USB-TTL
TX and ESP32 GPIO17 to PA10 at the same time.

## Build and flash the gateway

Run in a normal PowerShell terminal:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location .\firmware\esp32_gateway

idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash
```

Replace `COMx` with the ESP32 board port.

## Run the M10 HTTPS development server

Generate a development CA and a server certificate for the current WLAN IPv4,
then start the existing firmware API with TLS:

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
.\tools\setup_m10_tls.ps1 -ServerIp 192.168.31.170
.\tools\run_m10_https.ps1
```

The repository already contains the public development CA for `192.168.31.170`.
Skip certificate generation while that address and CA remain unchanged.
Private keys stay under the Git-ignored `server/certs/` directory. The public
CA is embedded into the ESP32 image, so rotating the CA requires rebuilding and
flashing the gateway. See `docs/m10_verification.md` for the complete board
procedure and use an HTTPS URL in `wifi configure`. The download task enters
`WAIT_TIME` after Wi-Fi connects and fails closed if SNTP cannot provide a valid
clock within the configured timeout.

## Build and load the local STM32 package

From the repository root, package the raw STM32 APP binary. The package version
must match `UPGRADE_APPLICATION_VERSION` compiled into that APP.

```powershell
py -3 .\tools\firmware_package.py `
  --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --output .\build\stm32_m7_package.bin `
  --version 3
```

Then load the package into the named custom partition:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location .\firmware\esp32_gateway

parttool.py -p COMx write_partition `
  --partition-name stm_fw `
  --input ..\..\build\stm32_m7_package.bin
```

The package has a 128-byte manifest, an erased padding area, and the raw image
at partition offset `0x1000`. The gateway validates the manifest and complete
image CRC32 before contacting the STM32.

## Run an upgrade from the console

Open the monitor:

```powershell
idf.py -p COMx monitor
```

Available commands:

```text
firmware info
firmware validate
upgrade probe
upgrade start
upgrade status
upgrade abort
help
```

Recommended first sequence:

```text
firmware validate
firmware info
upgrade probe
upgrade start
upgrade status
```

Exit the monitor with `Ctrl+]`. A complete run passes through `DISCOVER`,
`ENTER_BOOT`, `GET_INFO`, `START`, `ERASE`, `TRANSFER`, `VERIFY`, `ACTIVATE`,
`WAIT_APP`, waits for remote boot state 7, and finally reaches `SUCCESS`.

## Reliability test build

`CONFIG_GATEWAY_RELIABILITY_TEST` is disabled by default. The separate
`sdkconfig.reliability.defaults` enables one-shot `test fault` commands for
destructive board testing; do not use that build in production. Build it in a
separate directory so the normal `build` output remains production-safe:

```powershell
idf.py `
  -B ..\..\build\esp32_gateway_reliability `
  -D SDKCONFIG=..\..\build\esp32_gateway_reliability\sdkconfig `
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.reliability.defaults' `
  build
```

Close the ESP-IDF monitor before automation, then run R10-R15 from the repository
root (replace `COM_ESP` with the ESP32 console port):

```powershell
py -3 .\tools\run_m7_reliability.py `
  --port COM_ESP `
  --version 1 `
  --cases R10,R11,R12,R13,R14,R15 `
  --confirm-destructive
```

The tool saves the raw console log, JSON evidence, and a Markdown analysis under
`build/`. See `docs/m7_reliability_verification.md` for the complete board-test
order, acceptance criteria, physical-reset tests, and the automated 10-cycle tool.
