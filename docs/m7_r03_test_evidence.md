# M7 R03 ESP32传输中复位测试证据

## 测试配置

- 日期：2026-08-31
- ESP32控制台：COM5，115200 bit/s
- STM32测试镜像：262144字节，版本1，CRC32=`0xE71F580C`
- ESP32可靠性固件SHA-256：`643BD6FFA267DB40D2DD7341859DA1B4135230AD55C4B1353872969CBFB0051B`

## 复位前状态

升级进入TRANSFER后读取ESP32状态：

```text
State: TRANSFER
Session: 0x0842160F
Firmware version: 1
Progress: 77312/262144 bytes
Remote boot state: 4
Last result: ESP_OK, device status=0
```

随后只复位ESP32，STM32保持供电。串口日志表明ESP32复位实际发生前传输已推进至
241664字节附近。

## ESP32重启后的远端状态

ESP32重新启动后本地包仍通过校验：

```text
Firmware version: 1
Image size: 262144 bytes
Image CRC32: 0xE71F580C
Product/Hardware: 0x0001/0x0001
```

STM32保持原升级状态：

```text
Service: capabilities=0x000D, version=0x00010000
Device: product=0x0001, hardware=0x0001, boot_state=4
```

## 续传结果

重新执行`upgrade start`后，ESP32发现STM32 Bootloader并复用远端会话。状态机虽进入
逻辑`ERASE`状态，但约40毫秒后直接进入TRANSFER，没有重新执行耗时擦除或出现擦除
轮询超时；第一个进度点直接为245760字节：

```text
State -> START
State -> ERASE
State -> TRANSFER
Transferred 245760/262144 bytes (93%)
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
Session: 0x0842160F
Firmware version: 1
Progress: 262144/262144 bytes
Remote boot state: 6
Last result: ESP_OK, device status=0

Service: capabilities=0x0002, version=0x00000001
Device: product=0x0001, hardware=0x0001, boot_state=6
Versions: bootloader=0x00010000, application=1
APP: base=0x08020000, max_size=0x000E0000
```

结论：R03 PASS。ESP32传输中复位没有改变STM32会话；重启后本地包保持有效，ESP32
复用原Session和远端Offset继续传输，STM32没有实际重新擦除APP。
