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
| 单包固件数据 | 224 bytes |
| 字节序 | 多字节升级字段使用little-endian；Modbus CRC按标准低字节先发送 |
| 最大重试次数 | 5 |
| Metadata检查点 | 4096 bytes |
| CAN逻辑块 | 192或256 bytes，待CAN阶段实测冻结 |

Modbus RTU ADU最大为256字节。升级请求在功能码后的Data字段中预留子命令、版本、Session、Sequence、Offset和长度字段，因此固件数据固定为224字节，不使用原方案中的512字节。

## USART1复用规则

1. 开发日志构建中，APP和M1/M2 Bootloader可以输出文本，但不能同时连接或运行协议主站。
2. 升级协议联调和正式构建中，APP与Bootloader都必须关闭裸`printf`，USART1由Modbus RTU独占。
3. 正式Bootloader不主动发送文本或Modbus帧，只响应主站请求。
4. 协议模式中的诊断信息写入内存日志缓冲区，由主站通过`GET_LOG`查询。
5. 文本构建和协议构建通过编译配置明确区分，不能靠解析器猜测一段字节是日志还是固件。
