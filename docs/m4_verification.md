# M4 Metadata Journal验证记录

## 当前状态

- 状态：完成，可以进入M5。
- Metadata固定使用Sector 4，不与Bootloader或APP共用擦除扇区。
- 普通构建会在启动时扫描Metadata，并根据最新有效状态决定跳转APP或停留恢复模式。
- 自测宏默认值为0，不会在正常启动时擦除Sector 4。
- 2026-08-29完成显式板上自测；移除自测宏后系统能够自动跳转APP。

## 已实现功能

1. 76-byte固定记录和编译期尺寸检查，Sector 4容量为862条。
2. IEEE CRC32记录校验，序号由Journal自动递增。
3. Payload和CRC先写，`commit_marker`最后写，半写记录无效。
4. 扫描整个Sector，跳过损坏或未提交记录，选择序号最大的有效记录。
5. 记录镜像大小不得超过896 KiB，`received_bytes`不得超过`image_size`。
6. 只有`EMPTY`、`APP_VALID`、`CONFIRMED`允许整理扇区。
7. `RECEIVING`等活动状态空间不足时返回`BOOT_METADATA_UNSAFE_STATE`，不会擦除Journal。
8. Metadata损坏但APP向量合法时允许使用APP向量回退；升级活动状态则停留恢复模式。

## 编译验证

2026-08-29使用ARM Compiler 5.06 update 7直接完成：

| 配置 | 结果 | ROM占用 |
| --- | --- | --- |
| Bootloader普通 | 通过，0 Error、0 Warning | 8274 bytes Code，450 bytes RO-data，56 bytes RW-data |
| Bootloader debug | 通过，0 Error、0 Warning | 11728 bytes Code，588 bytes RO-data，68 bytes RW-data |
| M4 Metadata自测 | 通过，0 Error、0 Warning | 11584 bytes Code，604 bytes RO-data，68 bytes RW-data |

链接区域仍为`0x08000000～0x0800FFFF`，没有越过64 KiB Bootloader分区。

## 破坏性板上自测步骤

> 警告：该测试会擦除Metadata Sector 4。它不会主动擦除APP，但开始前仍应保留Bootloader和APP镜像。

1. 选择`stm32_bootloader_debug`。
2. 确认`FLASH_IF_SELF_TEST_ENABLE`未定义或为0。
3. 在C/C++预处理Define中临时加入`BOOT_METADATA_SELF_TEST_ENABLE=1`。
4. Rebuild并只下载Bootloader。
5. 打开USART1，115200、8N1，然后复位一次。
6. 预期输出：

   ```text
   [BOOT] WARNING: destructive Metadata self-test enabled
   [BOOT] Metadata self-test status=0, elapsed=..., HAL=0x00000000, failed=0xFFFFFFFF
   [BOOT] Metadata latest sequence=3, state=7, free=861/862
   ```

7. 成功后立即删除`BOOT_METADATA_SELF_TEST_ENABLE=1`并Rebuild。
8. 下载普通或debug Bootloader；正常启动应读取`sequence=3, state=7`并跳转APP。
9. 连续复位启动10次，确认APP、LED、VTOR和USART1正常。

`elapsed`使用`HAL_GetTick()`，同Bank Sector 4擦除期间可能暂停SysTick，因此只作软件观测，不作为真实墙钟擦除时间。

## 实测记录

| 项目 | 结果 |
| --- | --- |
| 测试日期 | 2026-08-29 |
| uVision普通Target | 通过；最新日志为0 Error、0 Warning，Code=7704、RO-data=516、RW-data=20、ZI-data=1100 |
| uVision debug自测Target | 自测固件已成功构建、下载并在板上完整运行 |
| 实际自测宏 | `BOOT_METADATA_SELF_TEST_ENABLE=1`；测试后已移除 |
| 空扇区扫描 | 通过 |
| 追加与序号递增 | 通过，最终`sequence=3` |
| 半写记录回退 | 通过 |
| 活动状态拒绝整理 | 通过 |
| CONFIRMED安全整理 | 通过，最终`state=7`、`free=861/862` |
| Bootloader和APP保护哈希 | 通过 |
| 自测状态/HAL/失败地址 | `status=0, elapsed=174 ms, HAL=0x00000000, failed=0xFFFFFFFF` |
| APP启动复验 | 移除自测宏后自动跳转APP，正常 |
| 重复启动回归 | M3恢复后已有连续10次复位通过；M4后如需完整重复留证可再补测，不阻塞M5 |

M4已完成。下一阶段为M5协议编解码和PC单元测试。
