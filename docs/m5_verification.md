# M5协议编解码与PC单元测试记录

## 当前状态

- 状态：完成，可以进入M6。
- 实现日期：2026-08-29。
- M5代码为纯C，不依赖STM32 HAL、ESP-IDF、Windows串口API或动态内存。
- 本阶段没有连接USART1、没有操作开发板Flash，也没有实现PC串口升级工具。

## 实现内容

1. Modbus CRC16，初值`0xFFFF`、多项式`0xA001`。
2. 4～256字节Modbus RTU ADU编码、解码和异常响应。
3. 自定义功能码`0x41`及18字节升级头。
4. 请求/响应编解码，所有升级多字节字段使用little-endian。
5. 224字节Payload限制和Payload实际长度一致性检查。
6. DATA ACK固定12字节编解码。
7. 重复、正常和超前Offset纯逻辑分类。
8. APP镜像DATA范围、对齐和最后块检查，使用减法防溢出。
9. t1.5/t3.5流式分帧、半帧丢弃、连续帧、溢出和计数器回绕处理。

## PC测试结果

环境：MinGW-w64 GCC 8.1.0。

构建参数：

```text
-std=c99 -Wall -Wextra -Werror -Wpedantic
```

执行：

```powershell
.\protocol\tests\run_tests.ps1
```

结果：

```text
[PASS] crc16_modbus
[PASS] modbus_rtu
[PASS] upgrade_protocol
[PASS] modbus_rtu_stream
4/4 suites passed
```

当前测试包含143个静态断言点，并通过循环穷举全部合法RTU Data长度和升级Payload长度，覆盖：

- `123456789 → CRC16 0x4B37`固定向量；
- 固定HELLO请求字节向量；
- 0～252 bytes每一种合法RTU Data长度；
- 256-byte最大ADU和超长拒绝；
- 错误CRC、非法地址、非法功能码和异常响应；
- 全部11个升级子命令；
- 协议版本、flags、状态码和Payload长度错误；
- 0～224 bytes每一种合法升级Payload长度，以及225-byte拒绝；
- DATA ACK、重复包、Offset空洞；
- 非最后块对齐、3-byte最后块、镜像越界及32位溢出输入；
- 单字节输入、帧间隔、字符间超时、连续帧、BUSY、输出空间不足、256-byte流缓存和时间戳回绕。

## ARM Compiler 5检查

使用ARM Compiler 5.06 update 7分别编译以下同源文件，全部通过且无警告：

- `crc16_modbus.c`
- `protocol_byte_order.c`
- `modbus_rtu.c`
- `modbus_rtu_stream.c`
- `upgrade_protocol.c`

生成对象位于忽略的`build/m5_ac5_check/`，证明协议库可以在M6加入STM32工程。

## M5验收结论

| 项目 | 结果 |
| --- | --- |
| 纯C、无平台依赖 | 通过 |
| 无动态内存 | 通过 |
| GCC严格告警构建 | 通过 |
| PC单元测试 | 4/4套件、143个静态断言点及全长度循环通过 |
| AC5交叉编译 | 5个源文件通过 |
| 最大RTU ADU | 256 bytes，已测试 |
| 最大升级Payload | 224 bytes，已测试 |
| 固定CRC和线格式向量 | 已建立 |
| 协议文档 | `docs/protocol.md` |

M5已完成并作为M6的协议基线。M6随后已完成PC串口升级工具、STM32 USART1收发、请求调度、Metadata/Flash接入和板级闭环验证；项目进入M7。M6结果见[`m6_test_evidence.md`](m6_test_evidence.md)。
