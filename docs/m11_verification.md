# M11目标板验收步骤

## 1. 验收目标

本轮验证两个闭环：正常镜像必须由APP确认后才成功；确认失败必须在有限次数后停留Bootloader，
并能使用ESP32缓存重新升级。测试使用STM32F407ZGT6、ESP32-S3和现有USART1直连。

## 2. 构建结果与文件

开发机已完成构建，当前文件为：

```text
firmware/stm32_bootloader/MDK-ARM/stm32_bootloader/stm32_bootloader.hex
firmware/stm32_app/MDK-ARM/stm32_app/stm32_app.bin
build/esp32_gateway_m11/esp32_gateway.bin
server/firmware/f407-node-1.3.0/firmware.bin
server/firmware/f407-node-1.3.0/manifest.json
```

其中`firmware.bin`和`build/`受Git忽略，提交源码不能替代保存二进制交付物。

## 3. 烧录顺序

1. 用Keil/ST-Link烧录M11 `stm32_bootloader`正式目标。不要启用日志或破坏性自测宏。
2. 不要直接用ST-Link烧录版本3 APP；应保留当前APP，通过网关升级，以验证完整M11路径。
3. 烧录ESP32 M11：

```powershell
Set-Location .\firmware\esp32_gateway
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
$env:IDF_TARGET = "esp32s3"

idf.py -B ..\..\build\esp32_gateway_m11 `
  -D SDKCONFIG=..\..\build\esp32_gateway_m11\sdkconfig `
  -p COM5 flash monitor
```

如果构建目录记录了另一Python解释器，删除并重新生成这个独立的M11构建目录，或执行同一
`-B`目录的`idf.py fullclean`；不要复用此前由其他ESP-IDF/Python环境生成的目录。

预期启动日志包含：

```text
ESP32 gateway M11 initialization starting
M11 gateway ready; upgrade success now requires STM32 APP confirmation
```

## 4. 启动HTTPS服务器并下载版本3

在仓库根目录启动已通过M10验收的可信HTTPS服务：

```powershell
.\tools\run_m10_https.ps1
```

ESP32控制台执行：

```text
wifi status
firmware download f407-node-1.3.0
firmware download status
firmware validate
```

预期关键值：

```text
State: READY
Progress: 17348/17348 bytes
Firmware version: 3
Image size: 13252 bytes
Image CRC32: 0xA81E6F7C
Product/Hardware: 0x0001/0x0001
```

## 5. 正常启动确认

执行：

```text
upgrade probe
upgrade start
upgrade status
```

`upgrade start`是异步命令；等待约5秒后再次查询状态。升级过程中允许短暂看到
`WAIT_APP`和远端状态6，但最终必须为：

```text
State: SUCCESS
Firmware version: 3
Progress: 13252/13252 bytes
Remote boot state: 7
Last result: ESP_OK, device status=0
```

再次执行：

```text
upgrade probe
```

必须看到APP服务、`application=3`和`boot_state=7`。如果APP已经运行但状态仍为6，M11不能
验收通过；ESP32也不应报告`SUCCESS`。

## 6. 确认前复位与失败恢复

该测试会让STM32进入`FAILED`恢复态，但不会擦除ESP32缓存。先确认第5节正向路径已保存证据，
然后重新执行一次`upgrade start`。每次观察到STM32 APP开始运行后，在3秒确认窗口内按下STM32
复位键；共重复3次。第4次Bootloader启动时不再跳转APP。

等待ESP32任务结束后执行：

```text
upgrade status
upgrade probe
```

预期状态：

```text
State: FAILED
Remote boot state: 8
Last result: ESP_ERR_INVALID_STATE, device status=13

Service: capabilities=0x000D, version=0x00010000
Device: product=0x0001, hardware=0x0001, boot_state=8
Versions: bootloader=0x00010000, application=3
```

如果人工复位时机不稳定，可构建测试APP并令
`UPGRADE_APP_TEST_WATCHDOG_RESET=1`，它会在`PENDING_BOOT`期间停止喂狗，自动走完相同路径。
测试完成后必须恢复为`0`并重新构建正式APP；该故障包必须使用不同Firmware ID，不能覆盖
`f407-node-1.3.0`。

## 7. 使用缓存重新升级

不要再次执行`firmware download`，直接运行：

```text
firmware validate
upgrade start
upgrade status
upgrade probe
```

最终必须重新得到`SUCCESS`、版本3和`boot_state=7`。这证明单APP设计的恢复路径是“网关缓存
重刷”，不是回滚到旧镜像。

## 8. 验收出口

| 用例 | 通过条件 | 当前状态 |
| --- | --- | --- |
| M11-01 正常确认 | APP版本3，状态7；ESP32随后才SUCCESS | `PASS` |
| M11-02 未确认限制 | 3次未确认启动后状态8、错误13 | `PASS` |
| M11-03 缓存恢复 | 不重新下载即可重刷并回到状态7 | `PASS` |
| M11-04 M10回归 | M11可信HTTPS下载通过；错误CA负向测试 | `PARTIAL / NOT RE-RUN` |
| M11-05 确认持久化 | STM32复位/掉电后仍为版本3、状态7 | `PASS` |

M11自身新增功能的目标板验收已经完成，阶段状态为`PHASE COMPLETE`。M11-04未重复执行的错误CA
负向测试沿用M10归档证据，但必须保持`NOT RE-RUN`标记，不能写成M11目标板`PASS`。
完整板测证据见[`m11_test_evidence.md`](m11_test_evidence.md)。
