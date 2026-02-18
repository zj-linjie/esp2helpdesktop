# AI小智 集成部署文档

## 📋 目录

- [项目概述](#项目概述)
- [环境准备](#环境准备)
- [项目结构](#项目结构)
- [硬件配置](#硬件配置)
- [编译流程](#编译流程)
- [烧录步骤](#烧录步骤)
- [测试验证](#测试验证)
- [Electron 集成](#electron-集成)
- [故障排除](#故障排除)

---

## 项目概述

### 什么是 AI小智？

AI小智是一个基于 MCP (Model Context Protocol) 的语音交互 AI 聊天机器人，运行在 ESP32 微控制器上。

**核心特性**：
- ✅ 离线语音唤醒（ESP-SR）
- ✅ 流式 ASR + LLM + TTS 架构
- ✅ 支持 Qwen/DeepSeek 大语言模型
- ✅ 官方 xiaozhi.me 服务（免费使用）
- ✅ MCP 协议扩展（智能家居、PC 控制等）
- ✅ 多语言支持（中文、英文、日文）

### 集成目标

将 AI小智 集成到 ESP32 Help Desktop 项目中，作为独立的 AI 助手模块：
- ESP32-S3R8 运行 AI小智 固件
- Electron 应用提供 AI 控制面板
- 通过 WebSocket 监控 AI 状态
- 独立运行，不与现有功能交互

---

## 环境准备

### 1. 系统要求

**推荐系统**：
- macOS 12.0+ / Linux (Ubuntu 20.04+) / Windows 10+
- 推荐使用 macOS 或 Linux（编译速度更快，驱动问题更少）

**硬件要求**：
- ESP32-S3R8 开发板
  - 16MB Flash ✅
  - 8MB PSRAM ✅
  - 麦克风 + 扬声器 ✅
  - 圆形显示屏 (360x360) ✅

### 2. 安装 ESP-IDF

AI小智 需要 **ESP-IDF 5.4 或更高版本**。

#### macOS 安装步骤

```bash
# 1. 安装依赖
brew install cmake ninja dfu-util

# 2. 创建 ESP 目录
mkdir -p ~/esp
cd ~/esp

# 3. 克隆 ESP-IDF
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git

# 4. 安装工具链
cd ~/esp/esp-idf
./install.sh esp32s3

# 5. 设置环境变量（每次使用前需要执行）
. ~/esp/esp-idf/export.sh

# 6. 验证安装
idf.py --version
```

#### 添加到 shell 配置（可选）

为了方便使用，可以添加别名到 `~/.zshrc` 或 `~/.bashrc`：

```bash
# 添加到 ~/.zshrc
echo 'alias get_idf=". ~/esp/esp-idf/export.sh"' >> ~/.zshrc
source ~/.zshrc

# 使用时只需运行
get_idf
```

### 3. 安装 VSCode + ESP-IDF 插件（推荐）

```bash
# 1. 安装 VSCode
brew install --cask visual-studio-code

# 2. 安装 ESP-IDF 插件
# 打开 VSCode -> Extensions -> 搜索 "ESP-IDF" -> 安装
```

**插件配置**：
- ESP-IDF Path: `~/esp/esp-idf`
- Tools Path: `~/.espressif`

### 4. 克隆 AI小智 项目

```bash
cd ~/esp2helpdesktop
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32
```

---

## 项目结构

```
xiaozhi-esp32/
├── main/                      # 主程序源代码
│   ├── application.cc         # 应用主逻辑
│   ├── audio/                 # 音频处理模块
│   ├── boards/                # 开发板配置（96+ 种）
│   │   ├── lilygo-t-circle-s3/  # 圆形屏幕参考配置
│   │   └── custom-board/      # 自定义配置（待创建）
│   ├── display/               # 显示驱动
│   ├── led/                   # LED 控制
│   ├── protocols/             # 通信协议（WebSocket/MQTT）
│   └── mcp_server.cc          # MCP 协议实现
├── partitions/                # 分区表配置
│   └── v2/                    # v2 版本分区表
├── scripts/                   # 工具脚本
├── sdkconfig.defaults         # 基础配置
├── sdkconfig.defaults.esp32s3 # ESP32-S3 专用配置
└── CMakeLists.txt             # 构建配置
```

---

## 硬件配置

### 1. 硬件规格

**ESP32-S3R8 配置**：
- 芯片：ESP32-S3
- Flash：16MB
- PSRAM：8MB (Octal)
- WiFi：2.4GHz 802.11 b/g/n

**外设**：
- 麦克风：待确认型号（MSM261/INMP441/其他）
- 扬声器：待确认放大器（MAX98357A/NS4168/其他）
- 显示屏：360x360 圆形 LCD
- LED：可选
- 按钮：可选

### 2. 创建自定义开发板配置

等硬件到货后，根据厂家提供的示例代码创建配置文件。

#### 配置文件模板

创建目录：`main/boards/esp32-helpdesk-360/`

**config.h** 模板：
```cpp
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include "pin_config.h"

// 音频输入配置
#define AUDIO_INPUT_REFERENCE true
#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 麦克风 I2S 引脚（待填写）
#define AUDIO_MIC_I2S_GPIO_BCLK static_cast<gpio_num_t>(GPIO_NUM_XX)
#define AUDIO_MIC_I2S_GPIO_WS static_cast<gpio_num_t>(GPIO_NUM_XX)
#define AUDIO_MIC_I2S_GPIO_DATA static_cast<gpio_num_t>(GPIO_NUM_XX)

// 扬声器 I2S 引脚（待填写）
#define AUDIO_SPKR_I2S_GPIO_BCLK static_cast<gpio_num_t>(GPIO_NUM_XX)
#define AUDIO_SPKR_I2S_GPIO_LRCLK static_cast<gpio_num_t>(GPIO_NUM_XX)
#define AUDIO_SPKR_I2S_GPIO_DATA static_cast<gpio_num_t>(GPIO_NUM_XX)
#define AUDIO_SPKR_ENABLE static_cast<gpio_num_t>(GPIO_NUM_XX)

// 显示屏配置
#define DISPLAY_WIDTH 360
#define DISPLAY_HEIGHT 360
#define DISPLAY_MOSI GPIO_NUM_XX
#define DISPLAY_SCLK GPIO_NUM_XX
#define DISPLAY_DC GPIO_NUM_XX
#define DISPLAY_RST GPIO_NUM_XX
#define DISPLAY_CS GPIO_NUM_XX
#define DISPLAY_BL static_cast<gpio_num_t>(GPIO_NUM_XX)

// 显示方向调整
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

// 背光控制
#define DISPLAY_BACKLIGHT_PIN DISPLAY_BL
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// 按钮配置（可选）
#define BUILTIN_LED_GPIO GPIO_NUM_NC
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#endif // _BOARD_CONFIG_H_
```

**pin_config.h** 模板：
```cpp
#pragma once

// 麦克风引脚（待填写）
#define MIC_BCLK XX
#define MIC_WS XX
#define MIC_DATA XX

// 扬声器引脚（待填写）
#define SPKR_BCLK XX
#define SPKR_LRCLK XX
#define SPKR_DATA XX
#define SPKR_SD_MODE XX

// 显示屏引脚（待填写）
#define LCD_WIDTH 360
#define LCD_HEIGHT 360
#define LCD_MOSI XX
#define LCD_SCLK XX
#define LCD_DC XX
#define LCD_RST XX
#define LCD_CS XX
#define LCD_BL XX

// I2C 引脚（如果有触摸屏）
#define IIC_SDA XX
#define IIC_SCL XX
```

### 3. 配置 menuconfig

```bash
cd xiaozhi-esp32

# 设置目标芯片
idf.py set-target esp32s3

# 打开配置菜单
idf.py menuconfig
```

**关键配置项**：

1. **Board Selection**
   - 选择 `Custom Board` 或你创建的配置

2. **WiFi Configuration**
   - WiFi SSID
   - WiFi Password

3. **Server Configuration**
   - 使用官方服务器：`xiaozhi.me`
   - 或自定义服务器地址

4. **Audio Configuration**
   - Sample Rate: 24000 Hz
   - Codec: OPUS

5. **Display Configuration**
   - Width: 360
   - Height: 360
   - Driver: 根据实际屏幕型号选择

---

## 编译流程

### 1. 准备编译环境

```bash
# 激活 ESP-IDF 环境
. ~/esp/esp-idf/export.sh

# 或使用别名
get_idf

# 进入项目目录
cd ~/esp2helpdesktop/xiaozhi-esp32
```

### 2. 清理旧构建（可选）

```bash
# 完全清理
idf.py fullclean

# 或只清理构建文件
idf.py clean
```

### 3. 配置项目

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置项目（可选）
idf.py menuconfig
```

### 4. 编译固件

```bash
# 编译
idf.py build

# 编译成功后，固件位于：
# build/xiaozhi-esp32.bin
```

**编译输出**：
```
Project build complete. To flash, run:
  idf.py -p (PORT) flash
or
  idf.py -p (PORT) flash monitor
```

### 5. 编译时间

- **首次编译**：约 5-10 分钟（取决于机器性能）
- **增量编译**：约 30 秒 - 2 分钟

---

## 烧录步骤

### 1. 连接硬件

```bash
# 查看串口设备
ls /dev/cu.*

# 通常是：
# /dev/cu.usbserial-XXXX
# /dev/cu.SLAB_USBtoUART
```

### 2. 烧录固件

```bash
# 方法 1：烧录并监控（推荐）
idf.py -p /dev/cu.usbserial-XXXX flash monitor

# 方法 2：只烧录
idf.py -p /dev/cu.usbserial-XXXX flash

# 方法 3：擦除后烧录（首次烧录推荐）
idf.py -p /dev/cu.usbserial-XXXX erase-flash flash monitor
```

### 3. 烧录参数

**波特率**：
- 默认：460800
- 如果烧录失败，可以降低波特率：
  ```bash
  idf.py -p /dev/cu.usbserial-XXXX -b 115200 flash
  ```

### 4. 监控串口输出

```bash
# 烧录后监控
idf.py -p /dev/cu.usbserial-XXXX monitor

# 退出监控：Ctrl + ]
```

---

## 测试验证

### 1. 启动日志检查

烧录完成后，查看串口输出，确认以下信息：

```
I (xxx) main: ESP32-S3 Help Desk AI Assistant
I (xxx) main: Flash: 16MB, PSRAM: 8MB
I (xxx) wifi: WiFi connecting...
I (xxx) wifi: WiFi connected, IP: 192.168.x.x
I (xxx) xiaozhi: Connecting to xiaozhi.me...
I (xxx) xiaozhi: Connected to server
I (xxx) audio: Audio initialized
I (xxx) display: Display initialized (360x360)
I (xxx) wakeup: Wake word engine started
```

### 2. 功能测试

#### 测试 1：语音唤醒
1. 说出唤醒词："你好小智"
2. 观察：
   - 显示屏应显示唤醒动画
   - LED 应亮起（如果有）
   - 串口输出：`Wake word detected`

#### 测试 2：语音对话
1. 唤醒后说："今天天气怎么样？"
2. 观察：
   - 串口输出识别的文字
   - AI 应该回复天气信息
   - 扬声器播放 TTS 语音

#### 测试 3：长时间运行
1. 让设备运行 1 小时
2. 检查：
   - 内存是否稳定
   - 是否有崩溃或重启
   - WiFi 连接是否稳定

### 3. 性能指标

**正常运行指标**：
- CPU 使用率：30-60%
- 内存使用：< 200KB (DRAM)
- PSRAM 使用：< 4MB
- WiFi 信号：> -70 dBm
- 响应延迟：< 2 秒

---

## Electron 集成

### 1. AI 控制页面

在 Electron 应用中添加 AI 控制页面，用于监控和管理 AI 功能。

**页面功能**：
- ✅ AI 状态显示（在线/离线、对话中）
- ✅ WiFi 配置
- ✅ 服务器配置
- ✅ 日志查看
- ✅ 固件更新
- ✅ 测试工具

### 2. WebSocket 通信

ESP32 AI 模块通过 WebSocket 与 Electron 应用通信。

**通信协议**：
```json
// ESP32 -> Electron: 状态更新
{
  "type": "ai_status",
  "data": {
    "online": true,
    "talking": false,
    "wifiSignal": -45,
    "uptime": 3600
  }
}

// ESP32 -> Electron: 对话内容
{
  "type": "ai_conversation",
  "data": {
    "role": "user",
    "text": "今天天气怎么样？"
  }
}

// Electron -> ESP32: 配置更新
{
  "type": "ai_config",
  "data": {
    "wifiSsid": "MyWiFi",
    "wifiPassword": "password"
  }
}
```

### 3. 集成步骤

详见后续创建的 `AIPage.tsx` 组件。

---

## 故障排除

### 问题 1：编译失败

**错误**：`fatal error: esp_idf_version.h: No such file or directory`

**解决方案**：
```bash
# 重新激活 ESP-IDF 环境
. ~/esp/esp-idf/export.sh

# 清理并重新编译
idf.py fullclean
idf.py build
```

### 问题 2：烧录失败

**错误**：`A fatal error occurred: Failed to connect to ESP32`

**解决方案**：
1. 检查 USB 连接
2. 按住 BOOT 按钮，然后按 RESET 按钮
3. 降低波特率：
   ```bash
   idf.py -p /dev/cu.usbserial-XXXX -b 115200 flash
   ```

### 问题 3：WiFi 连接失败

**错误**：`WiFi connection timeout`

**解决方案**：
1. 检查 WiFi SSID 和密码是否正确
2. 确认 WiFi 是 2.4GHz（ESP32 不支持 5GHz）
3. 检查路由器是否限制了设备连接

### 问题 4：语音唤醒不工作

**可能原因**：
1. 麦克风未正确连接
2. 麦克风引脚配置错误
3. 环境噪音过大

**解决方案**：
1. 检查麦克风硬件连接
2. 验证 `config.h` 中的麦克风引脚配置
3. 在安静环境中测试
4. 调整唤醒灵敏度（menuconfig）

### 问题 5：TTS 无声音

**可能原因**：
1. 扬声器未正确连接
2. 扬声器引脚配置错误
3. 音量设置为 0

**解决方案**：
1. 检查扬声器硬件连接
2. 验证 `config.h` 中的扬声器引脚配置
3. 调整音量设置

### 问题 6：显示屏不显示

**可能原因**：
1. 显示屏未正确连接
2. 显示屏引脚配置错误
3. 显示驱动不匹配

**解决方案**：
1. 检查显示屏硬件连接
2. 验证 `config.h` 中的显示屏引脚配置
3. 确认显示驱动型号（menuconfig）
4. 调整显示方向参数（MIRROR_X/Y, SWAP_XY）

---

## 参考资源

### 官方文档
- [AI小智 GitHub](https://github.com/78/xiaozhi-esp32)
- [AI小智 百科全书](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb)
- [ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/)

### 社区支持
- Discord: [AI小智 Discord](https://discord.gg/xiaozhi)
- QQ 群：见 GitHub README

### 相关项目
- [AI小智 服务器（Python）](https://github.com/xinnan-tech/xiaozhi-esp32-server)
- [自定义资源生成器](https://github.com/78/xiaozhi-assets-generator)

---

## 下一步

1. ✅ 等待硬件到货
2. ✅ 获取厂家示例代码
3. ✅ 创建自定义开发板配置
4. ✅ 编译并烧录固件
5. ✅ 测试基本功能
6. ✅ 集成到 Electron 应用
7. ✅ 完善 AI 控制页面

---

**文档版本**：v1.0
**最后更新**：2026-02-18
**维护者**：ESP32 Help Desktop Team
