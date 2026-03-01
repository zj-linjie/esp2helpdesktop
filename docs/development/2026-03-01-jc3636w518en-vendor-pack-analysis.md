# JC3636W518EN 厂家资料包分析与项目对齐

Date: 2026-03-01  
Status: Draft (for handover and implementation planning)

## 1. 目标

快速梳理 `docs/JC3636W518EN` 厂家压缩包中对当前项目最有价值的信息，并给出与现有仓库的对齐情况、可复用内容、风险点。

## 2. 资料包结构总览

路径：`docs/JC3636W518EN`

主要目录：
- `1-Instructions`
- `2-Program Example`
- `3-Specification`
- `4-Chip manual`
- `5-Dimension`
- `6-Schematic diagram`
- `7-User's manual`
- `8-Tool`
- `9-Burn`

关键观察：
- 厂家提供的是基础开发样例和资料，不是完整成品业务源码。
- 对我们最直接有用的是：引脚定义、LCD 初始化表、原理图、规格书、烧录地址与固件镜像。

## 3. 规格书与用户手册可确认信息

来源：
- `3-Specification/JC3636W518 Specifications-EN.pdf`
- `7-User's manual/Getting started JC3636W518.pdf`

确认要点：
- 主控：ESP32-S3（双核，最高 240MHz）
- 内存/存储：512KB SRAM，8MB PSRAM，16MB Flash
- 屏幕：1.8" 360x360，驱动 ST77916
- 触摸：电容触摸 CST816
- 供电：5V（规格书写典型功耗约 180mA）
- 功能宣传项：AIDA64 副屏、MP3、相册、MJPEG、天气、主题时钟、QI 无线供电

用户手册中的开发/使用相关信息：
- AP 配网信息：`My App` / `12345678`，配置页 `192.168.4.1`
- AIDA64 配置说明：默认端口 80，可自定义端口；提供了 `aida_remote_1.85.rslcd`
- Arduino 环境建议：Arduino 3.0.1+，LVGL 8.4.0 及以下，搭配 ESP32_Display_Panel / ESP32_IO_Expander

补充：
- `User precautions.txt` 仅有一句：`As a secondary screen, only the first folder needs to be viewed`

## 4. 程序样例中的硬件映射（最关键）

来源：
- `2-Program Example/Demo_Arduino_V2.0/.../ST77916_LVGL_DEMO/pincfg.h`

引脚映射（厂家样例）：
- LCD QSPI：`CS=10 SCK=9 SDA0=11 SDA1=12 SDA2=13 SDA3=14 RST=47 BLK=15`
- Touch I2C：`SCL=8 SDA=7 INT=41 RST=40`（CST816）
- SD_MMC：`D0=2 D1=1 D2=6 D3=5 CLK=3 CMD=4`
- I2S DAC：`BCK=18 WS=16 DO=17 MUTE=48`（MCK=-1）
- I2S MIC：`WS=45 SD=46`（V2 示例里 `MIC_I2S_SCK 42` 被注释）
- 按键：`BTN_PIN=0`

与当前项目对齐情况（`esp32-firmware/src/display/pincfg.h`）：
- LCD/Touch/SD/DAC 引脚一致。
- 我们额外定义了 `MIC_I2S_SCK=42`（可工作，但和厂家 V2 注释存在差异，需要以实测为准）。
- 我们多了 `ROTARY_ENC_PIN_A/B`（项目自定义扩展，不影响厂家基线）。

## 5. LCD 初始化表与屏幕兼容性信息

来源：
- `2-Program Example/.../Initialization code.txt`
- `2-Program Example/.../ST77916_BOE 1.8'' 360x360 initial code_V1.7_20231208.txt`
- `ST77916_LVGL_DEMO/scr_st77916.h`

价值：
- 包内有不止一版 ST77916 初始化参数表，且内容不同（疑似屏幕批次/面板版本差异）。
- 当前仓库已采用同族初始化流程。若后续出现特定批次显示异常，可优先尝试切换这两版初始化表做 A/B 验证。

## 6. 原理图中能支持我们后续开发的点

来源：`6-Schematic diagram/*.pdf`（5 张单页原理图）

可读到的关键网络：
- LCD 侧：FPC 接口含 `LCD_QSPI_*`、`LCD_RST`、`LCD_TE`、`LCD_BLK`、触摸 `TP_*`
- SD 侧：明确 `SDMMC_*` 六线
- 音频 DAC 侧：PCM5100A（I2S 输入，模拟 L/R 输出）
- MIC 侧：I2S 麦克风链路（WS/SD/SCK 网络可见）
- 供电侧：Type-C、电源降压、QI 接收相关电路

结论：
- 硬件资源与我们现在的功能路线（相册、视频、音频、语音）是匹配的。

## 7. 烧录资料与镜像信息

来源：
- `9-Burn/Burn files/*.bin`
- `9-Burn/flash_download_tool_3.9.3/bin/地址.txt`
- `9-Burn/flash_download_tool_3.9.3/logs/*.txt`

厂家给的固件镜像：
- `1.85_demo.bin` (MD5 `483c2d6eb9fedaf610fcb093e8813c8b`)
- `JC3636W518_2.0firmware.bin` (MD5 `9e4fbfcf458a8c43c3e356c2a43f244a`)

镜像格式检查（esptool image_info）：
- 两者均为 `ESP32-S3` 应用镜像（app image），不是“全量合并镜像”。

烧录地址参考：
- `0x0 bootloader`
- `0x8000 partitions`
- `0xe000 boot_app0`
- `0x10000 app`

日志中可见：
- 常用烧录波特率 `115200`
- flash id 多次记录为 `0x001640ef`

## 8. 对当前仓库的直接意义

已可确认（可放心继续）：
- 我们当前底层驱动方向是对的：ESP32-S3 + ST77916 + CST816 + SD_MMC + I2S。
- 我们当前引脚配置基本与厂家示例一致。
- 媒体能力路线（MP3/MJPEG/SD）与厂家资料一致，不是偏离方向。

建议优先保留并利用：
- 厂家两版 LCD init 表，作为显示异常时的快速回退点。
- `aida_remote_1.85.rslcd`，可用于后续副屏联动的兼容验证。
- 原理图中的音频链路标注，指导 3.5mm 输出与静音控制逻辑优化。

仍需注意的差异/风险：
- 厂家样例并未提供完整成品 UI/交互业务逻辑，需要我们自建上层应用。
- MIC SCK 在厂家 V2 `pincfg.h` 中被注释，说明不同批次或示例版本存在配置差异，语音功能需继续以实机校准。
- 厂家转换工具是 Windows 可执行包（`mjpeg Conversion tool2.0.zip`），无法直接在 macOS 原生运行。

## 9. 可执行后续动作（建议）

1. 将当前项目中的 `scr_st77916` 初始化表与厂家两份初始化表建立切换宏（A/B/C 三档）。
2. 针对语音采集链路补一页“MIC 引脚与采样参数实测矩阵”（含 SCK=42 与无 SCK 两种配置）。
3. 在 docs 增补“厂家资料索引页”，减少后续同事重复翻包。

## 10. 参考路径清单

- `docs/JC3636W518EN/3-Specification/JC3636W518 Specifications-EN.pdf`
- `docs/JC3636W518EN/7-User's manual/Getting started JC3636W518.pdf`
- `docs/JC3636W518EN/2-Program Example/Demo_Arduino_V2.0/Demo_Arduino_V2.0/ST77916_LVGL_DEMO/pincfg.h`
- `docs/JC3636W518EN/2-Program Example/Demo_Arduino_V2.0/Demo_Arduino_V2.0/Initialization code.txt`
- `docs/JC3636W518EN/2-Program Example/Demo_Arduino_V2.0/Demo_Arduino_V2.0/ST77916_BOE 1.8'' 360x360 initial code_V1.7_20231208.txt`
- `docs/JC3636W518EN/6-Schematic diagram/1_P1.pdf`
- `docs/JC3636W518EN/6-Schematic diagram/2_S3R8.pdf`
- `docs/JC3636W518EN/6-Schematic diagram/3_DAC.pdf`
- `docs/JC3636W518EN/6-Schematic diagram/4_LCD.pdf`
- `docs/JC3636W518EN/6-Schematic diagram/5_QI.pdf`
- `docs/JC3636W518EN/9-Burn/Burn files/1.85_demo.bin`
- `docs/JC3636W518EN/9-Burn/Burn files/JC3636W518_2.0firmware.bin`
- `docs/JC3636W518EN/9-Burn/flash_download_tool_3.9.3/bin/地址.txt`
- `docs/JC3636W518EN/1-Instructions/aida_remote_1.85.rslcd`
