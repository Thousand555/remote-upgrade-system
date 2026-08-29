# M2 Bootloader跳转验证记录

## 当前结论

- 状态：完成。
- 初次记录日期：2026-08-27；M3恢复后复验日期：2026-08-29。
- 硬件结论来源：开发者已确认开发板上电后可自动运行，M2阶段硬件基本正常工作。
- M3破坏性自测后重新下载APP，连续复位启动10次均正常，Bootloader跳转路径复验通过。

## 已核对的静态证据

| 项目 | 结果 |
| --- | --- |
| MCU | STM32F407ZGT6，1 MiB Flash |
| Bootloader链接范围 | `0x08000000～0x0800FFFF` |
| Metadata预留范围 | `0x08010000～0x0801FFFF` |
| APP链接范围 | `0x08020000～0x080FFFFF` |
| Bootloader/APP构建 | M2原构建记录为0 Error、0 Warning；本次又使用AC5直接编译和链接通过 |
| APP向量检查 | MSP位于SRAM/CCM且8字节对齐；Reset Handler位于APP范围并带Thumb位 |
| USART1日志策略 | 普通目标默认关闭；debug目标启用；协议阶段禁止裸文本 |

## M2归档前的板上留证

以下硬件细节仍应随最终硬件文档补齐，不阻塞M4：

1. 保存一次完整串口记录：Bootloader启动、APP地址、跳转提示、APP版本和VTOR。
2. 已完成连续10次复位启动；如果本次使用的是NRST或Keil Reset而非完全断电，最终可靠性测试仍需补做断电冷启动。
3. 确认APP中的`SCB->VTOR`为`0x08020000`，LED与SysTick持续正常。
4. 记录BOOT0实际电平、跳帽位置、开发板完整型号和NRST连接。
5. 建议在通过后创建Git标签：`m2-hardware-verified`。

## 与M3的恢复关系

M3板上自测会擦除APP的Sector 5。测试前必须保留可恢复的APP镜像；测试完成后重新下载APP，并再次执行本文件中的启动检查。
