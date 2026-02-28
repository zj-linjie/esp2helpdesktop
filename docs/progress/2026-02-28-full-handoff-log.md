# ESP32 项目完整交接记录（截至 2026-02-28）

## 1. 交接目标

这份文档用于给下一位同事接手开发，覆盖：

- 之前已完成的硬件上板与页面迁移记录
- 这次 App Launcher（第 8 页）开发与部署记录
- 当前未解决问题（`Launching...` 出现但 Mac 应用未拉起）
- 可直接复现的命令与排查路径

---

## 2. 项目与环境快照

- 仓库路径：`/Users/apple/dev/esp2helpdesktop`
- 硬件：`JC3636W518C`（ESP32-S3，360x360 圆屏）
- 常用串口：`/dev/cu.usbmodem21101`
- 后端端口：`8765`
- 当前分支：`main`（相对 `new-origin/main` 领先）

当前本地未提交改动（本次交接时）：

- `electron-app/src/main/websocket.ts`
- `esp32-firmware/src/main.cpp`
- `esp32-firmware/src/config.h`（含本地 WiFi/WS 地址）

---

## 3. 历史开发时间线（之前记录）

## 3.1 2026-02-27 硬件上板与基础联通

参考文档：

- `docs/progress/2026-02-27-jc3636w518c-bringup.md`

关键结论：

- 黑屏根因是板型引脚映射错误（`K518` -> `W518C` 后恢复）
- 屏幕、触摸、WiFi、WebSocket 均联通
- 已完成预装固件备份（`backups/esp32_preinstall_20260227_175918/`）

## 3.2 2026-02-27 页面迁移主线（提交记录）

1. `2d3cd1e`  
   bringup + 手势翻页/长按回主页
2. `c73f059`  
   系统监控页 + NTP 时钟页
3. `524c536`  
   设置/诊断页 + 触摸交互修正
4. `4c43e25`  
   Inbox & Tasks 页面 + 多类消息解析
5. `368bca6`  
   后端增加 `task_action` 处理
6. `f5051b4`  
   番茄钟页面
7. `556035a` ~ `ec75da6`  
   天气页接入、重构、中文条件转英文、显示优化
8. `7a12e18`  
   LVGL 字体与本地化文档

---

## 4. 本次开发记录（App Launcher 第 8 页）

这部分来自本次连续开发/调试过程。

## 4.1 需求与现象

- 新增第 8 页 `App Launcher`
- ESP32 可显示应用列表并可点击
- 用户反馈：
  - 早期仅首字母图标，识别性差
  - 后续出现 `Launching...` 提示，但 Mac 端应用未成功打开

## 4.2 后端改动（Electron）

文件：`electron-app/src/main/websocket.ts`

本次改动点：

1. ESM 兼容修复  
   把 `require(...)` 改为顶部 `import`：
   - `import { exec } from 'child_process'`
   - `import { promisify } from 'util'`
   - `const execAsync = promisify(exec)`

2. 新增消息处理入口  
   在 `handleMessage()` 里新增：
   - `app_list_request` -> `handleAppListRequest`
   - `launch_app` -> `handleLaunchApp`

3. 应用列表处理  
   `handleAppListRequest()`：
   - 扫描 `/Applications/*.app`
   - 返回最多 12 个应用（`app_list`）
   - 日志输出找到的应用列表

4. 启动应用处理  
   `handleLaunchApp()`：
   - 执行 `open "<appPath>"`
   - 返回 `launch_app_response`
   - 广播 `app_launched` 到控制面板

## 4.3 固件改动（ESP32）

文件：`esp32-firmware/src/main.cpp`

本次改动点：

1. 页面扩容  
   - 新增 `UI_PAGE_APP_LAUNCHER = 7`
   - 页面总数改为 `UI_PAGE_COUNT = 8`

2. App Launcher 数据结构  
   - 新增 `MacApp`（name/path/letter/color）
   - `appList[12]`、分页状态、每页 4 项

3. 通信逻辑  
   - 新增 `requestAppList()` 发送 `app_list_request`
   - 新增 `launchApp()` 发送 `launch_app`
   - WebSocket 收到 `app_list` 后渲染列表
   - WS 连上后自动请求 app 列表

4. UI 与交互  
   - 圆形彩色图标 + 首字母
   - 文本字体增大为 `lv_font_montserrat_16`
   - 长名称滚动显示 `LV_LABEL_LONG_SCROLL_CIRCULAR`
   - `Prev/Next` 分页按钮
   - 点击后显示绿色 `Launching <name>...` 状态标签（2 秒隐藏）

## 4.4 编译与部署记录（本次）

后端构建：

```bash
cd /Users/apple/dev/esp2helpdesktop/electron-app
npm run build
npm run start
```

固件构建与烧录：

```bash
cd /Users/apple/dev/esp2helpdesktop/esp32-firmware
.venv-pio312/bin/platformio run --target upload
```

结果：编译和烧录成功（会话记录中确认 upload 完成）。

---

## 5. 当前功能状态（截至本次交接）

页面状态：

1. `1/8` Home：基础状态展示
2. `2/8` System Monitor：CPU/MEM/网络
3. `3/8` Clock：NTP 同步时间
4. `4/8` Settings & Diagnostics：诊断动作 + 亮度
5. `5/8` Inbox & Tasks：任务消息浏览与动作回传
6. `6/8` Pomodoro：番茄钟
7. `7/8` Weather：天气数据显示
8. `8/8` App Launcher：应用列表、分页、点击启动反馈

---

## 6. 已知未解决问题（重点）

问题：  
在第 8 页点击应用后，屏幕会出现 `Launching ...`，但 Mac 侧应用没有被拉起。

已确认事实：

- `app_list_request` 通道可用（能拿到应用列表并渲染）
- 点击事件已触发（有 `Launching ...` UI 反馈）
- `launch_app` 逻辑已实现，但实际拉起仍失败（用户实测）

可能原因（优先排查顺序）：

1. 后端未在正确进程/目录运行，导致请求到达但 `open` 执行环境异常
2. `handleLaunchApp()` 报错被 catch，但错误信息未被完整追踪到接手文档
3. `appPath` 在极端情况下被截断或非法（固件端 `strncpy` + 长路径）
4. `open "<path>"` 在当前 Electron 运行上下文下被系统拒绝（权限/会话问题）

---

## 7. 建议下一位同事第一轮动作

1. 在 `handleLaunchApp()` 增强日志，打印：
   - `message.data` 原始内容
   - `appPath` 长度
   - `open` 的 stderr/stdout
2. 在失败响应里回传详细错误字符串到 ESP32（现在仅 `Failed to launch app`）
3. 在固件端收到 `launch_app_response` 时，把失败原因完整显示到 Inbox 页
4. 增加路径保护：
   - `strncpy` 后显式补 `\0`
   - 后端对 `appPath` 做白名单校验（必须在 `/Applications/` 下）
5. 用已知短路径应用先做最小验证（例如 `Safari.app`）

---

## 8. 接手时建议执行的验证命令

## 8.1 启动后端

```bash
cd /Users/apple/dev/esp2helpdesktop/electron-app
npm run build
npm run start
```

## 8.2 烧录固件

```bash
cd /Users/apple/dev/esp2helpdesktop/esp32-firmware
./.venv-pio312/bin/python -m platformio run -e esp32-s3-devkit -t upload --upload-port /dev/cu.usbmodem21101
```

## 8.3 串口观察

```bash
cd /Users/apple/dev/esp2helpdesktop/esp32-firmware
./.venv-pio312/bin/python -m platformio device monitor --port /dev/cu.usbmodem21101 --baud 115200
```

## 8.4 快速 USB 识别

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
ioreg -p IOUSB -w0 -l | rg -i "esp|usb jtag|serial"
```

---

## 9. 与旧文档的关系

- 上板与黑屏修复细节：`docs/progress/2026-02-27-jc3636w518c-bringup.md`
- 第一版接力文档：`docs/progress/2026-02-27-esp32-handoff.md`
- 本文档：在上述基础上补齐 2/28 新增开发记录与 App Launcher 问题状态，供当前同事直接接手。

