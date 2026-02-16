# ESP32-S3 智能桌面助手实施计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 构建一个基于 ESP32-S3R8 的圆形智能桌面助手，集成电子相框、天气、时钟、语音控制、本地 AI、系统监控、番茄钟等功能，通过 Electron 应用与 macOS 通信。

**Architecture:**
- ESP32-S3 端：使用 LVGL 构建圆形 UI，运行本地 AI 模型（TensorFlow Lite Micro），通过 WebSocket 与 Electron 通信
- Electron 端：提供 WebSocket 服务器，采集系统信息，处理复杂语音识别，管理照片和天气数据
- 通信协议：基于 JSON 的 WebSocket 双向通信

**Tech Stack:**
- ESP32: Arduino/PlatformIO, LVGL 8.3+, TensorFlow Lite Micro, ESP-SR, ArduinoJson
- Electron: React 18, TypeScript, ws, systeminformation, Whisper API
- 开发工具: PlatformIO, VS Code, Arduino IDE

---

## 阶段 1: 项目基础搭建（第 1-2 周）

### Task 1.1: 初始化 Electron 项目

**Files:**
- Create: `electron-app/package.json`
- Create: `electron-app/tsconfig.json`
- Create: `electron-app/vite.config.ts`
- Create: `electron-app/src/main/index.ts`
- Create: `electron-app/src/renderer/App.tsx`

**Step 1: 创建 package.json**

```bash
cd /Users/apple/esp2helpdesktop/electron-app
npm init -y
```

**Step 2: 安装核心依赖**

```bash
npm install electron@latest react@18 react-dom@18
npm install -D @types/react @types/react-dom @types/node
npm install -D vite electron-builder typescript
npm install -D @vitejs/plugin-react
```

**Step 3: 安装项目依赖**

```bash
npm install ws systeminformation node-os-utils
npm install @types/ws -D
```

**Step 4: 创建 TypeScript 配置**

在 `electron-app/tsconfig.json`:
```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "lib": ["ES2020", "DOM"],
    "jsx": "react-jsx",
    "moduleResolution": "node",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "resolveJsonModule": true,
    "outDir": "dist"
  },
  "include": ["src/**/*"]
}
```

**Step 5: 创建 Vite 配置**

在 `electron-app/vite.config.ts`:
```typescript
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    outDir: 'dist-renderer'
  }
})
```

**Step 6: 创建主进程入口**

在 `electron-app/src/main/index.ts`:
```typescript
import { app, BrowserWindow } from 'electron'
import path from 'path'

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  })

  if (process.env.NODE_ENV === 'development') {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(path.join(__dirname, '../dist-renderer/index.html'))
  }
}

app.whenReady().then(createWindow)

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
```

**Step 7: 创建渲染进程入口**

在 `electron-app/src/renderer/App.tsx`:
```typescript
import React from 'react'

function App() {
  return (
    <div style={{ padding: '20px' }}>
      <h1>ESP32 桌面助手控制中心</h1>
      <p>WebSocket 服务器状态: 未启动</p>
    </div>
  )
}

export default App
```

**Step 8: 创建渲染进程 HTML**

在 `electron-app/index.html`:
```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32 Desktop Assistant</title>
</head>
<body>
  <div id="root"></div>
  <script type="module" src="/src/renderer/main.tsx"></script>
</body>
</html>
```

**Step 9: 创建渲染进程主文件**

在 `electron-app/src/renderer/main.tsx`:
```typescript
import React from 'react'
import ReactDOM from 'react-dom/client'
import App from './App'

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
)
```

**Step 10: 更新 package.json 脚本**

在 `electron-app/package.json` 添加:
```json
{
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "electron": "electron .",
    "start": "concurrently \"npm run dev\" \"wait-on http://localhost:5173 && npm run electron\""
  },
  "main": "dist/main/index.js"
}
```

**Step 11: 测试运行**

```bash
npm run dev
```

Expected: Vite 开发服务器启动在 http://localhost:5173

**Step 12: Commit**

```bash
git init
git add .
git commit -m "feat: 初始化 Electron + React + TypeScript 项目"
```

---

### Task 1.2: 实现 WebSocket 服务器

**Files:**
- Create: `electron-app/src/main/websocket.ts`
- Modify: `electron-app/src/main/index.ts`

**Step 1: 创建 WebSocket 服务器模块**

在 `electron-app/src/main/websocket.ts`:
```typescript
import { WebSocketServer, WebSocket } from 'ws'

export class DeviceWebSocketServer {
  private wss: WebSocketServer
  private clients: Set<WebSocket> = new Set()

  constructor(port: number = 8765) {
    this.wss = new WebSocketServer({ port })
    this.setupServer()
  }

  private setupServer() {
    this.wss.on('connection', (ws: WebSocket) => {
      console.log('ESP32 设备已连接')
      this.clients.add(ws)

      ws.on('message', (data: Buffer) => {
        try {
          const message = JSON.parse(data.toString())
          this.handleMessage(ws, message)
        } catch (error) {
          console.error('解析消息失败:', error)
        }
      })

      ws.on('close', () => {
        console.log('ESP32 设备已断开')
        this.clients.delete(ws)
      })

      ws.on('error', (error) => {
        console.error('WebSocket 错误:', error)
      })
    })

    console.log(`WebSocket 服务器运行在端口 ${this.wss.options.port}`)
  }

  private handleMessage(ws: WebSocket, message: any) {
    console.log('收到消息:', message)

    // 处理握手
    if (message.type === 'handshake') {
      this.sendMessage(ws, {
        type: 'handshake_ack',
        data: {
          server_version: '3.0.0',
          update_interval: 1000
        }
      })
    }
  }

  public sendMessage(ws: WebSocket, message: any) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(message))
    }
  }

  public broadcast(message: any) {
    this.clients.forEach(client => {
      this.sendMessage(client, message)
    })
  }

  public close() {
    this.wss.close()
  }
}
```

**Step 2: 集成到主进程**

修改 `electron-app/src/main/index.ts`:
```typescript
import { app, BrowserWindow } from 'electron'
import path from 'path'
import { DeviceWebSocketServer } from './websocket'

let wsServer: DeviceWebSocketServer

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  })

  if (process.env.NODE_ENV === 'development') {
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(path.join(__dirname, '../dist-renderer/index.html'))
  }
}

app.whenReady().then(() => {
  createWindow()
  wsServer = new DeviceWebSocketServer(8765)
})

app.on('window-all-closed', () => {
  if (wsServer) {
    wsServer.close()
  }
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
```

**Step 3: 测试 WebSocket 服务器**

```bash
npm run start
```

Expected: 控制台显示 "WebSocket 服务器运行在端口 8765"

**Step 4: Commit**

```bash
git add .
git commit -m "feat: 实现 WebSocket 服务器基础功能"
```

---

### Task 1.3: 初始化 ESP32-S3 固件项目

**Files:**
- Create: `esp32-firmware/platformio.ini`
- Create: `esp32-firmware/src/main.cpp`
- Create: `esp32-firmware/src/config.h`

**Step 1: 创建 PlatformIO 配置**

在 `esp32-firmware/platformio.ini`:
```ini
[env:esp32-s3-devkit]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; 8MB PSRAM 支持
board_build.arduino.memory_type = qio_opi
board_build.partitions = huge_app.csv

; 编译选项
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DLV_CONF_INCLUDE_SIMPLE
    -DLV_LVGL_H_INCLUDE_SIMPLE

; 库依赖
lib_deps =
    lvgl/lvgl@^8.3.0
    bblanchon/ArduinoJson@^6.21.0
    links2004/WebSockets@^2.4.0

monitor_speed = 115200
```

**Step 2: 创建配置文件**

在 `esp32-firmware/src/config.h`:
```cpp
#ifndef CONFIG_H
#define CONFIG_H

// WiFi 配置
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// WebSocket 服务器配置
#define WS_SERVER_HOST "192.168.1.100"  // 替换为你的 Mac IP
#define WS_SERVER_PORT 8765

// 屏幕配置
#define SCREEN_WIDTH 360
#define SCREEN_HEIGHT 360

// 设备信息
#define DEVICE_ID "esp32_s3_001"
#define FIRMWARE_VERSION "3.0.0"

#endif
```

**Step 3: 创建主程序**

在 `esp32-firmware/src/main.cpp`:
```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"

WebSocketsClient webSocket;
bool isConnected = false;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] 已断开连接");
      isConnected = false;
      break;

    case WStype_CONNECTED:
      Serial.println("[WebSocket] 已连接到服务器");
      isConnected = true;

      // 发送握手消息
      {
        StaticJsonDocument<512> doc;
        doc["type"] = "handshake";
        JsonObject data = doc.createNestedObject("data");
        data["device_id"] = DEVICE_ID;
        data["firmware_version"] = FIRMWARE_VERSION;
        data["screen_resolution"] = "360x360";
        data["screen_shape"] = "circular";

        String output;
        serializeJson(doc, output);
        webSocket.sendTXT(output);
      }
      break;

    case WStype_TEXT:
      Serial.printf("[WebSocket] 收到消息: %s\n", payload);

      // 解析消息
      {
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          const char* type = doc["type"];
          Serial.printf("消息类型: %s\n", type);
        }
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32-S3 桌面助手启动 ===");

  // 连接 WiFi
  Serial.printf("连接到 WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi 已连接");
  Serial.print("IP 地址: ");
  Serial.println(WiFi.localIP());

  // 连接 WebSocket
  Serial.printf("连接到 WebSocket 服务器: %s:%d\n", WS_SERVER_HOST, WS_SERVER_PORT);
  webSocket.begin(WS_SERVER_HOST, WS_SERVER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();

  // 每 5 秒发送心跳
  static unsigned long lastHeartbeat = 0;
  if (isConnected && millis() - lastHeartbeat > 5000) {
    StaticJsonDocument<256> doc;
    doc["type"] = "ping";
    JsonObject data = doc.createNestedObject("data");
    data["signal_strength"] = WiFi.RSSI();

    String output;
    serializeJson(doc, output);
    webSocket.sendTXT(output);

    lastHeartbeat = millis();
  }
}
```

**Step 4: 编译固件**

```bash
cd /Users/apple/esp2helpdesktop/esp32-firmware
pio run
```

Expected: 编译成功，生成 .bin 文件

**Step 5: Commit**

```bash
cd /Users/apple/esp2helpdesktop
git add esp32-firmware/
git commit -m "feat: 初始化 ESP32-S3 固件项目，实现 WiFi 和 WebSocket 连接"
```

---

### Task 1.4: 测试端到端通信

**Step 1: 启动 Electron 应用**

```bash
cd /Users/apple/esp2helpdesktop/electron-app
npm run start
```

Expected: Electron 窗口打开，WebSocket 服务器启动

**Step 2: 更新 ESP32 配置**

修改 `esp32-firmware/src/config.h`，填入正确的 WiFi 和服务器 IP

**Step 3: 烧录固件到 ESP32**

```bash
cd /Users/apple/esp2helpdesktop/esp32-firmware
pio run --target upload
```

**Step 4: 监控串口输出**

```bash
pio device monitor
```

Expected:
- WiFi 连接成功
- WebSocket 连接成功
- 收到握手确认消息

**Step 5: 验证 Electron 端**

在 Electron 控制台查看:
- "ESP32 设备已连接"
- 收到握手消息

**Step 6: Commit**

```bash
git add .
git commit -m "test: 验证 ESP32 与 Electron 端到端通信"
```

---

## 阶段 2: 系统信息采集（第 2 周）

### Task 2.1: 实现系统信息采集模块

**Files:**
- Create: `electron-app/src/main/system.ts`
- Modify: `electron-app/src/main/websocket.ts`

**Step 1: 创建系统信息模块**

在 `electron-app/src/main/system.ts`:
```typescript
import si from 'systeminformation'
import os from 'os'

export interface SystemInfo {
  cpu: {
    usage: number
    temperature: number
  }
  memory: {
    used: number
    total: number
    percentage: number
  }
  network: {
    upload: number
    download: number
  }
  time: string
  date: string
}

export class SystemMonitor {
  private lastNetworkStats: any = null

  async getSystemInfo(): Promise<SystemInfo> {
    const [cpuLoad, mem, networkStats] = await Promise.all([
      si.currentLoad(),
      si.mem(),
      si.networkStats()
    ])

    // 计算网络速度
    let upload = 0
    let download = 0
    if (this.lastNetworkStats && networkStats[0]) {
      const timeDiff = (Date.now() - this.lastNetworkStats.timestamp) / 1000
      upload = (networkStats[0].tx_bytes - this.lastNetworkStats.tx_bytes) / timeDiff
      download = (networkStats[0].rx_bytes - this.lastNetworkStats.rx_bytes) / timeDiff
    }

    if (networkStats[0]) {
      this.lastNetworkStats = {
        tx_bytes: networkStats[0].tx_bytes,
        rx_bytes: networkStats[0].rx_bytes,
        timestamp: Date.now()
      }
    }

    const now = new Date()

    return {
      cpu: {
        usage: Math.round(cpuLoad.currentLoad),
        temperature: 0 // macOS 需要特殊权限获取温度
      },
      memory: {
        used: Math.round(mem.used / 1024 / 1024 / 1024 * 10) / 10,
        total: Math.round(mem.total / 1024 / 1024 / 1024 * 10) / 10,
        percentage: Math.round((mem.used / mem.total) * 100)
      },
      network: {
        upload: Math.round(upload / 1024),
        download: Math.round(download / 1024)
      },
      time: now.toLocaleTimeString('zh-CN', { hour12: false }),
      date: now.toLocaleDateString('zh-CN')
    }
  }
}
```

**Step 2: 集成到 WebSocket 服务器**

修改 `electron-app/src/main/websocket.ts`:
```typescript
import { WebSocketServer, WebSocket } from 'ws'
import { SystemMonitor } from './system'

export class DeviceWebSocketServer {
  private wss: WebSocketServer
  private clients: Set<WebSocket> = new Set()
  private systemMonitor: SystemMonitor
  private updateInterval: NodeJS.Timeout | null = null

  constructor(port: number = 8765) {
    this.wss = new WebSocketServer({ port })
    this.systemMonitor = new SystemMonitor()
    this.setupServer()
  }

  private setupServer() {
    this.wss.on('connection', (ws: WebSocket) => {
      console.log('ESP32 设备已连接')
      this.clients.add(ws)

      // 开始发送系统信息
      this.startSystemInfoUpdates(ws)

      ws.on('message', (data: Buffer) => {
        try {
          const message = JSON.parse(data.toString())
          this.handleMessage(ws, message)
        } catch (error) {
          console.error('解析消息失败:', error)
        }
      })

      ws.on('close', () => {
        console.log('ESP32 设备已断开')
        this.clients.delete(ws)
      })

      ws.on('error', (error) => {
        console.error('WebSocket 错误:', error)
      })
    })

    console.log(`WebSocket 服务器运行在端口 ${this.wss.options.port}`)
  }

  private async startSystemInfoUpdates(ws: WebSocket) {
    const sendUpdate = async () => {
      if (ws.readyState === WebSocket.OPEN) {
        const systemInfo = await this.systemMonitor.getSystemInfo()
        this.sendMessage(ws, {
          type: 'system_info',
          data: systemInfo
        })
      }
    }

    // 立即发送一次
    await sendUpdate()

    // 每秒更新
    const interval = setInterval(sendUpdate, 1000)

    ws.on('close', () => {
      clearInterval(interval)
    })
  }

  private handleMessage(ws: WebSocket, message: any) {
    console.log('收到消息:', message)

    if (message.type === 'handshake') {
      this.sendMessage(ws, {
        type: 'handshake_ack',
        data: {
          server_version: '3.0.0',
          update_interval: 1000
        }
      })
    }
  }

  public sendMessage(ws: WebSocket, message: any) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(message))
    }
  }

  public broadcast(message: any) {
    this.clients.forEach(client => {
      this.sendMessage(client, message)
    })
  }

  public close() {
    this.wss.close()
  }
}
```

**Step 3: 测试系统信息采集**

```bash
cd /Users/apple/esp2helpdesktop/electron-app
npm run start
```

Expected: 控制台每秒输出系统信息

**Step 4: Commit**

```bash
git add .
git commit -m "feat: 实现系统信息采集和实时推送"
```

---

### Task 2.2: ESP32 接收并显示系统信息

**Files:**
- Modify: `esp32-firmware/src/main.cpp`

**Step 1: 添加系统信息处理**

修改 `esp32-firmware/src/main.cpp` 的 `webSocketEvent` 函数:
```cpp
case WStype_TEXT:
  Serial.printf("[WebSocket] 收到消息: %s\n", payload);

  {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      const char* type = doc["type"];

      if (strcmp(type, "system_info") == 0) {
        JsonObject data = doc["data"];

        float cpuUsage = data["cpu"]["usage"];
        float memPercentage = data["memory"]["percentage"];
        const char* time = data["time"];

        Serial.println("=== 系统信息 ===");
        Serial.printf("CPU: %.1f%%\n", cpuUsage);
        Serial.printf("内存: %.1f%%\n", memPercentage);
        Serial.printf("时间: %s\n", time);
      }
    }
  }
  break;
```

**Step 2: 编译并烧录**

```bash
cd /Users/apple/esp2helpdesktop/esp32-firmware
pio run --target upload
pio device monitor
```

Expected: 串口每秒输出系统信息

**Step 3: Commit**

```bash
git add .
git commit -m "feat: ESP32 接收并显示系统信息"
```

---

## 下一步计划

完成阶段 1-2 后，接下来的任务包括：

**阶段 3: LVGL UI 开发（第 3-4 周）**
- Task 3.1: 配置 LVGL 和圆形屏幕驱动
- Task 3.2: 实现圆形导航盘 UI
- Task 3.3: 实现系统监控页面
- Task 3.4: 实现时钟页面

**阶段 4: 电子相框功能（第 4-5 周）**
- Task 4.1: TF 卡读取和照片解码
- Task 4.2: 照片显示和轮播
- Task 4.3: Electron 照片管理工具

**阶段 5: 天气功能（第 5 周）**
- Task 5.1: 天气 API 集成
- Task 5.2: 天气数据推送
- Task 5.3: 天气 UI 和动画

**阶段 6: 番茄钟功能（第 6 周）**
- Task 6.1: 番茄钟逻辑实现
- Task 6.2: 倒计时 UI
- Task 6.3: 音频提醒

**阶段 7: 语音控制（第 7-8 周）**
- Task 7.1: I2S 麦克风录音
- Task 7.2: 语音数据上传
- Task 7.3: Whisper API 集成
- Task 7.4: 命令解析和执行

**阶段 8: 本地 AI 集成（第 9-10 周）**
- Task 8.1: TensorFlow Lite Micro 集成
- Task 8.2: 训练和部署唤醒词模型
- Task 8.3: 训练和部署命令识别模型
- Task 8.4: AI 推理优化

**阶段 9: 测试和优化（第 11-12 周）**
- Task 9.1: 功能测试
- Task 9.2: 性能优化
- Task 9.3: UI/UX 优化
- Task 9.4: 文档编写

---

## 执行建议

**Plan complete and saved to `docs/plans/2026-02-15-esp32-desktop-assistant.md`. Two execution options:**

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

**Which approach?**
