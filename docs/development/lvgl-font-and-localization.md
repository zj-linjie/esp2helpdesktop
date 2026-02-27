# LVGL 字体与本地化指南

## 概述

ESP32固件使用LVGL图形库进行UI渲染。LVGL默认字体仅支持ASCII字符和部分西欧字符，不支持中文、emoji等特殊字符。本文档记录了在开发过程中遇到的字体问题及解决方案。

## 字体限制

### 可用字体

ESP32固件中可用的LVGL字体：
- `lv_font_montserrat_14` - 默认字体
- `lv_font_montserrat_16`
- `lv_font_montserrat_22`
- `lv_font_montserrat_32`

**注意：** 不存在18号、48号等其他尺寸的字体。

### 字符支持范围

默认Montserrat字体支持：
- ✅ ASCII字符（a-z, A-Z, 0-9）
- ✅ 基本标点符号
- ✅ 常用符号（°, %, -, +, 等）
- ❌ 中文字符
- ❌ Emoji表情（☀️🌧️❄️等）
- ❌ 特殊Unicode字符

## 常见问题与解决方案

### 问题1：中文字符显示为方块

**现象：**
```cpp
lv_label_set_text(label, "雾");  // 显示为 □
```

**原因：** LVGL默认字体不包含中文字符。

**解决方案：** 将中文转换为英文

```cpp
static const char* translateWeatherCondition(const char* condition) {
  if (strstr(condition, "雾")) return "Foggy";
  if (strstr(condition, "晴")) return "Sunny";
  if (strstr(condition, "多云")) return "Cloudy";
  if (strstr(condition, "阴")) return "Overcast";
  if (strstr(condition, "雨")) return "Rainy";
  if (strstr(condition, "雪")) return "Snowy";
  if (strstr(condition, "霾")) return "Haze";
  if (strstr(condition, "雷")) return "Thunder";
  if (strstr(condition, "风")) return "Windy";
  return condition;
}

// 使用
lv_label_set_text(label, translateWeatherCondition("雾"));  // 显示 "Foggy"
```

### 问题2：Emoji图标显示为方块

**现象：**
```cpp
lv_label_set_text(label, "☀️");  // 显示为 □
lv_label_set_text(label, "🌧️");  // 显示为 □
```

**原因：** LVGL默认字体不包含Emoji字符。

**解决方案：** 使用文字替代或自定义图标

```cpp
// 方案1：使用英文文字
lv_label_set_text(label, "Sunny");

// 方案2：使用LVGL符号（如果需要图标）
// 需要使用LVGL内置符号或自定义图片
```

### 问题3：浮点数温度显示异常

**现象：**
```cpp
lv_label_set_text_fmt(label, "%.1f°C", 2.5);  // 可能显示为 "f°C" 或乱码
```

**原因：** 某些情况下浮点数格式化可能导致字体渲染问题。

**解决方案：** 使用整数格式

```cpp
// 推荐：使用整数
lv_label_set_text_fmt(label, "%d°C", (int)temperature);  // 显示 "2°C"

// 或者分离数字和单位
lv_label_set_text_fmt(tempLabel, "%d", (int)temperature);
lv_label_set_text(unitLabel, "°C");
```

## 最佳实践

### 1. 文本显示原则

- ✅ 优先使用英文文本
- ✅ 使用整数格式化（%d）而非浮点数（%f）
- ✅ 将数字和单位分离显示（更灵活的布局）
- ❌ 避免使用中文字符
- ❌ 避免使用Emoji表情
- ❌ 避免使用特殊Unicode字符

### 2. 天气功能示例

```cpp
// 天气数据结构
struct WeatherData {
  float temperature;
  char condition[32];  // 可能包含中文
};

// 显示时转换
static void updateWeatherDisplay() {
  // 温度：使用整数 + 分离的单位标签
  lv_label_set_text_fmt(tempLabel, "%d", (int)weather.temperature);
  lv_label_set_text(unitLabel, "°C");

  // 天气状况：转换中文为英文
  lv_label_set_text(conditionLabel, translateWeatherCondition(weather.condition));
}
```

### 3. 多语言支持建议

如果未来需要支持中文显示，有以下选项：

**选项A：添加中文字体（不推荐）**
- 需要生成包含中文字符的LVGL字体文件
- 会显著增加固件大小（每个字体+1-2MB）
- 编译时间增加
- 内存占用增加

**选项B：使用图片（推荐用于图标）**
- 将常用图标转换为图片资源
- 使用`lv_img_create()`显示
- 适合天气图标、状态图标等

**选项C：保持英文（当前方案，推荐）**
- 固件体积小
- 性能最优
- 国际化友好

## 调试技巧

### 检查字符是否支持

如果不确定某个字符是否被字体支持，可以：

1. 先在模拟器中测试（Electron应用使用系统字体，支持所有字符）
2. 在ESP32上测试，观察是否显示为方块
3. 如果显示为方块，说明字体不支持该字符

### 常见方块字符原因

- □ 单个方块 = 单个不支持的字符
- □□ 两个方块 = Emoji（通常占用2个字符位置）
- 完全不显示 = 字符串格式化错误

## 相关文件

- `esp32-firmware/src/main.cpp` - 主要UI代码
- `esp32-firmware/src/lv_conf.h` - LVGL配置文件
- `electron-app/src/renderer/components/simulator/` - 模拟器UI（支持完整字符集）

## 参考资源

- [LVGL字体文档](https://docs.lvgl.io/master/overview/font.html)
- [LVGL在线字体转换器](https://lvgl.io/tools/fontconverter)
- 和风天气API文档（返回中文天气状况）

## 更新日志

- 2026-02-27: 初始版本，记录天气功能开发中的字体问题和解决方案
