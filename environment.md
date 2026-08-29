# Development Environment

`xxx`表示尚未确认，不得自行猜测。

## Windows和通用工具

- Windows版本：Windows 11 26100.4351
- VS Code版本：1.130.0
- Codex扩展版本：26.715.61943
- Git版本：2.50.1.windows.1
- Git仓库：已初始化；基线提交`0181706 m0 & m1 init`
- PowerShell版本：1.24.11911.0
- Python版本：3.13.13；当前Codex PowerShell环境的`PATH`尚未找到`python`
- CMake版本：未安装
- 主机C编译器： MinGW GCC 8.1.0

## STM32开发板

- MCU：STM32F407ZGT6
- 开发板完整型号：xxx
- 厂商和版本：xxx
- 原理图文件位置：.\STM32F407ZG-P1 Board schematic.pdf
- MCU封装：LQFP144 / xxx
- HSE频率：8 MHz
- LSE：32.768 KHz
- 供电电压：3.3 V
- BOOT0上电电平：未测量
- 上电启动行为：已实测可以自动启动当前用户Flash中的程序
- BOOT0结论：当前不是自定义Bootloader启动的阻塞项；后续硬件文档仍需补充实际电平和跳帽位置
- NRST连接：xxx

## LED

- LED编号：xxx
- GPIO端口：GPIOC
- GPIO引脚：GPIO_PIN_13
- 有效电平：低

## 调试与升级共用UART

- UART实例：USART1
- TX引脚：PA9
- RX引脚：PA10
- 波特率：115200
- Windows串口：COM3
- USB转TTL型号：xxx
- 逻辑电压：3.3 V / xxx
- 物理复用：调试日志与升级协议共用USART1
- 复用方式：分时独占，不允许文本日志与二进制协议同时发送
- 正式升级协议：Modbus RTU帧，自定义功能码`0x41`
- 单包固件数据：224字节
- 开发阶段：未连接协议主站时，允许APP或M1/M2 Bootloader输出文本日志
- 协议阶段：APP与Bootloader均关闭裸`printf`；运行日志保存在内存中，由主站通过协议查询

## DAP-Link

- 型号：xxx
- 固件版本：xxx
- SWDIO：PA13
- SWCLK：PA14
- NRST：NRST
- 可下载：是
- 可单步：是

## STM32软件

- STM32CubeMX版本：6.15.0
- STM32CubeF4版本：1.28.3
- Keil uVision版本：5.38.0.0
- ARM编译器：AC5
- 编译器完整版本：5.06 update 7 (build 960)
- UV4.exe路径：C:\Keil_v5\UV4
- fromelf.exe路径：C:\Keil_v5\ARM\ARM_Compiler_5.06u7\bin64\fromelf.exe
- Pack验证：Codex受限命令行环境曾无法解析`Keil.STM32F4xx_DFP.2.17.1`和`ARM.CMSIS.4.5.0`；开发者已在uVision GUI中完成Rebuild，工程为0 Error、0 Warning，因此Pack不再是项目阻塞项

## ESP32

- 开发板型号：xxx
- ESP32-S3模组：ESP32-S3-WROOM-1-N16R8
- Flash容量：16MB
- PSRAM容量：8MB
- ESP-IDF VS Code扩展版本：2.1.0
- ESP-IDF框架版本：xxx
- ESP-IDF路径：通过VS Code扩展使用；当前Codex PowerShell环境未设置`IDF_PATH`且未找到`idf.py`

## 测试设备

- USB转TTL数量：1
- 逻辑分析仪：无 
- 万用表：有
- 稳定电源：无
- RS485收发器：未购买
- CAN收发器：未购买
- 第二块STM32：未购买
