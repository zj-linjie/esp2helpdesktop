# ESP32-S3 智能桌面助手

基于 ESP32-S3R8 的圆形智能桌面助手系统，集成电子相框、天气、时钟、语音控制、本地 AI、系统监控、番茄钟等功能。

## 🎉 最新更新

✅ **已完成 Electron 控制面板和 Web 模拟器**

- [x] 实时系统监控界面（CPU、内存、网络图表）
- [x] WebSocket 连接状态显示
- [x] 消息日志和设备管理
- [x] 360×360 圆形屏幕模拟器
- [x] 圆形导航盘（8 个功能图标）
- [x] 系统监控页面（圆形进度环）
- [x] 时钟页面
- [x] 深色主题界面

## 项目状态

✅ **阶段 1-2 已完成**（项目基础搭建和系统信息采集）
✅ **Electron 界面开发完成**（控制面板 + Web 模拟器）

## 快速开始

### 一键启动

```bash
cd /Users/apple/esp2helpdesktop
./start.sh
```

或者手动启动：

```bash
cd electron-app
npm install  # 首次运行需要
npm start
```

### 应用界面

启动后你将看到两个标签页：

#### 1. 📊 控制中心
- **系统监控卡片**：实时显示 CPU、内存、网络使用率
- **性能图表**：30 秒历史数据折线图
- **连接状态**：WebSocket 服务器状态和已连接设备
- **消息日志**：实时消息记录（最近 50 条）
- **设备列表**：显示所有已连接的 ESP32 设备

#### 2. 📱 设备模拟器
- **圆形屏幕**：360×360 像素，完美模拟 ESP32-S3 圆形显示器
- **导航盘**：8 个功能图标环形排列
  - 📷 电子相框
  - 🌤️ 天气
  - 🕐 时钟
  - 🎤 语音
  - 🤖 AI 助手
  - 📊 系统监控
  - 🍅 番茄钟
  - ⚡ 快捷控制
- **系统监控页面**：圆形进度环显示 CPU 和内存使用率
- **时钟页面**：大字体实时时钟显示

### 测试 WebSocket 通信

在新终端中运行测试客户端：

```bash
cd /Users/apple/esp2helpdesktop
node test-client.js
```

你将在控制中心看到：
- 设备连接通知
- 实时系统信息更新
- 消息日志记录

## 项目结构

```
esp2helpdesktop/
├── electron-app/                    # Electron 桌面应用
│   ├── src/
│   │   ├── main/                   # 主进程
│   │   │   ├── index.ts           # 主进程入口
│   │   │   ├── websocket.ts       # WebSocket 服务器
│   │   │   └── system.ts          # 系统信息采集
│   │   └── renderer/               # 渲染进程
│   │       ├── App.tsx            # 主应用（标签页）
│   │       └── components/
│   │           ├── SystemMonitor.tsx      # 系统监控组件
│   │           ├── ConnectionStatus.tsx   # 连接状态
│   │           ├── MessageLog.tsx         # 消息日志
│   │           ├── DeviceList.tsx         # 设备列表
│   │           ├── ESP32Simulator.tsx     # 模拟器容器
│   │           └── simulator/
│   │               ├── CircularScreen.tsx      # 圆形屏幕
│   │               ├── NavigationDial.tsx      # 导航盘
│   │               ├── SystemMonitorPage.tsx   # 监控页面
│   │               └── ClockPage.tsx           # 时钟页面
│   └── package.json
├── esp32-firmware/                  # ESP32-S3 固件
│   ├── src/
│   │   ├── main.cpp                # 主程序
│   │   └── config.h                # 配置文件
│   └── platformio.ini
├── docs/plans/                      # 实施计划文档
├── test-client.js                   # WebSocket 测试客户端
├── start.sh                         # 一键启动脚本
└── README.md
```

## 功能特性

### Electron 控制面板

- ✅ **实时系统监控**
  - CPU 使用率（实时 + 历史图表）
  - 内存使用情况（实时 + 历史图表）
  - 网络速度（上传/下载）
  - 30 秒历史数据可视化

- ✅ **WebSocket 管理**
  - 连接状态显示
  - 自动重连机制
  - 已连接设备列表
  - 设备在线/离线状态

- ✅ **消息日志**
  - 实时消息记录
  - 消息类型分类（info/warning/error/success）
  - 时间戳显示
  - 最近 50 条消息

- ✅ **深色主题**
  - Material-UI 深色主题
  - 现代化界面设计
  - 响应式布局

### ESP32 Web 模拟器

- ✅ **圆形屏幕模拟**
  - 360×360 像素圆形显示
  - 黑色背景，模拟真实硬件
  - 触控交互支持

- ✅ **导航盘**
  - 中心显示实时时间和日期
  - 8 个功能图标环形排列
  - 渐变色按钮设计
  - 悬停和点击动画效果

- ✅ **功能页面**
  - 系统监控页面（圆形进度环）
  - 时钟页面（大字体显示）
  - 页面切换动画
  - 返回导航盘功能

- ✅ **实时数据**
  - 接收真实的系统信息
  - CPU、内存使用率显示
  - 网络速度显示
  - 时间自动更新

## 技术栈

### Electron 端
- **框架**: Electron 40.x + React 18 + TypeScript
- **UI 库**: Material-UI (MUI) v7
- **图表**: Recharts
- **构建工具**: Vite
- **WebSocket**: ws
- **系统信息**: systeminformation, node-os-utils

### ESP32 端
- **平台**: ESP32-S3R8 (双核 240MHz, 8MB PSRAM)
- **框架**: Arduino
- **构建工具**: PlatformIO
- **库**: ArduinoJson, WebSockets, LVGL

## 通信协议

### 握手协议

**ESP32 → Electron:**
```json
{
  "type": "handshake",
  "data": {
    "device_id": "esp32_s3_001",
    "firmware_version": "3.0.0",
    "screen_resolution": "360x360",
    "screen_shape": "circular"
  }
}
```

**Electron → ESP32:**
```json
{
  "type": "handshake_ack",
  "data": {
    "server_version": "3.0.0",
    "update_interval": 1000
  }
}
```

### 系统信息推送

**Electron → ESP32 (每秒):**
```json
{
  "type": "system_info",
  "data": {
    "cpu": { "usage": 45, "temperature": 0 },
    "memory": { "used": 8.0, "total": 16.0, "percentage": 50 },
    "network": { "upload": 1024, "download": 2048 },
    "time": "14:30:25",
    "date": "2026/2/16"
  }
}
```

## 截图预览

### 控制中心
- 实时系统监控卡片
- 性能趋势图表
- WebSocket 连接状态
- 消息日志面板

### 设备模拟器
- 360×360 圆形屏幕
- 导航盘（8 个功能图标）
- 系统监控页面（圆形进度环）
- 时钟页面

## 开发计划

详细的实施计划请查看：`docs/plans/2026-02-15-esp32-desktop-assistant.md`

**已完成：**
- ✅ 阶段 1-2: 项目基础搭建和系统信息采集
- ✅ Electron 控制面板开发
- ✅ ESP32 Web 模拟器开发

**下一步：**
- 🔄 阶段 3: LVGL UI 开发（真实硬件）
- 🔄 阶段 4: 电子相框功能
- 🔄 阶段 5: 天气功能
- 🔄 阶段 6: 番茄钟功能
- 🔄 阶段 7: 语音控制
- 🔄 阶段 8: 本地 AI 集成

## 故障排查

### Electron 应用无法启动
```bash
cd electron-app
rm -rf node_modules package-lock.json
npm install
npm start
```

### WebSocket 连接失败
- 检查防火墙是否允许端口 8765
- 确认 Electron 应用正在运行
- 检查控制台是否有错误信息

### 模拟器显示异常
- 刷新页面（Cmd+R）
- 检查浏览器控制台错误
- 确保 WebSocket 已连接

### 系统信息不更新
- 检查 WebSocket 连接状态
- 查看消息日志是否有错误
- 重启 Electron 应用

## Git 提交记录

```bash
fe6fa14 feat: 实现 Electron 控制面板和 ESP32 Web 模拟器
d380ee6 docs: 添加测试客户端和项目文档
af05a24 feat: 完成阶段 1-2 - 项目基础搭建和系统信息采集
```

## 开发工具

- **VS Code** + PlatformIO IDE 扩展
- **Node.js** 18+
- **Python** 3.x（用于 PlatformIO）
- **Chrome DevTools**（调试 Electron 渲染进程）

## 许可证

MIT

## 贡献者

- 项目设计和实施计划
- 使用 Superpowers 工作流进行开发
- Subagent-Driven 开发模式

---

**当前版本**: v0.2.0 (Electron 界面完成)
**最后更新**: 2026-02-16

## 使用提示

1. **首次运行**：使用 `./start.sh` 一键启动
2. **查看模拟器**：点击"设备模拟器"标签页
3. **测试通信**：运行 `node test-client.js` 模拟 ESP32 设备
4. **查看日志**：在控制中心的消息日志面板查看所有事件
5. **监控系统**：实时图表显示系统性能趋势

**提示**：模拟器中的系统监控页面会显示真实的系统数据，你可以在导航盘点击"📊 系统监控"图标查看效果！
