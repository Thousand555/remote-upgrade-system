# M7 ESP32 UART本地主机验证记录

> 本文包含M7最初主链路基线和后续状态。2026-09-01的权威归档结论、文件哈希和延期项
> 分别见[`m7_archive_summary.md`](m7_archive_summary.md)与
> [`m7_archive_manifest.md`](m7_archive_manifest.md)。

## 当前状态

- ESP-IDF工程、16 MiB Flash、8 MiB Octal PSRAM和自定义分区表已建立。
- VS Code工作区和命令行构建均固定为ESP-IDF v5.5.4。
- 升级UART使用UART1，ESP32 TX=GPIO17、RX=GPIO18；引脚来自当前开发板图片。
- ESP32侧本地包校验、UART事务、协议客户端、升级状态机和交互控制台已经实现。
- ESP32生产构建应用大小为`0x47460`；可靠性测试构建为`0x48250`。
- 当前Python固件包、协议和M7自动化回归共29项通过。
- STM32 APP已提升到版本2；当前`stm_fw`为917504字节镜像，CRC32=`0x1AEDDB50`。
- ESP32板上启动、STM32无破坏探测、完整UART升级主链路、R01～R06、R08及R10～R15
  均已通过；R07、R09和生产版恢复后基线暂缓。

## 板上主链路初始实测证据

- `upgrade probe`发现STM32 APP版本1，Product/Hardware均为`0x0001`，APP区域为
  `0x08020000/0x000E0000`。
- 实测状态依次经过`DISCOVER -> ENTER_BOOT -> GET_INFO -> START -> ERASE ->`
  `TRANSFER -> VERIFY -> ACTIVATE -> WAIT_APP -> SUCCESS`。
- Session为`0xE5869149`，12888字节传输完成，远端最终状态为
  `PENDING_BOOT(6)`，设备状态为`OK(0)`。
- 激活后静默1秒，ESP32成功重新发现STM32 APP版本1。
- 擦除期间两次`QUERY_PROGRESS(0x13)`单次超时随后自动恢复，未影响升级结果。

## M7代码范围

| 组件 | 当前实现 |
| --- | --- |
| `firmware_store` | 解析128字节Manifest，校验设备ID、长度、Header CRC32和整包CRC32，按偏移读取镜像 |
| `transport_uart` | UART1初始化、互斥访问、请求发送、RTU静默间隔收帧、总超时 |
| `upgrade_client` | HELLO、GET_INFO、ENTER_BOOT、START、ERASE、QUERY_PROGRESS、DATA、VERIFY、ACTIVATE、ABORT |
| `upgrade_manager` | 独立FreeRTOS任务、Session/Sequence、擦除轮询、4 KiB检查点、断点续传、激活后APP回探 |
| `gateway_console` | `firmware info/validate`和`upgrade probe/start/status/abort` |
| `firmware_package.py` | 将STM32 APP BIN封装为可写入`stm_fw`的本地升级包 |

M7不会自动升级。上电只初始化组件，只有控制台收到`upgrade start`才可能向STM32发送破坏性命令。

## 硬件连接

```text
ESP32 GPIO17 / UART1 TX -> STM32 PA10 / USART1 RX
ESP32 GPIO18 / UART1 RX <- STM32 PA9  / USART1 TX
ESP32 GND                - STM32 GND
```

- 电平：3.3 V TTL。
- 串口：115200、8N1、无流控。
- UART0保留给ESP32下载、日志和交互控制台。
- ESP32和USB-TTL不得同时驱动STM32 PA10。

## 已完成的软件验证

| 项目 | 结果 |
| --- | --- |
| ESP-IDF | v5.5.4 |
| Target | esp32s3 |
| Flash配置 | 16 MiB、Quad QIO、80 MHz |
| PSRAM配置 | 8 MiB、Octal、80 MHz、Caps分配 |
| 自定义分区 | `stm_fw=0x210000/0x120000` |
| Bootloader | `0x5700`字节，Bootloader分区剩余32% |
| ESP32生产应用 | `0x47460`字节，2 MiB应用分区剩余约86% |
| ESP32可靠性应用 | `0x48250`字节，测试专用故障注入已启用 |
| Python测试 | 29/29通过 |
| 初始M7样包 | 16984字节，其中STM32版本1镜像12888字节 |
| 当前可靠性包 | 921600字节，其中STM32版本2镜像917504字节 |

## 编译和烧录

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location .\firmware\esp32_gateway

idf.py build
idf.py -p COMx flash
```

首次只烧录ESP32并检查启动日志，不输入`upgrade start`。

## 制作并写入本地固件包

在仓库根目录执行：

```powershell
py -3 .\tools\firmware_package.py `
  --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --output .\build\stm32_m7_package.bin `
  --version 1
```

在ESP32工程目录写入`stm_fw`：

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location .\firmware\esp32_gateway

parttool.py -p COMx write_partition `
  --partition-name stm_fw `
  --input ..\..\build\stm32_m7_package.bin
```

写分区前确认`COMx`确实属于ESP32，不是STM32的USB-TTL串口。

## 板上验证顺序

### 1. ESP32单板启动

期望日志：

- ESP-IDF v5.5.4。
- Flash为16 MiB。
- PSRAM为8 MiB。
- NVS初始化成功。
- `stm_fw`地址为`0x210000`、大小为`0x120000`。
- UART1配置为TX=17、RX=18、115200。
- 控制台显示`gateway>`。
- 输出`M7 gateway ready; upgrades require an explicit console command`。

### 2. 本地包验证

```text
firmware validate
firmware info
```

期望版本1、大小12888、CRC32 `0x8A0EB599`、Product/Hardware均为`0x0001`。

### 3. STM32无破坏联调

接线后先执行无破坏探测：

```text
upgrade probe
```

该命令只发送`HELLO`和`GET_INFO`，不会发送`ENTER_BOOT`、`ERASE`或`DATA`。期望Product/Hardware均为`0x0001`，APP基地址为`0x08020000`，最大容量为`0x000E0000`。

### 4. 完整升级

```text
upgrade start
upgrade status
```

期望状态：

```text
VALIDATE_IMAGE -> DISCOVER -> ENTER_BOOT -> DISCOVER -> GET_INFO
-> START -> ERASE -> TRANSFER -> VERIFY -> ACTIVATE -> WAIT_APP -> SUCCESS
```

`ACTIVATE`后ESP32必须保持UART静默1秒。STM32 Bootloader复位后提供500 ms恢复窗口；
如果ESP32在该窗口内发送HELLO，Bootloader会将其视为升级活动并留在协议模式，
从而导致`WAIT_APP`超时。若旧版ESP32固件触发该现象，停止升级探测后按一次STM32
`NRST`，等待1秒，再执行`upgrade probe`即可恢复APP；重新烧录修正后的ESP32固件后
再进行完整升级验证。

`ACTIVATE`是会触发目标复位的非普通请求，只发送一次。其应答若超时或因复位边界而
不完整，ESP32不再立即重发，而是进入上述静默期并探测APP。最终以APP的HELLO、
GET_INFO及版本匹配作为激活成功依据，避免重试命令落到已经启动的APP后返回`BUSY`。

## 验收清单

### 软件侧

- [x] ESP-IDF v5.5.4构建0 Error。
- [x] 构建目标为ESP32-S3。
- [x] UART1默认GPIO17/18。
- [x] 固件包格式和打包工具完成。
- [x] 本地包Header与整包CRC32校验完成。
- [x] UART事务层完成。
- [x] ESP32协议客户端完成。
- [x] 完整升级状态机完成。
- [x] 升级默认不自动开始。
- [x] Python测试29/29通过。

### 板上侧

- [x] 启动日志识别16 MiB Flash。
- [x] 启动日志识别8 MiB PSRAM。
- [x] NVS初始化成功。
- [x] 找到`stm_fw=0x210000/0x120000`。
- [x] 控制台可输入命令。
- [x] 本地包板上CRC32校验通过。
- [x] ESP32能够发现STM32 APP。
- [x] ESP32能够请求STM32进入Bootloader。
- [x] 完整传输、VERIFY、ACTIVATE通过。
- [x] 激活后重新发现匹配版本APP；当前版本为2。
- [x] ACK丢失、重复DATA和偏移重同步通过。
- [x] STM32传输中复位后从4 KiB检查点续传。
- [x] 连续10次完整升级成功。
- [ ] R07本地坏包预发送拦截；当前为`DEFERRED`。
- [ ] R09双端冷启动10轮；当前为`DEFERRED`。
- [ ] 恢复生产版并完成最终基线；当前为`DEFERRED`。

## 当前边界

- M7只使用预装在`stm_fw`中的本地包。
- M7可靠性测试矩阵、故障注入边界和验收出口见
  [`m7_reliability_verification.md`](m7_reliability_verification.md)。
- M7阶段归档状态和后续安全接续顺序见
  [`m7_archive_summary.md`](m7_archive_summary.md)。
- PC服务器属于M8。
- ESP32 HTTP下载和缓存写入属于M9。
- 当前SHA-256保存在Manifest并传给STM32，但M7只用CRC32判断传输完整性；HTTPS和安全校验属于M10。
- STM32仍是单APP分区，真正的启动确认和异常恢复属于M11。
