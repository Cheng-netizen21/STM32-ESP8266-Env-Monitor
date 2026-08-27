好的，我在原有 README 的基础上增加了 **“🔧 OTA 远程升级机制”** 章节，详细介绍 STM32 + ESP8266 双 MCU 的 OTA 流程，包括 IAP 启动、握手协议、固件传输和断网续传等，并更新了亮点说明。以下是完整的 README.md 文件：

---

```markdown
# 基于 STM32 与 ESP8266 的物联网环境监测终端

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20ESP8266-blue)](https://github.com/Cheng-netizen21/STM32-ESP8266-Env-Monitor)

> 双MCU协同架构，支持断网续传、远程控制、OTA升级的物联网环境监测系统。

---

## 📖 项目简介

本项目以 **STM32F103C8T6** 为核心控制器，**ESP8266** 为通信协处理器，采集温湿度、光照、烟雾浓度等环境数据，通过 MQTT 协议上报至 OneNET 云平台。用户可通过云端实时查看数据曲线，并远程控制 LED 和蜂鸣器。

**系统亮点**：
- ✅ **双MCU协同架构**：STM32 负责采集与显示，ESP8266 负责网络通信
- ✅ **断网续传**：网络中断时数据缓存至 SPI Flash（W25Q64），恢复后自动补传
- ✅ **远程控制**：通过 OneNET 平台下发指令控制 LED 亮灭和蜂鸣器开关
- ✅ **边缘联动报警**：温度 ≥ 30℃ 或烟雾超标时，蜂鸣器自动报警
- ✅ **OTA 远程升级**：ESP8266 固件通过 WiFi 无线升级，STM32 固件通过 ESP8266 串口传输实现 IAP 升级，无需拆机烧录

---

## 🛠️ 硬件清单

| 模块 | 型号 | 用途 |
|------|------|------|
| 主控 MCU | STM32F103C8T6 | 传感器采集、OLED 显示、任务调度 |
| 通信协处理器 | ESP8266-01S | WiFi 连接、MQTT 协议栈、OTA 下载代理 |
| 温湿度传感器 | DHT11 | 采集温湿度 |
| 光照传感器 | 光敏电阻模块 | 采集光照强度 |
| 烟雾传感器 | MQ-2 | 检测烟雾 / 可燃气体 |
| 存储芯片 | W25Q64（SPI Flash） | 断网数据缓存 |
| 显示模块 | 0.96 寸 OLED | 本地显示四参数及网络状态 |
| 执行器 | LED + 蜂鸣器 | 远程控制与本地报警 |

---

## 🔌 硬件接线图

> 详见 `docs/wiring_diagram.png`

| STM32 引脚 | 连接设备 | 功能 |
|-----------|---------|------|
| PA0 | DHT11 DATA | 单总线数据 |
| PA1 | 光敏电阻 AO | ADC 采集光照 |
| PA2 | ESP8266 RX | USART2_TX |
| PA3 | ESP8266 TX | USART2_RX |
| PA4 | MQ-2 AO | ADC 采集烟雾 |
| PA5 | W25Q64 CLK | SPI 时钟 |
| PA6 | W25Q64 MISO | SPI 数据输入 |
| PA7 | W25Q64 MOSI | SPI 数据输出 |
| PA8 | LED 正极 | GPIO 输出控制 |
| PB0 | 蜂鸣器 IO | GPIO 输出控制 |
| PB6 | OLED SCL | I2C 时钟 |
| PB7 | OLED SDA | I2C 数据 |
| PB12 | W25Q64 CS | SPI 片选 |

> **供电说明**：ESP8266 使用独立 3.3V 电源（≥500mA），STM32 通过 USB 供电，所有 GND 共地。

---

## 📐 软件架构图

> 详见 `docs/architecture.png`

```
┌─────────────────────────────────────────────────────┐
│                   Application Layer                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │ Sensor_Task │  │ Display_Task│  │ Report_Task │ │
│  └─────────────┘  └─────────────┘  └─────────────┘ │
│        ↓                ↓                ↓          │
│  ┌─────────────────────────────────────────────────┐│
│  │         Message Queue (双队列设计)              ││
│  └─────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────┤
│         Middleware Layer (FreeRTOS + USART2 中断接收)│
├─────────────────────────────────────────────────────┤
│    BSP Layer (DHT11 / 光敏 / MQ-2 / OLED / W25Q64) │
├─────────────────────────────────────────────────────┤
│        Hardware Layer (STM32 + ESP8266 双MCU)       │
└─────────────────────────────────────────────────────┘
```

---

## 🔧 OTA 远程升级机制

本系统支持 **STM32 固件通过 ESP8266 无线升级**，无需连接烧录器，适用于远程部署和维护。

### 升级流程

1. **触发方式**：用户通过 OneNET 云平台向 ESP8266 的 MQTT 订阅主题（`$sys/{productId}/{deviceName}/thing/ota`）发送 JSON 指令，包含固件下载 URL：
   ```json
   {"cmd":"ota","url":"http://your-server/MyProject.bin"}
   ```
   或 ESP8266 定时检查版本文件（`version.txt`）自动升级。

2. **ESP8266 下载固件**：ESP8266 通过 HTTP 从指定服务器下载 `.bin` 固件文件，并缓存至内存。

3. **串口传输与 IAP 握手**：
   - STM32 重启后进入 IAP（In-Application Programming）模式，等待 `'A'` 指令进入 OTA 模式。
   - IAP 主动发送 `0x55` 握手信号，ESP8266 回复 `0xAA`，双方建立串口通信链路。
   - ESP8266 依次发送固件长度（4 字节大端）和分块数据（每块 1024 字节），STM32 每接收一块回复 `0x06`（ACK）确认。

4. **STM32 写入 Flash**：STM32 将接收到的固件按字（4 字节）编程到 `0x08003000` 起始地址（APP 区域），并在写入前预先擦除整个 APP 区域（64KB），避免旧代码残留。

5. **自动跳转**：传输完成后，STM32 自动复位并跳转至新固件，实现无缝升级。

### 关键设计

- **双串口分离**：USART1（PA9/PA10）用于调试日志，USART2（PA2/PA3）用于与 ESP8266 通信，互不干扰。
- **超时重传**：STM32 接收超时（5 秒无数据）会自动重新握手，保证传输可靠性。
- **断电续传**：若升级过程中断电，STM32 上电后仍可重新进入 OTA 模式，固件未写入完全则不会跳转，保护设备安全。
- **兼容性**：OTA 升级不影响原有传感器采集、显示和上报功能，升级完成后所有功能自动恢复。

---

## 🚀 快速开始

### STM32 端（Keil MDK）

1. 用 STM32CubeMX 打开 `STM32_Project/` 下的 `.ioc` 文件
2. 检查引脚配置（对照上表），确认 USART1、USART2、SPI1、ADC1 已使能
3. 点击 **GENERATE CODE** 生成代码（注意：不会覆盖你已添加的 `w25q64.c` 等文件）
4. 用 Keil MDK 打开 `MDK-ARM/` 下的工程文件，编译并下载

### ESP8266 端（Arduino IDE）

1. 在 Arduino IDE 中安装 **ESP8266** 开发板支持包和 **PubSubClient** 库
2. 打开 `ESP8266_Project/` 下的 `.ino` 文件
3. 修改 WiFi 和 OneNET 配置
4. 上传到 ESP8266（首次需用 USB 转 TTL，之后支持 OTA 无线升级）

---

## ❓ 常见问题

1. **DHT11 一直读取失败？**  
   检查 DATA 引脚（PA0）与 3.3V 之间是否连接了 4.7kΩ 上拉电阻。

2. **MQTT 连接返回错误码 4？**  
   错误码 4 表示用户名或密码错误。检查 `PRODUCT_ID`、`DEVICE_NAME` 和 `PASSWORD_TOKEN` 是否与 OneNET 平台一致，Token 是否已过期。

3. **W25Q64 ID 显示 0xEF40 而非 0xEF16？**  
   不影响使用，不同批次的 W25Q64 ID 可能有差异，读写功能已验证正常。

4. **ESP8266 烧录失败（连接超时）？**  
   烧录时需将 GPIO0 引脚短接到 GND，进入下载模式后再上电。

5. **OTA 升级后 STM32 无法启动？**  
   检查固件起始地址是否为 `0x08003000`（在 Keil 中设置 IROM1 起始地址），并确保 `SystemInit` 函数中已设置向量表偏移。

---

## 📁 目录结构

```
STM32-ESP8266-Env-Monitor/
├── STM32_Project/          # STM32 工程代码
├── ESP8266_Project/        # ESP8266 Arduino 代码
├── docs/                   # 项目文档与截图
├── .gitignore              # Git 忽略文件
└── README.md               # 本文件
```

---

## 📜 许可证

本项目采用 MIT 许可证。

---

## 📧 联系方式

程文轩 - 2304112432@qq.com

**项目链接**：[https://github.com/Cheng-netizen21/STM32-ESP8266-Env-Monitor](https://github.com/Cheng-netizen21/STM32-ESP8266-Env-Monitor)
```