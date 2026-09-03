# M11 APP启动确认与异常恢复：实现状态

## 当前结论

M11软件闭环与目标板核心验收均已完成，当前状态为`PHASE COMPLETE`。
STM32 APP版本提升为`3`，ESP32项目版本提升为`0.3.0`。正常确认、连续未确认失败、
缓存重刷恢复以及`CONFIRMED`复位/掉电持久化均已通过。

当前STM32仍采用单APP分区。M11实现的是启动确认、失败检测、停留Bootloader以及使用ESP32
缓存重新升级，不是A/B回滚，也不能自动恢复旧APP。

## 正常确认闭环

```text
Bootloader校验新镜像
  -> 追加PENDING_BOOT，error_code=0
  -> 激活并复位
  -> Bootloader追加本次启动尝试，error_code=1
  -> APP启动看门狗、时钟、GPIO、USART1和协议服务
  -> APP主循环稳定运行3000 ms
  -> APP核对Metadata中的版本、Session和完整接收长度
  -> 追加CONFIRMED，error_code=0
  -> ESP32读到boot_state=7后才报告SUCCESS
```

APP在3000 ms确认等待期内仍响应`HELLO`和`GET_INFO`，因此ESP32可观察到
`PENDING_BOOT(6)`，但不会提前把升级判为成功。

## 异常恢复闭环

- APP进入主程序前启动独立看门狗，名义超时为6秒；主循环持续喂狗。
- 若必要外设/升级服务初始化失败、Metadata与当前APP版本不匹配，或确认写入失败，APP停止喂狗。
- `UPGRADE_APP_TEST_WATCHDOG_RESET=1`可在测试构建中模拟确认前卡死；正式构建固定为`0`。
- Bootloader允许最多3次`PENDING_BOOT`启动。每次真正跳转APP前才增加次数，ESP32探测不会误耗次数。
- 第4次进入Bootloader时若仍未确认，Bootloader追加`FAILED(8)`，将`error_code`设为
  `UPG_STATUS_TIMEOUT(13)`并留在恢复服务中。
- ESP32等待确认的上限为45秒。若探测到上述`FAILED`，升级状态变为`FAILED`，保留本地缓存，
  用户可再次执行`upgrade start`重刷同一有效包。

`PENDING_BOOT`状态下的`error_code`在M11中解释为已启动次数，取值为0～3；其他状态仍按原有
错误码解释。Metadata记录仍为76字节、格式版本仍为1，M10留下的`error_code=0`记录可直接迁移。

## 主要代码变更

| 模块 | M11内容 |
| --- | --- |
| STM32 Bootloader | 跳转前持久化启动次数，超过3次转`FAILED`；待确认镜像跳过500 ms主站窗口，避免ESP探测阻止重试 |
| STM32 APP | 提前启动IWDG，检查待确认记录，主循环稳定3000 ms后追加`CONFIRMED` |
| ESP32 `upgrade_manager` | `PENDING_BOOT`只表示等待；仅`CONFIRMED`判成功；识别Bootloader恢复态和错误码 |
| 公共配置 | APP版本3、确认延时、启动上限和测试故障开关 |
| 发布物 | `f407-node-1.3.0`，版本码3 |

## 本机验证结果

| 验证项 | 结果 |
| --- | --- |
| STM32 APP ARMCC5构建 | `PASS`，0 Error、0 Warning，镜像13252字节 |
| STM32 APP SHA-256 | `18C9035A7AFCA7486ACFCB8BF729E56277CFCC60F2D5DDB893FCDF7EAB7FF282` |
| STM32 Bootloader ARMCC5构建 | `PASS`，0 Error、0 Warning；HEX SHA-256=`656B2E393B1C682F94A9B1FA5E22CED0EF38E0E07424A9F8711E308E35B55BA1` |
| ESP-IDF v5.5.4 / ESP32-S3构建 | `PASS`，项目版本0.3.0，镜像974624字节，factory剩余54% |
| ESP32镜像SHA-256 | `9714D2D919B07B1A2B17B2ED4310B0D91DA63DF0E5966F6D8D1650D6928CB3B9` |
| C协议回归 | 4/4 `PASS` |
| Python工具回归 | 33/33 `PASS` |
| 服务端回归 | 10/10 `PASS` |

开发发布包参数：

| 字段 | 值 |
| --- | --- |
| Firmware ID | `f407-node-1.3.0` |
| Firmware version code | `3` |
| APP大小 | 13252字节 |
| APP CRC32 | `A81E6F7C` |
| Package大小 | 17348字节 |
| Package SHA-256 | `4F164F8703CD15DD03C986AE760EC4EC2D7DCBF8990C714265DBDBBFCDC061CA` |

发布Manifest中的源码字段使用M10基线提交加M11工作区差异标识；M11正式归档时应记录最终提交号和
所有交付物哈希。

## 目标板结论

1. 版本3从`PENDING_BOOT`自动进入`CONFIRMED`，ESP32随后才报告`SUCCESS`：`PASS`。
2. 确认前连续3次复位后进入`FAILED(8)`、错误码13：`PASS`。
3. 不重新下载，直接用ESP32有效缓存重新刷写并回到`CONFIRMED(7)`：`PASS`。
4. `CONFIRMED`记录在STM32复位/掉电后仍保持有效：`PASS`。
5. M11镜像完成可信HTTPS正式包下载；错误CA负向用例未在M11镜像上重复执行，沿用M10已归档
   证据并记录为`NOT RE-RUN`，不冒充M11新增板测结果。

详细证据、步骤与归档入口分别见[`m11_test_evidence.md`](m11_test_evidence.md)、
[`m11_verification.md`](m11_verification.md)和[`m11_archive_summary.md`](m11_archive_summary.md)。
