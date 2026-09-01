# M9 ESP32 HTTP下载与缓存设计

## 目标与边界

M9让ESP32从M8服务器获取已审核的M7兼容缓存包，流式写入`stm_fw`分区，支持ESP32
复位后的HTTP Range续传，并在完整性验证后使既有`firmware validate`可通过。

M9不自动启动STM32升级；下载成功后仍需要用户显式执行已有的`upgrade start`。M9使用
受控局域网HTTP，HTTPS、服务器证书和签名不在本阶段实现。

## 与M8/M7的契约

```text
M8 Manifest
  ├─ product_id / hardware_id / firmware_version_code
  ├─ image_size / crc32 / sha256
  ├─ package_size / package_sha256 / download_url
  └─ ETag
          ↓
M9 HTTP下载器
  ├─ 直接写入 stm_fw 分区
  ├─ NVS保存下载检查点和ETag
  └─ 完整包SHA-256与firmware_store_validate()
          ↓
M7 upgrade_manager
  └─ 显式 upgrade start 后通过UART升级STM32
```

`download_url`返回的是M7完整缓存包，不是裸BIN。M9不得改写其Header、镜像偏移或
Manifest定义的长度；下载完成后的分区布局必须仍是`Header(128) -> 0xFF填充至0x1000
-> STM32镜像`。

## 设计方案

### 1. 网络与配置

新增ESP-IDF组件`firmware_downloader`和最小Wi-Fi配置层。SSID、密码、服务器基地址
不写入Git管理的`defaults`文件；优先通过串口控制台`wifi configure`写入NVS，
`menuconfig`仅保留为可选的编译期回退配置。

M9板测时服务器需监听PC局域网地址，例如：

```text
http://<PC-LAN-IP>:8000
```

`127.0.0.1`只用于PC本机M8验证，ESP32不能访问它。Windows防火墙仅需放行受控局域网
的TCP 8000端口。

### 2. 控制台与状态机

建议在既有`firmware`命令组增加：

```text
firmware download <firmware_id>
firmware download status
firmware download cancel
```

下载状态机：

```text
IDLE -> FETCH_MANIFEST -> VALIDATE_MANIFEST -> PREPARE
     -> DOWNLOAD -> VERIFY_PACKAGE -> COMMIT_PACKAGE -> READY
                                      └-> FAILED / CANCELED
```

`firmware download`只影响ESP32缓存分区，绝不发送`ENTER_BOOT`、`ERASE`或`DATA`到
STM32。`upgrade start`在`READY`后仍保持单独的显式操作。

### 3. Manifest预检

在擦除`stm_fw`前验证：

- `schema_version == 1`、Firmware ID和`download_url`格式合法；
- 产品/硬件ID匹配当前`0x0001/0x0001`；
- `firmware_version_code`非零；
- `image_size <= 0xE0000`；
- `package_format == 1`、Header=128、Image Offset=`0x1000`；
- `package_size == 0x1000 + image_size`，且不超过`stm_fw`分区；
- 所有CRC32和SHA-256字段格式正确。

M9只将Manifest身份和长度作为下载许可；最终以缓存包SHA-256、包内Header和现有
`firmware_store_validate()`共同确认可用性。

### 4. 流式写入与安全提交

HTTP读取块固定为4 KiB，不将完整固件放入RAM。新的下载先擦除所需ESP Flash扇区，再
按包偏移写入。正常下载每累计64 KiB持久化一次NVS下载记录；显式Cancel时另外保存最后一次Flash写入成功后的精确`received_size`，减少恢复时的重复下载。记录至少包含：

```text
schema_version, firmware_id, package_size, package_sha256, ETag, received_size, state
```

下载首个4 KiB时，M9必须临时将包Header中`valid_marker`（偏移60～63）保持为`0xFF`，
不能直接写入有效值。全部网络字节下载完成、完整包SHA-256匹配Manifest且包内Header
与Manifest一致后，才将真实`valid_marker`写入分区并清除NVS下载记录。这样ESP32在
任何下载中断点复位后，`firmware_store_validate()`都不会把半包误判为可升级固件。

当前只有一个`stm_fw`缓存分区；开始新下载后，旧缓存不能与新包同时保留。下载失败只会
使ESP32缓存不可用，不会改写STM32 APP；重新执行下载或恢复同一下载即可修复。

### 5. 复位续传

ESP32重启后若NVS存在`DOWNLOADING`记录，先重新获取Manifest并比较Firmware ID、
Package SHA-256、长度和ETag：

```text
一致：Range: bytes=<received_size>- + If-Range: <ETag>
      服务器返回206且Content-Range起点一致，继续写入

不一致或服务器返回200：擦除本次缓存范围，清除旧检查点，从0重新下载
```

完成后必须重新顺序读取整个`stm_fw`分区计算SHA-256；不能依赖复位前的临时Hash上下文。

## 验收与测试

1. 正常下载M8发布包，`firmware validate`通过且与Manifest Hash一致。
2. 传输中复位ESP32，从最近64 KiB检查点以`206`续传。
3. 修改M8发布物或ETag后，M9拒绝续传并安全从0重新下载。
4. 错误产品ID、硬件ID、长度、Header CRC、Package SHA-256均在写入或提交前拒绝。
5. HTTP截断、Wi-Fi断开或用户取消后，分区没有有效`valid_marker`，`upgrade start`不可用。
6. 下载完成后执行一次既有M7完整UART升级，确认APP版本与Manifest版本码匹配。

## 实现顺序

1. 增加组件接口、Wi-Fi/服务器配置和NVS下载记录。
2. 实现Manifest JSON解析与预检，先提供`firmware download status`。
3. 实现4 KiB流式下载、缓存写入、延迟Commit和完整SHA-256验证。
4. 实现Range/ETag恢复和取消。
5. 接入控制台、编译、主机模拟服务测试和ESP32板测。
