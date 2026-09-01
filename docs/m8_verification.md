# M8 PC固件服务器验证记录

## 当前结论

- 状态：完成，可以进入M9 ESP32网络下载。
- 验证日期：2026-09-01。
- 范围：Windows本机只读HTTP服务、M7兼容缓存包发布、Manifest、完整下载、单Range下载
  和ETag返回；本阶段不连接ESP32，不触发STM32升级。
- 发布基线：`f407-node-1.2.0`，对应STM32 APP版本码2，来源Git提交
  `1cf527e0c3caa907697a1cbbbed5a28cc5c4290f`。

## 发布物

| 项目 | 结果 |
| --- | --- |
| STM32原始镜像 | 12892 bytes，CRC32=`CF885C9E` |
| STM32镜像SHA-256 | `69D6E29DD187706496B4B90FDC2B1F7CBB67FD898273A9B65E1397B765F835AE` |
| HTTP缓存包 | 16988 bytes，包含128-byte Header和至`0x1000`的擦除填充 |
| 缓存包SHA-256 | `D0F0253A73C6A00FFD988B7AA277FE348ADFBBBCBCF879E20BB0A7819AB697FC` |
| 产品/硬件ID | `0x0001 / 0x0001` |
| 下载端点 | `/api/v1/firmwares/f407-node-1.2.0/binary` |

下载对象是现有M7 `firmware_store`可直接识别的缓存包，不是裸STM32 BIN。M9下载完成后
可直接写入ESP32的`stm_fw`分区。

## 自动化验证

| 测试范围 | 结果 |
| --- | --- |
| 既有工具与协议回归 | 32/32通过 |
| M8目录、Manifest、Range解析和HTTP API回归 | 9/9通过 |
| `git diff --check` | 通过 |

M8 API回归覆盖：完整Manifest与二进制、ETag、单Range `206`、错误`If-Range`回退完整
`200`、非法Range `416`、损坏发布物拒绝和非法Firmware ID拒绝。

## 本机服务实测

服务通过`uvicorn`监听`127.0.0.1:8000`后，操作者完成以下结果：

| 验证项 | 实测结果 |
| --- | --- |
| `GET /healthz` | `200`，`{"status":"ok"}` |
| `GET .../manifest` | `200`，字段与发布物一致 |
| 全量`GET .../binary` | 收到16988 bytes |
| 全量文件SHA-256 | `D0F0253A...9AB697FC`，与Manifest一致 |
| `Range: bytes=4096-8191` | `206 Partial Content`，收到4096 bytes |
| Range响应 | `Content-Range: bytes 4096-8191/16988` |
| Range响应ETag | `"d0f0253a...9ab697fc"` |

原始本机验证产物位于被Git忽略的`build/m8_full.bin`、`build/m8_range.bin`及对应
Headers文件；完整哈希见[`m8_archive_manifest.md`](m8_archive_manifest.md)。

## M8边界

1. M8只提供受控局域网HTTP下载，没有网络上传、任务调度或设备状态上报。
2. SHA-256只用于完整性比对；TLS、证书校验、签名和防回退属于M10/M16。
3. M8不会让ESP32自动执行`upgrade start`；M9下载并验证缓存包后仍须由显式升级命令
   触发M7 UART升级。

下一阶段设计与入口见[`m9_download_design.md`](m9_download_design.md)。
