# M10 HTTPS目标板验收步骤

## 1. 生成开发证书

先用`ipconfig`确认PC当前WLAN IPv4，不要使用VMware虚拟网卡地址。当前联调地址为
`192.168.31.170`：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
.\tools\setup_m10_tls.ps1 -ServerIp 192.168.31.170
```

当前工作区已经为`192.168.31.170`生成证书；PC地址未变化时可跳过本步骤，直接启动服务器。
如果证书已存在，脚本会拒绝覆盖。只有明确轮换CA时才使用`-Force`；轮换后必须重新构建
并烧录ESP32。服务器私钥位于`server/certs/`且受Git忽略，ESP32只嵌入公开CA证书。

## 2. 启动并验证HTTPS服务器

在第一个PowerShell窗口运行：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
.\tools\run_m10_https.ps1
```

默认监听`0.0.0.0:8443`。Windows防火墙只对当前受控局域网放行TCP 8443。

在第二个PowerShell窗口执行PC预检：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
py -3 -B .\tools\verify_m10_https.py
```

必须看到三项`PASS`：受信健康检查与Manifest、`206` Range下载、未受信证书拒绝。

## 3. 构建和烧录ESP32 M10

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0\firmware\esp32_gateway

idf.py -B ..\..\build\esp32_gateway_m10 build
idf.py -B ..\..\build\esp32_gateway_m10 -p COM5 flash monitor
```

构建必须显示项目版本`0.2.0`、应用大小不超过`0x200000`。启动日志应出现：

```text
ESP32 gateway M10 initialization starting
M10 gateway ready; HTTPS downloads and STM32 upgrades both require explicit console commands
```

普通`flash`不会覆盖`stm_fw`。不要执行`erase-flash`。

## 4. 配置HTTPS地址

旧M9 NVS配置使用HTTP，M10默认会把它视为无效。进入`gateway>`控制台后重新配置：

```text
wifi configure "<SSID>" "<PASSWORD>" https://192.168.31.170:8443
wifi status
```

不要把真实SSID或密码写入Git管理的文档。期望`wifi status`显示`connected`和HTTPS地址。

## 5. HTTPS下载与缓存校验

```text
firmware download f407-node-1.2.0
firmware download status
firmware validate
```

下载任务会先显示`WAIT_WIFI`，联网后进入`WAIT_TIME`等待SNTP，再开始Manifest和Binary请求。
重复查询状态直到`READY`。正式发布物期望值：

| 字段 | 期望 |
| --- | --- |
| Package | 16988字节 |
| Version | 2 |
| Image Size | 12892字节 |
| Image CRC32 | `CF885C9E` |
| Product/Hardware | `0x0001/0x0001` |

服务器窗口应看到Manifest和Binary均通过HTTPS访问。任何TLS失败都不得擦除当前有效缓存。

如果任务停在`WAIT_TIME`后失败，先检查DNS以及UDP 123是否可达，或将
`CONFIG_GATEWAY_SNTP_SERVER`改为当前网络可访问的NTP服务器后重新构建。M10选择失败关闭：
没有有效系统时间时不跳过证书日期校验，也不进入缓存擦除阶段。

## 6. STM32端到端回归

确认ESP32 GPIO17/18与STM32 PA10/PA9及公共GND连接正确，先执行无破坏探测：

```text
upgrade probe
```

探测成功后再执行真实擦写：

```text
upgrade start
upgrade status
```

验收结果必须为`SUCCESS / ESP_OK / device status 0`，进度12892/12892，最终
`upgrade probe`重新发现APP版本2。

## 7. 错误CA负向测试

保持受信HTTPS服务器的8443端口运行，在另一个PowerShell窗口启动隔离的错误CA端点：

```powershell
Set-Location C:\Users\baimin\Desktop\remote_upgrade_system_v1.0
.\tools\run_m10_untrusted_https.ps1 -ServerIp 192.168.31.170
```

该脚本只在被Git忽略的`build/m10_untrusted_tls/`中生成临时CA和服务器私钥，监听8444，
不会修改ESP32内嵌CA。将网关临时指向该端点并发起下载：

```text
wifi configure "<SSID>" "<PASSWORD>" https://192.168.31.170:8444
firmware download f407-node-1.2.0
firmware download status
firmware validate
```

下载必须以TLS证书校验错误结束，且此前合法的`firmware validate`仍然通过。完成后恢复
受信服务器地址：

```text
wifi configure "<SSID>" "<PASSWORD>" https://192.168.31.170:8443
```

2026-09-02目标板结果：TLS握手返回`mbedtls_ssl_handshake -0x2700`，下载状态为
`FAILED / ESP_ERR_HTTP_CONNECT`且进度0/0；随后`firmware validate`仍通过，版本2、镜像
12892字节、CRC32=`CF885C9E`。错误CA拒绝与旧缓存保护验收通过。

## 验收出口

| 项目 | 当前结果 |
| --- | --- |
| PC HTTPS与证书校验 | `PASS` |
| PC HTTPS Range/If-Range | `PASS` |
| ESP32 M10构建 | `PASS` |
| ESP32 M10烧录与启动 | `PASS` |
| 当前网络SNTP同步 | `PASS`，存在偶发UDP 123超时，重试成功 |
| SNTP失败后旧缓存校验 | `PASS` |
| STM32 APP无破坏探测 | `PASS`，版本2 |
| ESP32 HTTPS正式包下载 | `PASS`，16988/16988字节、状态`READY` |
| HTTPS下载后缓存校验 | `PASS`，版本2、CRC32 `CF885C9E` |
| HTTPS到STM32完整升级 | `PASS`，12892/12892字节、`SUCCESS` |
| 升级后STM32 APP回探 | `PASS`，版本2、`boot_state=6` |
| 板上错误CA拒绝且保留旧缓存 | `PASS`，TLS `-0x2700`、0/0字节，旧包校验通过 |
