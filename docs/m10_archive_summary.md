# M10阶段归档总览

## 归档结论

- 归档日期：2026-09-02（Asia/Shanghai）。
- 阶段状态：`PHASE COMPLETE`。
- 范围：ESP32-S3通过经过服务器身份验证的HTTPS获取M8固件发布物，复用M9安全缓存，
  并通过既有UART链路完成STM32升级。
- 未包含：固件数字签名、防降级、Secure Boot、Flash Encryption、双向TLS。这些不能由
  HTTPS或SHA-256完整性校验替代。

## 实现范围

| 模块 | M10内容 |
| --- | --- |
| `gateway_wifi` | 默认只接受HTTPS地址；冷启动后通过SNTP取得证书有效期校验所需时间 |
| `firmware_downloader` | 内嵌开发CA、证书链及主机名/IP校验、关闭自动重定向、保留Range续传与安全缓存提交 |
| TLS开发工具 | 生成开发CA和SAN服务器证书、TLS启动M8 API、验证受信/不受信链和Range响应 |
| 负向测试工具 | 使用隔离临时CA在8444端口启动不受信服务，不修改固件内嵌CA |

## 验收证据

| 用例 | 结果 | 核心证据 |
| --- | --- | --- |
| PC受信HTTPS与Manifest | `PASS` | 健康检查、Manifest与ETag通过 |
| PC HTTPS Range/If-Range | `PASS` | `206`、4096字节、Content-Range匹配 |
| PC未受信证书拒绝 | `PASS` | 未安装开发CA时TLS失败 |
| ESP32 HTTPS正式包下载 | `PASS` | `READY`、16988/16988字节 |
| ESP32缓存校验 | `PASS` | 版本2、镜像12892字节、CRC32=`CF885C9E` |
| HTTPS到STM32升级 | `PASS` | Session=`0xC48A1EF3`、`SUCCESS/ESP_OK/0`、12892/12892字节 |
| STM32 APP回探 | `PASS` | Application=2、`boot_state=6` |
| 错误CA拒绝 | `PASS` | TLS `-0x2700`、`FAILED/ESP_ERR_HTTP_CONNECT`、0/0字节 |
| 错误CA后的缓存保护 | `PASS` | 原缓存仍通过完整校验 |

## 归档构建与回归

| 项目 | 结果 |
| --- | --- |
| ESP-IDF | v5.5.4，Target=`esp32s3`，项目版本`0.2.0` |
| ESP32应用 | 974112字节（`0xEDD20`），factory分区剩余54% |
| ESP32应用SHA-256 | `4FB03A428C07B08FAB40666C2C732B01B71F98F6A89DAA82CCE7959741FACA28` |
| C协议回归 | 4/4 `PASS` |
| Python工具回归 | 33/33 `PASS` |
| M8/M9服务端回归 | 10/10 `PASS` |

## 已知边界与后续阶段

- 当前局域网NTP存在偶发超时；SNTP成功后下载正常。生产环境应提供可靠、受控的时间源，
  并设计可信时间持久化或安全引导策略。
- 开发CA私钥只保存在Git忽略的`server/certs/`，正式部署必须建立CA托管、轮换和吊销流程。
- `boot_state=6`是尚未实现APP启动确认闭环时的预期状态。M11负责APP自检确认、
  `CONFIRMED`状态和异常恢复。
- 关键文件与交付哈希见[`m10_archive_manifest.md`](m10_archive_manifest.md)。
