# M10 HTTPS可信下载：实现状态

状态：`PHASE COMPLETE`（2026-09-02归档）。

M10沿用M9已经通过的Manifest预检、流式写入、SHA-256整包复核、镜像CRC32、延迟有效
标记和Range/If-Range断点续传，只替换网络信任边界：ESP32默认拒绝明文HTTP，并通过
内嵌开发CA验证M8服务器证书及URL中的主机名/IP。数字签名、防降级、Secure Boot、
Flash Encryption和双向TLS不属于本轮“先跑通整体链路”的最小范围。

## 已实现内容

1. `gateway_wifi`接受`https://`服务器地址；`http://`只有显式启用
   `CONFIG_GATEWAY_ALLOW_INSECURE_HTTP`时才允许，默认关闭。
2. 冷启动后先通过SNTP同步系统时间，再进行证书有效期校验；下载状态会先进入
   `WAIT_TIME`。20秒内无法获得可信时间时本次下载失败，且不会擦除现有有效缓存。
3. `firmware_downloader`为HTTPS请求配置内嵌CA，保持证书主机名/IP校验开启，并关闭
   自动重定向，避免可信HTTPS端点把下载静默导向其他地址。
4. 开发CA公开证书嵌入ESP32镜像；CA私钥和服务器私钥仅保存在被Git忽略的
   `server/certs/`。
5. 新增`tools/setup_m10_tls.ps1`，为指定局域网IPv4生成开发CA和带IP/DNS SAN的
   服务器证书，并自动更新ESP32内嵌CA。
6. 新增`tools/run_m10_https.ps1`，以TLS模式启动现有FastAPI/Uvicorn固件服务。
7. 新增`tools/verify_m10_https.py`，验证受信HTTPS、Manifest、ETag、Range/If-Range
   `206`以及错误信任链拒绝。
8. ESP32项目版本提升为`0.2.0`，启动日志切换为M10。

## 信任模型

当前模型是“开发CA固定”：

```text
ESP32固件内嵌开发CA公钥证书
              |
              v
Wi-Fi连接后通过SNTP获得证书校验所需时间
              |
              v
验证M8服务器证书链 + URL主机名/IP
              |
              v
HTTPS获取Manifest和firmware.bin
              |
              v
M9 SHA-256/CRC32/valid_marker安全提交
```

这能够阻止未持有开发CA私钥的中间人伪造本地M8服务器，但仍不是最终固件来源签名。
如果服务器自身或开发CA私钥泄露，攻击者仍可发布恶意但哈希自洽的固件。该问题由M16
数字签名和防降级解决。

## 证书生命周期

- 当前开发CA仅提交公开证书；私钥不得进入Git、文档、日志或固件。
- `setup_m10_tls.ps1 -Force`会轮换开发CA。轮换后必须重新构建和烧录ESP32，否则板上
  仍信任旧CA，TLS握手会失败。
- 当前服务器证书包含`192.168.31.170`、`m10.local`和`localhost` SAN。WLAN地址变化
  后应重新生成证书、重新构建ESP32，并用新的HTTPS URL配置网关。
- 该开发CA只用于局域网联调。正式部署应替换为受控企业CA或公共CA，并建立吊销和轮换
  流程。

## 当前验证结果

| 项目 | 结果 |
| --- | --- |
| OpenSSL证书链与SAN | `PASS` |
| PC受信HTTPS健康检查和Manifest | `PASS` |
| PC HTTPS Range/If-Range | `PASS`，`206`且4096字节 |
| PC未受信证书拒绝 | `PASS` |
| C协议回归 | `PASS`，4/4套件 |
| Python工具回归 | `PASS`，33/33 |
| M8/M9服务端回归 | `PASS`，10/10 |
| ESP-IDF v5.5.4独立构建 | `PASS`，0 Error |
| ESP32 M10镜像 | 974112字节（`0xEDD20`），factory分区剩余54% |
| ESP32 M10镜像SHA-256 | `4FB03A428C07B08FAB40666C2C732B01B71F98F6A89DAA82CCE7959741FACA28` |
| ESP32目标板烧录与M10启动 | `PASS` |
| SNTP时间同步 | `PASS`；当前网络存在偶发UDP 123超时，下载重试后同步成功 |
| 时间同步失败时保留旧缓存 | `PASS`，旧包仍可通过`firmware validate` |
| STM32无破坏探测 | `PASS`，APP版本2、设备状态6 |
| ESP32目标板HTTPS下载 | `PASS`，16988/16988字节、状态`READY` |
| HTTPS下载后缓存校验 | `PASS`，版本2、镜像12892字节、CRC32 `CF885C9E` |
| HTTPS下载后STM32回归 | `PASS`，`SUCCESS / ESP_OK / device status 0`，12892/12892字节 |
| 升级后STM32 APP回探 | `PASS`，版本2、`boot_state=6` |
| 板上错误CA拒绝 | `PASS`，TLS `-0x2700`、`FAILED/ESP_ERR_HTTP_CONNECT`、0/0字节 |
| 错误CA失败后保留旧缓存 | `PASS`，版本2、镜像12892字节、CRC32 `CF885C9E` |

构建产物位于被Git忽略的`build/esp32_gateway_m10/`。本阶段板上验收步骤见
[`m10_verification.md`](m10_verification.md)，归档结论与文件哈希分别见
[`m10_archive_summary.md`](m10_archive_summary.md)和
[`m10_archive_manifest.md`](m10_archive_manifest.md)。

## M10完成边界

以下三项目标板证据已全部补齐，M10标记为`PHASE COMPLETE`：

1. ESP32使用HTTPS完成正式包下载，`firmware validate`与Manifest一致。
2. HTTPS下载后执行一次既有UART升级，最终`SUCCESS`并回探STM32 APP版本2。
3. 使用非内嵌CA签发的服务器证书时，ESP32在擦除当前有效缓存前拒绝连接。
