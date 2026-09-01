# 一、先确定项目边界

## 1.1 最终架构

```
                    Windows 11 PC
              ┌─────────────────────┐
              │ FastAPI固件服务器   │
              │ 固件仓库/任务管理   │
              │ 升级日志/测试脚本   │
              └──────────┬──────────┘
                         │ HTTPS
                         ▼
              ┌─────────────────────┐
              │ ESP32-S3升级网关    │
              │                     │
              │ Wi-Fi管理           │
              │ HTTPS下载           │
              │ STM32固件缓存       │
              │ 固件校验            │
              │ 节点管理            │
              │ 升级任务调度        │
              └──────────┬──────────┘
                         │
             ┌───────────┼───────────┐
             │           │           │
           UART        RS485        CAN
             │           │           │
             ▼           ▼           ▼
       STM32节点A    STM32节点B   STM32节点C
       Bootloader    Bootloader   Bootloader
       Minimal APP   Minimal APP  Minimal APP
```

目前只有一块 STM32F407ZGT6，完全可以先完成：

```
PC服务器
   ↓
ESP32-S3
   ↓ UART
STM32F407 Bootloader
   ↓
STM32 Minimal APP
```

在 UART 端到端链路稳定之前，不建议增加第二块 STM32，也不要开始 CAN。

## 1.2 项目必须实现的功能

最终建议实现：

- ESP32-S3 Wi-Fi连接和自动重连
- HTTP/HTTPS固件下载
- ESP32 Flash固件缓存
- Manifest解析
- SHA-256或CRC32完整性校验
- STM32 Bootloader与APP分区
- APP合法性检查和跳转
- 分包传输
- ACK/NACK
- 超时重传
- 重复包识别
- 断点查询和恢复
- 升级状态持久化
- UART传输
- RS485半双工传输
- CAN分片传输
- 节点寻址
- 单节点及顺序批量升级
- 故障注入测试
- 升级数据统计

## 1.3 明确不做的内容

第一版不要做：

- STM32 Bootloader自身远程升级
- 复杂Web前端
- 多租户和复杂权限
- 微服务架构
- MQTT和HTTPS同时作为任务通道
- 三条总线并行升级
- STM32内部Flash真正A/B回滚
- CAN-FD
- 差分升级
- 固件压缩
- 同时升级多个节点

这些功能会明显提高烂尾概率。

STM32F407ZGT6最多具有1 MB内部Flash；ESP32-S3自身提供经典TWAI控制器，但不是CAN-FD控制器，而且使用CAN总线仍需外接收发器。

------

# 二、当前硬件条件评估

你已经具备：

| 设备                  | 用途                         |
| --------------------- | ---------------------------- |
| STM32F407ZGT6开发板   | Bootloader与目标APP          |
| DAP-Link              | STM32下载和调试              |
| ESP32-S3开发板        | 网络升级网关                 |
| Windows 11 PC         | 服务器、开发和测试           |
| VSCode + ESP-IDF      | ESP32开发                    |
| Keil + Keil Assistant | STM32开发调试                |
| STM32CubeMX           | STM32时钟、引脚和外设初始化  |
| ChatGPT Plus          | 架构评审、代码检查、测试设计 |

可以立即开始UART版本。

## 2.1 建议补充的硬件

### 第一阶段必须补充

- 杜邦线
- 稳定的USB供电
- 一个USB转TTL串口模块
- 一个8通道USB逻辑分析仪

当前硬件只有一个USB转TTL，允许USART1同时承担调试日志和升级协议，但必须采用**分时独占**：

- APP正常调试模式可以输出文本日志。
- Bootloader进入升级协议模式后，USART1只传输协议帧，禁止裸`printf`。
- 正式运行时的Bootloader不主动输出日志，诊断信息写入内存缓冲区，由PC或ESP32通过协议查询。
- 文本模式和协议模式通过明确的状态切换或复位切换，不能在同一字节流中交错发送。

Modbus RTU或YMODEM只能定义升级帧，不能自动隔离任意文本日志。即使接收端能够依靠CRC丢弃文本，日志仍可能破坏当前帧并触发超时重传，因此不能把“解析器可以重新同步”当作安全复用机制。

逻辑分析仪用于检查：

- UART波特率
- RS485 DE时序
- 数据包间隔
- 超时重传
- CAN收发信号
- STM32复位时序

### RS485阶段需要

- 2个3.3V RS485收发模块
- 推荐选择MAX3485、SP3485一类3.3V器件
- 2个120Ω终端电阻
- 双绞线

不建议直接购买只适合5V逻辑的MAX485模块。

ESP-IDF的UART驱动原生支持RS485半双工模式，可以利用RTS控制收发器的DE/RE。

### CAN阶段需要

- 2个CAN收发器模块
- 可使用SN65HVD230等3.3V收发器
- 2个120Ω终端电阻
- 双绞线

ESP32-S3只有CAN控制器部分，没有CAN物理收发器；STM32同样需要外部CAN收发器。ESP32-S3的TWAI是经典CAN，不支持CAN-FD。

### 多节点阶段再补充

- 第二块STM32开发板
- 或其他带CAN/USART的STM32板

不要现在就购买多块。等UART版本完成后，再增加第二节点验证多节点调度。

## 2.2 可选的故障注入硬件

后期可以增加：

- 小型继电器模块
- N沟道MOSFET电源开关电路
- ESP32控制的STM32复位线
- ESP32控制的STM32电源开关

用于自动模拟：

- 写Flash期间断电
- 擦除期间断电
- 新APP第一次启动时断电
- 通信期间复位

开始阶段用手动拔电即可。

------

# 三、建议的开发方式

不要强行把ESP32、STM32、服务器统一到一个编译环境。

建议：

| 模块             | 工具                         |
| ---------------- | ---------------------------- |
| STM32 Bootloader | CubeMX生成工程，Keil编译调试 |
| STM32 APP        | CubeMX生成工程，Keil编译调试 |
| ESP32网关        | VSCode + ESP-IDF             |
| 服务器           | VSCode + Python              |
| 测试工具         | VSCode + Python              |
| 版本管理         | Git                          |
| 文档             | Markdown                     |

STM32CubeF4官方软件包包含HAL、LL、CMSIS和大量外设示例，可以作为Flash、UART、CAN等模块的实现参考。

------

# 四、建立工程仓库

建议在Windows创建不含中文和空格的路径：

```
D:\workspace\remote_upgrade_system
```

目录结构：

```
remote_upgrade_system/
├── firmware/
│   ├── esp32_gateway/
│   ├── stm32_bootloader/
│   └── stm32_app/
│
├── protocol/
│   ├── include/
│   │   ├── upgrade_protocol.h
│   │   ├── upgrade_commands.h
│   │   ├── modbus_rtu.h
│   │   └── firmware_manifest.h
│   ├── src/
│   │   ├── modbus_rtu.c
│   │   ├── crc16_modbus.c
│   │   └── crc32.c
│   └── tests/
│
├── server/
│   ├── app/
│   ├── firmware/
│   ├── database/
│   └── tests/
│
├── tools/
│   ├── pack_firmware.py
│   ├── serial_upgrade.py
│   ├── fault_injector.py
│   ├── generate_version.py
│   └── report_generator.py
│
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── flash_layout.md
│   ├── test_plan.md
│   ├── test_report.md
│   ├── hardware_wiring.md
│   └── ai_context.md
│
├── .gitignore
├── .gitattributes
└── README.md
```

`protocol`目录中的代码必须是纯C代码，不依赖STM32 HAL或ESP-IDF类型。这样同一套编解码逻辑可以运行在：

- STM32
- ESP32
- Windows主机测试程序

建议一开始就提交Git：

```
git init
git add .
git commit -m "chore: initialize remote upgrade system"
```

开发过程按小功能提交，例如：

```
feat(boot): add application vector validation
feat(protocol): add Modbus RTU upgrade codec
feat(gateway): add firmware partition writer
test(protocol): add duplicate packet cases
fix(boot): clear pending interrupts before app jump
```

------

# 五、冻结关键设计参数

在写代码前，新建：

```
docs/project_constants.md
```

内容至少包括：

```
MCU: STM32F407ZGT6
STM32 Flash: 1 MiB
Bootloader base: 0x08000000
APP base: 0x08020000
APP maximum size: 0x000E0000
Protocol version: 1
UART baud rate: 115200 initially
UART firmware block size: 224 bytes
RS485 firmware block size: 224 bytes
UART/RS485 framing: Modbus RTU
Upgrade function code: 0x41
CAN logical block size: 192 or 256 bytes
Metadata checkpoint size: 4096 bytes
Byte order: little-endian
Maximum retry count: 5
```

所有工程必须引用这些定义，不要在多个源文件中复制地址和数值。

------

# 六、STM32 Flash分区设计

STM32F407的Flash扇区大小不均匀，因此必须按扇区边界划分。具体擦写约束应以RM0090和PM0081为准。

建议使用：

```
0x08000000 ┌────────────────────────────┐
           │ Bootloader                 │
           │ Sector 0~3                 │
           │ 64 KiB                     │
0x08010000 ├────────────────────────────┤
           │ Boot Metadata Journal      │
           │ Sector 4                   │
           │ 64 KiB                     │
0x08020000 ├────────────────────────────┤
           │ STM32 APP                  │
           │ Sector 5~11                │
           │ 896 KiB                    │
0x08100000 └────────────────────────────┘
```

对应宏：

```
#define STM32_FLASH_BASE       0x08000000UL

#define BOOT_BASE_ADDR         0x08000000UL
#define BOOT_MAX_SIZE          0x00010000UL

#define META_BASE_ADDR         0x08010000UL
#define META_MAX_SIZE          0x00010000UL

#define APP_BASE_ADDR          0x08020000UL
#define APP_END_ADDR           0x08100000UL
#define APP_MAX_SIZE           0x000E0000UL
```

## 6.1 为什么给Bootloader保留64 KiB

因为最终Bootloader可能包含：

- UART
- RS485
- CAN
- Flash驱动
- 协议解析
- CRC32
- SHA-256
- 状态管理
- 日志
- 固件签名验证

只保留32 KiB可能过于紧张。

## 6.2 为什么元数据占一个64 KiB扇区

不是因为元数据真的需要64 KiB，而是因为STM32F407的扇区划分决定了该区域最小就是一个完整扇区。

不能每更新一次状态就擦除一次整个扇区。应采用追加日志：

```
Record 0
Record 1
Record 2
...
Record N
空白区域
```

每次追加一条记录。启动时扫描最后一条CRC有效且序号最大的记录。

------

# 七、第一阶段：先完成STM32 APP重定位

不要先写升级协议。第一步只验证：

```
Bootloader能够从0x08000000启动
→ 跳转到0x08020000
→ APP正常运行
```

## 7.1 创建STM32 APP工程

在CubeMX中：

1. 选择实际STM32F407ZGT6型号。
2. 配置系统时钟。
3. 配置一个LED GPIO。
4. 配置一个调试UART。
5. 暂时不使用FreeRTOS。
6. 生成Keil工程。

Minimal APP只需实现：

- LED周期翻转
- 串口打印版本
- 输出APP启动地址
- 输出复位原因

示例：

```
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USARTx_UART_Init();

    printf("STM32 APP started\r\n");
    printf("Version: 0.1.0\r\n");
    printf("APP base: 0x%08lX\r\n", APP_BASE_ADDR);

    while (1)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(500);
    }
}
```

## 7.2 修改Keil APP下载地址

在Keil中：

```
Options for Target
→ Target
→ IROM1
```

设置：

```
Start: 0x08020000
Size:  0x000E0000
```

## 7.3 修改中断向量表

推荐在`system_stm32f4xx.c`中设置：

```
#define VECT_TAB_OFFSET  0x00020000U
```

确认最终执行：

```
SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET;
```

不要只修改Keil下载地址而不修改向量表，否则普通代码可能运行，但SysTick和外设中断会进入错误的中断向量。

## 7.4 生成BIN文件

Keil编译后通常得到AXF/HEX文件。增加Post-build命令：

```
fromelf --bin --output ".\Objects\stm32_app.bin" ".\Objects\stm32_app.axf"
```

实际路径根据工程Target名称调整。

## 7.5 阶段验收

使用DAP-Link直接将APP下载到：

```
0x08020000
```

验收条件：

- 上电后暂时不能自动执行，因为0x08000000尚无Bootloader
- 使用调试器将PC设置到APP入口后能够运行
- LED正常闪烁
- UART日志正常
- 中断正常工作

------

# 八、第二阶段：实现最小Bootloader跳转

## 8.1 创建独立Bootloader工程

新建第二个CubeMX工程：

```
firmware/stm32_bootloader
```

配置：

- Bootloader位于0x08000000
- IROM1大小0x10000
- 调试UART
- LED
- 暂不配置RS485/CAN
- 暂不使用FreeRTOS

Bootloader不建议使用FreeRTOS。裸机状态机更小，更容易分析异常路径。

## 8.2 APP向量合法性检查

APP起始位置的前两个Word：

```
APP_BASE + 0：初始MSP
APP_BASE + 4：Reset_Handler地址
```

至少检查：

- MSP位于合法RAM区域
- Reset_Handler位于APP Flash范围
- Reset_Handler最低位为1，表示Thumb状态
- 地址不是0xFFFFFFFF
- APP状态已经确认有效

示意：

```
bool boot_is_app_vector_valid(void)
{
    uint32_t app_sp =
        *(volatile uint32_t *)(APP_BASE_ADDR + 0U);

    uint32_t app_reset =
        *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

    bool sp_valid =
        (app_sp >= 0x20000000UL) &&
        (app_sp <  0x20030000UL);

    bool reset_valid =
        ((app_reset & 1U) != 0U) &&
        ((app_reset & ~1UL) >= APP_BASE_ADDR) &&
        ((app_reset & ~1UL) < APP_END_ADDR);

    return sp_valid && reset_valid;
}
```

RAM范围需要根据你的具体链接脚本和芯片存储区域进一步完善。

## 8.3 跳转前清理环境

跳转前应：

1. 禁止全局中断。
2. 停止SysTick。
3. 反初始化Bootloader使用的外设。
4. 禁止并清除NVIC中断。
5. 设置VTOR。
6. 设置MSP。
7. 跳转至APP Reset_Handler。

示意：

```
typedef void (*app_entry_t)(void);

void boot_jump_to_app(void)
{
    uint32_t app_sp =
        *(volatile uint32_t *)(APP_BASE_ADDR + 0U);

    uint32_t app_reset =
        *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

    app_entry_t app_entry = (app_entry_t)app_reset;

    __disable_irq();

    HAL_SuspendTick();
    HAL_DeInit();

    for (uint32_t i = 0; i < 8; ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    SCB->VTOR = APP_BASE_ADDR;

    __set_MSP(app_sp);
    __DSB();
    __ISB();

    app_entry();

    while (1) {
    }
}
```

具体NVIC寄存器组数量应根据Cortex-M4实现确认，不要盲目复制固定数量。

## 8.4 最初启动逻辑

```
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DEBUG_UART_Init();

    printf("Bootloader started\r\n");

    if (boot_is_app_vector_valid())
    {
        printf("Application valid, jumping...\r\n");
        HAL_Delay(500);
        boot_jump_to_app();
    }

    printf("Application invalid, recovery mode\r\n");

    while (1)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        HAL_Delay(100);
    }
}
```

## 8.5 阶段验收

验收：

- 先烧录APP到0x08020000
- 再烧录Bootloader到0x08000000
- STM32复位
- 先输出Bootloader日志
- 自动跳入APP
- APP SysTick正常
- APP USART1调试输出正常
- APP复位后仍可正常启动

这一阶段失败时，不要开始任何升级协议。

------

# 九、第三阶段：实现STM32 Flash驱动

本阶段只实现受保护的APP Flash驱动和显式板上自测，不混入Modbus、Metadata或整包镜像校验。`image_validator`在协议传输与CRC校验阶段再加入。

建立模块：

```
stm32_bootloader/
└── App/
    ├── Inc/
    │   ├── flash_layout.h
    │   ├── flash_if.h
    │   └── flash_if_self_test.h
    └── Src/
        ├── flash_if.c
        └── flash_if_self_test.c
```

## 9.1 公共接口使用APP相对偏移

协议层不能向Flash驱动传递任意绝对地址。驱动只接受相对`APP_BASE_ADDR`的offset，绝对地址在边界检查通过后由驱动内部计算：

```c
bool flash_if_is_app_range(uint32_t offset, uint32_t length);

flash_if_status_t flash_if_get_erase_plan(
    uint32_t image_size,
    uint32_t *first_sector,
    uint32_t *sector_count
);

flash_if_status_t flash_if_erase_app(uint32_t image_size);

flash_if_status_t flash_if_write_app(
    uint32_t offset,
    const uint8_t *data,
    uint32_t length,
    bool final_chunk
);

flash_if_status_t flash_if_verify_app(
    uint32_t offset,
    const uint8_t *data,
    uint32_t length
);
```

该设计使正常升级接口无法表示Bootloader或Metadata地址，形成第一层写保护。Flash驱动仍必须在每次调用时重新执行范围检查。

## 9.2 地址检查必须防溢出

禁止只判断`offset + length <= APP_MAX_SIZE`，因为加法可能整数溢出。当前规则为：

```c
if ((length == 0U) || (offset >= APP_MAX_SIZE)) {
    return false;
}

if (length > (APP_MAX_SIZE - offset)) {
    return false;
}
```

必须拒绝空数据、超出896 KiB的镜像、越界offset和构造出的32位整数溢出输入。

## 9.3 擦除策略

APP使用Sector 5～11，每个Sector为128 KiB。根据镜像大小计算实际擦除数量：

```c
sector_count = 1U + ((image_size - 1U) / 0x20000U);
```

边界期望如下：

| image_size | 擦除范围 |
| --- | --- |
| 1 byte | Sector 5 |
| 128 KiB | Sector 5 |
| 128 KiB + 1 byte | Sector 5～6 |
| 896 KiB | Sector 5～11 |
| 896 KiB + 1 byte | 拒绝 |

擦除使用`FLASH_VOLTAGE_RANGE_3`，与当前3.3 V供电一致。Flash成功解锁后，无论擦除成功或失败都必须重新锁定；错误路径保存HAL错误码和失败Sector对应的起始地址。

## 9.4 写入与分包对齐策略

第一版使用32-bit Word编程，并采用以下固定规则：

- offset必须4字节对齐。
- 非最后数据块的length必须是4的倍数。
- 只有最后数据块允许不足4字节，并使用`0xFF`补齐最后一个Word。
- 不允许把未对齐的每一个协议包分别补齐，否则下一包可能与上一包的补齐Word重叠。
- 不使用未对齐的`uint32_t *`强制转换；逐字节组装Word。
- 每写入一个Word立即读回比较。
- 成功解锁后的所有返回路径都必须重新锁定Flash。

协议的224-byte固件数据长度本身是4的倍数，因此除固件最后一包外都天然满足对齐要求。

## 9.5 显式破坏性板上自测

普通和debug构建中，`FLASH_IF_SELF_TEST_ENABLE`均默认为0。只有准备好APP恢复镜像后，才可以手工设为1：

```
边界与Sector计算检查
→ 擦除Sector 5
→ 写入256字节确定性测试序列
→ 每Word立即读回
→ 再逐字节完整校验
→ 额外写入3字节最后块，验证`0xFF`补齐逻辑
→ 比较Sector 0～4测试前后的哈希
→ 输出状态、HAL错误码、失败地址和耗时
→ 停留在Bootloader
```

该测试会破坏当前APP。测试完成后必须通过DAP重新下载APP，并重新执行M2启动验证。自测宏不得提交为1，也不得在正式升级流程中自动触发。

## 9.6 阶段验收

- 普通和debug两个Keil目标均为0 Error、0 Warning。
- `1 byte`、`128 KiB`、`128 KiB + 1 byte`、`896 KiB`的Sector计算正确。
- `896 KiB + 1 byte`、空数据、越界offset、整数溢出和错误对齐输入均被拒绝。
- 实际擦除、写入和读回256 bytes通过。
- Sector 0～4未发生变化，公共接口无法表示其地址。
- Flash错误码和失败地址可查询，擦除测试耗时得到记录。
- 恢复APP后，Bootloader跳转、VTOR、LED和USART1调试输出均不退化。

------

# 十、第四阶段：实现升级元数据日志

## 10.1 状态定义

```
typedef enum {
    BOOT_STATE_EMPTY = 0,
    BOOT_STATE_APP_VALID,
    BOOT_STATE_UPDATE_REQUESTED,
    BOOT_STATE_ERASING,
    BOOT_STATE_RECEIVING,
    BOOT_STATE_VERIFYING,
    BOOT_STATE_PENDING_BOOT,
    BOOT_STATE_CONFIRMED,
    BOOT_STATE_FAILED
} boot_state_t;
```

## 10.2 元数据记录

```
typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t state;

    uint32_t sequence_number;
    uint32_t session_id;

    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t received_bytes;

    uint32_t image_crc32;
    uint8_t  image_sha256[32];

    uint32_t error_code;
    uint32_t record_crc32;
    uint32_t commit_marker;
} boot_record_t;
```

建议：

```
#define BOOT_RECORD_MAGIC 0x42544D44UL
#define BOOT_RECORD_COMMIT_MARKER 0x434D4954UL
```

当前格式版本1的记录固定为76 bytes，64 KiB Sector 4可容纳862条，末尾24 bytes不使用。CRC覆盖`magic`到`error_code`；`record_crc32`写完后，最后再写`commit_marker`。扫描器只有在字段范围、CRC和提交标记全部有效时才接受记录，因此掉电留下的半条记录会被跳过。

## 10.3 写入策略

不要每收到一个固件数据包就写一次元数据。

建议：

```
每收到4 KiB或16 KiB固件数据
→ 追加一条进度记录
```

必须追加记录的状态变化：

- 开始升级
- 擦除完成
- 每个断点
- 接收完成
- 校验开始
- 校验成功
- 启动待确认
- APP确认成功
- 升级失败

## 10.4 启动扫描

Bootloader启动时：

```
从Metadata扇区起点扫描
→ 检查magic
→ 检查record_crc32
→ 找到sequence_number最大的有效记录
→ 恢复当前升级状态
```

如果Metadata扇区损坏：

- APP向量及镜像合法：允许启动APP
- APP非法：停留在恢复模式
- 不允许盲目跳转

## 10.5 元数据扇区满时

只允许在安全状态整理：

```
APP_VALID
CONFIRMED
EMPTY
```

不允许在`RECEIVING`过程中擦除整个Metadata扇区。

开始新升级前应根据镜像大小和4 KiB检查点计算所需记录数。如果剩余槽位不足，必须在旧状态仍为`APP_VALID`或`CONFIRMED`时先整理，不能等到接收过程中才发现Journal已满。

整理过程：

```
读取最后有效记录到RAM
→ 擦除Metadata扇区
→ 写回一条最新快照
```

如果整理过程中掉电：

- APP仍然有效时可以重新判断
- APP无效时进入恢复模式
- 最坏情况丢失断点，但不能跳入损坏APP

## 10.6 M4显式板上自测

M4提供默认关闭的`BOOT_METADATA_SELF_TEST_ENABLE`。设为1后自测会擦除Sector 4，并验证：空扇区扫描、连续追加、序号递增、活动状态拒绝整理、半写记录回退、安全状态整理，以及Bootloader和APP区域哈希保持不变。成功输出中应看到`status=0`、最新`sequence=3`、`state=7`以及`free=861/862`。测试后必须立即移除该宏并重新构建普通Bootloader。

------

# 十一、第五阶段：设计统一升级协议

UART和RS485采用相同的Modbus RTU帧。PC或ESP32是主站，STM32是从站。CAN只复用升级子命令、状态码和Session语义，不封装Modbus RTU字节帧。

## 11.1 协议选型

正式协议选择：

```
Modbus RTU帧
+ 用户自定义功能码0x41
+ 项目自定义升级子命令
```

理由：

- Modbus RTU已经定义节点地址、CRC16、请求/响应模型和RS485时序。
- 功能码`0x41`位于Modbus用户自定义范围65~72。
- 后续从单节点UART迁移到多节点RS485时，不需要替换帧层。
- YMODEM适合PC到单片机的单文件传输，但缺少节点寻址、产品信息查询、跨复位断点状态和多链路统一语义。

必须明确：Modbus没有标准固件升级功能。这里复用的是标准RTU帧和CRC，升级服务本身仍是项目自定义协议。

## 11.2 UART/RS485帧结构

```
┌─────────┬──────────┬──────────────────┬─────────┐
│ address │ function │ data             │ CRC16   │
│ 1 byte  │ 0x41     │ 0..252 bytes     │ 2 bytes │
└─────────┴──────────┴──────────────────┴─────────┘
```

Modbus RTU ADU最大为256字节。升级Data字段统一为：

```
typedef struct __attribute__((packed))
{
    uint8_t  subfunction;
    uint8_t  protocol_version;
    uint16_t flags_or_status;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t offset_or_next_offset;
    uint16_t payload_length;
    uint8_t  payload[0...224];
} upgrade_modbus_data_t;
```

约定：

```
主站：PC或ESP32
从站地址：1..247
升级功能码：0x41
协议版本：1
最大固件数据块：224字节
升级字段字节序：小端
Modbus CRC16：按标准低字节先发送
```

`224`不是Modbus理论极限，而是给子命令、Session、Sequence、Offset、状态和后续扩展留出空间。不要再使用原设计的512字节UART数据块。

115200波特率下，接收状态机按Modbus串行规范使用固定超时：

```
字符间超时t1.5：750 us
帧间隔t3.5：1750 us
```

第一版可使用UART中断、微秒定时器和环形缓冲区。单元测试必须覆盖粘包、半帧、字符间超时和CRC错误。

## 11.3 升级子命令

```
typedef enum {
    UPG_SUB_HELLO          = 0x01,
    UPG_SUB_GET_INFO       = 0x02,
    UPG_SUB_ENTER_BOOT     = 0x03,

    UPG_SUB_START          = 0x10,
    UPG_SUB_ERASE          = 0x11,
    UPG_SUB_DATA           = 0x12,
    UPG_SUB_QUERY_PROGRESS = 0x13,
    UPG_SUB_VERIFY         = 0x14,
    UPG_SUB_ACTIVATE       = 0x15,
    UPG_SUB_ABORT          = 0x16,

    UPG_SUB_GET_LOG        = 0x20
} upgrade_subfunction_t;
```

正常响应继续使用功能码`0x41`，并回显`subfunction`。非法功能码或无法解析的PDU可以使用Modbus异常响应`0xC1`；Session、Offset、镜像大小等升级业务错误使用下述升级状态码返回，以便携带正确Offset等恢复信息。

## 11.4 状态码

```
typedef enum {
    UPG_STATUS_OK = 0,
    UPG_STATUS_BAD_FRAME,
    UPG_STATUS_BAD_CRC,
    UPG_STATUS_BAD_SESSION,
    UPG_STATUS_BAD_SEQUENCE,
    UPG_STATUS_BAD_OFFSET,
    UPG_STATUS_BAD_IMAGE_SIZE,
    UPG_STATUS_BAD_PRODUCT,
    UPG_STATUS_BAD_HARDWARE,
    UPG_STATUS_VERSION_REJECTED,
    UPG_STATUS_FLASH_ERROR,
    UPG_STATUS_VERIFY_FAILED,
    UPG_STATUS_BUSY,
    UPG_STATUS_TIMEOUT
} upgrade_status_t;
```

## 11.5 DATA响应内容

DATA响应不要只返回“成功”。

建议返回：

```
typedef struct __attribute__((packed))
{
    uint16_t status;
    uint16_t reserved;
    uint32_t accepted_sequence;
    uint32_t next_expected_offset;
} upgrade_data_ack_t;
```

这样ESP32发生ACK丢失后，可以安全重发。

## 11.6 重复包处理

STM32收到数据包后：

```
offset == next_expected_offset
    → 写Flash
    → 更新next_expected_offset
    → 返回ACK

offset < next_expected_offset
    → 认为是重复包
    → 不重复写Flash
    → 重新返回ACK

offset > next_expected_offset
    → 拒绝
    → 返回BAD_OFFSET和正确offset
```

这是可靠升级的核心逻辑之一。

## 11.7 第一版使用停止等待

第一版：

```
ESP发送一个不超过224字节的数据块
→ 等STM32响应
→ 再发送下一个块
```

先不要实现滑动窗口。

停止等待吞吐率较低，但逻辑清晰，容易测试掉包、重复包和断点恢复。

## 11.8 USART1日志与协议模式

同一USART1采用状态级复用，不进行字节级混流：

```
APP_TEXT_DIAGNOSTIC
    仅用于开发日志构建
    允许文本日志，但不得同时运行协议主站

APP_PROTOCOL_SERVICE
    用于升级联调和正式构建
    禁止裸文本
    响应Modbus请求
    收到UPG_SUB_ENTER_BOOT后记录升级请求并复位

BOOT_WAIT_REQUEST
    不主动输出文本
    收到有效Modbus RTU请求后进入BOOT_PROTOCOL_ACTIVE
    无升级请求且APP有效时跳转APP

BOOT_PROTOCOL_ACTIVE
    USART1由Modbus RTU独占
    禁止printf、启动横幅和异步日志
    诊断事件写入内存日志，由UPG_SUB_GET_LOG查询
```

STM32作为Modbus从站，不应主动发送日志帧。这样未来接入RS485多节点后，不会因为多个节点同时打印日志而产生总线冲突。

当前M1/M2阶段可使用APP和Bootloader的debug目标输出文本日志，用于验证跳转。开始实现UART升级协议时，必须使用`LOG_ENABLE=0`的普通目标，或将诊断信息写入可查询日志缓冲区。

## 11.9 YMODEM定位

YMODEM可作为独立实验或应急恢复通道，但不作为本项目正式协议：

| 项目 | YMODEM | Modbus RTU + `0x41` |
| --- | --- | --- |
| 单文件UART传输 | 简单，ST有IAP示例 | 需要实现升级子命令 |
| 节点地址 | 无 | 有 |
| RS485多节点 | 不自然 | 适合 |
| 查询设备/版本/日志 | 需另加协议 | 统一子命令 |
| 跨复位断点恢复 | 非标准能力 | 可结合Metadata定义 |
| CAN复用上层语义 | 较弱 | 清晰 |

参考规范：

- [MODBUS Application Protocol Specification V1.1b3](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [MODBUS over Serial Line Specification and Implementation Guide V1.02](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf)
- [ST AN4657：STM32 IAP using USART/YMODEM](https://www.st.com/resource/en/application_note/dm00161366-stm32-inapplication-programming-iap-using-the-usart-stmicroelectronics.pdf)

------

# 十二、第六阶段：UART Bootloader升级

## 12.1 UART连接

```
ESP32 TX → STM32 RX
ESP32 RX ← STM32 TX
ESP32 GND ↔ STM32 GND
```

确认双方均为3.3V逻辑。

USART1由调试日志和升级协议分时共用：

```
开发调试阶段：STM32 USART1 → USB转TTL → Windows文本终端
协议联调阶段：PC或ESP32主站 ↔ STM32 USART1从站
```

连接ESP32或PC升级主站并进入协议模式后，关闭USART1文本日志。不要让USB转TTL和ESP32同时驱动STM32 RX；需要抓取升级字节流时使用逻辑分析仪，或者只监听TX线且确保监听设备为高阻输入。

## 12.2 STM32接收模块

第一版可以使用：

- UART中断接收
- 环形缓冲区
- 主循环解析

后续再改为DMA+IDLE。

Bootloader中不需要为了展示技术栈强行使用复杂DMA；可靠性比技术堆叠更重要。

模块结构：

```
transport_uart.c
ring_buffer.c
modbus_rtu.c
crc16_modbus.c
upgrade_session.c
```

主循环：

```
while (1)
{
    transport_uart_poll();

    while (modbus_rtu_get_request(&request))
    {
        upgrade_session_process(&request, &response);
        modbus_rtu_send_response(&response);
    }

    boot_watchdog_feed();
}
```

## 12.3 START命令处理

START Payload至少包含：

```
typedef struct __attribute__((packed))
{
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t  image_sha256[32];

    uint16_t product_id;
    uint16_t hardware_id;
} upgrade_start_request_t;
```

STM32检查：

- 产品ID匹配
- 硬件ID匹配
- image_size非0
- image_size不超过APP_MAX_SIZE
- 版本策略允许
- 当前没有其他升级Session
- session_id有效

通过后写入：

```
UPDATE_REQUESTED
```

## 12.4 ERASE命令处理

擦除操作可能耗时较长。

Modbus RTU必须保持一问一答，STM32从站不能在同一次请求后主动发送第二个响应。建议流程：

```
ESP发送ERASE
→ STM32校验Session和image_size
→ STM32返回ERASE_ACCEPTED或BUSY
→ STM32执行擦除并更新内部进度/结果
→ ESP周期发送QUERY_PROGRESS
→ STM32响应ERASING、COMPLETED或FAILED
```

ESP对ERASE请求使用普通响应超时；擦除总超时由多次`QUERY_PROGRESS`轮询共同控制。轮询间隔第一版可取100 ms，整次擦除总超时建议先取60 s并在M3实测后收紧。重复ERASE请求必须按Session幂等处理，不能因为响应丢失而再次无条件擦除。

## 12.5 DATA命令处理

流程：

```
校验Modbus RTU CRC16
→ 校验Session
→ 校验Sequence
→ 校验Offset
→ 校验地址范围
→ 写Flash
→ 读回比较
→ 更新内存进度
→ 达到检查点时持久化
→ 返回ACK
```

## 12.6 VERIFY命令处理

第一版先实现CRC32：

```
从APP_BASE开始读取image_size
→ 计算CRC32
→ 与START中的CRC32比较
```

后续增加SHA-256。

校验成功：

```
状态 → PENDING_BOOT
```

校验失败：

```
状态 → FAILED
保持Bootloader恢复模式
```

## 12.7 ACTIVATE命令

```
返回ACTIVATE_RSP
→ 等待UART发送完成
→ 软件复位
→ Bootloader重新启动
→ 判断PENDING_BOOT
→ 启动APP
```

不要在ACK尚未发送完成时立即复位。

------

# 十三、第七阶段：先用PC Python工具升级STM32

在ESP32介入之前，先让Windows PC直接通过USB串口完成升级。

这样可以将问题拆开：

```
PC Python工具
→ UART
→ STM32 Bootloader
```

## 13.1 编写serial_upgrade.py

功能：

```
读取stm32_app.bin
→ 计算大小和CRC
→ HELLO
→ GET_INFO
→ START
→ ERASE
→ 逐块DATA
→ VERIFY
→ ACTIVATE
```

命令示例：

```
python tools/serial_upgrade.py ^
  --port COM8 ^
  --baud 115200 ^
  --node 1 ^
  --file firmware/stm32_app/Objects/stm32_app.bin
```

输出：

```
Node ID: 1
Bootloader: 0.1.0
Current APP: 0.1.0
Image size: 184320 bytes
Erase complete
Progress: 10%
Progress: 20%
...
Verify: OK
Activate: OK
```

## 13.2 为什么PC工具必须先做

它可以独立验证：

- STM32协议是否正确
- Modbus RTU组帧、地址和功能码是否正确
- Modbus CRC16和帧间超时是否正确
- Flash写入是否正确
- ACK和重传是否正确

如果直接从ESP开始，出现故障时很难判断是：

- ESP UART
- ESP任务调度
- 网络下载
- 协议
- STM32 Flash
- Bootloader

哪一层有问题。

## 13.3 UART阶段验收门槛

至少完成：

- 正常升级10次
- 随机重复数据包
- 随机丢弃ACK
- 在任意offset中断后重新查询进度
- 发送错误CRC
- 发送超大镜像
- 发送错误产品ID
- 发送错误offset
- 升级后APP正常运行

此时再开始ESP32。

------

# 十四、第八阶段：建立ESP32-S3基础工程

在：

```
firmware/esp32_gateway
```

创建ESP-IDF工程。

## 14.1 先记录ESP-IDF版本

执行：

```
idf.py --version
```

将结果写入：

```
docs/environment.md
```

后续不要混用其他ESP-IDF版本的TWAI、HTTP或分区示例。

## 14.2 基础工程先实现

- 启动日志
- 芯片信息输出
- Flash大小输出
- NVS初始化
- Wi-Fi STA连接
- 自动重连
- UART发送接收
- LED或状态输出

ESP32-S3的Flash容量取决于具体开发板，先从启动日志或工具确认，不能假定是8 MB或16 MB。

## 14.3 ESP32组件结构

```
esp32_gateway/
├── main/
│   └── app_main.c
│
├── components/
│   ├── wifi_manager/
│   ├── gateway_config/
│   ├── firmware_store/
│   ├── firmware_downloader/
│   ├── manifest_parser/
│   ├── upgrade_manager/
│   ├── node_manager/
│   ├── upgrade_protocol/
│   ├── transport_uart/
│   ├── transport_rs485/
│   ├── transport_twai/
│   └── gateway_log/
│
├── partitions.csv
├── CMakeLists.txt
└── sdkconfig.defaults
```

不要把所有代码放在`app_main.c`。

## 14.4 推荐任务模型

```
wifi_manager_task
    管理Wi-Fi和连接事件

job_manager_task
    获取、创建和调度升级任务

download_task
    下载并校验固件

upgrade_task
    执行STM32升级状态机

transport_rx_task
    接收UART/RS485/CAN数据

report_task
    上报进度和结果
```

第一版只允许一个升级任务执行。

不要并行升级多个STM32节点。

------

# 十五、第九阶段：ESP32固件缓存分区

ESP-IDF允许在分区表中定义自定义数据分区，并使用`esp_partition`接口枚举、擦除、读取和写入。

## 15.1 先检查ESP Flash容量

例如确认开发板至少为8 MB后，可以设计：

```
# Name,       Type, SubType, Offset,   Size
nvs,          data, nvs,     0x9000,   0x6000
phy_init,     data, phy,     0xF000,   0x1000
factory,      app,  factory, 0x10000,  0x180000
stm_fw,       data, 0x40,              0x120000
gateway_log,  data, 0x41,              0x080000
```

`stm_fw`大小约1.125 MB，足以保存最大896 KiB的STM32 APP以及头部信息。

实际表必须根据开发板Flash容量重新计算。

## 15.2 firmware_store接口

```
esp_err_t firmware_store_open(void);

esp_err_t firmware_store_erase(
    size_t image_size
);

esp_err_t firmware_store_write(
    size_t offset,
    const void *data,
    size_t length
);

esp_err_t firmware_store_read(
    size_t offset,
    void *data,
    size_t length
);

esp_err_t firmware_store_calculate_sha256(
    uint8_t output[32]
);
```

## 15.3 缓存状态

在NVS中存储：

```
download_state
firmware_id
expected_size
received_size
etag
expected_sha256
manifest_version
```

不要把整个固件放进NVS。NVS只保存小型配置和状态，固件放自定义数据分区。

------

# 十六、第十阶段：ESP32先发送本地固件

这一阶段暂时不联网。

做法：

1. 使用`esptool`或ESP-IDF构建流程，将测试固件写进`stm_fw`分区。
2. ESP启动后读取分区。
3. 通过UART向STM32执行升级。
4. 完整复用PC工具已经验证过的协议。

升级管理状态机：

```
typedef enum {
    GW_UPG_IDLE = 0,
    GW_UPG_QUERY_NODE,
    GW_UPG_START,
    GW_UPG_ERASE,
    GW_UPG_TRANSFER,
    GW_UPG_VERIFY,
    GW_UPG_ACTIVATE,
    GW_UPG_WAIT_CONFIRM,
    GW_UPG_SUCCESS,
    GW_UPG_FAILED
} gateway_upgrade_state_t;
```

DATA阶段：

```
从ESP分区读取不超过224字节
→ 组装0x41/DATA请求
→ 计算Modbus CRC16
→ UART发送
→ 等待DATA响应
→ 超时则重试
→ 更新offset
```

重试策略：

```
普通命令超时：例如500 ms
数据包超时：例如500 ms
擦除命令超时：按实测单独设置
最大重试：5次
```

数值最终应由实际测试调整。

## 16.1 阶段验收

- ESP能够查询STM32信息
- ESP能够升级完整固件
- ESP重启后能够重新查询STM32进度
- STM32 ACK丢失后不会重复写Flash
- 固件升级完成后APP正常运行

------

# 十七、第十一阶段：搭建最小服务器

Windows 11上使用Python即可。

## 17.1 创建虚拟环境

```
cd server
python -m venv .venv
.venv\Scripts\activate
pip install fastapi uvicorn python-multipart sqlalchemy
```

第一版可以暂时不用数据库。

## 17.2 第一版API

```
GET /api/v1/firmwares/{firmware_id}/manifest
GET /api/v1/firmwares/{firmware_id}/binary
```

Manifest示例：

```
{
  "schema_version": 1,
  "firmware_id": "f407-node-1.1.0",
  "product_id": 1001,
  "hardware_id": 1,
  "firmware_version": "1.1.0",
  "firmware_version_code": 10100,
  "app_base": "0x08020000",
  "image_size": 184320,
  "crc32": "92B401ED",
  "sha256": "完整64字符SHA256",
  "download_url": "/api/v1/firmwares/f407-node-1.1.0/binary"
}
```

## 17.3 编写固件打包脚本

```
python tools/pack_firmware.py ^
  --input firmware/stm32_app/Objects/stm32_app.bin ^
  --product-id 1001 ^
  --hardware-id 1 ^
  --version 1.1.0 ^
  --output server/firmware/f407-node-1.1.0
```

脚本负责：

- 读取BIN
- 检查大小
- 计算CRC32
- 计算SHA-256
- 生成Manifest
- 保存构建信息
- 保存Git commit ID

## 17.4 第一阶段先使用HTTP

启动：

```
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Windows防火墙允许局域网访问该端口。

ESP访问：

```
http://Windows电脑局域网IP:8000
```

先验证整个业务闭环，再切换HTTPS。

------

# 十八、第十二阶段：ESP32网络下载

ESP-IDF的`esp_http_client`提供HTTP/HTTPS请求接口。ESP官方OTA示例也展示了通过客户端配置提供根证书进行HTTPS校验。

## 18.1 下载流程

```
获取Manifest
→ 解析JSON
→ 检查product_id
→ 检查hardware_id
→ 检查image_size
→ 检查版本
→ 擦除stm_fw分区
→ 流式下载binary
→ 分块写入stm_fw
→ 计算SHA-256
→ 比较Manifest
→ 标记DOWNLOAD_READY
```

不要把完整固件先存进RAM。

建议：

```
HTTP读取4 KiB
→ 写ESP Flash
→ 更新hash
→ 循环
```

## 18.2 下载状态持久化

每写入例如64 KiB：

```
将received_size保存到NVS
```

不要每4 KiB写一次NVS。

## 18.3 断点续传

服务器支持HTTP Range：

```
Range: bytes=262144-
```

ESP保存：

```
received_size
ETag
firmware_id
```

恢复时：

1. 发送`Range`。
2. 携带`If-Range`或比较ETag。
3. 服务器返回206：继续写。
4. 服务器返回200：说明不支持或文件变化，擦除后重下。
5. 完成后重新读取整个ESP分区计算SHA-256。

不要只依赖下载过程中的临时Hash上下文，因为掉电后该上下文通常难以安全恢复。

## 18.4 HTTPS阶段

本地开发可使用：

- 自签CA
- 由该CA签发服务器证书
- 将CA公钥证书编译进ESP32

不要配置为跳过证书校验。

完成后链路：

```
服务器HTTPS
→ ESP下载
→ ESP SHA-256校验
→ ESP UART分发
→ STM32 CRC/SHA校验
→ APP启动
```

------

# 十九、第十三阶段：STM32 Minimal APP升级闭环

第二个项目中的APP可以很小，但不能只是闪灯。

建议实现：

```
节点ID
硬件版本
固件版本
心跳
进入Bootloader命令
启动自检
新版本确认
复位原因记录
测试故障注入
```

## 19.1 APP版本信息

```
typedef struct
{
    uint32_t magic;
    uint32_t product_id;
    uint32_t hardware_id;
    uint32_t firmware_version;
    char git_commit[12];
    char build_time[20];
} app_info_t;
```

将其放置到固定只读区或固定Section中。

## 19.2 进入Bootloader

建议支持两种方法。

方法一：APP软件请求。

```
ESP发送ENTER_BOOTLOADER
→ APP写UPDATE_REQUESTED
→ NVIC_SystemReset()
→ Bootloader进入升级模式
```

方法二：硬件请求。

```
ESP拉起BOOT_REQ引脚
→ 拉低STM32 NRST
→ Bootloader检测BOOT_REQ
→ 进入升级模式
```

UART直连场景可以同时实现两种。

RS485/CAN远端节点主要依赖软件请求。

## 19.3 APP启动确认

升级完成后：

```
Bootloader写PENDING_BOOT
→ 启动APP
→ APP完成基础初始化和自检
→ APP追加CONFIRMED记录
```

基础自检可以包括：

- 时钟正常
- RAM基本检查
- 必要外设初始化成功
- 参数结构合法
- 主循环或任务能够运行

需要明确：

> 由于当前设计没有STM32侧A/B镜像，APP确认失败后不能恢复旧APP，只能进入Bootloader等待ESP重新下发固件。

因此简历写：

- 启动确认
- 失败恢复
- 重新升级

不要写：

- A/B回滚
- 自动恢复旧版本

------

# 二十、第十四阶段：增加RS485

UART完整通过后，再实现RS485。

## 20.1 架构不变

```
upgrade_manager
       ↓
reliable_protocol
       ↓
transport interface
   ┌─────────────┐
   │ UART        │
   │ RS485       │
   │ CAN         │
   └─────────────┘
```

接口：

```
typedef struct
{
    int (*init)(void);
    int (*send)(
        const uint8_t *data,
        uint32_t length
    );
    int (*receive)(
        uint8_t *data,
        uint32_t capacity,
        uint32_t timeout_ms
    );
    int (*flush)(void);
    int (*reset)(void);
} upgrade_transport_t;
```

协议层不能直接调用：

```
HAL_UART_Transmit()
uart_write_bytes()
```

只能调用Transport接口。

## 20.2 ESP32 RS485

ESP32-S3 UART驱动支持RS485半双工模式，RTS可以控制收发器DE/RE。

连接：

```
ESP UART TX → DI
ESP UART RX ← RO
ESP RTS     → DE和/RE
```

## 20.3 STM32 RS485

STM32侧：

```
TX前：
DE = 1

等待UART TC置位：
确认最后一个停止位已经发送完成

然后：
DE = 0
切换接收
```

不能在`HAL_UART_Transmit`返回后立刻假定线路已经完全发送完毕，应确认Transmission Complete条件。

## 20.4 多节点通信原则

ESP始终是主站：

```
ESP发请求
→ 指定destination
→ 只有目标节点响应
```

不要允许多个节点主动同时发送。

节点地址：

```
0：网关
1~254：STM32节点
65535：广播
```

广播只用于：

- 节点发现请求
- 进入升级准备
- 状态刷新

固件数据不要广播。否则ACK管理非常复杂。

## 20.5 RS485验收

- 一个STM32节点正常升级
- 错误节点地址不响应
- 节点离线不阻塞系统
- 超时后任务失败并释放总线
- 重复包不重复写Flash
- DE切换时序正确
- 增加第二节点后可顺序升级
- 节点A失败不影响节点B

------

# 二十一、第十五阶段：增加CAN

ESP32-S3通过TWAI控制器支持经典CAN，需要外部收发器，不支持CAN-FD。

## 21.1 不直接套用UART字节帧

CAN本身已有：

- 帧边界
- CRC
- 仲裁
- ACK
- 错误检测

因此CAN Transport应负责：

```
逻辑升级消息
→ CAN分片
→ 多个CAN帧
→ STM32重组
→ 交给统一升级协议
```

不要在CAN上封装Modbus RTU地址、功能码、CRC和帧间隔；CAN只复用升级子命令、Session、Sequence、Offset和状态码。

## 21.2 建议使用扩展CAN ID

例如：

```
Priority | Message Class | Destination | Source | Channel
```

需要在`docs/protocol_can.md`中固定每个Bit的含义。

不要边写代码边改变ID布局。

## 21.3 第一版CAN分片

经典CAN每帧最多8字节。

可定义：

```
Byte 0：分片类型
Byte 1：分片序号
Byte 2~7：数据
```

分片类型：

```
START
CONTINUE
END
SINGLE
```

逻辑块：

```
192字节或240字节
```

重组完成后：

- 检查逻辑块CRC
- 写Flash
- 返回块级ACK

不要对每个CAN帧都发送应用层ACK，否则效率极低。

## 21.4 CAN异常处理

至少处理：

- 接收队列满
- 发送超时
- 错误告警
- Error Passive
- Bus-Off
- Bus-Off恢复
- 分片超时
- 分片序号错误
- 节点离线

## 21.5 CAN验收

- 单节点完整升级
- 丢弃一个逻辑分片后能够重传整个块
- 错误分片序号被拒绝
- 总线断开后任务失败
- 总线恢复后可以重新建立升级Session
- Bus-Off不会导致网关任务永久阻塞

------

# 二十二、第十六阶段：多节点任务调度

第二块STM32接入后，再实现多节点。

## 22.1 节点表

ESP32维护：

```
typedef struct
{
    uint16_t node_id;
    uint16_t product_id;
    uint16_t hardware_id;

    uint32_t app_version;
    uint32_t boot_version;

    uint8_t transport_type;
    bool online;

    uint32_t last_seen_ms;
    uint32_t last_error;
} gateway_node_t;
```

## 22.2 一次只升级一个节点

任务队列：

```
Job 1：RS485节点1
Job 2：RS485节点2
Job 3：CAN节点3
```

执行：

```
节点1成功或失败
→ 释放Transport和Session
→ 再执行节点2
```

这样更容易保证：

- 总线互斥
- 日志可读
- 失败隔离
- 状态机简单

## 22.3 升级结果

每个任务记录：

```
任务ID
网关ID
节点ID
固件版本
链路类型
开始时间
结束时间
传输字节数
重试次数
断点恢复次数
最终状态
错误码
```

------

# 二十三、第十七阶段：升级服务器任务管理

增加API：

```
POST /api/v1/firmwares
GET  /api/v1/firmwares
GET  /api/v1/firmwares/{id}/manifest
GET  /api/v1/firmwares/{id}/binary

POST /api/v1/gateways/{gateway_id}/jobs
GET  /api/v1/gateways/{gateway_id}/jobs/next

PATCH /api/v1/jobs/{job_id}/progress
POST  /api/v1/jobs/{job_id}/complete
```

数据库表：

```
firmwares
gateways
nodes
upgrade_jobs
upgrade_events
```

第一版使用SQLite即可。

不需要复杂网页。可以直接使用：

- FastAPI自动API文档
- Python命令行
- 简单HTML状态页

重点是升级闭环，而不是前端开发。

------

# 二十四、第十八阶段：故障注入

项目能否称为可靠升级系统，主要取决于这一阶段。

## 24.1 软件链路故障注入

在ESP Transport中加入测试配置：

```
typedef struct
{
    uint32_t drop_every_n;
    uint32_t duplicate_every_n;
    uint32_t corrupt_every_n;
    uint32_t delay_ms;
    uint32_t disconnect_at_offset;
    uint32_t reset_at_offset;
} fault_profile_t;
```

生产构建关闭，测试构建启用。

可模拟：

- 丢包
- ACK丢失
- 重复包
- Payload篡改
- 延迟
- 乱序
- 链路断开
- ESP复位

## 24.2 STM32故障注入

Bootloader增加测试宏：

```
#define TEST_RESET_AFTER_BYTES  0
#define TEST_FAIL_FLASH_OFFSET  0
#define TEST_FORCE_BAD_CRC      0
```

APP增加：

```
启动后立即复位
启动后不确认
模拟HardFault
模拟看门狗复位
```

## 24.3 物理掉电测试

分别在以下阶段断电：

- 擦除前
- 擦除过程中
- 写入10%
- 写入50%
- 写入99%
- 整包校验时
- PENDING_BOOT写入后
- APP确认前
- APP确认后

期望：

| 掉电位置    | 期望行为               |
| ----------- | ---------------------- |
| 下载中      | ESP继续下载或重新下载  |
| STM32擦除中 | 重启后进入Bootloader   |
| STM32接收中 | 查询检查点后恢复或重传 |
| 校验中      | 重启后重新校验或重传   |
| APP未确认   | 记录失败并进入恢复流程 |
| APP已确认   | 正常启动APP            |

最重要的安全属性是：

```
任何异常都不能跳转到已知损坏的APP。
```

------

# 二十五、第十九阶段：完整性和安全

建议分四级实现。

## 25.1 第一级：数据包CRC32

作用：

- 检测传输错误
- 检测随机位翻转

不提供固件来源认证。

## 25.2 第二级：整包SHA-256

ESP：

```
服务器Manifest SHA-256
↔ 下载后的缓存镜像SHA-256
```

STM32：

```
START中的SHA-256
↔ 内部Flash镜像SHA-256
```

STM32F407没有专门的硬件Hash加速，应使用体积可控的软件实现。

## 25.3 第三级：固件数字签名

推荐最终实现：

```
构建机持有私钥
→ 对固件Manifest或镜像Hash签名
→ Bootloader内置公钥
→ Bootloader验证签名
```

私钥不能：

- 写入ESP32
- 写入STM32
- 提交Git
- 上传公开仓库

公钥可以固化到Bootloader。

## 25.4 第四级：防降级

Manifest提供：

```
firmware_version_code
minimum_bootloader_version
security_version
```

STM32保存：

```
confirmed_version
minimum_allowed_version
```

默认拒绝低版本镜像。

最终没有实现签名时，不要在简历中写“安全升级”；可以写：

- HTTPS下载
- SHA-256完整性验证
- 版本合法性检查

实现数字签名后才能更有底气地写：

- 固件真实性验证
- 防未授权固件
- 防版本回退

------

# 二十六、测试体系

## 26.1 协议单元测试

在Windows使用C或Python测试：

- Modbus RTU编码解码
- 地址和功能码过滤
- t1.5字符间超时和t3.5帧间隔
- CRC16错误帧
- 空Payload
- 最大Payload
- CRC错误
- 帧被截断
- 多帧粘连
- 数据逐字节到达
- 超长帧
- 非法版本
- 非法消息类型
- 整数边界

## 26.2 Bootloader测试

- APP向量有效
- APP向量非法
- APP MSP非法
- Reset_Handler越界
- Image size越界
- Flash写入越界
- 重复块
- 错序块
- Session错误
- Verify失败
- Metadata最后记录损坏

## 26.3 网关测试

- Wi-Fi断开
- Wi-Fi重新连接
- HTTP 404
- HTTP下载中断
- Range不支持
- Manifest损坏
- 文件大小不匹配
- SHA-256不匹配
- ESP重启后恢复
- STM32离线
- STM32返回Flash错误

## 26.4 最终验收门槛

这些是建议目标，不是可以直接写进简历的实际结果：

- 每条链路连续正常升级至少100次
- 1%模拟丢包时仍能完成升级
- ACK随机丢失时不重复写Flash
- 随机重复包不会破坏镜像
- 随机掉电后100%进入可恢复状态
- 错误型号固件100%拒绝
- 超大固件100%拒绝
- 校验失败时100%不启动APP
- 节点失败不影响其他节点任务

简历只能写最终实测结果。

------

# 二十七、建议记录的量化指标

每次升级记录：

```
固件大小
链路类型
波特率或CAN速率
总耗时
有效吞吐率
重传次数
重复包数
断点次数
Flash擦除时间
Flash写入时间
整体校验时间
最终结果
```

最终报告可以形成：

| 链路  | 固件大小 | 配置 | 升级时间 | 有效吞吐率 | 重传次数 |
| ----- | -------- | ---- | -------- | ---------- | -------- |
| UART  | 实测     | 实测 | 实测     | 实测       | 实测     |
| RS485 | 实测     | 实测 | 实测     | 实测       | 实测     |
| CAN   | 实测     | 实测 | 实测     | 实测       | 实测     |

------

# 二十八、推荐实施顺序

严格按以下顺序：

截至2026-09-01：M0～M6已完成；M7 ESP32 UART本地主机的主功能闭环以及R01～R06、
R08、R10～R15可靠性用例已通过，正常升级稳定性为10/10。R07本地坏包拦截、R09双端
冷启动10轮和测试后生产版回归按计划暂缓，因此M7当前为“阶段性归档”，尚未关闭完整
可靠性验收。M8已完成本地PC只读HTTP固件服务器、可追溯Manifest、与M7兼容的缓存包
发布工具以及Range/ETag下载接口的自动化和本机验收；归档见`docs/m8_verification.md`
和`docs/m8_archive_manifest.md`。M9 ESP32网络下载的核心链路已阶段性完成：Wi-Fi/HTTP下载、
完整包安全提交、ESP32复位与用户取消后的断点续传，以及正式基线到STM32的端到端升级回归
均已通过目标板验收；ETag变化和错误Manifest身份拦截保留为`DEFERRED`，不阻塞后续阶段开发。
M7归档见`docs/m7_archive_summary.md`、`docs/m7_archive_manifest.md`和
`docs/m7_reliability_verification.md`，M9设计见`docs/m9_download_design.md`，实现状态与板上验收见
`docs/m9_implementation.md`，阶段归档见`docs/m9_archive_summary.md`和
`docs/m9_archive_manifest.md`。

```
M0：建立仓库和设计文档

M1：APP重定位到0x08020000

M2：Bootloader合法跳转APP

M3：STM32 Flash擦写与读回验证

M4：Metadata追加日志

M5：协议编解码和PC单元测试

M6：PC通过UART升级STM32

M7：ESP通过UART发送本地固件

M8：PC服务器提供固件下载

M9：ESP通过HTTP下载并缓存

M10：HTTPS与SHA-256

M11：APP启动确认和异常恢复

M12：RS485 Transport

M13：第二STM32节点和任务队列

M14：CAN Transport

M15：故障注入和自动化报告

M16：数字签名与防回退，可选增强

M17：整理文档、演示视频和简历
```

不要调整为：

```
先把UART、RS485、CAN驱动都写完
→ 再开始Bootloader
```

这种顺序很容易形成大量孤立模块，却没有一个端到端可运行版本。

------

# 二十九、基于你现有条件的具体建议

## 29.1 STM32开发

继续使用：

```
CubeMX生成初始化
Keil编译和调试
DAP-Link下载
VSCode浏览和编辑代码
```

不要在第一阶段折腾复杂的跨平台GCC迁移。

CubeMX只负责：

- 时钟
- GPIO
- UART
- CAN
- DMA
- 基础初始化

业务代码放在独立目录，不要大量写入自动生成的`main.c`。

## 29.2 ESP32开发

完全使用ESP-IDF，不建议切换Arduino。

原因是本项目需要：

- 自定义Flash分区
- HTTPS
- FreeRTOS任务
- UART RS485模式
- TWAI
- NVS
- 细粒度错误处理

ESP-IDF对这些能力提供了正式API；其分区、HTTP客户端、UART RS485和TWAI文档均可作为直接实现依据。

## 29.3 不要急于整合第一个项目

第二个项目最初使用Minimal APP。

待整个升级系统完成后，再做一次演示：

```
将第一个工业控制终端项目
调整链接地址到0x08020000
→ 通过ESP32网关升级
→ 正常启动FreeRTOS控制应用
```

这可以成为最终演示，但不应作为早期依赖。

## 29.4 DAP-Link使用建议

DAP-Link主要用于：

- 单步调试
- 查看HardFault
- 检查Flash内容
- 查看向量表
- 检查Metadata
- 下载Bootloader

不要依赖调试器来实现正式升级流程。

最终演示时应做到：

```
只给设备上电
→ 不连接Keil
→ 服务器下发任务
→ ESP下载
→ STM32升级
→ APP启动
```

## 29.5 日志必须分级

统一日志级别：

```
ERROR
WARN
INFO
DEBUG
TRACE
```

USART1共用时采用以下输出策略：

- APP开发模式可以输出人类可读文本。
- Bootloader协议模式禁止裸文本输出。
- Bootloader事件先写入定长内存环形缓冲区，至少记录级别、事件码、Session、Sequence、Offset和状态。
- PC或ESP32使用`0x41/UPG_SUB_GET_LOG`分页查询日志。
- STM32从站不主动上报日志；ESP32负责为查询结果添加时间戳并持久化或上报服务器。
- Flash擦写关键区不得为了日志进行额外Flash写入，掉电恢复所需状态只写Metadata Journal。

正式升级测试时记录：

```
时间戳
模块
节点
Session
Sequence
Offset
状态
错误码
```

例如：

```
[124503][UPG][node=1][session=39A42F01]
DATA_ACK seq=128 next_offset=65536 retry=1
```

------

# 三十、ChatGPT的正确使用方式

ChatGPT适合帮助你：

- 评审Flash分区
- 检查状态机是否存在遗漏
- 审查地址溢出
- 生成协议测试用例
- 生成Python测试脚本
- 检查重复包逻辑
- 解释HardFault寄存器
- 分析串口日志
- 检查任务死锁
- 生成测试报告结构
- 审查简历表述是否夸大

不适合直接执行：

```
“帮我一次生成完整企业级Bootloader”
```

这种代码通常会出现：

- HAL版本混用
- 地址假设错误
- 中断清理不完整
- Flash Sector错误
- 协议状态遗漏
- 错误处理缺失

## 30.1 建立固定AI上下文

创建：

```
docs/ai_context.md
```

内容：

```
MCU: STM32F407ZGT6
Flash: 1 MiB
Bootloader: 0x08000000, 64 KiB
Metadata: 0x08010000, 64 KiB
APP: 0x08020000, 896 KiB
Compiler: Keil ARM Compiler [实际版本]
STM32CubeF4: [实际版本]
ESP-IDF: [实际版本]
Transport: UART first
Frame: Modbus RTU, user-defined function 0x41
Firmware block: 224 bytes
Shared USART1: text diagnostics and upgrade protocol are time-multiplexed
No STM32 A/B partition
No Bootloader self-update
```

每次让AI审查代码时附上这段，减少前后假设不一致。

## 30.2 推荐提问方式

差的提问：

```
帮我写Bootloader。
```

更好的提问：

```
这是STM32F407 Bootloader的Flash地址检查函数。
APP范围为0x08020000到0x080FFFFF。
请检查整数溢出、边界条件和错误路径；
不要修改HAL接口，只输出问题清单和修订后的函数。
```

再例如：

```
下面是升级状态机。
请从断电、重复包、ACK丢失、ESP重启、
STM32重启五个角度检查是否存在无法恢复的状态。
```

## 30.3 AI生成代码的验收规则

每段代码都应经过：

```
阅读
→ 理解
→ 编译
→ 单元测试
→ 板上验证
→ 故障测试
→ Git提交
```

不能因为代码能编译，就认为设计正确。

------

# 三十一、最终文档交付物

项目完成时，仓库至少包含：

```
README.md
系统架构图
硬件接线图
STM32 Flash分区图
ESP32 Flash分区图
升级协议说明
状态机说明
服务器接口说明
错误码说明
测试计划
测试报告
故障注入报告
性能数据
演示步骤
已知限制
后续改进
```

README首页建议展示：

```
1. 系统架构图
2. 30秒演示动图或视频
3. 核心功能
4. 可靠性机制
5. 实测结果
6. 快速运行方法
7. 文档链接
```

------

# 三十二、最终完成标准

当以下流程不连接调试器也能运行时，项目才算完成：

```
1. STM32运行旧版本APP
2. 服务器创建升级任务
3. ESP32获取Manifest
4. ESP32通过HTTPS下载固件
5. ESP32完成SHA-256校验
6. ESP命令STM32进入Bootloader
7. Bootloader检查产品和版本
8. STM32擦除APP区
9. ESP分包发送固件
10. 丢包时自动重传
11. 通信中断后恢复
12. STM32完成整包校验
13. Bootloader启动新APP
14. APP完成启动确认
15. ESP上报升级成功
16. 服务器保存升级记录
```

同时满足：

```
错误固件不能启动
错误型号不能升级
超大镜像不能写入
断电后设备保持可恢复
重复包不能重复写Flash
失败节点不能阻塞其他节点
```

这时第二个项目与第一个项目的分工会非常清晰：

```
项目一：
证明你能开发STM32工业控制应用。

项目二：
证明你能开发ESP32联网网关、
STM32 Bootloader和可靠升级基础设施。
```

当前最应该开始的实际工作只有三个：

1. 创建仓库和Flash分区文档。
2. 将Minimal APP链接到`0x08020000`。
3. 完成最小Bootloader跳转APP。

在这三个步骤通过前，不应开始服务器、RS485或CAN。
