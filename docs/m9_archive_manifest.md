# M9归档文件清单与SHA-256

本清单生成于2026-09-01。长度单位为字节，SHA-256均为大写十六进制。`build/`、ESP-IDF
构建目录和所有`.bin`文件受`.gitignore`忽略，不会随Git提交保存；复制、迁移或交付时
必须单独保存所需二进制，并按本清单重新计算哈希。

## 1. 正式固件与M8发布物

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `firmware/esp32_gateway/build/esp32_gateway.bin` | 968896 | `5F3038B8F85EEA2E551D6130A60B1C28DA9DD378BEC29A7B4AFF1DD992E0D723` |
| `server/firmware/f407-node-1.2.0/manifest.json` | 797 | `CC0E9CAEFBB6FD2451A189D34BBDB75C915A4BE121F8B6DE056689CE0D841E84` |
| `server/firmware/f407-node-1.2.0/firmware.bin` | 16988 | `D0F0253A73C6A00FFD988B7AA277FE348ADFBBBCBCF879E20BB0A7819AB697FC` |
| `firmware/stm32_app/MDK-ARM/stm32_app/stm32_app.bin` | 12892 | `69D6E29DD187706496B4B90FDC2B1F7CBB67FD898273A9B65E1397B765F835AE` |

ESP32应用大小检查结果为`0xEC8C0`，相对`0x200000` factory分区剩余`0x113740`
字节（54%）。正式发布包SHA-256与M8 Manifest的`package_sha256`一致。

## 2. M9板测发布物

这些文件位于受Git忽略的`build/`目录，只用于复位、Cancel、ETag和错误身份分支测试。
归档时测试A已用于复位与Cancel续传；B和错误身份发布物已生成但对应板测为`DEFERRED`。

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `build/m9_test_firmware/f407-node-m9-resume/firmware.bin` | 921600 | `8D7E25682FC9C87BF4A6BC791A40546B7D9A02333E2AEEC04C596B44DFCFD9F8` |
| `build/m9_test_firmware/f407-node-m9-resume/manifest.json` | 819 | `457BC76FD6125E6B91A75C4D02D20AE68B3F479321ED60E386006D9A4BA16743` |
| `build/m9_test_firmware_variant_b/f407-node-m9-resume/firmware.bin` | 856064 | `087A9EEB290C8F18AE4CC674BD68C3FF30B602F55EF4EF66D6FB5CB1CFB6C1DE` |
| `build/m9_test_firmware_variant_b/f407-node-m9-resume/manifest.json` | 819 | `FF3904FFD1F2C1B631B34A3883CBC16D454C661E9103E6B1E2E98A8AB0324DCA` |
| `build/m9_test_invalid_identity/f407-node-m9-wrong-product/firmware.bin` | 16988 | `D7CED5CF93735C003256B2B44F9BB9F8570DEFD008F0CA4D0B30817AEF546D4B` |
| `build/m9_test_invalid_identity/f407-node-m9-wrong-product/manifest.json` | 830 | `C8974CFB0C63A8035A71AD003CA6DFFC9AC9AEC98B9718112CFD181613ADC925` |

## 3. ESP32 M9关键源码

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `components/firmware_downloader/firmware_downloader.c` | 39031 | `9868FB584603C525489A47161EF8F2A986C4778E583C28C191E3DFFC868F4D47` |
| `components/firmware_downloader/include/firmware_downloader.h` | 1235 | `ACF990A787B2FFA54B171C160D2FDAC3F3644941E1646A61EA68DFE233BA2F29` |
| `components/firmware_downloader/CMakeLists.txt` | 234 | `219CEF2C6D61D9707B213563818658B90D50F158AAB415355D43F22BA3FAC66C` |
| `components/gateway_wifi/gateway_wifi.c` | 13121 | `D3946AD797B256EF497BAD2A1CD2C4061738AC35A1C137024D2724C066CAB3FF` |
| `components/gateway_wifi/include/gateway_wifi.h` | 1132 | `AA71D4ABFCC7F1DDC2E47B36E04C1225C361D2DDA5D78A4FD97F405B4DDE9B8F` |
| `components/gateway_wifi/CMakeLists.txt` | 199 | `E95A564EF24B334EB5AEDA67FD987F7D353E4A28A02632ECFB2405C86B37261F` |
| `components/firmware_store/firmware_store.c` | 13193 | `D4342A5F7823FECC538CC89458AF70EEFD19712F0C9662248422F0CD1787F46C` |
| `components/firmware_store/include/firmware_store.h` | 1819 | `73E3BAA12C59D9227DDD172F6109356F9F98D10CCEEA93D1AD1F386334522D06` |
| `components/gateway_console/gateway_console.c` | 16587 | `9DA79F4622D798ECC9A885271099354B3FDBF0299E4C6E419DBD834F59F06904` |
| `components/gateway_console/CMakeLists.txt` | 498 | `1A7D8B820476CA6F9F966220ABB6243C4D7AECABE676ACC63334DD2B677665BB` |
| `components/gateway_config/include/gateway_config.h` | 2761 | `36E3456F48CA679E76F175D6975DB21EF9C03BEA5E212E9CD3E680845EC6239E` |
| `components/gateway_config/Kconfig` | 2700 | `DFA10F81D0016CEAE3F4F022AC44941C1D9F3CA28D6253DE9AEF56843B7745C2` |
| `main/esp32_gateway.c` | 4727 | `B5CD3CDF6F7804599A4210D433D53E462FF8B93F167221B22F90A4E4D7DD3443` |
| `main/CMakeLists.txt` | 554 | `B610D7B3033F281D96C0FC0F3BA91619F0571A1BD875A748D4E2A09980EEF532` |

本节路径均相对于`firmware/esp32_gateway/`。分区表未改变，`partitions.csv` SHA-256为
`88BF674F7F87D3A371A97BDCDA156A331CCC8A3E6387938112E3D6EE429CB7E4`。

## 4. M8服务、发布工具与回归源码

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `server/app/main.py` | 5259 | `F5648FE445628C4FDBFDB22531798864BB0F6D87E46F03E6DA30D7CFBA03428E` |
| `server/app/catalog.py` | 5601 | `4915EEB25281663EE3FD32D467E72EBDC3C6C4811E9E5207BC3A0AABA8103683` |
| `server/app/range_requests.py` | 1831 | `D8BAC220AC29529A69B4D4028D4AB77FE6FCAB3D9EDB9FF95F371CD1D16C8A28` |
| `server/tests/test_api.py` | 4295 | `5A05045EC04AECADB2058B82F61BD226BD96B780A6343F96394EDD114EE376EB` |
| `server/tests/test_catalog.py` | 3112 | `F7EEE7D7E8B97D24FE8FC10A602BA09778BFFB8BF9F43FA2ACD057A1D4092EA0` |
| `server/tests/test_range_requests.py` | 1315 | `DE8B8D4BAB93B79D23204C8F62784BE682DF8A847ED3125BA7D7B635B8CEEC00` |
| `tools/pack_firmware.py` | 5891 | `3792F1C7AECF64947232FCC662F7AFD112EEBFF8739EEBE8E70ADB0BB0D52A86` |
| `tools/test_pack_firmware.py` | 3384 | `66672F5C25F4221D805ECDF26DC161B61BF25650A1D6909184689EDE49DD2A4C` |
| `tools/run_tests.ps1` | 1161 | `B40939416BF544809087A585808616EB42C53930803F32DDE167A26FB1430BAA` |

## 5. M9文档快照

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `docs/m9_download_design.md` | 5318 | `81E02CF87331E3E8D7713F5E09685FEDA48B10B2CEC8F4444AF24F6460D6EBA0` |
| `docs/m9_implementation.md` | 6635 | `93DEE6388061DDE6251065E7674B52E30A94DEF5DABB3F308A37106D3886E90E` |
| `docs/m9_remaining_verification.md` | 10723 | `3BC1204A17B46CE0A29571B57950F40824637DA96375672FB602C5579F206D3B` |
| `docs/m9_archive_summary.md` | 6380 | `D33536F047C4734A42B3062BBE1DE21D648E5232CA0655C8CBFADF2937962DA0` |

归档清单不记录自身哈希，避免自引用。若上述文件继续修改，提交前必须重新生成对应行。

## 6. 归档时验证结果

| 验证项 | 结果 |
| --- | --- |
| 工具Python单元测试 | 33/33 `PASS` |
| M8/M9服务器单元测试 | 10/10 `PASS` |
| ESP-IDF应用构建与分区检查 | `PASS`，`NINJA_EXIT=0` |
| M9目标板核心链路 | `PASS`，详细记录见`m9_remaining_verification.md` |
| ETag变化目标板测试 | `DEFERRED` |
| 错误Manifest身份目标板测试 | `DEFERRED` |

## 7. 复核命令

```powershell
Get-FileHash -Algorithm SHA256 <文件路径>
Get-Item <文件路径> | Select-Object Length
```

