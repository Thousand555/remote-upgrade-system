# 项目文档导航

## 总体设计与环境

- [`../基于 ESP32网关与 STM32 Bootloader 的多链路远程升级系统.md`](../基于%20ESP32网关与%20STM32%20Bootloader%20的多链路远程升级系统.md)：系统总体设计与里程碑顺序。
- [`../environment.md`](../environment.md)：开发工具、板卡、串口和ESP-IDF环境。
- [`project_constants.md`](project_constants.md)：Flash布局、版本、设备身份和固定参数。
- [`protocol.md`](protocol.md)：UART/Modbus RTU升级协议。
- [`m4_m6_learning_guide.md`](m4_m6_learning_guide.md)：Metadata Journal、协议和升级闭环学习说明。

## 阶段验证

- [`m2_verification.md`](m2_verification.md)：Bootloader跳转APP。
- [`m3_verification.md`](m3_verification.md)：APP Flash擦写。
- [`m4_verification.md`](m4_verification.md)：Metadata追加日志。
- [`m5_verification.md`](m5_verification.md)：协议编解码。
- [`m6_verification.md`](m6_verification.md)：PC经UART升级STM32。
- [`m6_test_evidence.md`](m6_test_evidence.md)：M6板上证据。

## M7归档

- [`m7_archive_summary.md`](m7_archive_summary.md)：**当前M7阶段结论、板上状态、已完成项、延期项和恢复顺序的首要入口**。
- [`m7_archive_manifest.md`](m7_archive_manifest.md)：固件、升级包、日志和关键源码SHA-256。
- [`m7_verification.md`](m7_verification.md)：ESP32 UART本地主链路实现与基础验证。
- [`m7_reliability_verification.md`](m7_reliability_verification.md)：R01～R15测试矩阵、步骤和验收出口。
- [`m7_r02_test_evidence.md`](m7_r02_test_evidence.md)：STM32传输中复位。
- [`m7_r03_test_evidence.md`](m7_r03_test_evidence.md)：ESP32传输中复位。
- [`m7_r04_test_evidence.md`](m7_r04_test_evidence.md)：双端掉电和Journal缺陷修复。
- [`m7_r05_test_evidence.md`](m7_r05_test_evidence.md)：UART短时/长时中断。
- [`m7_r06_test_evidence.md`](m7_r06_test_evidence.md)：ABORT失败态和新Session恢复。

归档时R07本地坏包拦截、R09冷启动10轮和生产版恢复后基线回归均为`DEFERRED`，
不得从其他已通过用例推断这些项目已经通过。

已生成`build/m7_document_archive_20260901.zip`，其中包含归档文档和现有原始日志/报告；
其SHA-256记录在[`m7_archive_manifest.md`](m7_archive_manifest.md)。

## 运行期证据位置

自动化原始日志和生成报告位于仓库根目录`build/`。该目录被Git忽略；需要长期保存或
移交时，应连同文件复制并按[`m7_archive_manifest.md`](m7_archive_manifest.md)复核哈希。

## M8 PC固件服务器

- [`m8_server.md`](m8_server.md)：发布包格式、HTTP API、Range/ETag断点下载契约和M8验收出口。
- [`m8_verification.md`](m8_verification.md)：M8自动化与本机HTTP实测记录。
- [`m8_archive_manifest.md`](m8_archive_manifest.md)：M8发布物、下载证据和关键源码SHA-256。

## M9 ESP32网络下载

- [`m9_download_design.md`](m9_download_design.md)：M9缓存写入、延迟有效标记、NVS检查点与Range/ETag续传设计。
- [`m9_implementation.md`](m9_implementation.md)：已完成的实现范围、配置方式、控制台命令和阶段性验收结论。
- [`m9_remaining_verification.md`](m9_remaining_verification.md)：大包限速、复位/Cancel续传、STM32回归的PASS证据，以及ETag变化和错误身份拦截的DEFERRED记录。
- [`m9_archive_summary.md`](m9_archive_summary.md)：M9阶段结论、实现范围、目标板证据、延期项与后续边界。
- [`m9_archive_manifest.md`](m9_archive_manifest.md)：M9固件、发布物、关键源码与文档SHA-256。

## M10 HTTPS可信下载

- [`m10_implementation.md`](m10_implementation.md)：HTTPS信任模型、已实现范围、证书生命周期和当前软件验证结论。
- [`m10_verification.md`](m10_verification.md)：开发证书、HTTPS服务、ESP32烧录、下载和STM32回归的目标板验收步骤。
- [`m10_archive_summary.md`](m10_archive_summary.md)：M10阶段结论、目标板证据、安全边界和M11交接入口。
- [`m10_archive_manifest.md`](m10_archive_manifest.md)：M10固件、证书、关键源码和文档SHA-256。
