# M7 R05 UART中断测试证据

## 测试环境

- ESP32控制台：`COM5 @ 115200`
- STM32升级包：版本2，917504字节，CRC32=`0x1AEDDB50`
- 中断线路：仅断开STM32 PA9到ESP32 GPIO18的返回线
- ESP32到STM32发送线、供电和GND始终保持连接

## R05-A 短时中断

- Session：`0xE1ABAE42`
- 返回线断开约2秒。
- ESP32观察到DATA第1次超时和第2次残帧解码失败。
- 重试窗口内恢复后，当前任务继续运行，没有重新启动升级。
- 最终完成917504/917504字节并发现APP版本2。

结论：短时返回线中断可由当前DATA事务自动重试恢复，R05-A PASS。

## R05-B 长时中断

- Session：`0xDFB9C6C7`
- 脚本触发进度：65536字节。
- 实际返回线中断时间：16.199秒。
- DATA第1～5次全部返回`ESP_ERR_TIMEOUT`，当前任务以
  `ESP_ERR_TIMEOUT/device status 0`进入`FAILED`。
- 失败状态进度：93472/917504字节。
- 失败后探测到STM32 Bootloader `RECEIVING(4)`。
- 接线恢复后重新执行升级，Session仍为`0xDFB9C6C7`；ERASE阶段没有重新创建或擦除
  现有会话，首条恢复进度为94208/917504字节。
- 最终完成917504/917504字节，状态为`SUCCESS/ESP_OK/device status 0`，并发现APP版本2。

结论：超过重试窗口的返回线中断会使当前网关任务明确失败；线路恢复后可以沿用STM32
持久化Session继续传输，不会激活不完整镜像，R05-B PASS。

## 留档

- 原始日志：`build/m7_r05b_result.txt`
- 自动分析报告：`build/m7_r05b_report.md`
- 原始日志SHA-256：
  `A69D95C6144DC94A8FB07EC14598700F1B2EC979A030C8F5FBBE8D3A8A34A0E6`
