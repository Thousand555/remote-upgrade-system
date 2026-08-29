# M3 Flash驱动验证记录

## 当前状态

- 状态：完成，可以进入M4。
- Flash驱动、地址保护、Sector计算、写后读回和显式自测入口已实现。
- Bootloader普通、debug和自测配置已经通过ARM Compiler 5直接编译并完整链接。
- 2026-08-29完成破坏性板上擦写测试；APP恢复后连续复位启动10次均正常。

## 自动保护

1. 公共接口只接受APP相对offset，不能传入Bootloader或Metadata绝对地址。
2. 范围判断使用减法，避免32位地址加法溢出。
3. 擦除范围固定为Sector 5～11，并按镜像大小缩小实际范围。
4. offset必须4字节对齐；非最后块长度必须是4的倍数。
5. 只有最后块允许用`0xFF`补齐最后一个32-bit Word。
6. 写入每个Word后立即读回；完整块随后逐字节验证。
7. 成功解锁Flash后，成功和错误路径都会重新锁定。
8. 自测宏默认值为0，不会在正常启动时擦除APP。

## uVision构建说明

Codex受限命令行环境启动uVision时，RTE曾在编译前报告无法访问以下Pack：

- `Keil.STM32F4xx_DFP.2.17.1`
- `ARM.CMSIS.4.5.0`

这不属于源码编译错误。开发者已在uVision GUI中分别完成以下Target的Rebuild：

1. `stm32_bootloader`
2. `stm32_bootloader_debug`

两个Target均为0 Error、0 Warning，Pack问题不再阻塞项目。

## 破坏性板上测试步骤

> 警告：该测试会擦除APP Sector 5。开始前必须保留可通过DAP恢复的APP镜像。

1. 在`stm32_bootloader_debug`的C/C++预处理Define中临时加入`FLASH_IF_SELF_TEST_ENABLE=1`。
2. Rebuild并只下载Bootloader。
3. 打开USART1，配置为115200、8N1。
4. 复位一次，确认先看到破坏性测试警告。
5. 自测将先执行边界、溢出和对齐拒绝检查，再擦除Sector 5。
6. 自测写入并校验256-byte序列，再写3-byte最后块验证`0xFF`补齐，同时比较Sector 0～4测试前后的哈希。
7. 成功时应输出`status=0`、实际耗时、`HAL=0x00000000`和`failed=0xFFFFFFFF`。
8. 记录测试结果后立即删除`FLASH_IF_SELF_TEST_ENABLE=1`，避免每次复位重复擦除。
9. 恢复普通Bootloader和APP镜像。
10. 再次执行M2的10次冷启动、VTOR、LED和USART1检查。

## 实测记录

| 项目 | 结果 |
| --- | --- |
| 测试日期 | 2026-08-29 |
| Bootloader普通目标 | uVision Rebuild通过，0 Error、0 Warning |
| Bootloader debug目标 | uVision Rebuild通过，0 Error、0 Warning |
| Sector边界检查 | 通过 |
| 256-byte写入/读回 | 通过 |
| 3-byte最后块补齐 | 通过 |
| 自测输出 | `status=0, elapsed=19 ms, HAL=0x00000000, failed=0xFFFFFFFF` |
| 擦除耗时 | `HAL_GetTick()`观测为19 ms；同Bank擦除可能暂停SysTick，不作为真实墙钟擦除时间 |
| HAL错误码 | `0x00000000` |
| 失败地址 | `0xFFFFFFFF`，无失败地址 |
| Sector 0～4保持不变 | 通过，自测前后保护区哈希一致 |
| APP恢复及启动复验 | APP已恢复，连续复位启动10次均正常 |
| 破坏性自测开关 | 已从Keil Target移除，源码默认值为0 |

M3已完成。建议提交：`feat(boot): add bounded flash interface and self-test`。
