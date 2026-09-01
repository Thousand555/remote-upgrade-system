# M9 ESP32 网络下载：实现状态

状态：`PHASE COMPLETE / DEFERRED TESTS`。Wi-Fi、HTTP正常下载、完整包安全提交、`firmware validate`、ESP32复位后HTTP `206`续传、用户取消后半包拦截及续传，以及正式基线到STM32的实际擦写回归已在目标ESP32-S3板上通过。ETag变化和错误Manifest身份拦截未执行，明确保留为`DEFERRED`，不阻塞进入后续开发阶段。

## 已实现范围

- 新增 `gateway_wifi`：通过串口命令配置 Wi-Fi STA 与 M8 地址并保存到 NVS，重启自动恢复；不在 `sdkconfig.defaults` 中保存 SSID、密码或服务器地址。
- 新增 `firmware_downloader`：获取 M8 Manifest，预检设备身份、长度、格式、SHA-256、CRC32、下载路径和 ETag。
- 从 M8 `/binary` 端点以 4 KiB 块流式写入 `stm_fw`；固件文件不会完整驻留在 RAM。
- NVS 以 64 KiB 为间隔保存固件 ID、包长度、包 SHA-256、ETag 和已接收长度。复位后同一发布物使用 `Range` 与 `If-Range` 恢复；服务器回退为 `200` 时安全地从零重下。
- 下载时强制屏蔽包头 `valid_marker`。只有整包从 Flash 重读 SHA-256、包头字段和镜像 CRC 全部通过后才写入标记，因此中断下载不可被 M7 当作本地可升级包。
- 2026-09-01板测发现首个完整下载在暂存包SHA-256校验时返回`ESP_ERR_INVALID_CRC`。根因是Flash中被延迟提交的`valid_marker`为`0xFFFFFFFF`，与M8原始包中的`0xA5C3F00D`不同。现在仅在重读哈希的RAM块中恢复预期标记，Flash标记仍保持无效，直到整包SHA-256、包头和镜像CRC全部通过。
- 新增控制台命令，并禁止其与 UART 升级并行占用缓存：

```text
firmware download <firmware_id>
firmware download status
firmware download cancel
```

下载成功只使 `firmware validate` 可用；仍须人工执行既有的 `upgrade start`，M9 不会自动操作 STM32。

## 如何进入ESP32命令行

本项目的`wifi`、`firmware`和`upgrade`都是**ESP32串口控制台命令**，不是Windows
PowerShell命令。必须先连接ESP32命令行，看到`gateway>`提示符后再输入。

当前硬件和端口用途如下：

| 端口/链路 | 用途 | 能否输入`wifi`命令 |
| --- | --- | --- |
| `COM5`，ESP32 UART0，115200 8N1 | ESP32日志和`gateway>`交互控制台 | 可以 |
| `COM3` | STM32 USB-TTL调试端口 | 不可以 |
| ESP32 UART1，GPIO17/18 | ESP32与STM32的二进制升级链路 | 不可以 |

### 方法A：ESP-IDF Monitor（推荐）

1. 用ESP32开发板的调试/USB-UART接口连接PC。
2. 在新的PowerShell窗口进入工程并激活ESP-IDF：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0\firmware\esp32_gateway
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
idf.py -p COM5 monitor
```

如果未烧录最新固件，可将最后一条换为：

```powershell
idf.py -p COM5 flash monitor
```

如果已经处于Monitor中，必须先按`Ctrl+]`退出以释放`COM5`，再执行烧录命令。
本次M9最新镜像为`build/esp32_gateway.bin`，大小968,896字节，SHA-256为
`5F3038B8F85EEA2E551D6130A60B1C28DA9DD378BEC29A7B4AFF1DD992E0D723`。

3. 启动日志停止滚动后按一次`Enter`。正常情况会看到：

```text
gateway>
```

4. 在`gateway>`后输入`help`，按`Enter`：

```text
gateway> help
```

能看到`wifi`、`firmware`和`upgrade`命令即表示已进入正确终端。输入实际命令时，
不要把提示符`gateway>`一起复制。

5. 按`Ctrl+]`退出ESP-IDF Monitor。

### 方法B：其他串口工具

也可用PuTTY、MobaXterm或VS Code Serial Monitor打开`COM5`，参数为`115200`、
8数据位、1停止位、无校验、无流控。一次只能有一个程序占用`COM5`。

如果没有看到`gateway>`：

- 先关闭其他可能占用`COM5`的串口程序。
- 确认选的是`COM5`而不是`COM3`。
- 按一次开发板`EN/RESET`，等启动日志结束后再按`Enter`。
- 确认已烧录M9固件；旧版本不包含`wifi configure`命令。

如果执行`wifi status`直接返回`Unrecognized command`，说明串口和提示符正常，但板上
仍是未注册`wifi`命令的旧固件。执行`help`可进一步确认；如果列表中没有`wifi`，
退出Monitor并执行`idf.py -p COM5 flash monitor`。重刷成功后启动日志应包含：

```text
ESP32 gateway M9 initialization starting
Console ready: wifi <status|configure|clear>, ...
```

## 本地配置

2026-09-01通过`ipconfig`检测到当前PC WLAN地址为`192.168.31.170/24`，默认网关为
`192.168.31.1`。`192.168.192.1`和`192.168.245.1`是VMware虚拟网卡地址，不用于ESP32。
网络变化后应重新运行`ipconfig`确认地址。

先保持PC上的M8服务窗口继续运行，再在另一个ESP32串口窗口中，看到
`gateway>`后逐条执行：

```text
gateway> wifi status
gateway> wifi configure "<SSID>" "<PASSWORD>" http://192.168.31.170:8000
gateway> wifi status
```

上面的`gateway>`只用于表示输入位置，不是命令的一部分。将`<SSID>`和`<PASSWORD>`
替换为真实值，不要保留尖括号。例如：

```text
gateway> wifi configure "MyHomeWiFi" "MyPassword123" http://192.168.31.170:8000
```

开放网络用`-`代替密码：

```text
gateway> wifi configure "<SSID>" - http://192.168.31.170:8000
```

可用`wifi clear`清除NVS配置。`menuconfig`中的三项网络参数仅作为没有NVS配置时的可选回退，
正常板测不再需要为换网络而重新编译固件。

不要填写 `127.0.0.1`：对 ESP32 而言它是板子自身。PC 上的 M8 服务须监听局域网地址，Windows 防火墙仅放行受控局域网的 TCP 8000。

## 板上验收结论

1. 正常发布物`f407-node-1.2.0`已板测通过：日志显示完成，`firmware validate`返回Version=2、Size=12892、CRC32=`CF885C9E`和Product/Hardware=`0x0001/0x0001`。
2. 大包复位续传和用户取消续传已通过；正式基线经M9下载后执行M7 UART升级，最终12892/12892字节、`SUCCESS/ESP_OK/0`且APP版本2回探成功。
3. ETag变化与错误身份Manifest拦截保留为`DEFERRED`。完整步骤和板测证据见[`m9_remaining_verification.md`](m9_remaining_verification.md)。

当前验证证据：2026-09-01，工具回归33/33、M8/M9服务器回归10/10通过；ESP-IDF v5.5.4组件编译、ELF链接、二进制生成和分区大小检查通过。应用镜像`0xEC8C0`，相对`0x200000` factory分区余量`0x113740`。

阶段归档总览和当前文件哈希分别见[`m9_archive_summary.md`](m9_archive_summary.md)与
[`m9_archive_manifest.md`](m9_archive_manifest.md)。
