# M11目标板测试证据

## 测试结论

- 测试日期：2026-09-03（Asia/Shanghai）。
- 硬件：ESP32-S3-WROOM-1-N16R8网关、STM32F407ZGT6节点、USART1 115200直连。
- M11核心功能结果：`PASS`。
- 最终设备状态：STM32 APP版本3、`CONFIRMED(7)`。

## E01 可信HTTPS下载与缓存校验

ESP32连接`https://192.168.31.170:8443`，下载`f407-node-1.3.0`：

```text
State: READY
Firmware ID: f407-node-1.3.0
Progress: 17348/17348 bytes
Last result: ESP_OK

Firmware version: 3
Image size: 13252 bytes
Image CRC32: 0xA81E6F7C
Product/Hardware: 0x0001/0x0001
```

结果：`PASS`。

## E02 正常启动确认

升级前设备为APP版本2、`PENDING_BOOT(6)`。Session `0x700DD63C`完成版本3升级后，网关先观察到
APP健康但仍待确认，随后才报告成功：

```text
State -> ACTIVATE
State -> WAIT_APP
STM32 APP is healthy but startup confirmation is pending
STM32 APP confirmed, version=3
M11 confirmed UART upgrade completed successfully

State: SUCCESS
Firmware version: 3
Progress: 13252/13252 bytes
Remote boot state: 7
Last result: ESP_OK, device status=0
```

回探结果：

```text
Service: capabilities=0x0002, version=0x00000003
Device: product=0x0001, hardware=0x0001, boot_state=7
Versions: bootloader=0x00010000, application=3
APP: base=0x08020000, max_size=0x000E0000
```

结果：`PASS`。ESP32没有把可通信的`PENDING_BOOT`误判为升级成功。

## E03 连续未确认启动与恢复态

使用Session `0xE0DB346C`重新升级后，在3秒确认窗口内连续复位STM32。Bootloader在3次未确认
启动后停止跳转APP，网关识别到确认超时：

```text
STM32 APP was not confirmed; Bootloader entered recovery, error=13
M11 upgrade failed: ESP_ERR_INVALID_STATE, device_status=13

State: FAILED
Firmware version: 3
Progress: 13252/13252 bytes
Remote boot state: 8
Last result: ESP_ERR_INVALID_STATE, device status=13
```

Bootloader回探：

```text
Service: capabilities=0x000D, version=0x00010000
Device: product=0x0001, hardware=0x0001, boot_state=8
Versions: bootloader=0x00010000, application=3
APP: base=0x08020000, max_size=0x000E0000
```

复位期间出现少量`HELLO`解码失败和请求超时，是复位切断UART帧产生的预期瞬态；解析器丢弃
残帧后继续探测，最终状态与错误码均正确。结果：`PASS`。

## E04 ESP32缓存重新刷写

进入`FAILED(8)`后没有重新下载固件。`firmware validate`仍显示版本3、13252字节、
CRC32 `A81E6F7C`。直接以新Session `0x0334D9C1`执行`upgrade start`：

```text
State -> WAIT_APP
STM32 APP is healthy but startup confirmation is pending
STM32 APP confirmed, version=3
M11 confirmed UART upgrade completed successfully

State: SUCCESS
Firmware version: 3
Progress: 13252/13252 bytes
Remote boot state: 7
Last result: ESP_OK, device status=0
```

最终回探为APP版本3、`CONFIRMED(7)`。结果：`PASS`。该能力是网关缓存重刷，不是STM32侧
A/B回滚或恢复旧APP。

## E05 CONFIRMED持久化

用户在目标板上完成STM32复位/掉电验证，确认复位/重新上电后仍运行APP版本3且Metadata保持
`CONFIRMED(7)`。结果：`PASS`。

本项由用户明确确认；本轮对话未附新的原始串口片段，因此归档只记录结论，不虚构额外输出。

## 回归边界

- M11镜像上的可信HTTPS正式包下载与缓存校验已通过。
- M10阶段已经归档错误CA拒绝和失败后缓存保护证据。
- 错误CA负向用例没有在M11镜像上重复执行，标记为`NOT RE-RUN`；M11未修改下载器/TLS信任
  实现，但这只能作为风险判断，不能替代一次新的目标板测试。
