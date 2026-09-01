# M7 ESP32 UART升级可靠性验证方案

> 2026-09-01归档状态：R01～R06、R08、R10～R15和正常升级10/10已通过；R07、R09
> 以及恢复生产版后的最终基线按用户要求暂缓。完整阶段结论见
> [`m7_archive_summary.md`](m7_archive_summary.md)。

## 1. 目标与范围

本方案验证ESP32作为升级主机时，在应答丢失、重复请求、Offset不一致、设备复位、
网关复位、串口中断和错误固件包等条件下，不写错STM32 APP、不误报成功，并能在
条件恢复后继续或重新完成升级。

STM32设备端的DATA幂等、4 KiB Metadata检查点、错误CRC拒绝和10次连续升级已在
M6通过PC工具验证。M7需要用ESP32状态机重新覆盖这些场景，不能用M6结果代替。

## 2. 统一通过判据

每个用例均需同时满足以下要求：

1. ESP32只有在重新发现APP，且Product、Hardware和版本均匹配后才能进入`SUCCESS`。
2. 任何本地包错误必须在发送`ENTER_BOOT`、`ERASE`或`DATA`之前被拒绝。
3. DATA应答丢失或重复发送不得造成Flash内容变化、Offset倒退死循环或重复擦除。
4. STM32复位后的恢复Offset必须是已持久化的4 KiB检查点，最多重传4095字节。
5. ESP32或链路异常导致当前任务失败后，重新执行`upgrade start`必须能够发现原
   Session并恢复；不能无条件重新擦除已接收数据。
6. CRC或设备身份不匹配时不得执行`ACTIVATE`。
7. 每次最终`upgrade probe`均应发现STM32 APP，不能停留在Bootloader或无响应。
8. 连续稳定性测试要求10/10成功，失败、错误激活和人工恢复次数均为0。

## 3. 测试前准备

- 使用已修复ACTIVATE单次发送和1秒静默等待的ESP32固件，当前构建大小`0x47460`。
- ESP32 GPIO17接STM32 PA10，GPIO18接PA9，两板共地；USB-TTL不得并联驱动PA10。
- 保留ST-Link、已验证的Bootloader和STM32 APP BIN，以便破坏性测试后恢复。
- PC保存一份正确的`stm32_m7_package.bin`，记录其版本、大小、CRC32和SHA-256。
- 每轮开始前执行：

```text
firmware validate
upgrade probe
```

- 每轮结束后执行：

```text
upgrade status
upgrade probe
```

可靠性测试会反复擦除STM32 APP。测试期间不得断开公共GND，不得修改Bootloader和
Metadata分区，不得在升级任务运行时覆盖ESP32的`stm_fw`分区。

### 3.1 构建并烧录可靠性测试版

生产配置默认关闭`CONFIG_GATEWAY_RELIABILITY_TEST`。测试版使用独立配置和构建目录，
当前测试固件大小为`0x48250`：

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
Set-Location .\firmware\esp32_gateway

idf.py `
  -B ..\..\build\esp32_gateway_reliability `
  -D SDKCONFIG=..\..\build\esp32_gateway_reliability\sdkconfig `
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.reliability.defaults' `
  build

idf.py -B ..\..\build\esp32_gateway_reliability -p COM_ESP flash
```

`COM_ESP`必须是ESP32控制台/下载端口，不是连接STM32 USART1的USB-TTL端口。普通
`flash`不会覆盖`stm_fw`；禁止使用`erase-flash`。启动后必须看到警告：

```text
Reliability fault injection is ENABLED; do not use this build in production
```

并验证：

```text
test fault show
```

所有一次性故障初始状态都应为`off`。

## 4. 当前固件可直接执行的用例

### 当前板上执行进度

| 用例 | 结果 | 实测证据 |
| --- | --- | --- |
| R01 测试版3轮基线 | PASS | 第1轮Session=`0xA805F6CA`、约6.35秒；3/3均为12888/12888字节、`SUCCESS/ESP_OK/0`、APP版本1回探成功，全部故障项为off |
| 正常升级10轮稳定性 | PASS | 10/10均为12888/12888、`SUCCESS/ESP_OK/0`且APP版本1回探成功；Session均不同；单轮7.721～7.790秒、平均7.767秒；总耗时78.121秒 |
| R02 STM32传输中复位 | PASS | 256 KiB镜像传输中复位；Session=`0x412011A5`；一次残帧后执行`84608 -> 81920`检查点重同步，当前任务自动完成262144/262144及APP版本1回探 |
| R03 ESP32传输中复位 | PASS | 复位前后Session均为`0x0842160F`；ESP32重启后STM32保持RECEIVING(4)，未实际重新擦除，首个进度直接为245760字节并最终完成APP版本1回探 |
| R04 双端掉电恢复 | PASS | 917504字节版本2镜像；两轮在110592和421888检查点双端冷断电；重启后均保持RECEIVING(4)和原Session，约40毫秒跳过实际擦除，最终完成整包校验并回探APP版本2 |
| R05-A/B UART中断 | PASS | 短中断Session=`0xE1ABAE42`在当前任务内恢复；长中断Session=`0xDFB9C6C7`耗尽5次重试后失败，STM32保持RECEIVING(4)，接线恢复后沿用同Session从93472字节附近续传，最终完成917504字节及APP版本2回探 |
| R06 ABORT及恢复 | PASS | 原Session=`0xB2CDF3AE`在进度66880处以`ESP_ERR_INVALID_STATE`失败，实时探测STM32为FAILED(8)；恢复使用新Session=`0xEF267165`重新擦除并完成917504字节及APP版本2回探 |
| R07 本地坏包拦截 | DEFERRED | 未执行；本地镜像CRC、Header CRC、设备身份和长度错误仍需板上验证 |
| R08 ACTIVATE应答边界 | PASS | R14直接覆盖最不确定的“有效ACTIVATE应答完全丢失”边界；ACTIVATE未重发，以APP回探成功判定升级完成 |
| R09 冷启动保持性 | DEFERRED | 未执行双端完全断电10秒后的连续10轮保持性验证 |
| R10 DATA ACK丢失 | PASS | Session=`0x28AD346F`；有效offset 0应答被丢弃一次，相同DATA重发后继续至12888/12888，约6.40秒，APP版本1回探成功，故障项自动恢复off |
| R11 重复DATA | PASS | Session=`0xDF65E8D8`；offset 0的重复DATA被幂等接受且进度不变，最终12888/12888、APP版本1回探成功 |
| R12 Gap Offset | PASS | Session=`0xF68B4A6C`；offset 224被拒绝并返回期望offset 0，重同步后升级成功 |
| R13 错误传输CRC | PASS | 错误CRC Session=`0x4FFCD7FE`在VERIFY阶段以`ESP_ERR_INVALID_CRC/device status 11`失败，未进入ACTIVATE；正确CRC以新Session=`0xF60DE936`恢复成功 |
| R14 ACTIVATE ACK丢失 | PASS | Session=`0x1D6CD7AC`；有效ACTIVATE响应被丢弃且未重发，通过APP版本1回探判定成功 |
| R15 指定次数超时 | PASS | 4次DATA超时的Session=`0xC7ED4E66`由第5次尝试恢复；5次超时的Session=`0x39B916FF`按预期失败并留驻RECEIVING，下一轮沿用同一Session续传成功 |

R11～R15于2026-08-31通过自动脚本在`COM5 @ 115200`完成，原始日志为
`build/m7_reliability_result.txt`，SHA-256为
`AE06DABC1D0FCBA4192C0DF726161C7C4EA329F147EF42E14879FB16040E057E`。

正常升级10轮稳定性于2026-08-31通过自动脚本在`COM5 @ 115200`完成。原始日志为
`build/m7_stability_result.txt`，SHA-256为
`8228CED9E4FC1BAA4D95E91645B7C4602B009E171703E4702204B48DD6740836`。日志中仅有
每轮擦除阶段固定2次`QUERY_PROGRESS(0x13)`轮询超时，无其他警告或错误。

### R01 基线重复升级

连续执行3轮`upgrade start`，每轮等待`SUCCESS`后再执行下一轮。

期望：每轮均完成12888字节传输、VERIFY、ACTIVATE和APP回探；擦除期间允许出现
有限次`QUERY_PROGRESS(0x13)`超时，但必须随后进入`TRANSFER`。

### R02 STM32传输中复位与4 KiB检查点恢复

1. 执行`upgrade start`。
2. 日志出现`Transferred 4096/...`后、到达下一个检查点前按一次STM32 `NRST`。
3. 不复位ESP32，不重新输入命令，继续观察当前任务。

期望：STM32保持同一Session，返回持久化Offset 4096；ESP32出现Offset重同步日志，
从4096重新发送，最终进入`SUCCESS`。不得重新擦除APP。

实测使用256 KiB尾部`0xFF`填充镜像，在offset 84608附近复位STM32，设备恢复最后持久化
检查点81920；修复无效响应快速重试问题后，当前任务完成`84608 -> 81920`重同步并最终
成功。完整过程见[`m7_r02_test_evidence.md`](m7_r02_test_evidence.md)。

当前12888字节镜像传输窗口较短。正式故障测试宜使用保持有效向量表、尾部填充
`0xFF`的较大测试镜像延长操作时间；填充后的镜像仍不得超过`0x000E0000`。

### R03 ESP32传输中复位与Session恢复

1. 执行`upgrade start`。
2. 出现`Transferred 4096/...`后按ESP32 `EN/RESET`，STM32保持供电。
3. ESP32重启后执行`firmware validate`和`upgrade start`。

期望：ESP32查询到STM32现有Session和Offset，使用同一Session继续传输；STM32处于
`RECEIVING`时收到重复ERASE只返回OK，不实际再次擦除。最终APP回探成功。

实测复位前后Session均为`0x0842160F`。ESP32重启后STM32保持`RECEIVING(4)`；恢复任务
没有耗时擦除或擦除轮询，首个进度直接为245760/262144，最终成功。完整过程见
[`m7_r03_test_evidence.md`](m7_r03_test_evidence.md)。

### R04 双端掉电恢复

1. 传输越过4096字节后同时断开两板电源。
2. 重新上电并等待ESP32控制台就绪。
3. 执行`firmware validate`和`upgrade start`。

期望：STM32从Metadata载入最后检查点，ESP32发现并复用Session，从检查点继续。
最终进入`SUCCESS`。至少选择两个不同的4 KiB对齐检查点执行；人工操作无法保证恰好
在最初计划的4096和8192字节断电时，应记录日志中实际落盘的检查点，不得用命令发出时
的瞬时进度代替。

实测使用917504字节版本2镜像，在110592和421888两个检查点完成双端冷断电。两轮重新
上电后STM32均为`RECEIVING(4)`，恢复前后Session分别保持`0xD89D0988`和
`0xF6666D96`；恢复任务约40毫秒跳过实际擦除，第一条进度分别为114688和425984字节，
最终均完成917504/917504、整包校验、激活和APP版本2回探。完整过程见
[`m7_r04_test_evidence.md`](m7_r04_test_evidence.md)。

R04准备阶段还发现并修复了连续升级后的Journal空间缺陷：最新状态为`PENDING_BOOT`
且剩余记录不足232条时，旧APP会令`ENTER_BOOT`返回`FLASH_ERROR(10)`。修复后允许在
保留最新已校验记录的前提下整理`PENDING_BOOT`；活动写入和校验状态仍禁止整理。

### R05 UART临时中断

1. 传输期间只断开STM32 PA9到ESP32 GPIO18的返回线1～3秒，然后恢复。
2. 保持TX、供电和GND不变。

期望：短中断在5次重试内恢复；同一DATA被重发并由STM32幂等接受。若中断超过重试
窗口导致`FAILED`，恢复接线后重新执行`upgrade start`应从原Session继续并成功。

R05-A已实测通过：版本2、917504字节镜像在Session `0xE1ABAE42`传输期间断开返回线
约2秒；ESP32观察到DATA第1次超时及第2次残帧解码失败，随后在当前任务内继续传输，
最终完成整包校验、激活和APP版本2回探。

R05-B也已实测通过：Session `0xDFB9C6C7`的返回线实际中断16.199秒，5次DATA重试
全部超时，当前任务按预期以`ESP_ERR_TIMEOUT`失败；STM32保持`RECEIVING(4)`，失败进度
93472/917504。接线恢复后脚本沿用同一Session重新启动，首条进度为94208，最终完成
917504/917504、`ESP_OK`和APP版本2回探。完整记录见
[`m7_r05_test_evidence.md`](m7_r05_test_evidence.md)。

### R06 人工ABORT及恢复

1. `TRANSFER`期间输入`upgrade abort`。
2. 确认当前任务进入`FAILED`，设备不会激活不完整镜像。
3. 再次执行`upgrade start`。

期望：ABORT使原Session进入失败态；下一轮创建新Session、重新擦除并完成升级，
最后能够发现APP。

实测原Session `0xB2CDF3AE`在日志进度65536后收到ABORT，任务以
`ESP_ERR_INVALID_STATE`失败，状态记录进度66880。`upgrade status`中的远端状态4是ABORT
前的缓存值；紧随其后的实时`upgrade probe`确认STM32已经为`FAILED(8)`。恢复任务创建
新Session `0xEF267165`，重新经历约8.9秒擦除并从4096字节开始传输，最终完成
917504/917504和APP版本2回探。完整记录见
[`m7_r06_test_evidence.md`](m7_r06_test_evidence.md)。

### R07 本地包损坏拦截

分别准备以下测试包并写入`stm_fw`：

- 镜像数据翻转1 bit，但不更新Manifest CRC32；
- Manifest Header CRC32错误；
- Product ID或Hardware ID不为`0x0001`；
- 镜像长度为0或超过`0x000E0000`。

执行`firmware validate`和`upgrade start`。

期望：分别返回CRC、格式、不支持设备或长度错误；状态停在`FAILED`，Session为0，
STM32原APP仍可由`upgrade probe`发现。测试后恢复正确包并再次校验。

### R08 ACTIVATE应答边界回归

正常升级中即使ACTIVATE应答丢失、截断或落在复位边界，ESP32也只能发送一次
ACTIVATE，然后保持UART静默1秒，以APP的HELLO和GET_INFO作为最终判据。

期望：日志可出现`ACTIVATE response was ambiguous`，但随后必须进入`WAIT_APP`并
发现正确版本APP；不得把重试ACTIVATE发给APP并因`BUSY(12)`误判失败。

### R09 冷启动保持性

完成一次升级后，两板完全断电10秒再上电，连续执行10轮。

期望：每轮ESP32本地包仍能通过CRC32，STM32均从Bootloader跳转APP，`upgrade probe`
返回一致的版本、Product、Hardware和APP区域。

### R05/R06/R09引导式自动测试与分析

项目已提供`tools/run_m7_guided.py`。脚本自动等待指定传输进度、执行命令、解析重试和
Session、核对Bootloader状态、核对最终APP版本并生成报告；只有真实拔线和断电动作由
本机终端提示操作者完成。运行前用`Ctrl+]`退出VS Code串口监视器。

R05-B的重现命令：

```powershell
py -3 .\tools\run_m7_guided.py `
  --port COM5 `
  --case R05B `
  --version 2 `
  --trigger-bytes 65536 `
  --case-timeout 240 `
  --confirm-destructive
```

脚本到达65536字节后才提示断开STM32 PA9到ESP32 GPIO18的返回线，自动倒计时10秒，
再提示接回。它要求当前任务先耗尽重试并进入`FAILED`、STM32保持`RECEIVING(4)`，然后
自动重新启动升级，并验证沿用原Session续传成功。该用例已经通过，命令仅用于重现。

R06不需要物理操作，可自动完成ABORT、新Session、重新擦除及恢复验证：

```powershell
py -3 .\tools\run_m7_guided.py `
  --port COM5 `
  --case R06 `
  --version 2 `
  --trigger-bytes 65536 `
  --case-timeout 240 `
  --confirm-destructive
```

该用例已经通过，命令仅用于重现。

R09每轮仅提示一次断电和一次上电，脚本负责10秒断电倒计时、COM口重连和结果判定：

```powershell
py -3 .\tools\run_m7_guided.py `
  --port COM5 `
  --case R09 `
  --version 2 `
  --cycles 10 `
  --power-off-seconds 10 `
  --confirm-destructive
```

R09尚未执行，归档状态为`DEFERRED`。

每项默认输出`build/m7_<用例>_result.txt`原始日志和
`build/m7_<用例>_report.md`分析报告，并打印日志SHA-256。失败时脚本还会自动采集
`upgrade status`、`upgrade probe`和`test fault show`，在失败报告中写入首要原因。

## 5. 需要测试专用故障注入钩子的用例

以下测试不建议依赖手工拔线，因为时序不可重复。项目已增加默认关闭的
`CONFIG_GATEWAY_RELIABILITY_TEST`和一次性故障注入命令；R10～R15已通过板上验证。
生产构建关闭该选项且不注册`test`命令。

| 用例 | 注入位置 | 注入动作 | 期望结果 |
| --- | --- | --- | --- |
| R10 DATA ACK丢失 | ESP32收到有效DATA响应后 | 丢弃一次响应并重发完全相同ADU | STM32比较已写Flash后ACK，Offset不重复增长，最终成功 |
| R11 重复DATA | 一个DATA成功后 | 主动再次发送同Offset和数据 | 返回OK和相同next offset，Flash不变化 |
| R12 Gap Offset | DATA发送前 | 一次性发送`offset + length` | STM32返回`BAD_OFFSET`和真实Offset，ESP32重同步后成功 |
| R13 错误传输CRC | START Manifest编码前 | 只覆盖发送给STM32的CRC，不修改本地包 | VERIFY_FAILED，不发送ACTIVATE；关闭注入后可恢复 |
| R14 ACTIVATE ACK丢失 | ACTIVATE收到有效响应后 | 丢弃该响应 | 不重发ACTIVATE，静默后APP回探成功 |
| R15 指定次数超时 | UART事务返回上层前 | 对指定命令模拟1～4次超时 | 未超过5次时恢复；超过上限进入FAILED且可续传 |

建议的测试控制接口：

```text
test fault show
test fault clear
test fault drop_data_ack_once
test fault duplicate_data_once
test fault gap_offset_once
test fault bad_manifest_crc_once
test fault drop_activate_ack_once
test fault timeout <command> <count>
```

每个注入项必须是“一次性消费”，执行后自动清除，避免后续用例受到残留配置影响。

单项用例的通用执行方式：

```text
test fault clear
test fault <fault-name>
test fault show
upgrade start
upgrade status
upgrade probe
test fault show
```

最后一次`show`必须证明相应用例已自动恢复为`off`。

### R10～R15一键自动测试与分析

项目已提供`tools/run_m7_reliability.py`，可代替逐条输入上述命令。运行前必须：

1. 给ESP32烧录可靠性测试构建，而不是生产构建；
2. STM32与ESP32保持当前UART接线并正常供电；
3. 用`Ctrl+]`退出并关闭VS Code/ESP-IDF串口监视器，避免COM口被占用；
4. 确认`stm_fw`中的包仍是已通过`firmware validate`的版本1测试包。

从仓库根目录执行全部软件故障注入用例：

```powershell
py -3 .\tools\run_m7_reliability.py `
  --port COM_ESP `
  --version 1 `
  --cases R10,R11,R12,R13,R14,R15 `
  --confirm-destructive
```

将`COM_ESP`换成ESP32控制台串口。R10已经手工通过时，可只运行剩余用例：

```powershell
py -3 .\tools\run_m7_reliability.py `
  --port COM_ESP `
  --version 1 `
  --cases R11,R12,R13,R14,R15 `
  --confirm-destructive
```

脚本会逐项完成清除故障、注入、升级、状态核对和恢复，并分析以下关键行为：

- R10～R12：重试或Offset重同步后必须成功，且一次性故障必须自动变为`off`；
- R13：错误CRC必须得到`VERIFY_FAILED/device status 11`，不得进入`ACTIVATE`，随后自动用正确CRC恢复；
- R14：ACTIVATE只发送一次，应答丢失后通过APP回探判定成功；
- R15：4次DATA超时应在第5次重试恢复；5次超时应进入`FAILED`并保留`RECEIVING`会话，下一轮必须沿用同一Session续传成功。

任一用例未满足判据时，脚本停止后续用例，自动保存`upgrade status`、`upgrade probe`
和`test fault show`，以免后续结果被前一故障污染。默认输出：

- `build/m7_reliability_result.txt`：完整原始串口日志；
- `build/m7_reliability_result.json`：机器可读的逐项判定和证据；
- `build/m7_reliability_report.md`：可直接阅读的逐项分析报告；
- 终端输出原始日志SHA-256，用于验收留档。

`run_m7_reliability.py`自动覆盖R10～R15；`run_m7_guided.py`覆盖R05、R06和R09。
没有可编程电源、复位控制器或UART模拟开关时，真实拔线、复位和断电本身仍需人工执行，
但触发时刻、倒计时、串口命令、状态分析和报告均由脚本负责。

## 6. 连续10次稳定性验证

故障注入全部关闭并恢复正确包后，独立执行10轮正常升级。每轮必须完成：

```text
firmware validate
upgrade start
upgrade status
upgrade probe
```

自动化脚本应等待状态从运行态变为`SUCCESS/FAILED`，不得使用固定短延时判断成功。
任一轮非`SUCCESS`、进度不等于镜像长度、APP探测失败或版本不匹配都立即终止。

建议统计：

- 成功轮数和失败轮数；
- 每轮Session、总耗时、重试次数和擦除查询超时次数；
- 传输字节数、最终设备状态和APP版本；
- 最小、最大和平均升级耗时；
- 完整原始日志文件SHA-256。

当前12888字节镜像的正常升级实测约6.4秒。稳定性验收可将单轮上限暂定为20秒；
超时必须保留日志分析，不能只放宽阈值。

项目已提供自动化脚本。运行前关闭VS Code串口监视器，避免两个程序同时打开ESP32
控制台端口：

```powershell
py -3 .\tools\run_m7_stability.py `
  --port COM_ESP `
  --cycles 10 `
  --version 1 `
  --clear-faults `
  --confirm-destructive
```

默认日志为`build/m7_stability_result.txt`。脚本不使用固定短延时，而是等待ESP32
状态机的成功或失败日志，再核对`upgrade status`和`upgrade probe`；完成后输出日志
SHA-256。

## 7. 结果记录模板

| 字段 | 内容 |
| --- | --- |
| 用例ID | Rxx |
| 日期/操作者 |  |
| ESP32固件SHA-256 |  |
| STM32包版本/大小/CRC32 |  |
| Session |  |
| 故障注入点和持续时间 |  |
| 故障前Offset |  |
| 恢复Offset |  |
| 最终状态 | SUCCESS / FAILED |
| APP回探结果 |  |
| 总耗时/重试次数 |  |
| 原始日志路径及SHA-256 |  |
| 结论 | PASS / FAIL |

## 8. M7可靠性验收出口

以下条件全部满足后，M7可靠性验证才能关闭：

- [ ] R01～R09板上测试通过并留存日志；R07、R09当前为`DEFERRED`。
- [x] 测试专用故障注入默认关闭，生产构建不包含可触发入口。
- [x] R10～R15故障注入测试通过。
- [x] STM32复位、ESP32复位和双端掉电均能从检查点恢复。
- [ ] 本地坏包和错设备预发送拦截尚未执行；传输CRC错误不ACTIVATE已由R13通过。
- [x] 正常完整升级连续10/10成功。
- [ ] 冷启动探测连续10/10成功；当前为`DEFERRED`。
- [ ] 测试后恢复生产配置并重新完成一次基线升级；当前为`DEFERRED`。

当前结论为“已执行用例通过、阶段性归档”，不是“完整可靠性验收关闭”。
