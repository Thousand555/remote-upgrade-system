# M11归档文件清单与SHA-256

## 归档信息

- 归档日期：2026-09-03（Asia/Shanghai）。
- 阶段状态：`PHASE COMPLETE`。
- Git基线：`4a3a2126b8fceadbba289271876819433fdacc05`加当前M11未提交工作区。
- 哈希算法：SHA-256；长度单位：字节。
- 目标板结论：正常确认、3次未确认后失败、ESP32缓存重刷恢复、`CONFIRMED`复位/掉电持久化均为`PASS`。
- 回归边界：错误CA负向用例未在M11镜像上重复执行，保持`NOT RE-RUN`，沿用M10归档证据。

## 固件与发布物

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `build/esp32_gateway_m11/esp32_gateway.bin` | 974624 | `9714D2D919B07B1A2B17B2ED4310B0D91DA63DF0E5966F6D8D1650D6928CB3B9` |
| `firmware/stm32_bootloader/MDK-ARM/stm32_bootloader/stm32_bootloader.hex` | 46933 | `656B2E393B1C682F94A9B1FA5E22CED0EF38E0E07424A9F8711E308E35B55BA1` |
| `firmware/stm32_app/MDK-ARM/stm32_app/stm32_app.bin` | 13252 | `18C9035A7AFCA7486ACFCB8BF729E56277CFCC60F2D5DDB893FCDF7EAB7FF282` |
| `server/firmware/f407-node-1.3.0/firmware.bin` | 17348 | `4F164F8703CD15DD03C986AE760EC4EC2D7DCBF8990C714265DBDBBFCDC061CA` |
| `server/firmware/f407-node-1.3.0/manifest.json` | 847 | `AC928F8D6309B18869E3E9C6CBBAAE26DCBC1B3DDE20D1FFB0EE77065261AFF1` |

`build/`和发布目录中的`firmware.bin`受Git忽略。归档或移交时必须单独保存这些二进制，不能仅以
Git提交替代，并应使用本表重新计算哈希。

## M11关键源码

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `firmware/common/Inc/upgrade_config.h` | 1983 | `81704854E17C849B1F3A85028858BDFD599CC9060FD98A190EC8F81B70C128B2` |
| `firmware/stm32_bootloader/App/Inc/boot_upgrade.h` | 818 | `A0D43B645E5CECD3DC79D22C5FE4B5E655449511AEE9F5B5DE3ADA0003013FC2` |
| `firmware/stm32_bootloader/App/Src/boot_upgrade.c` | 24235 | `635423FAC1A3F62672FBF18B929A82A01271062946F46FEB634BB13A3CF6D2D2` |
| `firmware/stm32_bootloader/Core/Src/main.c` | 14787 | `928BD316D8BF7B5C6AEDAF46B4D32C84785E457A00C90872D6D41FB806554335` |
| `firmware/stm32_app/Core/Inc/app_upgrade.h` | 343 | `4FD3201766C317B739E0AD3505FB790148DD912E06B16044E283B6008665CA44` |
| `firmware/stm32_app/Core/Src/app_upgrade.c` | 10253 | `30BCC409B1A9A4A9CFC838D45212EA90921B91966392E3D3510D56CE305BED29` |
| `firmware/stm32_app/Core/Src/main.c` | 6323 | `C71F0CAAA28017B4AEFFECC69A5C5CE984F691BD9C505BD29874A1AB3BC89DD0` |
| `firmware/esp32_gateway/CMakeLists.txt` | 313 | `9DA973C8F328F6776D832D6B5450D07E899F5CD3F3B493353B9C7166A41C2E1A` |
| `firmware/esp32_gateway/components/gateway_config/include/gateway_config.h` | 3130 | `BD61D9EDB958E77B7BA63810D05175678DADA67C65C56B3E53D44992BDCDEC00` |
| `firmware/esp32_gateway/components/upgrade_manager/upgrade_manager.c` | 31937 | `E676941AC4D80BC691085391C078FE694537CC06002B9049DD1A688D520FC4B1` |
| `firmware/esp32_gateway/main/esp32_gateway.c` | 4718 | `C1B1E8477199E0B50F704B90CEF962A62BD939B592CEFBE4B4A29DF71CFCFAD4` |

## 文档快照

| 文件 | 长度 | SHA-256 |
| --- | ---: | --- |
| `docs/m11_implementation.md` | 4600 | `58C25E35171057766CB2B6116233C273D7C68A1D3D61AFA72E0F17506DB917DE` |
| `docs/m11_verification.md` | 4798 | `A335F6C528C62C53830FF7FCC050E4B52048C1053714355EA3D7BF8F8C31F286` |
| `docs/m11_test_evidence.md` | 3659 | `8F1007C7E7888E2122EF9713C73DF6D07661565D6F8564E11FB1CA984A9C96F2` |
| `docs/m11_archive_summary.md` | 2932 | `14240C95C620DF0990EF3FC1377E3B69AC956936A972E26798B1EC6500727058` |
| `docs/project_constants.md` | 5300 | `772EFF6DBCFB5ACA7C46C4C7D8CA7C4B13E1BC866BE360428853C89284142246` |
| `docs/README.md` | 4864 | `C587C8D7E2C0599E592864D641CBBE3901B15687492FD2C62C458280A77DC45C` |
| `基于 ESP32网关与 STM32 Bootloader 的多链路远程升级系统.md` | 64873 | `053EB308D41B9FC2DEE1671B56B5E5DB66EAD8983631F4E84500578FB78D0248` |

本清单不记录自身哈希，避免自引用导致哈希在每次写入后变化。若上述文件在提交前继续修改，必须
重新生成对应长度和SHA-256。提交后可用Git标签或发布说明把本快照与最终M11提交号绑定；若直接
修改本表已覆盖的文档，则必须同步刷新受影响的哈希。

## 已执行验证

| 项目 | 结果 |
| --- | --- |
| STM32 APP ARMCC5构建 | `PASS`，0 Error、0 Warning |
| STM32 Bootloader ARMCC5构建 | `PASS`，0 Error、0 Warning |
| ESP-IDF v5.5.4 / ESP32-S3构建 | `PASS`，项目版本0.3.0 |
| C协议回归 | 4/4 `PASS` |
| Python工具回归 | 33/33 `PASS` |
| 服务端回归 | 10/10 `PASS` |
| M11目标板核心闭环 | `PASS` |
