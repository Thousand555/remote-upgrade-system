# M11阶段归档总览

## 归档结论

- 归档日期：2026-09-03（Asia/Shanghai）。
- 阶段状态：`PHASE COMPLETE`。
- 范围：STM32新APP启动确认、独立看门狗、未确认启动次数限制、Bootloader恢复态、ESP32确认
  成功判据，以及使用ESP32有效缓存重新刷写。
- 架构边界：STM32仍为单APP分区；实现的是失败检测和重新升级，不是A/B回滚，也不能恢复
  已被覆盖的旧APP。

## 实现范围

| 模块 | M11内容 |
| --- | --- |
| STM32 APP | 启动IWDG；验证Metadata版本、Session与长度；主循环稳定3000 ms后追加`CONFIRMED` |
| STM32 Bootloader | 每次实际跳转前持久化启动次数；3次未确认后转`FAILED/13`并保留升级服务 |
| ESP32网关 | 仅`CONFIRMED(7)`判成功；识别Bootloader `FAILED(8)`及错误13；等待上限45 s |
| Metadata | 保持格式版本1和76字节记录；`PENDING_BOOT.error_code`复用为0～3启动次数 |
| 发布物 | APP版本3、显示版本1.3.0、Firmware ID `f407-node-1.3.0` |

## 目标板证据

| 用例 | 结果 | 核心证据 |
| --- | --- | --- |
| 可信HTTPS下载 | `PASS` | `READY`、17348/17348字节、版本3、CRC32=`A81E6F7C` |
| 正常启动确认 | `PASS` | Session=`0x700DD63C`，先观察状态6，再进入`SUCCESS/7/ESP_OK/0` |
| 未确认启动限制 | `PASS` | Session=`0xE0DB346C`，最终`FAILED/8/ESP_ERR_INVALID_STATE/13` |
| Bootloader恢复服务 | `PASS` | capabilities=`0x000D`、application=3、boot_state=8 |
| 缓存保持 | `PASS` | 失败后本地版本3包仍通过完整校验 |
| 缓存重新刷写 | `PASS` | Session=`0x0334D9C1`，未重新下载即恢复`SUCCESS/7` |
| CONFIRMED持久化 | `PASS` | 用户确认STM32复位/掉电后仍为版本3、状态7 |
| M11错误CA复测 | `NOT RE-RUN` | M10已有归档证据；M11镜像未重复执行，不能标记为新增PASS |

详细目标板记录见[`m11_test_evidence.md`](m11_test_evidence.md)。

## 构建与自动化回归

| 项目 | 结果 |
| --- | --- |
| STM32 APP ARMCC5 | 0 Error、0 Warning，13252字节 |
| STM32 Bootloader ARMCC5 | 0 Error、0 Warning |
| ESP-IDF | v5.5.4，Target=`esp32s3`，项目版本`0.3.0` |
| ESP32应用 | 974624字节，factory分区剩余54% |
| C协议回归 | 4/4 `PASS` |
| Python工具回归 | 33/33 `PASS` |
| 服务端回归 | 10/10 `PASS` |

## 已知边界与后续入口

1. APP看门狗名义超时6秒，实际时间受STM32 LSI频率误差影响。
2. `PENDING_BOOT.error_code`具有状态相关语义；只有在该状态下才表示启动次数。
3. Metadata损坏时仍保留M11之前的APP向量回退策略，后续安全阶段应单独评估是否收紧。
4. 数字签名、防降级、Secure Boot、Flash Encryption仍属于M16安全增强。
5. 下一阶段M12可在不改变升级状态机的前提下增加RS485 Transport。

关键文件和交付哈希见[`m11_archive_manifest.md`](m11_archive_manifest.md)。
