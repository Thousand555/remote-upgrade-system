# M8 PC固件服务器设计与验证

## 目标

M8在Windows PC上提供只读、可复现的本地HTTP固件发布服务。它不直接连接STM32，
不触发ESP32升级，也不接受网络上传；固件发布由开发者在本地审核后写入服务器目录。

M8下载的`firmware.bin`是与M7 `stm_fw`分区兼容的完整缓存包：包含128字节Header、
至`0x1000`的擦除填充及STM32 APP镜像。因此M9可以将响应流直接写入`stm_fw`，随后
复用既有`firmware_store_validate()`校验，不需要在ESP32 RAM中重封装固件。

## 发布目录和Manifest

每个发布目录必须不可变：

```text
server/firmware/<firmware_id>/
├── firmware.bin
└── manifest.json
```

Manifest schema v1包含以下两类校验数据：

| 字段 | 含义 |
| --- | --- |
| `image_size`、`crc32`、`sha256` | STM32 APP镜像本身的长度和完整性 |
| `package_size`、`package_crc32`、`package_sha256` | HTTP下载对象，即M7兼容缓存包的完整性 |
| `product_id`、`hardware_id`、`firmware_version_code` | M9预检和M7/STM32升级身份一致性 |
| `download_url` | 固定为该发布的二进制端点 |
| `source.git_commit` | 打包时源码基线；用于可追溯性，不是安全签名 |

CRC32用于传输完整性；HTTP阶段的SHA-256用于完整镜像比对。TLS、证书、签名和防回退
仍属于M10/M16，M8 HTTP不能作为不可信网络上的安全发布机制。

## API

```text
GET /healthz
GET /api/v1/firmwares/{firmware_id}/manifest
GET /api/v1/firmwares/{firmware_id}/binary
```

二进制端点始终返回`ETag`和`Accept-Ranges: bytes`。它支持一个标准Byte Range：

```text
Range: bytes=262144-
If-Range: "<package_sha256>"
```

当ETag一致时返回`206 Partial Content`；不一致时忽略Range并返回完整`200`，使M9
能够擦除旧缓存后安全地重新下载。无效或多范围请求返回`416`。

## 创建发布

从仓库根目录执行，版本码必须与STM32 APP中的
`UPGRADE_APPLICATION_VERSION`一致：

```powershell
py -3 .\tools\pack_firmware.py `
  --input .\firmware\stm32_app\MDK-ARM\stm32_app\stm32_app.bin `
  --firmware-id f407-node-1.2.0 `
  --version 2 `
  --display-version 1.2.0 `
  --output .\server\firmware\f407-node-1.2.0
```

工具会生成`firmware.bin`和`manifest.json`，并在Manifest中保存当前Git commit。已经
存在的发布目录默认拒绝覆盖；只能为明确的重建操作传入`--force`。

## 启动与本机验证

```powershell
py -3 -m pip install -r .\server\requirements.txt
Set-Location .\server
py -3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

验证：

```powershell
Invoke-RestMethod http://127.0.0.1:8000/healthz
Invoke-RestMethod http://127.0.0.1:8000/api/v1/firmwares/f407-node-1.2.0/manifest
curl.exe -H "Range: bytes=4096-8191" -D - -o NUL http://127.0.0.1:8000/api/v1/firmwares/f407-node-1.2.0/binary
```

主机回归（包含HTTP端到端测试）使用开发依赖：

```powershell
py -3 -m pip install -r .\server\requirements-dev.txt
py -3 -B -m unittest discover -s .\server\tests -p "test_*.py" -v
```

## M8验收出口

- [x] `pack_firmware.py`生成的包可由现有M7 `firmware_package.py`解析。
- [x] Manifest和Binary可被本机HTTP服务读取。
- [x] 二进制全量下载前校验SHA-256与`package_sha256`一致。
- [x] 合法Range返回`206`、正确`Content-Range`和`ETag`。
- [x] 不匹配的`If-Range`安全回退为完整`200`；非法Range返回`416`。
- [x] ESP32 M9能够下载、重启续传并校验后使`firmware validate`通过。

前五项属于M8；最后一项已在2026-09-01的M9目标板验收中通过。
