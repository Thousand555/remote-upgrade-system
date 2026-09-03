# M10归档文件清单与SHA-256

本清单生成于2026-09-02。长度单位为字节，SHA-256为大写十六进制。私钥、服务器证书和
错误CA测试材料分别位于Git忽略的`server/certs/`与`build/m10_untrusted_tls/`，不得作为
源码交付物提交。`build/`中的ESP32二进制也受Git忽略，复制或发布时必须单独保存并复核。

## 固件、发布物与信任锚

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `build/esp32_gateway_m10/esp32_gateway.bin` | 974112 | `4FB03A428C07B08FAB40666C2C732B01B71F98F6A89DAA82CCE7959741FACA28` |
| `firmware/esp32_gateway/components/firmware_downloader/certs/m10_ca.pem` | 1220 | `CEAEC0C43D38EDD9016B649CE3923A74217720BA68E72FDFF493019EB56D8A63` |
| `server/firmware/f407-node-1.2.0/manifest.json` | 797 | `CC0E9CAEFBB6FD2451A189D34BBDB75C915A4BE121F8B6DE056689CE0D841E84` |
| `server/firmware/f407-node-1.2.0/firmware.bin` | 16988 | `D0F0253A73C6A00FFD988B7AA277FE348ADFBBBCBCF879E20BB0A7819AB697FC` |
| `firmware/stm32_app/MDK-ARM/stm32_app/stm32_app.bin` | 12892 | `69D6E29DD187706496B4B90FDC2B1F7CBB67FD898273A9B65E1397B765F835AE` |

ESP32应用大小为`0xEDD20`，相对`0x200000` factory分区剩余`0x1122E0`字节（54%）。
正式STM32发布包SHA-256与Manifest中的`package_sha256`一致。

## ESP32 M10关键源码

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `firmware/esp32_gateway/components/firmware_downloader/firmware_downloader.c` | 40014 | `FEF125FD2C19754BB6A46658C2D13D14B359CB14032FAF6F95474C1330D690F9` |
| `firmware/esp32_gateway/components/firmware_downloader/include/firmware_downloader.h` | 1259 | `AA764C7D04E06CDE0F88CEAD6CF959DBFA6061CF9ED5BEA933D8531B90074809` |
| `firmware/esp32_gateway/components/firmware_downloader/CMakeLists.txt` | 291 | `A873684D094FFBD6989094B202A40E47F642FB9EA7698CD6FBCF884E6106DB8C` |
| `firmware/esp32_gateway/components/gateway_wifi/gateway_wifi.c` | 14292 | `D89CDC78479040930B469BF2F5CDA08E555B359FB9C63D41848135E950182439` |
| `firmware/esp32_gateway/components/gateway_wifi/include/gateway_wifi.h` | 1194 | `C5AE6570E301E7BF3621C937C2518FFD0667BE5548A487FD1FBC70E413E4DF1D` |
| `firmware/esp32_gateway/components/gateway_config/Kconfig` | 3486 | `E004934229206DB75052B3BCB0F09332D2E8ABF61A30CD5E92837D14B4F29FB3` |
| `firmware/esp32_gateway/components/gateway_config/include/gateway_config.h` | 3078 | `14F2AF7833BE33A73FED2DA64B635ACD883ECE8FCC4FD8739BA54DBCD69F64C8` |
| `firmware/esp32_gateway/components/gateway_console/gateway_console.c` | 16593 | `D45364401DAD50097049350D7AB0CADC93A86C4DC47E89B64EAC388398C91F7A` |
| `firmware/esp32_gateway/main/esp32_gateway.c` | 4740 | `ECD846FEFF52F52852DD3CC965CC4EBE9771F5A23876105C12EEA9ED97C88920` |
| `firmware/esp32_gateway/sdkconfig.defaults` | 1035 | `273F5505A1DA76ED08BF3ADF5BD0BE5E44848CC110E8B6AFB952AE7A8B6ACBBA` |

## TLS工具与自动化

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `tools/setup_m10_tls.ps1` | 3907 | `61FD0A8209C0C4B557E96B3D0B178E571051D1DFB76C760C7DC0C2F083CF9F57` |
| `tools/run_m10_https.ps1` | 1207 | `46881CF390E2D48E8B82251292A4EB73365B33557431112511BACDFAA1B293A1` |
| `tools/verify_m10_https.py` | 2633 | `68E3589AF8D7FD204ECA480D9F5F66D299ED752DFE3AF5D0BEBF4D741DFD7663` |
| `tools/run_m10_untrusted_https.ps1` | 3713 | `1A7AFC86E14407FB611E86FEE8D0249FD4046DB39D332283B597DE86C51A13A6` |
| `tools/run_tests.ps1` | 1080 | `2561EA626C4371208DD86D9FAA8730FEC960CE997BBBDD7A18382C15792B5797` |

## M10文档快照

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `docs/m10_implementation.md` | 4943 | `00A7E0B5C8389018EC3BFEF7938A8FAE0D5AE3EB5FAEE034888F3EB9FE35EEDC` |
| `docs/m10_verification.md` | 5274 | `364D2CB9B91A7D045EE525F3FFA5F9EAB6305B92E7733C5D917235CB0873484C` |
| `docs/m10_archive_summary.md` | 2689 | `5DE285BA4D616EADA4428520FFFEABEF66BB193F578C56CDCE7CD6631B3CB79C` |

本清单不记录自身哈希，避免自引用。如果以上文件继续修改，应重新生成对应条目。

## 归档验证结果

| 验证项 | 结果 |
| --- | --- |
| ESP-IDF应用构建与分区检查 | `PASS`，`NINJA_EXIT=0` |
| C协议回归 | 4/4 `PASS` |
| Python工具回归 | 33/33 `PASS` |
| M8/M9服务端回归 | 10/10 `PASS` |
| M10目标板正向链路 | `PASS`，详见`m10_archive_summary.md` |
| M10错误CA与缓存保护 | `PASS`，详见`m10_verification.md` |
