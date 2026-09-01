# M7 R04双端掉电恢复测试证据

## 测试配置

- 日期：2026-09-01
- ESP32控制台：COM5，115200 bit/s
- ESP32可靠性固件SHA-256：`643BD6FFA267DB40D2DD7341859DA1B4135230AD55C4B1353872969CBFB0051B`
- STM32 APP：版本2，原始大小12892字节，SHA-256=`69D6E29DD187706496B4B90FDC2B1F7CBB67FD898273A9B65E1397B765F835AE`
- 掉电测试镜像：917504字节，版本2，CRC32=`0x1AEDDB50`
- ESP32分区包：921600字节，SHA-256=`8D7E25682FC9C87BF4A6BC791A40546B7D9A02333E2AEEC04C596B44DFCFD9F8`

测试镜像保留原始APP向量和代码，尾部904612字节全部填充`0xFF`。使用APP分区最大
尺寸是为了给人工双端断电保留足够长的操作窗口。

## 前置可靠性缺陷与修复

连续升级和大镜像测试消耗Metadata Journal记录后，旧APP在`ENTER_BOOT`返回设备状态
10（`FLASH_ERROR`）。当时STM32仍能正常运行APP版本1，远端最新状态为
`PENDING_BOOT(6)`。

根因是APP在进入Bootloader前保守预留232条Journal记录；剩余记录不足时需要整理
Metadata，但`boot_metadata_state_is_safe_to_compact()`没有把已经完成整包校验的
`PENDING_BOOT`视为安全整理状态。因此当前实现尚未追加`CONFIRMED`时，连续升级最终会
无法开始下一轮。

修复内容：

1. 允许整理`PENDING_BOOT`，整扇区擦除前保留并重写最新有效记录；
2. 继续禁止在`UPDATE_REQUESTED/ERASING/RECEIVING/VERIFYING`活动升级状态整理；
3. 目标自测增加`PENDING_BOOT`整理、记录保留和后续追加`CONFIRMED`覆盖；
4. STM32 APP版本提升到2，用于证明板上实际运行修复版。

修复后APP和Bootloader Keil构建均为0 Error、0 Warning，23项Python主机回归测试通过。
在同样的`PENDING_BOOT(6)`现场，`ENTER_BOOT`成功，状态机进入TRANSFER，不再返回状态10。

## R04-A：检查点110592双端掉电

断电前状态：

```text
State: TRANSFER
Session: 0xD89D0988
Firmware version: 2
Progress: 33440/917504 bytes
Remote boot state: 4
Last result: ESP_OK, device status=0
```

人工切断ESP32和STM32全部电源前，串口最后记录的持久化进度为110592字节；随后串口
立即断开，没有出现VERIFY、ACTIVATE或SUCCESS。重新上电后：

```text
Service: capabilities=0x000D, version=0x00010000
Device: product=0x0001, hardware=0x0001, boot_state=4
Versions: bootloader=0x00010000, application=2
```

重新执行`upgrade start`后约40毫秒即从逻辑ERASE进入TRANSFER，没有实际重新擦除；
Session仍为`0xD89D0988`，第一条进度为114688字节，紧接110592字节检查点。最终结果：

```text
State: SUCCESS
Session: 0xD89D0988
Firmware version: 2
Progress: 917504/917504 bytes
Remote boot state: 6
Last result: ESP_OK, device status=0
```

## R04-B：检查点421888双端掉电

第二轮使用新Session，在不同传输位置执行双端掉电：

```text
State: TRANSFER
Session: 0xF6666D96
Firmware version: 2
Progress: 369760/917504 bytes
Remote boot state: 4
Last result: ESP_OK, device status=0
```

串口最后记录的持久化进度为421888字节，随后立即断开且没有成功日志。重新上电后，
STM32再次报告Bootloader能力`0x000D`和`RECEIVING(4)`。恢复任务保持Session
`0xF6666D96`，约40毫秒跳过实际擦除，第一条进度为425984字节。最终结果：

```text
State: SUCCESS
Session: 0xF6666D96
Firmware version: 2
Progress: 917504/917504 bytes
Remote boot state: 6
Last result: ESP_OK, device status=0

Service: capabilities=0x0002, version=0x00000002
Device: product=0x0001, hardware=0x0001, boot_state=6
Versions: bootloader=0x00010000, application=2
```

## 结论

R04 PASS。两轮均在TRANSFER期间发生真实双端冷断电，并分别从110592和421888两个不同
的4 KiB对齐检查点恢复；断电前后Session保持一致，没有实际重新擦除，整包CRC校验、
激活和APP版本2回探全部成功。

