# Project Constants

这些参数是当前设计基线。任何地址、帧大小或超时调整都应先修改本文件，再同步到STM32、ESP32、PC工具和协议测试。

## STM32

| 参数 | 值 |
| --- | --- |
| MCU | STM32F407ZGT6 |
| Flash容量 | 1 MiB |
| Bootloader基址 | `0x08000000` |
| Bootloader最大长度 | `0x00010000`，64 KiB |
| Metadata基址 | `0x08010000` |
| Metadata最大长度 | `0x00010000`，64 KiB |
| APP基址 | `0x08020000` |
| APP结束地址 | `0x08100000`，不包含 |
| APP最大长度 | `0x000E0000`，896 KiB |

## UART与协议

| 参数 | 值 |
| --- | --- |
| UART实例 | USART1 |
| 引脚 | PA9/TX，PA10/RX |
| 波特率 | 115200，8N1 |
| 物理复用 | 调试日志与升级协议分时共用 |
| UART/RS485帧 | Modbus RTU |
| 升级功能码 | `0x41`，用户自定义功能码 |
| 协议版本 | 1 |
| 升级Data头 | 18 bytes |
| 单包固件数据 | 224 bytes |
| 最大升级ADU | 246 bytes；通用Modbus RTU上限仍为256 bytes |
| 字节序 | 多字节升级字段使用little-endian；Modbus CRC按标准低字节先发送 |
| CRC16参数 | Modbus，初值`0xFFFF`、多项式`0xA001` |
| t1.5 / t3.5 | 115200下固定为750 us / 1750 us |
| 请求flags | 协议版本1固定为0 |
| 最大重试次数 | 5 |
| Metadata检查点 | 4096 bytes |
| Modbus节点地址 | `1` |
| 产品ID / 硬件ID | `0x0001` / `0x0001`，产品定型时必须替换 |
| Bootloader版本 | `0x00010000` |
| APP固件版本 | 单调递增`uint32_t`，`0`非法；M6暂不阻止降级 |
| Bootloader恢复窗口 | 500 ms；有效APP且无协议请求时自动跳转 |
| 普通请求超时 / 重试 | 1000 ms / 5次 |
| 擦除总超时 / 查询间隔 | 60 s / 100 ms |
| 镜像CRC32 | IEEE reflected，poly `0xEDB88320`、init/xorout `0xFFFFFFFF`，兼容Python `binascii.crc32()` |
| 微秒时间源 | TIM2，1 MHz自由运行；M6中由共享传输模块直接配置并独占 |
| CAN逻辑块 | 192或256 bytes，待CAN阶段实测冻结 |

Modbus RTU ADU最大为256字节。升级请求在功能码后的Data字段中预留子命令、版本、Session、Sequence、Offset和长度字段，因此固件数据固定为224字节，不使用原方案中的512字节。

## USART1复用规则

1. 开发日志构建中，APP和M1/M2 Bootloader可以输出文本，但不能同时连接或运行协议主站。
2. 升级协议联调和正式构建中，APP与Bootloader都必须关闭裸`printf`，USART1由Modbus RTU独占。
3. 正式Bootloader不主动发送文本或Modbus帧，只响应主站请求。
4. 协议模式中的诊断信息写入内存日志缓冲区，由主站通过`GET_LOG`查询。
5. 文本构建和协议构建通过编译配置明确区分，不能靠解析器猜测一段字节是日志还是固件。

## Flash驱动约束

| 参数 | 值 |
| --- | --- |
| 公共写入地址 | 相对`APP_BASE_ADDR`的offset，不接受绝对地址 |
| 编程粒度 | 32-bit Word，4 bytes |
| offset对齐 | 必须4字节对齐 |
| 非最后数据块 | 长度必须是4的倍数 |
| 最后数据块 | 允许不足4字节，驱动使用`0xFF`补齐最后一个Word |
| 擦除范围 | 仅Sector 5～11，按镜像长度计算实际Sector数 |
| 擦除电压范围 | `FLASH_VOLTAGE_RANGE_3`，对应当前3.3 V供电 |
| Flash自测 | `FLASH_IF_SELF_TEST_ENABLE=0`为默认；设为1会擦除Sector 5 |

## Metadata Journal

| 参数 | 值 |
| --- | --- |
| Flash区域 | Sector 4，`0x08010000～0x0801FFFF` |
| Record magic | `0x42544D44` |
| Format version | 1 |
| Commit marker | `0x434D4954`，最后一个Word写入 |
| Record大小 | 76 bytes，固定且4字节对齐 |
| Record容量 | 862条，末尾保留24 bytes不用 |
| Record校验 | IEEE CRC32，覆盖`magic`至`error_code` |
| 整理安全状态 | `EMPTY`、`APP_VALID`、`CONFIRMED` |
| Metadata自测 | `BOOT_METADATA_SELF_TEST_ENABLE=0`为默认；设为1会擦除Sector 4 |

Metadata追加先写Payload和CRC，最后写Commit marker。扫描只接受magic、版本、字段范围、CRC和Commit marker全部有效的记录；断电留下的半条记录会被跳过。启动新升级前必须按镜像大小预留足够的检查点记录，不能等到`RECEIVING`过程中才擦除Sector 4。

## STM32构建模式

| Keil目标/用途 | `LOG_ENABLE` | `FLASH_IF_SELF_TEST_ENABLE` | `BOOT_METADATA_SELF_TEST_ENABLE` |
| --- | --- | --- | --- |
| `stm32_bootloader`正式/协议构建 | 0（源码默认值） | 0（源码默认值） | 0（源码默认值） |
| `stm32_bootloader_debug`文本调试 | 1 | 0（源码默认值） | 0（源码默认值） |
| M3破坏性板上自测 | 1 | 手工设为1，测试后必须恢复为0 | 0 |
| M4破坏性板上自测 | 1 | 0 | 手工设为1，测试后必须恢复为0 |
