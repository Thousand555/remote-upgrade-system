# M7阶段归档总览

## 1. 归档结论

- 归档日期：2026-09-01（Asia/Shanghai）。
- 归档范围：ESP32-S3通过本地UART向STM32F407 Bootloader发送预装固件，以及已完成的
  板上可靠性验证、自动化工具、构建产物和问题修复。
- 阶段结论：M7主功能闭环已经完成，R01～R06、R08和R10～R15均有板上PASS证据，
  正常升级稳定性为10/10。
- 验收状态：**阶段性归档，未关闭完整可靠性验收**。按用户要求，R07本地坏包拦截、
  R09双端冷启动10轮和测试后生产版回归暂缓执行。
- 本次归档没有启动R07、没有改写`stm_fw`、没有烧录设备，也没有创建Git提交。

## 2. 归档时设备状态

| 项目 | 归档状态 |
| --- | --- |
| ESP32硬件 | ESP32-S3-WROOM-1-N16R8，16 MiB Flash，8 MiB Octal PSRAM |
| ESP-IDF | v5.5.4，Target=`esp32s3` |
| ESP32板上固件 | 可靠性测试版，`CONFIG_GATEWAY_RELIABILITY_TEST=y` |
| ESP32测试版应用 | 295504字节（`0x48250`），SHA-256=`643BD6FFA267DB40D2DD7341859DA1B4135230AD55C4B1353872969CBFB0051B` |
| ESP32生产版应用 | 已构建但尚未恢复烧录，291936字节（`0x47460`），SHA-256=`7DDC083BFE3CF37756246E47CAD462D44F2211698E4B277555751ADC8B6B1C12` |
| `stm_fw`本地包 | 版本2，镜像917504字节，CRC32=`0x1AEDDB50`，分区包921600字节 |
| `stm_fw`包SHA-256 | `8D7E25682FC9C87BF4A6BC791A40546B7D9A02333E2AEEC04C596B44DFCFD9F8` |
| STM32 Bootloader | 版本`0x00010000` |
| STM32 APP | 版本2，原始BIN 12892字节，SHA-256=`69D6E29DD187706496B4B90FDC2B1F7CBB67FD898273A9B65E1397B765F835AE` |
| 最后一次板上任务 | R06恢复成功，Session=`0xEF267165`，917504/917504，`ESP_OK` |
| 最后一次APP探测 | Product/Hardware=`0x0001/0x0001`，APP版本2，区域`0x08020000/0x000E0000` |
| 故障注入 | R06开始前已执行`test fault clear`；没有遗留已武装的一次性故障 |
| 控制台 | ESP32=`COM5`，STM32 USB-TTL=`COM3`；归档时COM5未被脚本占用 |

当前ESP32仍是测试版，启动时会提示可靠性故障注入已启用。不得把该构建作为生产固件。

## 3. 已实现的软件范围

### ESP32网关

| 目录/组件 | 归档内容 |
| --- | --- |
| `firmware/esp32_gateway/main` | 硬件容量检查、NVS、组件初始化和显式启动控制 |
| `components/firmware_store` | 128字节Manifest、Header CRC32、镜像CRC32、身份和长度校验、分区读取 |
| `components/transport_uart` | UART1、GPIO17/18、115200、互斥事务、RTU静默收帧和超时 |
| `components/upgrade_client` | HELLO至ABORT的协议客户端、5次重试、Offset同步和测试故障钩子 |
| `components/upgrade_manager` | FreeRTOS升级任务、Session管理、擦除轮询、4 KiB检查点续传、激活和APP回探 |
| `components/gateway_console` | `firmware`、`upgrade`以及测试构建专用`test fault`命令 |
| `components/reliability_test` | 默认关闭的一次性软件故障注入 |
| `partitions.csv` | `stm_fw=0x210000/0x120000`、`gateway_log`和`coredump`分区 |

### STM32与主机工具

| 范围 | 归档内容 |
| --- | --- |
| STM32 APP | 版本2、APP区`0x08020000/0xE0000`、ENTER_BOOT服务 |
| STM32 Bootloader | Modbus RTU升级服务、Flash保护、Metadata Journal、4 KiB检查点和幂等DATA |
| `firmware_package.py` | 生成ESP32 `stm_fw`本地包并执行离线解析校验 |
| `run_m7_stability.py` | 正常升级循环、状态/版本核对和日志SHA-256 |
| `run_m7_reliability.py` | R10～R15软件故障注入、逐项分析和JSON/Markdown报告 |
| `run_m7_guided.py` | R05、R06、R09的进度触发、人工动作提示、自动恢复和报告 |

M7上电后不会自动升级；只有显式执行`upgrade start`才可能发送ENTER_BOOT、ERASE和DATA。

## 4. M7期间发现并修复的问题

1. **ACTIVATE复位边界误重试**：ACTIVATE改为只发送一次，应答不确定时静默1秒并以
   APP HELLO/GET_INFO和版本匹配作为最终判据，避免重试命令落到已启动APP后返回BUSY。
2. **STM32复位时无效响应过快耗尽重试**：协议解码失败和响应不匹配后增加250毫秒
   退避，使5次尝试覆盖约1秒复位窗口；R02当前任务可自动完成检查点重同步。
3. **Metadata Journal空间耗尽**：允许在保留最新有效记录的前提下整理
   `PENDING_BOOT(6)`，继续禁止在ERASING、RECEIVING和VERIFYING等活动写入状态整理；
   增加目标自测，APP版本提升到2。该修复消除了连续升级后ENTER_BOOT返回
   `FLASH_ERROR(10)`的问题。
4. **非交互串口ANSI同步**：Python自动化客户端处理ESP-IDF linenoise的`ESC[6n`
   查询，并只接受完整命令提交后的最终提示符，避免首字符丢失和大包CRC校验期间过早返回。

## 5. 板上验证汇总

| 用例 | 结果 | 核心证据 |
| --- | --- | --- |
| R01 测试版基线 | PASS | 3/3完整升级成功，故障项均为off |
| 正常稳定性 | PASS | 10/10，日志SHA-256=`8228CED9...40836` |
| R02 STM32复位 | PASS | Session=`0x412011A5`，`84608 -> 81920`检查点重同步，当前任务恢复 |
| R03 ESP32复位 | PASS | Session=`0x0842160F`保持，首条恢复进度245760，无实际重擦除 |
| R04 双端掉电 | PASS | Session=`0xD89D0988`、`0xF6666D96`分别从110592、421888检查点恢复 |
| R05-A 短UART中断 | PASS | Session=`0xE1ABAE42`，当前DATA重试恢复 |
| R05-B 长UART中断 | PASS | Session=`0xDFB9C6C7`，5次超时后FAILED，恢复后同Session续传成功 |
| R06 ABORT | PASS | 旧Session=`0xB2CDF3AE`进入FAILED(8)，新Session=`0xEF267165`全量恢复 |
| R07 本地坏包 | **DEFERRED** | 未执行，不得写为PASS |
| R08 ACTIVATE边界 | PASS | 由R14的有效ACTIVATE应答完全丢失场景直接覆盖 |
| R09 冷启动10轮 | **DEFERRED** | 未执行，不得写为PASS |
| R10 DATA ACK丢失 | PASS | 同DATA重发并成功，Session=`0x28AD346F` |
| R11 重复DATA | PASS | 幂等接受且Offset不重复推进，Session=`0xDF65E8D8` |
| R12 Gap Offset | PASS | BAD_OFFSET后按设备Offset重同步，Session=`0xF68B4A6C` |
| R13 错误传输CRC | PASS | VERIFY_FAILED且未ACTIVATE，正确CRC以新Session恢复 |
| R14 ACTIVATE ACK丢失 | PASS | 不重发ACTIVATE，通过APP回探成功 |
| R15 指定次数超时 | PASS | 4次恢复；5次失败后保持同Session续传 |

详细步骤和判据见[`m7_reliability_verification.md`](m7_reliability_verification.md)，
R02～R06的独立证据见同目录对应的`m7_rXX_test_evidence.md`。

## 6. 构建与主机回归

- STM32 APP：ARM Compiler 5.06 update 7，0 Error、0 Warning；Code=12356，
  RO-data=448，RW-data=88，ZI-data=6992。
- STM32 Bootloader：ARM Compiler 5.06 update 7，0 Error、0 Warning；Code=15898，
  RO-data=450，RW-data=92，ZI-data=7068。
- ESP32生产版：ESP-IDF v5.5.4构建成功，应用`0x47460`。
- ESP32可靠性测试版：ESP-IDF v5.5.4构建成功，应用`0x48250`。
- 当前Python回归：29/29通过，覆盖固件包、协议编解码、稳定性报告解析、可靠性用例
  解析、引导测试和ANSI控制台同步。
- 完整文件哈希见[`m7_archive_manifest.md`](m7_archive_manifest.md)。

## 7. 暂缓项与完整验收差距

以下工作明确延期，而不是失败：

1. R07：本地镜像CRC、Header CRC、Product/Hardware和长度错误的板上预发送拦截。
2. R09：ESP32与STM32同时完全断电10秒后上电，连续10轮包保持和APP探测。
3. 恢复ESP32生产版配置，确认控制台不再注册`test`命令。
4. 生产版恢复后再完成一次`firmware validate -> upgrade start -> status -> probe`基线。
5. M7当前改动尚未提交；最新Git提交仍为M6基线`a15bfc7`。

因此不能宣称“M7全部可靠性验收关闭”，但可以宣称“M7本地UART升级主链路及已执行的
可靠性用例通过，阶段成果已归档”。

## 8. 后续恢复工作的安全顺序

1. 阅读本归档、哈希清单和可靠性验证方案，确认板上仍为APP版本2及正确本地包。
2. 若继续R07，先保留正确包`build/stm32_m7_package_896k_v2.bin`及其SHA-256；任何坏包
   测试结束后必须恢复正确包并重新执行`firmware validate`和`upgrade probe`。
3. 执行R09时使用`run_m7_guided.py --case R09 --version 2 --cycles 10`，每轮真实断电
   至少10秒。
4. 全部可靠性测试完成后，从`firmware/esp32_gateway`烧录生产构建；只执行普通
   `flash`，禁止`erase-flash`，以免清除`stm_fw`。
5. 生产版必须确认`test fault`不可用、本地包有效、APP版本2可探测，并完成一次基线升级。
6. 复核工作区后建立独立M7检查点提交；提交前不得把`build/`中临时产物误认为已纳入Git。

## 9. 归档入口

- 文档导航：[`README.md`](README.md)
- M7主链路记录：[`m7_verification.md`](m7_verification.md)
- M7可靠性矩阵：[`m7_reliability_verification.md`](m7_reliability_verification.md)
- 产物与证据哈希：[`m7_archive_manifest.md`](m7_archive_manifest.md)
- 文档与日志归档包：`build/m7_document_archive_20260901.zip`，SHA-256=
  `9137DEC9D34D42D8C1AA877F017B9E6286AA7D8998960CF9376BA09DEED24F06`
- 环境记录：[`../environment.md`](../environment.md)
- 协议规范：[`protocol.md`](protocol.md)
- 固定参数：[`project_constants.md`](project_constants.md)
