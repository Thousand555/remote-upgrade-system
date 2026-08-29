# Upgrade Protocol Version 1

## 1. 范围与分层

UART和RS485使用标准Modbus RTU帧承载项目自定义升级服务。PC或ESP32是主站，STM32是从站。CAN阶段只复用子命令、状态码、Session和Offset语义，不直接封装Modbus RTU字节帧。

```text
Modbus RTU ADU
└─ address + function 0x41 + upgrade data + CRC16
                              └─ subfunction/session/sequence/offset/payload
```

Modbus没有标准固件升级功能；本项目只复用标准RTU寻址、请求/响应模型和CRC16，升级Data属于自定义协议。

## 2. 固定参数

| 参数 | 值 |
| --- | --- |
| 协议版本 | 1 |
| 从站地址 | 1～247；第一块板默认1 |
| 正常功能码 | `0x41` |
| Modbus异常功能码 | `0xC1` |
| 最大Modbus RTU ADU | 256 bytes |
| 升级头长度 | 18 bytes |
| 最大升级Payload | 224 bytes |
| 最大升级ADU | 246 bytes |
| 多字节升级字段 | little-endian |
| Modbus CRC16 | Polynomial `0xA001`，初值`0xFFFF`，低字节先发送 |
| UART | USART1，115200、8N1 |
| t1.5 | 750 us |
| t3.5 | 1750 us |
| 请求flags | 版本1固定为0 |

## 3. Modbus RTU外层

```text
┌─────────┬──────────┬──────────────┬───────────────┐
│ address │ function │ data         │ CRC16         │
│ 1 byte  │ 1 byte   │ 0..252 bytes │ low, high     │
└─────────┴──────────┴──────────────┴───────────────┘
```

通用RTU编解码器允许4～256字节ADU。升级编解码器额外要求功能码为`0x41`、Data至少18字节，并限制升级Payload不超过224字节。

非法功能码或无法解释的PDU可返回Modbus异常响应；Session、Sequence、Offset、产品、版本、Flash和镜像校验错误使用正常功能码`0x41`加升级状态码返回。

## 4. Upgrade Data布局

| Data偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | subfunction | 升级子命令 |
| 1 | 1 | protocol_version | 固定为1 |
| 2 | 2 | flags_or_status | 请求为flags；响应为状态码 |
| 4 | 4 | session_id | 本次升级会话ID |
| 8 | 4 | sequence | 请求序号/确认序号 |
| 12 | 4 | offset_or_next_offset | 数据Offset或下一期望Offset |
| 16 | 2 | payload_length | 0～224，必须等于实际Payload长度 |
| 18 | N | payload | 子命令数据 |

不要将接收缓冲区强制转换成C结构体。实现必须逐字节读写多字节字段，以避免编译器填充、CPU字节序和未对齐访问问题。

## 5. 子命令

| 值 | 名称 | 用途 |
| ---: | --- | --- |
| `0x01` | HELLO | 探测升级服务 |
| `0x02` | GET_INFO | 查询设备、硬件和固件信息 |
| `0x03` | ENTER_BOOT | 请求APP记录升级意图并复位 |
| `0x10` | START | 创建升级Session并检查镜像参数 |
| `0x11` | ERASE | 启动APP区擦除 |
| `0x12` | DATA | 发送固件数据块 |
| `0x13` | QUERY_PROGRESS | 查询擦除或接收进度 |
| `0x14` | VERIFY | 请求整包校验 |
| `0x15` | ACTIVATE | 激活并启动新APP |
| `0x16` | ABORT | 中止当前Session |
| `0x20` | GET_LOG | 查询内存诊断日志 |

M5冻结并编解码通用消息头；M6已冻结下面的命令Payload并接入STM32状态机。

## 6. 状态码

| 值 | 状态 | 含义 |
| ---: | --- | --- |
| 0 | OK | 成功 |
| 1 | BAD_FRAME | 帧格式错误 |
| 2 | BAD_CRC | CRC错误 |
| 3 | BAD_SESSION | Session不匹配 |
| 4 | BAD_SEQUENCE | Sequence不匹配 |
| 5 | BAD_OFFSET | Offset错误 |
| 6 | BAD_IMAGE_SIZE | 镜像大小非法 |
| 7 | BAD_PRODUCT | 产品不匹配 |
| 8 | BAD_HARDWARE | 硬件版本不匹配 |
| 9 | VERSION_REJECTED | 固件版本被拒绝 |
| 10 | FLASH_ERROR | Flash操作失败 |
| 11 | VERIFY_FAILED | 整包验证失败 |
| 12 | BUSY | 操作仍在进行 |
| 13 | TIMEOUT | 操作超时 |

正常响应必须回显请求的`subfunction`。版本1响应中的`flags_or_status`只能取上述0～13。

## 7. DATA与ACK

固件块固定不超过224字节。Offset必须4字节对齐；非最后块长度必须是4的倍数；最后块可以不足4字节。范围检查使用`payload_length <= image_size - offset`，禁止使用可能溢出的`offset + payload_length`作为唯一判断。

DATA分类规则：

```text
received_offset == next_expected_offset → ACCEPT_NEW
received_offset <  next_expected_offset → DUPLICATE，不重复写Flash
received_offset >  next_expected_offset → REJECT_GAP，返回正确Offset
```

DATA ACK Payload固定为12字节：

| Payload偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 2 | status |
| 2 | 2 | reserved，版本1为0 |
| 4 | 4 | accepted_sequence |
| 8 | 4 | next_expected_offset |

主站第一版使用停止等待：发送一个DATA块，收到ACK后再发送下一块。ACK丢失时允许重发同一Offset，STM32必须返回相同进度而不能重复写Flash。

## 8. 流式接收

`modbus_rtu_stream`接受单字节及其微秒时间戳：

- 字节间隔不超过t1.5：继续当前帧。
- 间隔大于t1.5但小于t3.5：丢弃此前半帧，并从当前字节重新开始。
- 间隔大于等于t3.5：此前帧完成，当前字节作为下一帧首字节。
- 没有后续字节时，由周期性`poll()`在t3.5后完成帧。
- 超过256字节时报告OVERFLOW并丢弃当前帧。
- 时间戳使用无符号减法，支持一次`uint32_t`计数器回绕。

分帧器只识别RTU静默间隔，不判断CRC；取出完整候选帧后再调用RTU和升级解码器。

## 9. 固定字节示例

地址1的空Payload HELLO请求：

```text
01 41 01 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 31 95
```

其中最后`31 95`是Modbus CRC的低、高字节。

Session=`0x12345678`、Sequence=5、下一Offset=224的DATA成功响应示例：

```text
01 41 12 01 00 00 78 56 34 12 05 00 00 00 E0 00 00 00
0C 00 00 00 00 00 05 00 00 00 E0 00 00 00 B9 0F
```

## 10. M6接入约束

- 协议模式必须使用`LOG_ENABLE=0`，USART1不得混入文本。
- UART ISR/DMA只负责收字节和时间戳，不在中断里解析、擦除或写Flash。
- 完整帧交给协议层后，M6命令处理器再操作Metadata和Flash。
- STM32作为从站只响应主站请求，不主动发送日志帧。
- ERASE采用一次请求一次响应，耗时操作通过QUERY_PROGRESS轮询。

## 11. M6命令Payload

所有字段继续使用little-endian。响应的通用消息头`flags_or_status`是命令执行状态；DATA响应Payload内再次携带状态，是为了让ACK可以单独解析和交叉核对。

### 11.1 HELLO响应：8 bytes

| Offset | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 2 | capabilities |
| 2 | 2 | max_payload_size，固定224 |
| 4 | 4 | service_version |

能力位：bit0为Bootloader服务、bit1为APP可执行ENTER_BOOT、bit2为断点恢复、bit3为CRC32校验。

### 11.2 GET_INFO响应：24 bytes

| Offset | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 2 | product_id |
| 2 | 2 | hardware_id |
| 4 | 4 | bootloader_version |
| 8 | 4 | application_version |
| 12 | 4 | application_base |
| 16 | 4 | application_max_size |
| 20 | 2 | boot_state |
| 22 | 2 | capabilities |

### 11.3 START请求：48 bytes

| Offset | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 4 | firmware_version |
| 4 | 4 | image_size |
| 8 | 4 | image_crc32 |
| 12 | 32 | image_sha256 |
| 44 | 2 | product_id |
| 46 | 2 | hardware_id |

M6保存SHA-256但不据此进行安全认证；M6的整包验收依据是CRC32。签名和SHA-256认证在后续安全阶段实现。

### 11.4 QUERY_PROGRESS响应：16 bytes

| Offset | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 2 | boot_state |
| 2 | 2 | reserved，固定0 |
| 4 | 4 | received_bytes |
| 8 | 4 | image_size |
| 12 | 4 | error_code |

DATA的`offset`是续传与幂等判断的唯一进度依据；`sequence`用于请求/响应关联和ACK回显，M6不依赖sequence恢复断点。

## 12. M6擦除时序说明

ERASE成功受理后先返回`BUSY`，等待USART1发送完成，再执行阻塞式Sector擦除。STM32F407单Bank Flash擦除期间可能无法响应查询，因此PC允许单次QUERY超时，但以60秒墙钟总超时为失败判据。擦除完成后状态变为`RECEIVING`；同一Session重复ERASE在该状态下只返回成功，不再次擦除。
