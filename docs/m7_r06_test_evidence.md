# M7 R06 ABORT恢复测试证据

## 测试环境

- ESP32控制台：`COM5 @ 115200`
- STM32升级包：版本2，917504字节，CRC32=`0x1AEDDB50`
- 故障动作：ESP32在TRANSFER阶段主动执行`upgrade abort`

## ABORT阶段

- 初始探测发现APP版本2，Product/Hardware=`0x0001/0x0001`。
- 原Session：`0xB2CDF3AE`。
- 脚本在日志进度达到65536/917504字节后发送ABORT。
- ABORT命令被控制台接受，当前任务以
  `ESP_ERR_INVALID_STATE/device status 0`进入`FAILED`。
- 失败状态记录的实际进度为66880/917504字节。
- `upgrade status`中的`Remote boot state: 4`是网关在ABORT前保存的最后通信状态；
  随后的实时`upgrade probe`发现STM32 Bootloader，能力值`0x000D`、`boot_state=8`，
  证明ABORT已经持久化为FAILED状态。

## 恢复阶段

- 再次执行`upgrade start`后创建新Session：`0xEF267165`，未复用失败Session。
- 状态机重新经过START和ERASE；ERASE约8.9秒后进入TRANSFER，首条进度从4096字节
  开始，证明旧失败会话没有被当作可续传会话使用。
- 最终完成917504/917504字节，经过VERIFY、ACTIVATE和WAIT_APP。
- 最终状态：`SUCCESS/ESP_OK/device status 0`。
- 最终探测：APP版本2，Product/Hardware=`0x0001/0x0001`，APP区域
  `0x08020000/0x000E0000`。

结论：人工ABORT会终止并持久化当前会话，未完成镜像不会被激活；后续升级使用新Session
重新擦除并完整恢复，R06 PASS。

## 留档

- 原始日志：`build/m7_r06_result.txt`
- 自动分析报告：`build/m7_r06_report.md`
- 原始日志SHA-256：
  `4947F26A8C613956A369304781854CC35D2A3FFB669515145BD91AFD6B701B9E`
