# M7 R02 STM32传输中复位测试证据

## 测试配置

- 日期：2026-08-31
- ESP32控制台：COM5，115200 bit/s
- STM32测试镜像：262144字节，版本1，CRC32=`0xE71F580C`
- 测试包SHA-256：`F8409AFD2AF7B68F7BFE457B1501C3A897A3187C0DDB9EC39E92D7C2447AB69E`
- ESP32可靠性固件SHA-256：`643BD6FFA267DB40D2DD7341859DA1B4135230AD55C4B1353872969CBFB0051B`

测试镜像保留原始12888字节APP内容，尾部填充`0xFF`至256 KiB。离线检查确认
原APP前缀逐字节一致，全部新增区域均为`0xFF`。

## 首轮测试与问题发现

首次在TRANSFER期间按住STM32 NRST约1秒。复位边界连续产生残缺或错地址响应，原实现
对协议无效响应立即重试，约200毫秒内耗尽5次尝试：

```text
Command 0x12 attempt 1/5 decode failed: protocol=3, length=2
Command 0x12 attempt 2/5 decode failed: protocol=4, length=43
...
Command 0x12 attempt 5/5 decode failed: protocol=4, length=43
M7 upgrade failed: ESP_ERR_INVALID_RESPONSE, device_status=0
State: FAILED
Session: 0xC98E66FC
Progress: 127424/262144 bytes
Remote boot state: 4
```

STM32安全留驻`RECEIVING(4)`，未进入VERIFY或ACTIVATE。再次执行`upgrade start`后复用
Session `0xC98E66FC`并完成升级，证明失败后会话可续传；但首轮不满足“当前任务自动
恢复”的R02严格判据。

## 修复

在协议解码失败或响应地址/命令/序列不匹配后增加250毫秒重试退避。5次尝试因此覆盖
约1秒STM32复位窗口；正常超时、有效响应以及一次性故障注入路径保持不变。

修复后23项Python测试、ESP32生产构建和可靠性构建均通过。

## 复测结果

修复后重新执行同一测试，STM32复位时ESP32正在发送offset 84608。STM32重启后从
Metadata恢复最后持久化的81920字节检查点：

```text
Command 0x12 attempt 1/5 decode failed: protocol=3, length=3
STM32 requested offset resynchronization: 84608 -> 81920
Transferred 81920/262144 bytes (31%)
...
Transferred 262144/262144 bytes (100%)
State -> VERIFY
State -> ACTIVATE
State -> WAIT_APP
STM32 APP connected, version=1
M7 local UART upgrade completed successfully
```

最终状态：

```text
State: SUCCESS
Session: 0x412011A5
Firmware version: 1
Progress: 262144/262144 bytes
Remote boot state: 6
Last result: ESP_OK, device status=0

Service: capabilities=0x0002, version=0x00000001
Device: product=0x0001, hardware=0x0001, boot_state=6
Versions: bootloader=0x00010000, application=1
APP: base=0x08020000, max_size=0x000E0000
```

结论：R02 PASS。当前任务在STM32传输中复位后保持Session，回退到最近4 KiB检查点，
不重新擦除APP，并自动完成升级和APP回探。
