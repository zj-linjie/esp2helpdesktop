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

## 6. 当时未解决问题（已在第 11 节闭环）

当时问题：  
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

---

## 10. 2026-02-28 二次修复（App Launcher 可用性 + 启动反馈链路）

本轮针对用户提出的 5 个问题完成了落地修改：

1. 应用名称可见性  
- 列表项改为固定“图标 + 名称”行布局
- 名称颜色强制为亮色，非仅首字母图标
- 长名称使用 `LV_LABEL_LONG_DOT` 防止溢出

2. 翻页按钮可点性  
- `Prev/Next` 从边角移到底部中间安全区导航条
- 按钮尺寸加大，点击热区变大
- 支持根据页码自动禁用不可点击按钮（透明度降低）

3. 未连接时阻断假启动  
- 点击应用前先校验 `isConnected`
- 断连时页面状态栏显示 `WS disconnected`，并写入 Inbox 告警

4. 后端返回详细失败原因  
- `launch_app` 改用 `execFile('open', [path])`，避免 shell 引号问题
- 增加路径校验（必须是 `/Applications/*.app`）
- 增加文件存在性校验
- 失败响应返回 `reason`（截断至 180 字符）

5. 启动响应双通道展示  
- 固件新增 `launch_app_response` 专门处理分支
- 成功/失败同时更新：
  - App Launcher 状态栏（绿色成功、红色失败）
  - Inbox 消息（event/alert）

本轮改动文件：

- `esp32-firmware/src/main.cpp`
- `electron-app/src/main/websocket.ts`

本轮验证：

- Electron 构建通过：`npm run build`
- 固件构建通过：`platformio run -e esp32-s3-devkit`
- 固件已烧录到设备：`platformio run -t upload --upload-port /dev/cu.usbmodem21101`

备注：

- 串口监视阶段未抓到可读业务日志（设备持续输出点状字符），建议接手同事在下轮调试时配合 Electron 控制台日志做端到端确认。

---

## 11. 失败经验复盘（本轮已闭环）

本轮“点击有 Launching 但不打开 / 左右滑动疑似假死”的根因不是单点，而是多因素叠加：

1. WiFi 配置误刷为占位值  
- 现象：设备看起来卡住、页面交互异常，串口持续异常输出。
- 根因：`esp32-firmware/src/config.h` 被模板值覆盖，设备无法稳定连到后端。
- 处理：恢复本地 WiFi 与 WS 地址后重新烧录。

2. App Launcher 容器吞手势  
- 现象：在第 8 页左右滑动不灵，像“假死”。
- 根因：列表容器与按钮区域对触摸事件拦截，页面级手势没有稳定透传。
- 处理：关闭列表滚动、开启 gesture bubble，并对关键容器绑定手势回调。

3. 启动链路错误信息不透明  
- 现象：只有“Failed to launch app”，无法判断失败点。
- 根因：后端未返回详细失败原因。
- 处理：后端新增路径校验/文件存在性校验，返回 `reason` 字段；固件页面 + Inbox 同步展示。

4. 并行构建导致伪失败  
- 现象：一度出现 `undefined reference to setup/loop` 与 `firmware.elf not found`。
- 根因：同一环境并行启动多个 PlatformIO 任务，构建产物竞争。
- 处理：统一改为串行流程：`run` 后再 `run -t upload`。

当前状态：  
- 第 8 页应用名称显示正常  
- 翻页按钮命中率已提升  
- Launch 成功/失败路径均有可读反馈  
- 设备联机后左右滑动恢复正常（用户已口头确认“好了成功了”）

---

## 12. Electron 端“自定义 Launch 配置”现状

结论：已有基础代码，不是从零开始。

已有能力：

1. 主进程 IPC  
- `electron-app/src/main/index.ts` 已有：
  - `launch-app`
  - `scan-applications`

2. 渲染层配置服务  
- `electron-app/src/renderer/services/appLauncherService.ts` 已支持：
  - 本地持久化（`app_launcher_settings`）
  - 扫描应用、添加/删除应用、启动应用

3. 设置界面  
- `electron-app/src/renderer/components/SettingsPanel.tsx` 已有“应用启动器管理”卡片：
  - 扫描 `/Applications`
  - 已添加应用列表
  - 可添加应用列表 + 搜索 + 删除

与当前 ESP32 第 8 页关系：

- 现在 ESP32 从 WebSocket 端拿到的是后端临时扫描的 top 12。
- 若要“后台自定义后再同步到硬件”，下一步应把 SettingsPanel 里维护的 app 列表接入 `websocket.ts` 的 `app_list_request` 响应源（而不是每次临时扫描）。

---

## 13. 2026-02-28 主线推进：自定义应用列表已接入 ESP32

本轮已完成“Electron 后台自定义配置 -> ESP32 App Launcher”闭环：

1. 主进程新增持久化  
- 文件：`~/Library/Application Support/electron-app/app-launcher-settings.json`
- 启动时自动加载并注入到 WebSocket 服务。

2. WebSocket `app_list_request` 改造  
- 优先返回主进程的自定义应用列表（最多 12 项）。
- 仅当自定义列表为空时，才退回扫描 `/Applications`。

3. IPC 同步接口  
- `app-launcher-sync-settings`：渲染层保存后同步到主进程并落盘
- `app-launcher-get-settings`：渲染层可从主进程拉取配置

4. 渲染层服务接入  
- `appLauncherService.saveSettings()` 自动触发 IPC 同步
- 设置页加载时优先尝试从主进程读取，避免前后端配置漂移

联调结果（已验证）：

- 写入 2 条自定义应用（Safari/Calendar）后，设备请求 `app_list_request` 返回即为这 2 条；
- 后端日志出现：`[应用列表] 使用自定义配置 2 项`。

---

## 14. 2026-02-28 SD 卡实机检测（关键里程碑）

本轮已在真实硬件上完成“SD 最小可用验证”并烧录：

1. 固件能力新增（`esp32-firmware/src/main.cpp`）
- 引入 `SD_MMC` 挂载流程（优先 4-bit，失败自动回退 1-bit）
- 启动阶段执行 SD 挂载与根目录扫描
- 采集并展示：卡类型、总容量、已用容量、根目录目录/文件计数、根目录预览
- 在 Settings & Diagnostics 页新增两行：
  - `SD: ...`
  - `Root: ...`
- 在 Inbox 注入启动结果事件（`SD mounted` / `SD not mounted`）

2. 现场验证结果（串口日志）
- `"[SD] mounted mode=4-bit type=SDSC used=400.1MB total=476.0MB dirs=10 files=0 root=System Volume Info..., aida64, clockbg"`
- 结论：当前插卡状态稳定可用，SD 读目录能力已打通。

3. 对主线开发的影响结论
- 当前主线（监控/时钟/WS/App Launcher）不依赖 SD，插卡不会阻塞继续开发。
- 相框/本地媒体功能从现在开始可基于 SD 状态做真实实现，不再停留在“仅模拟数据”阶段。

4. 风险与注意事项
- 当前板级映射里 SD CLK 使用 `GPIO3`（strapping pin），对“冷启动稳定性”存在潜在电气风险。
- 现阶段建议：
  - 保持当前映射继续开发（已验证可用）
  - 增加冷启动回归测试（插卡/不插卡各 20 次）
  - 若后续出现偶发上电异常，再评估切换 1-bit 固定模式或硬件侧上拉/时序优化

5. 对“模拟器资产迁移”的直接建议（下一步）
- 优先迁移原模拟器里的“电子相框页面”到硬件：
  - 原因：该模块与 SD 能力直接耦合，本轮已具备前置条件
  - 顺序建议：目录扫描 -> JPG 解码单图显示 -> 手势翻图 -> 自动轮播
