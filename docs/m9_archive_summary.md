# M9阶段归档总览

## 1. 归档结论

- 归档日期：2026-09-01（Asia/Shanghai）。
- 归档范围：ESP32-S3通过Wi-Fi从M8 HTTP服务器获取M7兼容固件包、流式写入`stm_fw`、
  下载检查点恢复、安全提交，以及下载完成后通过既有M7 UART链路升级STM32F407。
- 阶段结论：**M9核心链路阶段性完成，可以进入M10开发**。
- 验收状态：正常下载、完整性校验、ESP32复位续传、用户取消续传和STM32端到端回归
  均有目标板`PASS`证据；ETag变化和错误Manifest身份拦截为`DEFERRED`，不得写为`PASS`。
- M9不会自动升级STM32；只有显式执行`upgrade start`才会擦写STM32 APP。

## 2. 已实现范围

| 范围 | 归档内容 |
| --- | --- |
| `gateway_wifi` | 串口配置Wi-Fi STA与M8地址、NVS持久化、重启自动恢复、状态与清除命令 |
| `firmware_downloader` | Manifest获取与预检、4 KiB流式写入、HTTP Range/If-Range、ETag与NVS检查点、取消和状态查询 |
| `firmware_store` | 下载期间无效标记保护、分区写入接口、完成后包头/镜像完整性复核 |
| `gateway_console` | `wifi`、`firmware download/status/cancel`与既有`upgrade`命令互斥协调 |
| M8服务 | Manifest/Binary API、单Range `206`、If-Range回退、测试专用分块延时 |
| 发布工具 | 生成M7兼容发布目录与Manifest；`--pad-to`生成续传测试大包 |

Wi-Fi SSID、密码和M8地址不写入Git管理的默认配置，实际值只保存于ESP32 NVS。

## 3. 安全与恢复设计

1. Manifest在擦除缓存前检查产品/硬件身份、包格式、长度、下载路径、CRC32和SHA-256字段。
2. HTTP响应按4 KiB块写入`stm_fw`，固件包不完整驻留RAM。
3. 下载期间将包头`valid_marker`保持为`0xFFFFFFFF`；整包SHA-256、包头和镜像CRC32全部
   通过后才提交真实标记，半包不能被`upgrade start`接受。
4. 正常下载每64 KiB保存NVS检查点；显式Cancel额外保存最后一次Flash写入成功后的精确
   `received_size`。
5. 复位或重试时发送`Range`与`If-Range`；只有`206`且`Content-Range`起点一致才续写，
   服务端返回`200`时从0安全重下。
6. 下载器与UART升级管理器不能并行占用缓存，网络下载完成后仍须人工触发STM32升级。

## 4. 实现期间修复的问题

- 首次板测完成下载后返回`ESP_ERR_INVALID_CRC`。根因是Flash中延迟提交的
  `valid_marker=0xFFFFFFFF`与M8原包的`0xA5C3F00D`不同，导致暂存包SHA-256不一致。
  修复方式是在重读哈希的RAM块中临时恢复期望标记，Flash中的标记仍保持无效，直至
  SHA-256、包头和镜像CRC32全部通过。
- 初版串口固件未注册`wifi`命令，重新构建并烧录M9镜像后命令可用；使用文档已明确
  ESP32控制台、STM32调试串口和ESP32↔STM32 UART链路的区别。
- 下载测试难以稳定命中检查点，因此为M8增加默认关闭的测试延时，并为发布工具增加
  `--pad-to`，生成不改变原始程序字节的有效大包。

## 5. 板上验收汇总

| 用例 | 结果 | 核心证据 |
| --- | --- | --- |
| 正式包正常下载 | `PASS` | `f407-node-1.2.0`为`READY`，16988/16988，包内镜像12892字节、CRC32=`CF885C9E` |
| ESP32复位续传 | `PASS` | 从655360恢复，M8返回`206 bytes 655360-921599/921600`，最终完整校验成功 |
| 用户取消续传 | `PASS` | Cancel后标记无效；从397312恢复，最终921600/921600并校验成功 |
| ETag变化 | `DEFERRED` | 测试发布物已准备，未执行目标板切换测试 |
| 错误Manifest身份 | `DEFERRED` | 错误Product ID发布物已准备，未执行目标板拦截测试 |
| STM32端到端回归 | `PASS` | Session=`0xD51A5E63`，12892/12892，`SUCCESS/ESP_OK/0`，APP版本2回探成功 |

完整命令、判据和逐项证据见[`m9_remaining_verification.md`](m9_remaining_verification.md)。

## 6. 归档时设备与发布物状态

| 项目 | 状态 |
| --- | --- |
| ESP32硬件 | ESP32-S3-WROOM-1-N16R8，16 MiB Flash，8 MiB Octal PSRAM |
| ESP-IDF | v5.5.4，Target=`esp32s3` |
| ESP32 M9应用 | 968896字节（`0xEC8C0`），SHA-256=`5F3038B8F85EEA2E551D6130A60B1C28DA9DD378BEC29A7B4AFF1DD992E0D723` |
| Factory分区 | 大小`0x200000`，剩余`0x113740`字节（54%） |
| 正式发布物 | `f407-node-1.2.0`，包16988字节，包SHA-256=`D0F0253A73C6A00FFD988B7AA277FE348ADFBBBCBCF879E20BB0A7819AB697FC` |
| STM32 Bootloader | 版本`0x00010000` |
| STM32 APP | Product/Hardware=`0x0001/0x0001`，版本2，区域`0x08020000/0x000E0000` |
| 最后一次升级 | Session=`0xD51A5E63`，12892/12892，`SUCCESS/ESP_OK/0` |
| 最后一次探测 | APP服务`capabilities=0x0002`，`boot_state=PENDING_BOOT(6)`，Application=2 |

`PENDING_BOOT(6)`是当前未实现APP启动确认与回滚闭环时的预期状态，该闭环计划在M11完成。

## 7. 构建与自动化回归

- 2026-09-01重新执行工具Python回归：33/33通过。
- 2026-09-01重新执行M8/M9服务器回归：10/10通过。
- ESP-IDF增量构建、应用大小与分区检查通过：镜像`0xEC8C0`，余量`0x113740`。
- M8本机全量与Range下载、SHA-256及HTTP头验证已归档在
  [`m8_verification.md`](m8_verification.md)和[`m8_archive_manifest.md`](m8_archive_manifest.md)。
- 当前M9源码与固件哈希见[`m9_archive_manifest.md`](m9_archive_manifest.md)。

## 8. 延期项与阶段边界

以下项目是延期而非失败：

1. 同一Firmware ID切换为不同包/ETag后，确认旧检查点被放弃并从0重下。
2. 错误Product/Hardware Manifest在擦除当前有效缓存前被拒绝。
3. HTTPS、服务器证书校验和更强的来源认证属于M10及后续安全阶段。
4. APP启动确认、超时回滚和`CONFIRMED`状态闭环属于M11。

因此可宣称“M9网络下载与UART升级核心链路通过并阶段性归档”，不能宣称“M9全部异常
分支测试关闭”。

## 9. 归档入口

- 实现状态：[`m9_implementation.md`](m9_implementation.md)
- 设计与协议边界：[`m9_download_design.md`](m9_download_design.md)
- 板测步骤与证据：[`m9_remaining_verification.md`](m9_remaining_verification.md)
- 文件与固件哈希：[`m9_archive_manifest.md`](m9_archive_manifest.md)
- M8服务验收：[`m8_verification.md`](m8_verification.md)
- 文档导航：[`README.md`](README.md)

