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
- ✅ **OTA 远程升级**：ESP8266 固件通过 WiFi 无线升级，无需插线

---

## 🛠️ 硬件清单

| 模块 | 型号 | 用途 |
|------|------|------|
| 主控 MCU | STM32F103C8T6 | 传感器采集、OLED 显示、任务调度 |
| 通信协处理器 | ESP8266-01S | WiFi 连接、MQTT 协议栈 |
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
┌─────────────────────────────────────────────────────┐
│ Application Layer │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ │
│ │ Sensor_Task │ │ Display_Task│ │ Report_Task │ │
│ └─────────────┘ └─────────────┘ └─────────────┘ │
│ ↓ ↓ ↓ │
│ ┌─────────────────────────────────────────────────┐│
│ │ Message Queue (双队列设计) ││
│ └─────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────┤
│ Middleware Layer (FreeRTOS + USART2 中断接收) │
├─────────────────────────────────────────────────────┤
│ BSP Layer (DHT11 / 光敏 / MQ-2 / OLED / W25Q64) │
├─────────────────────────────────────────────────────┤
│ Hardware Layer (STM32 + ESP8266 双MCU) │
└─────────────────────────────────────────────────────┘

text

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

---

## 📁 目录结构
STM32-ESP8266-Env-Monitor/
├── STM32_Project/ # STM32 工程代码
├── ESP8266_Project/ # ESP8266 Arduino 代码
├── docs/ # 项目文档与截图
├── .gitignore # Git 忽略文件
└── README.md # 本文件

text

---

## 📜 许可证

本项目采用 MIT 许可证。

---

## 📧 联系方式

程文轩 - 2304112432@qq.com

**项目链接**：[https://github.com/Cheng-netizen21/STM32-ESP8266-Env-Monitor](https://github.com/Cheng-netizen21/STM32-ESP8266-Env-Monitor)