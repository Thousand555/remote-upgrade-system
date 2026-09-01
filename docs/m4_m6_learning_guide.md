# M4～M6 代码吸收与面试准备指南

## 1. 学习目标

这份指南的目标不是让你记住所有代码，而是让你最终能够独立完成下面五件事：

1. 不看代码画出 Metadata 状态机、协议分层和完整升级时序。
2. 从一条终端日志反查到 STM32 和 PC 两端对应的函数。
3. 解释每个关键设计解决了什么故障，以及为什么没有采用更简单的做法。
4. 修改一个小行为、补一条测试，并预判修改会影响哪些模块。
5. 面试时能诚实说明当前实现、实测证据、取舍和未完成项。

“能看懂”不是吸收完成。至少满足以下标准才算真正掌握一个模块：

- 能用自己的话讲清原理；
- 能画出输入、输出和状态变化；
- 能找到关键函数；
- 能设计一个失败用例；
- 能在不看答案的情况下解释测试结果。

建议新建个人学习分支再做实验：

```powershell
git switch -c study/m4-m6
```

不要一开始阅读 `Drivers/` 下的 HAL 和 CMSIS 源码。当前阶段只需要知道 HAL API 的作用，不需要追到芯片库内部。

## 2. 先建立全局地图

M4～M6 不是三个孤立功能，而是一条逐层闭环的链路：

```text
PC升级工具
  tools/serial_upgrade.py
          │ USART1字节流
          ▼
UART接收环形队列 + RTU静默间隔分帧
  firmware/common/Src/uart_rtu_transport.c
          │ 完整Modbus RTU ADU
          ▼
Modbus RTU + 自定义0x41升级协议编解码
  protocol/src/*.c
          │ upgrade_message_t
          ▼
APP服务 / Bootloader命令状态机
  app_upgrade.c / boot_upgrade.c
          │ 状态记录         │ 固件数据
          ▼                  ▼
Metadata Journal          APP Flash
  boot_metadata.c           flash_if.c
```

三个阶段分别回答三个问题：

| 阶段 | 核心问题 | 一句话答案 |
| --- | --- | --- |
| M4 | 复位或掉电后，Bootloader 如何知道升级进行到哪里？ | 用 Sector 4 中只追加、不原地改写的 Metadata Journal 保存状态 |
| M5 | PC 和 STM32 如何无歧义地表达命令、会话、偏移和错误？ | 用 Modbus RTU 外层承载自定义功能码 `0x41` 的升级消息 |
| M6 | 协议怎样真正驱动 USART、Flash、复位和续传？ | 用 APP/Bootloader 两个服务和状态机把 M4、M5 接成闭环 |

## 3. 推荐的十次学习安排

每次控制在 60～90 分钟。每次必须留下一个“输出物”，只读代码不算完成。

| 次数 | 内容 | 必须留下的输出物 |
| ---: | --- | --- |
| 1 | Flash 分区、写入约束、Metadata Record | 手画分区图和 76-byte Record 布局 |
| 2 | M4 扫描、追加、提交标记 | 不看代码写出追加伪代码 |
| 3 | M4 状态机、掉电与整理 | 状态转换图和三个掉电场景答案 |
| 4 | M5 Modbus RTU 外层和 CRC16 | 手工拆解一帧 HELLO |
| 5 | M5 升级消息、Session/Sequence/Offset | 三者职责对比表和 DATA 判定伪代码 |
| 6 | M5 流式分帧与单元测试 | 新增或口述三个边界测试 |
| 7 | M6 UART Transport 和 APP 入口 | 从中断到 `ENTER_BOOT` 的调用链 |
| 8 | M6 Bootloader 正常升级状态机 | 完整命令/状态时序图 |
| 9 | M6 ACK 丢失、复位续传、CRC 失败 | 用实测日志逐行反查函数 |
| 10 | 局限、优化和模拟面试 | 90 秒项目介绍和十个追问答案 |

第一遍只掌握主路径，第二遍再看异常分支。不要试图一次记住所有枚举和错误码。

---

# 第一部分：M4 Metadata Journal

## 4. M4 要解决的根本问题

RAM 中的升级状态复位后会消失，但直接在 Flash 同一个结构体上反复覆盖也不可靠：

- STM32 Flash 擦除后位为 `1`，编程主要是把 `1` 改成 `0`；不能像 RAM 一样任意反复覆盖。
- STM32F407 按 Sector 擦除，Sector 4 是完整 64 KiB，不能只擦掉一条记录。
- 写一条记录过程中可能掉电，Flash 中会留下半条数据。
- 在接收固件时擦除 Metadata，可能同时丢失唯一的恢复进度。

因此当前实现选择 append-only Journal：每次状态变化写到下一个空槽，旧记录不修改。启动时扫描全部记录，选择序号最大的有效记录作为最新状态。

## 5. 实际 Flash 布局

定义位于 `firmware/stm32_bootloader/App/Inc/flash_layout.h`：

```text
0x08000000 ┌──────────────────────────┐
           │ Bootloader，64 KiB       │ Sector 0～3
0x08010000 ├──────────────────────────┤
           │ Metadata，64 KiB         │ Sector 4
0x08020000 ├──────────────────────────┤
           │ APP，896 KiB             │ Sector 5～11
0x08100000 └──────────────────────────┘
```

这里最重要的设计不是地址本身，而是三个区域没有共享擦除 Sector。擦除 APP 时不应碰到 Bootloader 和 Metadata；整理 Metadata 时不应碰到 APP。

代码还用预处理期 `#error` 检查三个分区是否连续、APP 是否正好结束于物理 Flash 末尾。面试时可以把它称为“把地址布局错误前移到编译期”。

## 6. 76-byte Record 怎样保证可识别

结构体位于 `boot_metadata.h`，实际持久化布局如下：

| 偏移 | 字段 | 作用 |
| ---: | --- | --- |
| 0 | `magic` | 判断是不是本项目记录 |
| 4 | `format_version`、`state` | 格式版本和升级状态 |
| 8 | `sequence_number` | 判断新旧记录 |
| 12 | `session_id` | 区分升级会话 |
| 16 | `firmware_version` | 目标固件版本 |
| 20 | `image_size` | 镜像长度 |
| 24 | `received_bytes` | 已持久化接收进度 |
| 28 | `image_crc32` | 目标整包 CRC32 |
| 32 | `image_sha256[32]` | 预留完整性/安全字段，M6 尚未用于认证 |
| 64 | `error_code` | 最近失败原因 |
| 68 | `record_crc32` | 校验前 68 bytes |
| 72 | `commit_marker` | 最后写入，表示整条记录已提交 |

`typedef char ...[(sizeof(...) == 76) ? 1 : -1]` 是 C89 风格的编译期断言。如果结构体因为字段或对齐变化不再是 76 bytes，编译会失败，避免悄悄改变持久化格式。

Sector 4 可容纳：

```text
65536 / 76 = 862条，余24 bytes
```

第 `n` 个槽地址为：

```text
0x08010000 + n × 76
```

## 7. 原子提交的关键：commit marker 最后写

`boot_metadata_write_slot()` 的顺序是：

```text
解锁Flash
→ 清错误标志
→ 写magic到record_crc32
→ 每写一个Word立即读回比较
→ 最后单独写commit_marker
→ 锁Flash
```

扫描时，一条记录只有同时满足以下条件才有效：

- magic 正确；
- 格式版本正确；
- sequence 合法；
- state、image_size、received_bytes 范围合法；
- record CRC32 正确；
- commit marker 正确。

掉电场景可以这样分析：

| 掉电时刻 | Flash 结果 | 下次启动行为 |
| --- | --- | --- |
| 新记录写入前 | 只有旧记录 | 使用旧记录 |
| Payload/CRC 写到一半 | 新槽非空但没有有效提交标记 | 跳过半写槽，使用旧记录 |
| commit marker 写完后 | 新记录完整 | 使用新记录 |

commit marker 不是为了校验内容；CRC32 才负责发现内容损坏。commit marker 负责表达“写入过程已经走到最后一步”。两者职责不同。

## 8. 扫描与追加调用链

先按下面顺序阅读，不要从文件第一行机械读到最后一行：

### 8.1 读取最新记录

```text
boot_metadata_load_latest()
└─ boot_metadata_scan()
   ├─ boot_metadata_slot_is_erased()
   ├─ boot_metadata_read_slot()
   └─ boot_metadata_is_record_valid()
```

`boot_metadata_scan()` 会扫描全部 862 个槽，而不是遇到第一个空槽就停止。这样即使中间存在半写记录或异常空洞，后面的有效记录仍有机会被找到。最新记录按最大 `sequence_number` 选择。

### 8.2 追加记录

```text
boot_metadata_append(desired, written)
├─ 扫描当前Journal
├─ 计算sequence = latest + 1
├─ 必要时执行安全整理
├─ 填magic、format、sequence
├─ 计算record_crc32
└─ boot_metadata_write_slot()
   └─ commit_marker最后写
```

调用者只提供“想写的业务状态”，Journal 层负责 magic、版本、sequence、CRC 和 commit marker，避免上层重复实现持久化细节。

## 9. 状态和“允许启动/允许整理”不是一回事

| 状态 | 允许启动 APP | 允许擦除并整理 Metadata | 原因 |
| --- | :---: | :---: | --- |
| `EMPTY` | 是 | 是 | 没有活动升级 |
| `APP_VALID` | 是 | 是 | 已有可用 APP |
| `UPDATE_REQUESTED` | 否 | 否 | 已进入升级流程 |
| `ERASING` | 否 | 否 | APP 可能正在被擦除 |
| `RECEIVING` | 否 | 否 | APP 只有部分数据 |
| `VERIFYING` | 否 | 否 | 尚未确认镜像完整 |
| `PENDING_BOOT` | 是 | 是 | 镜像已完整校验；整理只保留最新记录，不代表启动确认 |
| `CONFIRMED` | 是 | 是 | APP 已确认稳定 |
| `FAILED` | 否 | 否 | 必须留在恢复模式 |

“允许启动”由 `boot_metadata_state_allows_app_boot()` 判断；“允许整理”由 `boot_metadata_state_is_safe_to_compact()` 判断。

## 10. 为什么升级前要预留 Journal 空间

最大镜像为 896 KiB，检查点为 4 KiB：

```text
896 KiB / 4 KiB = 224个进度检查点
224 + 8条状态开销 = 232条记录
```

`boot_upgrade_handle_start()` 计算实际所需记录数；APP 侧在 `ENTER_BOOT` 前使用保守上限 `APP_UPGRADE_MAX_REQUIRED_RECORDS = 232`。

如果等到 `RECEIVING` 中途才发现空间不足，就不能安全擦除 Sector 4，因为擦除后会失去当前会话和进度。因此 `boot_metadata_compact_if_needed()` 必须在活动升级开始前执行；活动状态下空间不足应报错，而不是冒险整理。

## 11. M4 必做练习

### 练习 A：纸面推演

不看代码回答：现有最新记录 sequence=9、state=`RECEIVING`，下一个槽写到 CRC 前掉电。复位后应选择哪条记录？为什么？

正确推理：新槽没有有效 commit marker，会被跳过；仍选择 sequence=9。不能只回答“CRC 会保护”，因为真正区分未提交记录的是 commit marker 与完整合法性检查。

### 练习 B：函数复述

关掉代码，用五分钟写出 `boot_metadata_append()` 的伪代码，然后与实现比较。重点检查你是否遗漏：

- 序号耗尽；
- 已使用但无有效记录的 `CORRUPT`；
- 空间满时的安全整理；
- CRC 覆盖范围；
- commit marker 最后写。

### 练习 C：Keil 观察

只做无破坏观察：在 `boot_metadata_load_latest()`、`boot_metadata_scan()` 和 `boot_metadata_is_record_valid()` 设置断点，观察最新记录的 `sequence_number`、`state`、`received_bytes`。

不要为了学习再次开启破坏性自测宏；M4 板测已经有归档结果。除非你已准备好重新恢复 Metadata 和 APP，才做掉电写入实验。

## 12. M4 面试追问

### 为什么不用一个结构体原地覆盖？

Flash 不能像 RAM 一样任意改写，原地更新还会让掉电破坏唯一状态。追加日志保留旧的有效记录，新记录只有完整提交后才生效。

### CRC32 和 commit marker 是否重复？

不重复。CRC32 检测内容损坏；commit marker 表示写入事务已完成。半写记录即使部分字段看似合理，也不会被当作已提交记录。

### 当前 Journal 有什么局限？

- 只有一个 Metadata Sector；安全状态整理时如果在“擦除后、重写最新记录前”断电，Journal 可能变空。
- 每次追加都会扫描 862 个槽，逻辑简单但效率不是最优。
- CRC32 不是密码学认证，不能抵御恶意篡改。

更强方案可使用双 Sector 轮换、代际标记和恢复提交，或使用外部 EEPROM/FRAM。回答局限不会削弱项目，反而体现你理解可靠性边界。

---

# 第二部分：M5 协议编解码

## 13. 先区分“标准部分”和“自定义部分”

本项目不是使用“标准 Modbus 固件升级协议”，因为 Modbus 没有统一的标准固件升级功能。

标准部分：

- RTU 地址；
- 主站请求、从站响应；
- 功能码位置；
- CRC16；
- t1.5/t3.5 静默间隔分帧。

自定义部分：

- 用户功能码 `0x41`；
- HELLO、START、DATA、VERIFY 等子命令；
- Session、Sequence、Offset；
- Manifest、进度和状态码。

面试时应准确说：“使用标准 Modbus RTU 帧承载自定义升级服务”，不要说“使用标准 Modbus 升级协议”。

## 14. 协议分层

```text
层3：命令Payload
     START Manifest / DATA bytes / Progress / ACK

层2：Upgrade Message
     subfunction + version + status + session + sequence + offset + length

层1：Modbus RTU ADU
     address + function 0x41 + data + CRC16

层0：USART1字节流
     115200 8N1，以t3.5静默间隔结束一帧
```

对应文件阅读顺序：

1. `protocol_byte_order.c`：小端读写；
2. `crc16_modbus.c`：Modbus CRC16；
3. `modbus_rtu.c`：完整 ADU 编解码；
4. `modbus_rtu_stream.c`：按时间从字节流切出 ADU；
5. `upgrade_protocol.c`：升级消息和各 Payload 编解码；
6. `protocol/tests/`：边界和固定向量。

## 15. 一帧到底有多长

升级头固定 18 bytes，最大固件 Payload 为 224 bytes：

```text
address 1
+ function 1
+ upgrade header 18
+ payload 224
+ CRC16 2
= 246 bytes
```

这没有超过 Modbus RTU ADU 的 256-byte 上限。224 同时是 4 的倍数，适合 STM32 32-bit Word 编程。

HELLO 没有 Payload，因此长度为：

```text
1 + 1 + 18 + 0 + 2 = 22 bytes
```

## 16. Upgrade Message 线格式

在完整 ADU 中，各字段偏移如下：

| ADU偏移 | 字段 |
| ---: | --- |
| 0 | Modbus address |
| 1 | function=`0x41` |
| 2 | subfunction |
| 3 | protocol version |
| 4～5 | flags/status，小端 |
| 6～9 | session_id，小端 |
| 10～13 | sequence，小端 |
| 14～17 | offset，小端 |
| 18～19 | payload_length，小端 |
| 20～ | payload |
| 最后2 bytes | Modbus CRC16，低字节在前 |

不要把接收缓冲区直接强制转换为 `upgrade_message_t *`。原因包括结构体填充、CPU 字节序、未对齐访问和协议格式演进。当前代码使用 `protocol_read_le16/32()` 和 `protocol_write_le16/32()` 逐字段处理。

## 17. 三个容易混淆的字段

| 字段 | 真正职责 | 不能替代什么 |
| --- | --- | --- |
| Session | 标识一次升级事务，防止旧升级命令混入新升级 | 不是安全令牌，不做身份认证 |
| Sequence | 关联请求和响应，DATA ACK 回显请求序号 | 当前 M6 不靠它恢复进度，也未强制拒绝重复序号 |
| Offset | 表示镜像真实字节位置，是幂等和断点续传的进度依据 | 不能只靠 Sequence 推算 |

协议虽然定义了 `BAD_SEQUENCE`，但当前 Bootloader 没有实现严格的 Sequence 窗口或防重放逻辑。面试时不要声称已经实现了序号防重放。

## 18. DATA 幂等是 M5 最重要的算法

`upgrade_classify_data_offset()` 的规则：

```text
received_offset == next_expected_offset → 新数据，接受
received_offset <  next_expected_offset → 重复数据，不盲目重写
received_offset >  next_expected_offset → 中间有缺口，拒绝并返回正确Offset
```

这解决 ACK 丢失问题：

```text
PC发送offset=4096
→ STM32写入并回复next=4320
→ ACK在链路中丢失
→ PC重发offset=4096
→ STM32识别为重复块，核对Flash内容
→ 返回当前next=4320
```

如果没有幂等设计，重试可能重复编程 Flash、推进两次进度或破坏镜像。

范围检查采用：

```c
payload_length <= image_size - offset
```

而不是只判断 `offset + payload_length <= image_size`，因为后者的加法可能发生 `uint32_t` 溢出。

## 19. RTU 流式分帧为什么需要微秒时间

USART 只提供连续字节，没有“这一帧在这里结束”的长度前导。Modbus RTU 使用静默间隔分帧。在 115200 下当前固定：

- t1.5 = 750 us；
- t3.5 = 1750 us。

`modbus_rtu_stream_feed()` 的行为：

- 间隔不超过 t1.5：继续当前帧；
- 大于 t1.5、小于 t3.5：此前半帧作废，当前字节开始新帧；
- 大于等于 t3.5：此前帧完成，当前字节是下一帧首字节；
- 没有后续字节时，`poll()` 在 t3.5 后完成当前帧；
- 超过 256 bytes：报告 overflow 并丢弃。

`timestamp_us - last_timestamp_us` 使用无符号减法，因此可以正确处理一次 32-bit 计数器回绕。

## 20. M5 必做练习

### 练习 A：手拆 HELLO

对照 `docs/protocol.md`，逐字节解释固定向量：

```text
01 41 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 31 95
```

你应能说明前 20 bytes 的每个字段，以及最后 `31 95` 为什么是低字节在前。

### 练习 B：给 DATA 分类补测试

先不改实现，自己写出三条期望：

```text
(4096, 4096) → ACCEPT_NEW
(3872, 4096) → DUPLICATE
(4320, 4096) → REJECT_GAP
```

然后在 `protocol/tests/test_upgrade_protocol.c` 中找到现有 Offset 测试，与自己的答案比较。进阶任务是补充 `offset=0xFFFFFFFC`、小镜像和最后不足 4 bytes 的用例。

### 练习 C：破坏一帧

复制 HELLO 固定向量，修改一个非 CRC 字节但不更新 CRC，预判 `modbus_rtu_decode()` 返回什么；再更新 CRC、但把版本改为 2，预判升级解码器返回什么。

这能帮助你理解“外层 CRC 正确”不等于“内层业务字段合法”。

运行回归：

```powershell
.\tools\run_tests.ps1
```

## 21. M5 面试追问

### 为什么选 Modbus RTU，不直接用 YMODEM？

项目后续需要 UART、RS485、多节点寻址、统一请求响应和远程网关调度。Modbus RTU 的地址、静默分帧和主从模型更容易复用于 RS485；YMODEM 更适合点对点文件传输。升级语义仍由自定义 `0x41` 功能码实现。

### CRC16 和镜像 CRC32 各负责什么？

CRC16 保护单个 RTU 帧，发现传输中的局部错误；CRC32 在全部 DATA 完成后校验整个 APP 镜像。二者都不提供来源认证或抗恶意篡改。

### 为什么使用停止等待，不连续发多个 DATA？

第一版优先保证状态机、幂等和断点恢复简单可靠。停止等待吞吐较低，但每个块只有一个明确的已确认 Offset。后续可增加窗口传输，但需要更复杂的缓存、乱序和重传管理。

---

# 第三部分：M6 板级升级闭环

## 22. M6 模块职责

| 文件 | 职责 | 掌握程度 |
| --- | --- | --- |
| `firmware/common/Inc/upgrade_config.h` | 两端共享节点、产品、版本、能力和超时 | 必须能解释每个常量 |
| `uart_rtu_transport.c` | USART 中断、环形队列、TIM2 时间戳、RTU 分帧 | 必须能画调用链 |
| `app_upgrade.c` | APP 中响应 HELLO/GET_INFO/ENTER_BOOT | 必须能复述 |
| `boot_upgrade.c` | 升级命令状态机，连接 Metadata 与 Flash | 核心，必须重点掌握 |
| Bootloader `main.c` | 启动判定、500 ms 恢复窗口、跳转 APP | 必须能解释主路径 |
| `flash_if.c` | APP 相对 Offset 擦写和读回校验 | 要理解边界与对齐 |
| `tools/serial_upgrade.py` | PC 主站、重试、续传、故障注入 | 必须能对应设备端行为 |

## 23. 上电后的 Bootloader 决策

Bootloader 启动后先读取 Metadata，再检查 APP 向量表：

```text
读取最新Metadata
├─ 状态允许启动 + APP向量合法
│  ├─ 初始化协议服务
│  ├─ 等待500 ms恢复窗口
│  ├─ 收到有效寻址请求 → 留在Bootloader
│  └─ 没有活动 → 跳转APP
├─ 状态不允许启动 → 留在恢复模式
└─ APP向量非法 → 留在恢复模式
```

`Boot_IsAppValid()` 检查：

- 初始 MSP 是否位于 SRAM/CCM RAM，且满足 8-byte 对齐；
- Reset_Handler 是否设置 Thumb bit；
- Reset_Handler 地址是否落在 APP 分区。

跳转前 `Boot_JumpToApp()` 会关闭中断、复位 HAL/RCC、停止 SysTick、清 NVIC enable/pending、设置 `SCB->VTOR`，最后通过汇编切换 MSP 并跳到 APP Reset_Handler。

## 24. 为什么 APP 也需要升级协议服务

设备大多数时间运行 APP。如果只有 Bootloader 会收协议，主机必须精确抢到上电后的 500 ms 窗口，远程系统很难可靠操作。

因此正式 APP 支持三个命令：

- `HELLO`：告诉主机当前是 APP 服务，能力位包含 `ENTER_BOOT`；
- `GET_INFO`：返回产品、硬件、分区和 Metadata 状态；
- `ENTER_BOOT`：预留 Journal 空间、追加 `UPDATE_REQUESTED`，回复成功后复位。

关键点是“回复成功后复位”。`app_upgrade_poll()` 只有在响应编码和发送都成功后才调用 `NVIC_SystemReset()`，否则 PC 会看到无响应，却不知道设备是否已经切换。

## 25. 一次正常升级的完整时序

```text
PC                    APP                 Bootloader       Metadata/Flash
│ HELLO                │                       │                  │
├─────────────────────>│                       │                  │
│ APP capability       │                       │                  │
│<─────────────────────┤                       │                  │
│ ENTER_BOOT(session A)│                       │                  │
├─────────────────────>│ append UPDATE_REQUESTED                 │
│ OK                   │                       │                  │
│<─────────────────────┤ reset                 │                  │
│                      └──────────────────────>│                  │
│ HELLO / GET_INFO                             │                  │
├─────────────────────────────────────────────>│                  │
│ START(session B, manifest)                   │ validate + append│
├─────────────────────────────────────────────>│ UPDATE_REQUESTED │
│ ERASE                                       │ append ERASING   │
├─────────────────────────────────────────────>│ reply BUSY       │
│<─────────────────────────────────────────────┤                  │
│                                              │ deferred erase   │
│ QUERY_PROGRESS                               │ append RECEIVING │
├─────────────────────────────────────────────>│                  │
│ DATA offset=0                                │ write APP        │
├─────────────────────────────────────────────>│                  │
│ ACK next=224                                 │                  │
│<─────────────────────────────────────────────┤                  │
│ ...每到4 KiB追加进度检查点...                │                  │
│ VERIFY                                      │ append VERIFYING │
├─────────────────────────────────────────────>│ compute CRC32    │
│                                              │ append PENDING_BOOT
│ ACTIVATE                                    │ reply OK         │
├─────────────────────────────────────────────>│ deferred reset   │
│                                              └───────> APP启动  │
```

APP 中 `ENTER_BOOT` 使用的随机 Session 与后续 START Session 可以不同。真正绑定镜像和续传的是 START 写入 Metadata 的 Session。

## 26. ERASE 为什么必须延后执行

`boot_upgrade_handle_erase()` 不直接擦除，只做两件事：

1. 追加状态 `ERASING`；
2. 设置 `s_deferred_action = BOOT_DEFER_ERASE`，返回 `BUSY`。

`boot_upgrade_poll()` 先编码并发送响应，确认 USART1 发送完成后才调用 `boot_upgrade_run_deferred_action()` 擦除 APP。

原因是 STM32F407 为单 Bank Flash。擦除同一 Bank 时 CPU 取指和中断响应可能暂停。如果先擦除，`BUSY` 响应可能无法及时发出，主机无法判断命令是否被受理。这也解释了板测中 ERASE 后短暂出现一次查询超时，随后 `QUERY_PROGRESS` 又恢复正常。

## 27. UART Transport 的中断/主循环分工

接收中断 `HAL_UART_RxCpltCallback()` 只做：

```text
读取1 byte
→ 读取TIM2微秒时间戳
→ 写入512项环形队列
→ 重新启动下一字节中断接收
```

主循环 `uart_rtu_transport_receive()` 才做：

```text
从环形队列取 byte+timestamp
→ 送入modbus_rtu_stream_feed()
→ t3.5后取出完整候选帧
→ 上层再检查CRC和业务格式
```

这样中断中没有 CRC、协议分发、Flash 擦除或写入等耗时操作。TIM2 配为 1 MHz 自由运行计数器，因为 1 ms 的 `HAL_GetTick()` 无法可靠区分 750 us 的 t1.5。

环形队列是单生产者（USART ISR）、单消费者（主循环）模型。`__DMB()` 保证先写数据再公开 head、先读数据再推进 tail 的内存访问顺序。

当前使用单字节中断，优点是简单、容易验证；缺点是 115200 下中断次数较多。更高吞吐版本可以改为 DMA + IDLE/定时器，但不是 M6 必需条件。

## 28. `boot_upgrade_poll()` 是总调度入口

一次请求的主路径：

```text
uart_rtu_transport_receive()
→ modbus_rtu_decode()：先识别地址和功能码
→ upgrade_decode_request()：解析升级头和Payload
→ upgrade_message_init(response)
→ boot_upgrade_dispatch()
→ 对应boot_upgrade_handle_xxx()
→ upgrade_encode_response()
→ uart_rtu_transport_send()
→ boot_upgrade_run_deferred_action()
```

外层 Modbus 在这里被解码两次：第一次便于先处理地址和非法功能码，第二次封装在升级协议解码器内。它有少量重复计算，但使层次和错误响应更清楚；当前吞吐下可以接受。

## 29. DATA 处理器的真正难点

`boot_upgrade_handle_data()` 不是简单的“收到就写”，而是：

```text
检查Session和RECEIVING状态
→ 检查offset、长度、4-byte对齐、最终块
→ 根据offset分类
   ├─ gap：BAD_OFFSET，返回设备真实进度
   ├─ duplicate：读回Flash，只有内容一致才ACK
   └─ new：
      ├─ 目标区是0xFF → 写Flash并读回
      └─ 目标区非0xFF → 比较已有内容，匹配则视作已写
→ 更新RAM进度
→ 跨越4 KiB或到镜像末尾时追加Metadata
→ 返回DATA ACK中的next_expected_offset
```

“目标区非 `0xFF` 时比较已有内容”是断点恢复的关键。复位可能发生在 Flash 已经写入、但下一个 Metadata 检查点尚未提交的时候。恢复后从较早检查点重发，Bootloader 不能再次把非擦除 Word 当成新数据盲目编程；它先比较内容，一致就安全前进。

如果 Metadata 追加失败，代码把 RAM 中的 `s_record` 回滚到 `previous_record`。已经写入 Flash 的数据不会消失；下次重试会走“非 `0xFF`，比较已有内容”的路径，再尝试持久化进度。

## 30. 为什么 offset 7008 复位后从 4096 恢复

这是预期行为：

```text
RAM进度：7008
最近持久化检查点：4096
复位 → RAM丢失
扫描Metadata → received_bytes=4096
```

旧的 PC 发送流程仍尝试发送 7008 之后的数据，相对设备的 4096 属于 gap，所以设备返回：

```text
BAD_OFFSET, next=4096
```

重新运行 `--resume` 后：

1. PC 用 `QUERY_PROGRESS` 读取 Session 和 4096；
2. START 检查 Session 与 Manifest 是否一致；
3. 重复 ERASE 在 `RECEIVING` 状态只返回 OK，不实际擦除；
4. 从 4096 重发；
5. 4096～7008 已写区域通过读回比较快速追平；
6. 之后继续真正写入新数据。

PC 的 `next_chunk_end()` 会主动在 4 KiB 边界截短数据块，因此检查点恰好落在 DATA 边界。例如正常 224-byte 块接近 4096 时会发送一个 64-byte 块到达 4096，而不是跨过检查点。

## 31. VERIFY、ACTIVATE 和当前未完成的确认闭环

VERIFY 只有在：

- Session 匹配；
- 状态为 `RECEIVING` 或 `VERIFYING`；
- `received_bytes == image_size`；

时才执行。它先追加 `VERIFYING`，再从 APP Flash 读取整个镜像计算 IEEE CRC32：

- 相等：追加 `PENDING_BOOT`；
- 不等：追加 `FAILED`，返回 `VERIFY_FAILED`。

ACTIVATE 只接受 `PENDING_BOOT`，发送 OK 后延迟复位。Bootloader 重启时允许 `PENDING_BOOT` 启动 APP。

当前 M6 尚未实现 APP 启动后的“我运行正常”确认，因此状态会保持 `PENDING_BOOT`。M11 才计划实现确认与异常恢复。并且当前是单 APP 分区，不是 A/B 分区；即使将来检测启动失败，也没有本地旧镜像可瞬时切回，只能依赖网关缓存或重新传输恢复，除非重新设计分区。

## 32. PC 工具应重点阅读的五处

### 32.1 `encode_message()` / `decode_message()`

它们是 C 协议实现的 Python 对照版本。`struct.pack("<BBHIIIH", ...)` 中 `<` 表示小端。

### 32.2 `UpgradeClient.transact()`

它完成发送、等待、CRC/格式检查、子命令和 Sequence 匹配，以及超时重试。故障注入中的丢 ACK 是“收到正确响应后故意丢弃”，用于稳定复现重传。

### 32.3 `ensure_bootloader()`

它以约 100 ms 的短超时轮询 HELLO，区分 APP 和 Bootloader 能力。如果连接到 APP，就发送 ENTER_BOOT 并等待 Bootloader 重连。100 ms 是为了不漏掉 500 ms 恢复窗口。

### 32.4 `next_chunk_end()`

它同时限制：最大 224 bytes、不能越过下一个 4 KiB 检查点、不能越过镜像末尾。

### 32.5 `run_upgrade()`

它是主机状态机：发现设备、GET_INFO、选择 Session、START、等待 ERASE、按 Offset 传输、VERIFY、ACTIVATE。阅读时在纸上写出每个分支对应的设备状态。

## 33. 首帧栈溢出故障应该怎样讲

实测现象：APP LED 原本闪烁；发送 HELLO 后 LED 停止，PC 一直得到 `timeout (0/2 bytes)`。这说明 RX 路径已经触发，但程序在形成响应之前失去正常运行。

根因：轮询函数曾在 1 KiB 启动栈上同时分配：

- 两个 256-byte ADU；
- 一个约 256-byte Modbus Frame；
- 两个包含 224-byte Payload 的 Upgrade Message；
- 其他局部变量和调用栈。

总占用超过栈预算，首个完整帧进入解析路径时发生栈破坏。修复是把单线程复用的请求/响应工作区改为静态 RAM。

面试回答应包含取舍：静态工作区降低栈压力，但让函数不可重入。当前主循环单线程、一次只处理一个请求，所以不可重入是可接受约束。如果未来引入 RTOS 多任务并发，需要改成任务私有上下文或受控缓冲池。

## 34. M6 必做练习

### 练习 A：日志反查代码

对下面每一行写出设备端函数和 PC 端大致位置：

```text
RX ENTER_BOOT: status=OK
RX ERASE: status=BUSY
RX DATA: status=OK, next=4096
RX DATA: status=BAD_OFFSET, next=4096
RX VERIFY: status=VERIFY_FAILED
```

答案入口分别是 `app_upgrade_handle_enter_boot()`、`boot_upgrade_handle_erase()`、`boot_upgrade_handle_data()`、`boot_upgrade_handle_verify()`，PC 端统一经过 `transact()` 和 `run_upgrade()`。

### 练习 B：只改一个小功能

在学习分支完成以下任选一项：

- 给 Python 单元测试补一个“响应 Sequence 不匹配必须拒绝”的用例；
- 给 C 测试补最后一块为 1、2、3 bytes 时的数据块合法性测试；
- 给 PC verbose 输出增加当前 Session，但不改变线协议；
- 写一个只读取并打印 HELLO 固定帧字段的小脚本。

完成后运行 `tools/run_tests.ps1`。这个练习的目的，是证明你能安全修改和验证，而不是只会复述。

### 练习 C：画两条状态路径

不看代码画出：

```text
正常：UPDATE_REQUESTED → ERASING → RECEIVING → VERIFYING → PENDING_BOOT
失败：UPDATE_REQUESTED → ERASING → RECEIVING → VERIFYING → FAILED
```

再说明每个箭头由哪个命令触发、是否会写 Metadata、是否允许启动 APP。

### 练习 D：解释实测数据

根据 `docs/m6_test_evidence.md`，回答：

- 为什么 ACK 丢失没有写坏镜像？
- 为什么复位后倒退到 4096？
- 为什么错误 CRC 后仍能恢复？
- 为什么连续 10 次升级只能说明当前样本稳定，不能证明绝对可靠？

## 35. 当前实现的边界与改进方向

这些是面试最可能深入追问的部分：

| 当前实现 | 边界 | 可演进方向 |
| --- | --- | --- |
| 单 APP 分区 | 升级时会擦掉旧 APP，没有本地 A/B 回滚 | 双分区、外部 Flash 或由 ESP32 缓存恢复 |
| CRC16 + CRC32 | 只能发现偶然错误，不能认证来源 | SHA-256 + 数字签名，安全版本计数 |
| SHA-256 已存 Metadata | M6 没有据此验签 | M10/M16 接入校验和签名 |
| 版本只要求非 0 | 当前允许降级 | 单调版本策略和防回退存储 |
| 随机 Session | 只隔离事务，不是安全凭证 | 认证会话、随机挑战或签名 Manifest |
| 单 Sector Journal | 整理窗口仍有掉电风险 | 双 Sector 代际切换和原子回收 |
| 每次追加扫描全 Sector | 简单但效率一般 | RAM 缓存尾指针，启动时全扫描恢复 |
| 单字节 USART 中断 | 中断开销较高 | DMA + IDLE/定时器分帧 |
| 停止等待 DATA | 吞吐受往返时延限制 | 滑动窗口、批量 ACK |
| 单 Bank 擦除 | 擦除期间暂时不能响应 | 主机长超时/轮询；硬件支持时用双 Bank |
| `GET_LOG` 仅有协议枚举 | 当前处理器尚未实现日志读取 | 内存环形日志 + 分页查询 |
| `BAD_SEQUENCE` 已定义 | 当前设备端未做严格序号策略 | 明确去重窗口和回放规则 |
| `PENDING_BOOT` 可启动 | 尚无 APP 确认闭环 | M11 增加看门狗、启动次数和 CONFIRMED |

不要把这些说成“系统缺陷清单”。更好的表达是：“M6 的目标是先完成可靠串口闭环；安全、回滚和性能分别在后续阶段演进，接口已经预留，但尚未冒充完成。”

---

# 第四部分：面试表达

## 36. 90 秒项目介绍模板

不要逐字背诵，按自己的真实参与程度改写：

> 我做的是 ESP32 网关加 STM32 Bootloader 的多链路远程升级系统，目前完成到 PC 通过 USART1 升级 STM32。STM32F407 的 1 MiB Flash 被分为 64 KiB Bootloader、64 KiB Metadata 和 896 KiB APP。为了抗复位，我在 Metadata Sector 中使用追加式 Journal，每条记录用 CRC32 校验并最后写 commit marker，4 KiB 保存一次进度。通信上用标准 Modbus RTU 帧承载自定义 0x41 升级服务，Session 隔离升级事务，Offset 实现 DATA 幂等和断点续传。APP 可以响应 ENTER_BOOT，Bootloader 完成擦除、分块写入、整包 CRC32 校验和激活。板上验证过 ACK 丢失、重复包、传输中复位、错误 CRC 和 10 次连续升级。当前还不是最终安全版本，数字签名、真正的启动确认和回滚会在后续阶段完成。

如果代码大量使用了 AI 辅助，建议诚实表述：

> 我使用 AI 辅助生成和审查部分代码，但分区、协议约束和验收场景由我确认；我完成了板上调试、故障复现、日志分析，并逐模块做了代码走读和测试补充。

面试官真正关心的是你能否解释、修改和验证，而不是是否逐字符手写。

## 37. 三个最好的 STAR 故事

### 故事一：首帧后 LED 停止

- Situation：无破坏探测时设备收到 HELLO 后停止闪灯，主机无响应。
- Task：判断是串口方向、协议错误还是运行时故障。
- Action：根据“收到请求后才停止”缩小到解析路径，核算局部对象和 1 KiB 栈，改为静态单线程工作区并重新构建下载。
- Result：APP/Bootloader HELLO、GET_INFO 均恢复，后续完整升级通过。

### 故事二：7008 后复位返回 BAD_OFFSET=4096

- Situation：传输中复位后旧流程收到 `BAD_OFFSET`。
- Task：区分真正的数据损坏与预期的持久化回退。
- Action：对比 RAM 进度和 4 KiB Metadata 检查点，使用相同 Session 的 `--resume`，从 4096 重发；设备对已写区读回比较而非重擦。
- Result：从 4096 续传完成，整包 CRC32 和激活通过。

### 故事三：错误 CRC 故障注入

- Situation：需要证明设备不是“收完就启动”。
- Task：验证整包校验失败时不会激活损坏镜像，并能恢复。
- Action：主机用 `--override-crc32 0` 发送错误 Manifest，观察 `VERIFY_FAILED` 和 Bootloader 留驻，再发送正确镜像。
- Result：错误镜像被拒绝，正确固件恢复成功。

## 38. 高频问题自测

请先口头回答，再回代码确认：

1. 为什么 Metadata 不能和 APP 放在同一个 Sector？
2. commit marker 为什么必须最后写？如果 commit 写完但 CRC 错了怎么办？
3. 为什么 `PENDING_BOOT` 可以安全整理，但仍不能等同于 `CONFIRMED`？
4. 224-byte Payload 是怎样从 256-byte ADU 上限推导并选择的？
5. 为什么 CRC16、CRC32 和数字签名不能互相替代？
6. Session、Sequence、Offset 分别解决什么问题？
7. ACK 丢失后为什么可以重发相同 DATA？
8. 设备进度为什么可能从 7008 回退到 4096？已写数据怎么处理？
9. 为什么 ERASE 响应要先发送，Flash 擦除要延后？
10. 为什么使用 TIM2，而不是 `HAL_GetTick()` 做 RTU 分帧？
11. 为什么 ISR 只收字节，不在中断里解析和写 Flash？
12. 静态协议缓冲区修复了什么？带来了什么限制？
13. 当前实现是否支持真正回滚？为什么？
14. 当前 SHA-256 字段发挥了什么作用？是否已经安全？
15. 如果升级时 Metadata Sector 满了，为什么不能直接擦除？
16. `BAD_SEQUENCE` 当前是否真正实现？
17. 单字节中断和停止等待对性能有什么影响？
18. 如何证明 Bootloader 和 Metadata 没被 APP 擦除？
19. 10/10 连续升级能证明什么，不能证明什么？
20. 如果进入量产，你会优先补哪三项？

推荐优先级答案：启动确认/可恢复回滚、签名与防回退、自动化掉电和负向协议测试。具体顺序应结合产品风险决定。

## 39. 最终掌握检查表

### M4

- [ ] 能画分区和 Record 字节布局。
- [ ] 能解释 CRC、commit marker、sequence 的不同职责。
- [ ] 能复述 scan、append、compact 的调用链。
- [ ] 能解释为什么活动升级中禁止整理。
- [ ] 能指出单 Sector Journal 的掉电边界。

### M5

- [ ] 能手拆 HELLO 和 DATA 帧。
- [ ] 能解释标准 Modbus RTU 与自定义升级服务的边界。
- [ ] 能区分 Session、Sequence、Offset。
- [ ] 能写出 DATA 幂等三分支。
- [ ] 能解释 t1.5/t3.5 和溢出安全范围检查。

### M6

- [ ] 能画 APP → Bootloader → APP 的完整时序。
- [ ] 能从 `boot_upgrade_poll()` 追到 Metadata/Flash。
- [ ] 能解释 deferred ERASE/RESET。
- [ ] 能完整解释 7008 → 4096 的续传过程。
- [ ] 能讲清首帧栈溢出的诊断和取舍。
- [ ] 能明确当前安全、回滚、性能方面尚未完成的内容。

全部勾选后，再进入 M7 会更稳，因为 ESP32 UART 主机本质上是在另一平台重新实现当前 Python 主机的状态机。

## 40. 第一轮现在就做什么

本轮只学习 M4，不要同时打开 M5/M6：

1. 阅读 `flash_layout.h` 和 `boot_metadata.h`。
2. 手画 Flash 分区和 76-byte Record。
3. 只按第 8 节的函数顺序阅读 `boot_metadata.c`。
4. 不看代码写一遍 append 伪代码。
5. 口头回答第 12 节三个面试问题。

完成后，用自己的话发回“Record 如何写入、掉电后如何选最新记录、什么时候允许整理”。下一轮再集中讲 M5，并根据你的表述纠正理解偏差。

## 41. 代码与验证资料索引

- M4 板测：[`m4_verification.md`](m4_verification.md)
- M5 验证：[`m5_verification.md`](m5_verification.md)
- M6 操作与验收：[`m6_verification.md`](m6_verification.md)
- M6 实测证据：[`m6_test_evidence.md`](m6_test_evidence.md)
- 协议线格式：[`protocol.md`](protocol.md)
- 固定参数：[`project_constants.md`](project_constants.md)
