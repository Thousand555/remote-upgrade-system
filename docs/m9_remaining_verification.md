# M9 剩余板上验收

## 目标

正常16,988字节发布物已通过Wi-Fi、Manifest获取、完整下载、暂存包重读、
安全提交和`firmware validate`。本文只验证M9剩余的异常路径：

1. 64 KiB检查点后ESP32复位，HTTP `Range`/`If-Range`返回`206`并续传。
2. 用户取消后包仍无效，再次下载从检查点续传。
3. 同一Firmware ID的ETag/包发生变化后，旧检查点失效并从0重下。
4. 错误产品ID的Manifest在擦除缓存前被拒绝。
5. 恢复正式M8发布物，显式执行一次M7 UART升级回归。

## 测试能力

M8服务新增仅测试时使用的`M8_TEST_STREAM_DELAY_MS`。默认为0，生产服务不限速；
板测设为1000后，每发送64 KiB暂停1秒，便于在NVS检查点后复位或取消。

`tools/pack_firmware.py --pad-to`使用`0xFF`扩展STM32镜像，不改变向量表和原始程序字节。
测试发布物均在被Git忽略的`build` 目录，不进入正式`server/firmware`。

| 目录 | Firmware ID | Package | Image CRC32 | Package SHA-256 |
| --- | --- | ---: | --- | --- |
| `build/m9_test_firmware` | `f407-node-m9-resume` | 921600 | `1AEDDB50` | `8D7E2568...FCFD9F8` |
| `build/m9_test_firmware_variant_b` | `f407-node-m9-resume` | 856064 | `5BBE5848` | `087A9EEB...FB6C1DE` |
| `build/m9_test_invalid_identity` | `f407-node-m9-wrong-product` | 16988 | `CF885C9E` | `D7CED5CF...F546D4B` |

`build`目录被清理后，可在仓库根目录用以下命令重建三个发布物：

```powershell
py -3 .\tools\pack_firmware.py --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin --firmware-id f407-node-m9-resume --version 2 --display-version 1.2.0-m9-resume-a --pad-to 0xE0000 --output .\build\m9_test_firmware\f407-node-m9-resume --force
py -3 .\tools\pack_firmware.py --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin --firmware-id f407-node-m9-resume --version 2 --display-version 1.2.0-m9-resume-b --pad-to 0xD0000 --output .\build\m9_test_firmware_variant_b\f407-node-m9-resume --force
py -3 .\tools\pack_firmware.py --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin --firmware-id f407-node-m9-wrong-product --version 2 --display-version 1.2.0-m9-invalid --product-id 2 --output .\build\m9_test_invalid_identity\f407-node-m9-wrong-product --force
```

## 0. 前置条件

1. 退出当前Monitor，烧录最新ESP32固件：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0\firmware\esp32_gateway
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
idf.py -p COM5 flash monitor
```

2. 保持ESP32和PC连接同一个局域网，`wifi status`应为`connected`，服务器地址为
   `http://192.168.31.170:8000`。
3. M8服务窗口和ESP32 `COM5` Monitor必须使用两个独立终端。

## 1. 复位后206续传

先停止正式M8服务。在PC服务端终端执行：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
$m9FirmwareRoot = (Resolve-Path .\build\m9_test_firmware).Path
$env:FIRMWARE_ROOT = $m9FirmwareRoot
$env:M8_TEST_STREAM_DELAY_MS = '1000'
Set-Location .\server
py -3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --access-log
```

在ESP32终端执行：

```text
firmware download f407-node-m9-resume
```

看到类似以下日志后立即按开发板`EN/RESET`：

```text
Downloaded 65536/921600 bytes
```

如果已到`131072`或更高的64 KiB倍数也可以，恢复应以最后已持久化的数值为准。
重启后先确认半包不可用：

```text
firmware validate
```

应返回校验失败。然后执行：

```text
firmware download f407-node-m9-resume
```

ESP32关键日志：

```text
Requesting HTTP resume at byte 65536 with If-Range "..."
HTTP resume accepted: bytes 65536-921599/921600
M9 package download completed
```

M8 access log中同一个`/binary`请求应为`206 Partial Content`。完成后：

```text
firmware download status
firmware validate
```

预期`READY`、`921600/921600`、`ESP_OK`，且Image Size为`917504`、CRC32为`1AEDDB50`。

## 2. 用户取消与续传

保持发布物A和1秒限速，再次执行：

```text
firmware download f407-node-m9-resume
```

看到首个`Downloaded 65536/...`后执行：

```text
firmware download cancel
firmware download status
firmware validate
```

预期状态为`CANCELED`、`Resume checkpoint: available`，且`firmware validate`失败。
重新执行同一下载命令，应再次看到`Requesting HTTP resume`、`206`和最终`READY`。

## 3. ETag变化后从0重下

先在发布物A上再制造一个取消检查点，然后用`Ctrl+C`停止M8服务。
在PC服务端终端切换到同ID但不同ETag的发布物B：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
$m9FirmwareRoot = (Resolve-Path .\build\m9_test_firmware_variant_b).Path
$env:FIRMWARE_ROOT = $m9FirmwareRoot
$env:M8_TEST_STREAM_DELAY_MS = '1000'
Set-Location .\server
py -3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --access-log
```

在ESP32上执行：

```text
firmware download f407-node-m9-resume
```

预期日志：

```text
Saved checkpoint does not match the current manifest; restarting from byte 0
```

此次`/binary`应为HTTP `200`，不得使用旧`Range`偏移。最终`firmware validate`应显示
Image Size=`851968`、CRC32=`5BBE5848`。

## 4. 错误设备身份拦截

停止服务器B，启动错误产品ID目录：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
$m9FirmwareRoot = (Resolve-Path .\build\m9_test_invalid_identity).Path
$env:FIRMWARE_ROOT = $m9FirmwareRoot
$env:M8_TEST_STREAM_DELAY_MS = '0'
Set-Location .\server
py -3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --access-log
```

在ESP32执行：

```text
firmware download f407-node-m9-wrong-product
firmware download status
firmware validate
```

下载应为`FAILED / ESP_ERR_INVALID_RESPONSE`。随后`firmware validate`必须仍能验证上一个
合法的B包，证明错误Manifest在`stm_fw`擦除前已被拦截。

## 5. 恢复基线与STM32链路回归

停止测试服务，恢复默认发布目录：

```powershell
Remove-Item Env:FIRMWARE_ROOT -ErrorAction SilentlyContinue
Remove-Item Env:M8_TEST_STREAM_DELAY_MS -ErrorAction SilentlyContinue
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0\server
py -3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --access-log
```

在ESP32恢复正式基线：

```text
firmware download f407-node-1.2.0
firmware download status
firmware validate
upgrade probe
```

`firmware validate`应恢复为Version=2、Size=12892、CRC32=`CF885C9E`。

以下命令会真实擦写STM32 APP。只有在ESP32 GPIO17/18与STM32 PA10/PA9及共地连接正确、
`upgrade probe`成功后才执行：

```text
upgrade start
upgrade status
```

重复`upgrade status`直到`SUCCESS`，最后再执行`upgrade probe`，确认STM32 APP版本码为2。

## 验收出口

| 项目 | 通过标准 | 当前结果 |
| --- | --- | --- |
| 正常下载 | `READY`且基线`firmware validate`与Manifest一致 | `PASS` |
| 复位续传 | 检查点偏移为64 KiB倍数，M8返回`206` | `PASS` |
| 取消 | `CANCELED`、标记无效、同发布物可续传 | `PASS` |
| ETag变化 | 不使用旧偏移，安全从0下载 | `DEFERRED` |
| Manifest身份 | 错误产品ID被拒绝，旧有效缓存保留 | `DEFERRED` |
| STM32回归 | `upgrade status=SUCCESS`，升级后APP探测版本码为2 | `PASS` |

## 板测记录

### R-M9-01 ESP32复位后HTTP 206续传

- 日期：2026-09-01。
- 结果：`PASS`。
- 发布物：`f407-node-m9-resume`，Package Size=921600，Package SHA-256=`8D7E2568...FCFD9F8`。
- 复位后`firmware validate`返回`ESP_ERR_INVALID_RESPONSE`，证明半包的`valid_marker`未被提交。
- NVS恢复偏移为655360字节，等于10个64 KiB检查点。
- ESP32发送`Range`/`If-Range`，M8接受恢复区间`bytes 655360-921599/921600`。
- 续传后依次持久化720896、786432、851968、917504和921600字节检查点。
- 最终日志为`M9 package download completed`，整包重读、标记提交和本地包校验成功。

### R-M9-02 用户取消、半包拦截与续传

- 日期：2026-09-01。
- 结果：`PASS`。
- 取消前缓存A已为`READY`，921600/921600字节、`ESP_OK`，且`firmware validate`返回Version=2、Image Size=917504、CRC32=`1AEDDB50`。
- 新下载进行到393216字节时执行Cancel，最终状态为`CANCELED`、`Resume checkpoint: available`、`ESP_ERR_INVALID_STATE`。
- Cancel后`firmware validate`返回`ESP_ERR_INVALID_RESPONSE`，证明暂存包的有效标记未提交。
- Cancel会在周期性64 KiB检查点之外，额外保存最后一次Flash写入成功后的精确`received_size`；本次的397312恰好等于97个4 KiB块，是已完整写入的安全恢复点。
- 重试发送`Range`/`If-Range`，M8接受`bytes 397312-921599/921600`，最终日志为`M9 package download completed`。

### R-M9-03 正式基线恢复与STM32无破坏探测

- 日期：2026-09-01。
- 结果：`PASS`（升级前置检查）。
- 正式发布物`f407-node-1.2.0`下载状态为`READY`、16988/16988字节、无续传检查点、`ESP_OK`。
- `firmware validate`返回Version=2、Image Size=12892、CRC32=`CF885C9E`、Product/Hardware=`0x0001/0x0001`，与M8 Manifest一致。
- `upgrade probe`返回APP服务`capabilities=0x0002`、version=`0x00000002`，设备身份为`0x0001/0x0001`，APP区域为`0x08020000`、最大长度为`0x000E0000`，满足基线包升级前置条件。
- `capabilities=0x0002`表示APP支持`ENTER_BOOT`；`boot_state=6`表示`PENDING_BOOT`。该状态与M7成功升级后的板测结果一致，是当前尚未实现APP启动确认/回滚闭环时的预期状态，相关闭环计划在M11完成。

### R-M9-04 正式基线到STM32端到端升级回归

- 日期：2026-09-01。
- 结果：`PASS`。
- 发布物：`f407-node-1.2.0`，Firmware Version=2，Image Size=12892。
- `upgrade status`最终为`SUCCESS`，Session=`0xD51A5E63`，进度12892/12892字节，远端状态6，`ESP_OK`且设备状态0。
- 升级后`upgrade probe`重新发现APP服务：`capabilities=0x0002`、version=`0x00000002`、Product/Hardware=`0x0001/0x0001`、Application=2。
- APP区域仍为`0x08020000`、最大长度为`0x000E0000`，证明M8发布物经M9下载缓存后可由既有M7链路完整写入、激活并回探。

## 阶段结论

M9核心链路阶段性通过：正式包下载与校验、下载中断安全性、ESP32复位续传、用户取消续传及STM32端到端升级均已有目标板证据。ETag变化和错误Manifest身份拦截未执行，状态保留为`DEFERRED`，不得写为`PASS`；它们不阻塞进入后续开发阶段，但在完整可靠性验收关闭前仍需补测。
