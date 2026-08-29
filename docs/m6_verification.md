# M6 USART1升级链路验证记录

## 当前状态

- 状态：已完成。代码、主机测试、Keil构建和开发板闭环验证均已通过。
- Bootloader正式目标：0 Error、0 Warning，Code 15894 bytes，未超过64 KiB分区。
- APP正式目标：0 Error、0 Warning，Code 12352 bytes；`stm32_app.bin`为12888 bytes。
- M5/M6主机测试：C协议4/4套件通过，Python协议6/6测试通过。
- 板级验证：无破坏探测、完整升级、故障注入、断点续传、错误CRC32、正确固件恢复和连续10次升级均通过。
- 详细证据：[`m6_test_evidence.md`](m6_test_evidence.md)。

## 实现范围

1. USART1使用115200 8N1、节点地址1和功能码`0x41`。
2. USART1中断只接收字节和TIM2微秒时间戳；主循环完成RTU分帧和请求调度。
3. Bootloader上电提供500 ms恢复窗口；收到有效寻址帧后留在协议模式，否则有效APP自动启动。
4. APP正式目标支持HELLO、GET_INFO和ENTER_BOOT；ENTER_BOOT写入`UPDATE_REQUESTED`并在ACK发完后复位。
5. Bootloader支持HELLO、GET_INFO、START、ERASE、DATA、QUERY_PROGRESS、VERIFY、ACTIVATE和ABORT。
6. DATA按Offset幂等：重复块回读一致后ACK，跳块返回BAD_OFFSET；每4096字节持久化进度。
7. VERIFY使用兼容Python的IEEE CRC32；ACTIVATE仅在`PENDING_BOOT`状态允许执行。
8. PC工具支持自动识别APP/Bootloader、进入Bootloader、断点恢复、ACK丢失和重复包注入。

## 主机验证

在仓库根目录执行：

```powershell
.\tools\run_tests.ps1
```

期望看到：

```text
4/4 suites passed
Ran 6 tests
OK
```

## 已解决的首帧栈占用问题

首次板上无破坏探测表现为：LED原本正常闪烁，收到HELLO后停止，同时PC一直等待响应。根因是APP和Bootloader的轮询函数曾将两个256字节ADU、两个升级消息对象和RTU对象同时放在1 KiB启动栈上，首帧处理的自动变量超过栈预算。现已将单线程协议工作区移至静态RAM；修复后Bootloader和APP重新通过0 Error、0 Warning构建。该修复需要重新下载两个正式目标后才能在板上生效。

## 第一步：无破坏板上验证

先在Keil选择并下载普通目标`stm32_bootloader`和`stm32_app`。不能使用带`LOG_ENABLE=1`的debug目标进行二进制协议联调。

连接要求：USB-TTL TX接PA10、RX接PA9、GND共地、TTL电平3.3 V。ESP32不得同时驱动PA10。

安装依赖：

```powershell
py -3 -m pip install -r .\tools\requirements.txt
```

打开串口后先运行无破坏探测：

```powershell
py -3 .\tools\serial_upgrade.py `
  --port COM3 `
  --probe-only `
  --verbose
```

该命令只执行HELLO和GET_INFO，不会发送ENTER_BOOT、START、ERASE或DATA。正常升级时工具才会自动从APP请求进入Bootloader，因此通常不需要手动抢500 ms窗口。

如果板上仍是M2阶段旧APP，它没有APP侧协议服务。此时保持上述命令运行并按一次NRST；工具会约每100 ms发送HELLO，在Bootloader的500 ms窗口内完成连接。若仍然始终显示`serial response timeout (0/2 bytes)`，优先检查USB-TTL TX到STM32 PA10的接收方向；此前文本日志只能证明PA9到USB-TTL RX的发送方向正常。

## 第二步：完整升级

确认已保存当前可恢复的APP `.bin` 后执行：

```powershell
py -3 .\tools\serial_upgrade.py `
  --port COM3 `
  --file .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --version 1 `
  --verbose
```

成功标志依次为：

```text
APP service connected; requesting Bootloader entry...
Bootloader connected
Transferring from offset 0
CRC32 verification passed
Activation acknowledged
```

实测12888 bytes镜像传输完成，CRC32 `0x8A0EB599`校验通过，激活后自动复位进入APP。

## 第三步：幂等与异常验证

随机丢ACK和重复DATA：

```powershell
py -3 .\tools\serial_upgrade.py `
  --port COM3 `
  --file .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --version 1 `
  --inject-drop-ack-rate 0.1 `
  --inject-duplicate-rate 0.1 `
  --verbose
```

错误整包CRC测试：

```powershell
py -3 .\tools\serial_upgrade.py `
  --port COM3 `
  --file .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --version 1 `
  --override-crc32 0
```

实测在VERIFY阶段返回`VERIFY_FAILED`，错误镜像未激活；随后用正确CRC重新升级恢复成功。

断点续传测试：传输中复位STM32，再使用相同文件执行：

```powershell
py -3 .\tools\serial_upgrade.py `
  --port COM3 `
  --file .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --version 1 `
  --resume `
  --verbose
```

实测在offset 7008后复位，设备返回持久化检查点4096；工具发现同一Session `0xFF9B5FBC`，未重新擦除APP，并从4096继续至校验、激活成功。

## M6验收结果

- [x] 正常完整升级连续成功10次。
- [x] ACK随机丢失和DATA随机重复时仍能完成。
- [x] 复位后能够恢复到持久化检查点。
- [x] 错误offset被拒绝并返回设备期望offset。
- [x] 错误CRC被拒绝，错误镜像未激活，随后可恢复正确固件。
- [x] 同一Session恢复时重复ERASE未再次擦除已经接收的数据。
- [x] 连续测试后Bootloader、Metadata和APP区域仍正常工作。
- [x] 协议串口中不存在混杂的`[BOOT]`、`[APP]`或其他文本日志。
- [ ] 错误产品ID、错误硬件ID、超大镜像和错误Session的原始帧板级负向测试未单独归档；拒绝逻辑已实现，发布前加入自动化协议回归。

结论：M6当前开发阶段验收通过，进入M7 ESP32 UART主机实现；未归档的负向原始帧用例不阻塞M7。

## 连续升级稳定性

错误CRC恢复完成后，重新独立执行10次正常升级；此前故障注入和断点续传不计入这10次。脚本每轮执行完整升级和ACTIVATE，等待1秒，再用`--probe-only`确认APP协议服务已恢复。任一升级或探测失败都会立即终止：

```powershell
.\tools\run_m6_stability.ps1 `
  -Port COM3 `
  -Cycles 10 `
  -Version 1 `
  -ConfirmDestructive
```

实际最终输出：

```text
[PASS] cycle 10/10
[PASS] M6 stability test: 10/10 cycles
```

验证期间不得同时连接ESP32到USART1 RX；使用普通APP和Bootloader目标，不使用`LOG_ENABLE=1`的debug目标。

本次10/10轮完整升级、CRC32校验、激活和升级后APP探测均成功，错误/失败为0，总耗时00:00:50。原始日志为`build/m6_stability_result.txt`，SHA-256为`C91E13F81BCD205A490B53865B6F6EBC1754DF8696F6886B7B8C3952811E5FE7`。
