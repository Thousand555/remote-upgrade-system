# M6 STM32 串口升级板级验证归档

## 1. 归档信息

- 阶段：M6 STM32 USART1 串口升级闭环
- 验证日期：2026-08-29
- 验证接口：USART1，115200 8N1
- 主机端口：COM3
- Bootloader 版本：`0x00010000`
- 产品/硬件标识：`0x0001 / 0x0001`
- APP 地址/容量：`0x08020000 / 0x000E0000`
- 测试镜像：`firmware/stm32_app/MDK-ARM/stm32_app/stm32_app.bin`
- 镜像大小：12888 bytes
- 镜像 CRC32：`0x8A0EB599`
- 测试版本号：1

## 2. 构建与主机测试基线

| 项目 | 结果 |
| --- | --- |
| Bootloader Keil Rebuild | 0 errors，0 warnings；Code=15894，RO=450，RW=92，ZI=7068 |
| APP Keil Rebuild | 0 errors，0 warnings；Code=12352，RO=448，RW=88，ZI=6992 |
| `tools/tests/test_upgrade_protocol.py` | 4/4 通过 |
| `tools/tests/test_serial_upgrade.py` | 6/6 通过 |

## 3. 开发板验证结果

| 验证项 | 结果 | 关键证据 |
| --- | --- | --- |
| APP 无破坏探测 | 通过 | `HELLO`、`GET_INFO` 返回 `OK`；状态 `CONFIRMED`；能力位 `0x0002` |
| Bootloader 无破坏探测 | 通过 | Bootloader 可发现并返回版本、分区和升级状态 |
| APP 请求进入 Bootloader | 通过 | `ENTER_BOOT` 返回 `OK`，复位后主机重新发现 Bootloader |
| 擦除、传输、校验、激活 | 通过 | 12888 bytes 传输完成；设备 CRC32 校验通过；`ACTIVATE` 返回 `OK` |
| 激活后 APP 可运行 | 通过 | 自动复位进入 APP；后续探测状态为 `PENDING_BOOT` |
| ACK 丢失与重复 DATA | 通过 | 故障注入后重试成功，重复数据未造成镜像损坏 |
| 传输中复位与断点续传 | 通过 | 复位前到达 offset 7008；恢复点回退到已持久化的 4096；会话 `0xFF9B5FBC` 从 4096 继续且未重擦 APP |
| 非法偏移处理 | 通过 | 旧请求收到 `BAD_OFFSET`，设备返回期望 offset 4096；主机据此重新同步 |
| 错误 CRC32 | 通过 | 使用 `--override-crc32 0` 后 `VERIFY` 返回 `VERIFY_FAILED`，设备未激活错误镜像 |
| 校验失败后的恢复 | 通过 | 随后重新传输正确镜像，CRC32 校验和激活均成功 |
| 连续升级稳定性 | 通过 | 10/10 次完整升级和升级后探测成功，无失败 |

说明：错误产品号、硬件号、镜像越界和错误会话的拒绝逻辑已有固件实现及主机侧检查，但本次没有保存独立的原始帧板测日志。它们应在发布前纳入自动化负向协议回归，不作为当前进入 M7 的阻塞项。

## 4. 连续升级统计

原始日志：`build/m6_stability_result.txt`。`build/` 为 Git 忽略目录，因此本文件记录可提交的结果摘要和日志指纹。

| 指标 | 结果 |
| --- | --- |
| 测试循环 | 10 |
| 完整升级成功 | 10 |
| CRC32 校验成功 | 10 |
| 激活成功 | 10 |
| 激活后 APP 探测成功 | 10 |
| 错误/失败 | 0 |
| 总耗时 | 00:00:50 |
| 日志 SHA-256 | `C91E13F81BCD205A490B53865B6F6EBC1754DF8696F6886B7B8C3952811E5FE7` |

每轮升级前设备状态为 `UPDATE_REQUESTED`，激活并进入 APP 后为 `PENDING_BOOT`。`PENDING_BOOT` 是当前未实现 APP 启动确认/回滚前的预期状态，将在 M11 闭环。

## 5. 验证中确认的行为

- STM32F407 单 Bank 擦除期间无法处理 USART 请求；主机看到一次短暂超时后通过 `QUERY_PROGRESS` 恢复，属于当前架构的预期行为。
- 断点信息按 4 KiB 检查点持久化，所以在 offset 7008 复位后从 4096 恢复是正确行为，不是数据倒退故障。
- 首帧接收曾因协议工作区占用任务栈导致设备只收不回；工作区改为静态存储并增大栈余量后，APP 与 Bootloader 探测均恢复正常。
- 主机发现轮询间隔已缩短到约 100 ms，以适配 APP 请求进入 Bootloader 后较短的等待窗口。

## 6. 阶段结论

M6 的 USART1 二进制协议、APP/Bootloader 切换、Flash 写入、设备端 CRC32 校验、激活、异常重试、断点续传、失败后恢复及连续 10 次稳定性验证均已完成。M6 可以归档，项目进入 M7。

