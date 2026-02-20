# 时钟页面增强设计文档

**日期**: 2026-02-20
**状态**: 已批准
**作者**: Claude Sonnet 4.5

---

## 概述

增强 ESP32 Help Desktop 的时钟页面，添加多种表盘样式、计时器/倒计时功能，以及酷炫的动画效果。适配 360x360 圆形屏幕。

## 需求

1. 添加酷炫的动画效果
2. 增加计时器和倒计时功能
3. 实现 4 种表盘设计（极简、指针、运动、像素）
4. 适配 360x360 圆形屏幕
5. 支持左右滑动切换表盘
6. 保持长按返回功能

## 整体架构

### 组件结构

```
ClockPage (主容器)
├── WatchFaceContainer (表盘容器 - 处理滑动切换)
│   ├── MinimalistFace (极简数字表盘)
│   ├── AnalogFace (模拟指针表盘)
│   ├── SportFace (运动风格表盘)
│   └── PixelFace (像素风格表盘)
├── ModeSelector (底部模式切换器)
│   ├── ClockMode (时钟模式)
│   ├── StopwatchMode (计时器模式)
│   └── TimerMode (倒计时模式)
└── LongPressHandler (长按返回处理)
```

### 状态管理

```typescript
interface ClockPageState {
  currentFaceIndex: number;        // 当前表盘索引 (0-3)
  currentMode: 'clock' | 'stopwatch' | 'timer';  // 当前模式
  dragOffset: number;               // 拖拽偏移量
  isDragging: boolean;              // 是否正在拖拽
  stopwatchTime: number;            // 计时器时间（毫秒）
  stopwatchRunning: boolean;        // 计时器是否运行
  timerRemaining: number;           // 倒计时剩余时间（秒）
  timerRunning: boolean;            // 倒计时是否运行
  timerDuration: number;            // 倒计时总时长（秒）
}
```

### 滑动切换机制

复用 PhotoFramePage 的滑动逻辑：
- 水平拖拽检测（阈值 80px）
- 拖拽预览（显示前/后表盘）
- 平滑过渡动画（0.3s ease）
- 长按返回（800ms）与滑动互不冲突

---

## 四个表盘设计

### 1. 极简数字表盘 (MinimalistFace)

**视觉风格:**
- 纯白色背景
- 黑色超大数字（使用 SF Pro Display 或 Roboto，极细字重）
- 时间下方显示日期和星期（灰色小字）
- 极简主义设计，无多余装饰

**动画效果:**
- **数字翻页动画**: 每秒/分/时变化时，数字从下往上翻转（类似机场显示屏）
- **切换动画**: 淡入淡出 + 轻微缩放（0.95 → 1.0）
- **呼吸效果**: 整个表盘有微弱的透明度变化（0.95 ↔ 1.0，周期 3秒）

**实现细节:**
```typescript
// 数字翻页动画
@keyframes flipNumber {
  0% { transform: translateY(100%); opacity: 0; }
  50% { transform: translateY(-10%); }
  100% { transform: translateY(0); opacity: 1; }
}
```

### 2. 模拟指针表盘 (AnalogFace)

**视觉风格:**
- 深色背景（深蓝或黑色）
- 金色/白色指针（时针粗、分针中、秒针细）
- 12 个刻度点（3/6/9/12 位置加粗）
- 中心有装饰性圆点
- 外圈有细圆环边框

**动画效果:**
- **指针平滑旋转**: 使用 CSS transform rotate，每秒更新
- **秒针弹跳**: 秒针每跳一格有轻微回弹效果（elastic easing）
- **切换动画**: 3D 翻转效果（rotateY 180deg）
- **刻度发光**: 当前时间对应的刻度会发光高亮

**实现细节:**
```typescript
// 计算指针角度
const secondAngle = (time.getSeconds() / 60) * 360;
const minuteAngle = (time.getMinutes() / 60) * 360 + (time.getSeconds() / 60) * 6;
const hourAngle = ((time.getHours() % 12) / 12) * 360 + (time.getMinutes() / 60) * 30;

// 秒针弹跳效果
transition: 'transform 0.5s cubic-bezier(0.68, -0.55, 0.265, 1.55)'
```

### 3. 运动风格表盘 (SportFace)

**视觉风格:**
- 黑色背景
- 橙色主题色
- 中心显示时间（大号数字）
- 外圈有 3 个圆环进度条（模拟活动/运动/站立数据）
  - 外圈：红色（活动）
  - 中圈：绿色（运动）
  - 内圈：蓝色（站立）
- 底部显示模拟的心率、步数

**动画效果:**
- **圆环动画**: 圆环从 0% 动画到当前进度（使用 stroke-dashoffset）
- **数据跳动**: 心率数字每秒轻微跳动（scale 1.0 → 1.1 → 1.0）
- **切换动画**: 卡片式滑动 + 圆环重新绘制
- **脉冲效果**: 心率图标有脉冲动画

**实现细节:**
```typescript
// SVG 圆环动画
const circumference = 2 * Math.PI * radius;
const offset = circumference - (progress / 100) * circumference;

<circle
  strokeDasharray={circumference}
  strokeDashoffset={offset}
  style={{ transition: 'stroke-dashoffset 1s ease' }}
/>

// 模拟数据（每小时变化）
const activityProgress = (time.getHours() / 24) * 100;
const exerciseProgress = Math.random() * 60 + 20;
const standProgress = Math.random() * 80 + 10;
```

### 4. 像素风格表盘 (PixelFace)

**视觉风格:**
- 深蓝色背景（#0a1929）
- 绿色像素字体（#00ff41）
- 使用 Press Start 2P 或类似像素字体
- 复古游戏机风格边框
- 8-bit 风格装饰元素

**动画效果:**
- **像素闪烁**: 数字变化时有像素闪烁效果（opacity 快速变化）
- **扫描线**: 背景有从上到下移动的扫描线效果
- **切换动画**: 像素溶解效果（grid fade out/in）
- **边框呼吸**: 边框颜色周期性变化

**实现细节:**
```typescript
// 像素闪烁
@keyframes pixelFlicker {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.8; }
}

// 扫描线
@keyframes scanline {
  0% { transform: translateY(-100%); }
  100% { transform: translateY(100%); }
}

// 边框呼吸
@keyframes borderPulse {
  0%, 100% { borderColor: '#00ff41'; }
  50% { borderColor: '#00aa2b'; }
}
```

---

## 模式切换与功能

### 底部模式切换器 (ModeSelector)

**UI 设计:**
- 位置: 距离底部 20px，水平居中
- 样式: 半透明黑色背景（rgba(0,0,0,0.6)）
- 毛玻璃效果: backdrop-filter: blur(10px)
- 三个按钮: 时钟 | 计时器 | 倒计时
- 当前模式高亮（橙色/蓝色边框 + 图标颜色变化）
- 圆角 20px，padding 8px
- 按钮间距 12px

**交互:**
- 点击按钮切换模式
- 切换时有轻微震动反馈（如果支持 Vibration API）
- 按钮 hover 时放大 1.1 倍
- 使用 `e.stopPropagation()` 防止触发表盘滑动

**图标:**
- 时钟: AccessTime
- 计时器: Timer
- 倒计时: HourglassEmpty

### 时钟模式 (ClockMode)

**功能:**
- 显示当前时间
- 根据不同表盘显示不同样式
- 自动更新（每秒）

**实现:**
```typescript
useEffect(() => {
  const timer = setInterval(() => {
    setTime(new Date());
  }, 1000);
  return () => clearInterval(timer);
}, []);
```

### 计时器模式 (StopwatchMode)

**功能:**
- 显示计时时间（00:00:00.00 格式）
- 开始/暂停按钮（中心位置，大按钮）
- 重置按钮（右下角小按钮）
- 支持毫秒显示

**UI 变化:**
- 时间数字变大，居中显示
- 底部模式切换器上方增加控制按钮
- 运动表盘: 圆环变成计时进度（每圈 1 分钟）

**动画:**
- 开始时: 数字从灰色变为高亮色（绿色）
- 运行中: 毫秒数字快速滚动
- 暂停时: 轻微抖动效果

**实现:**
```typescript
// 使用 requestAnimationFrame 获得更精确的计时
const useStopwatch = () => {
  const [time, setTime] = useState(0);
  const [isRunning, setIsRunning] = useState(false);
  const startTimeRef = useRef<number>(0);
  const animationFrameRef = useRef<number>(0);

  const tick = () => {
    setTime(Date.now() - startTimeRef.current);
    animationFrameRef.current = requestAnimationFrame(tick);
  };

  const start = () => {
    startTimeRef.current = Date.now() - time;
    setIsRunning(true);
    animationFrameRef.current = requestAnimationFrame(tick);
  };

  const pause = () => {
    setIsRunning(false);
    cancelAnimationFrame(animationFrameRef.current);
  };

  const reset = () => {
    setTime(0);
    setIsRunning(false);
    cancelAnimationFrame(animationFrameRef.current);
  };

  return { time, isRunning, start, pause, reset };
};
```

### 倒计时模式 (TimerMode)

**功能:**
- 预设时间选择（1分钟、3分钟、5分钟、10分钟、自定义）
- 显示剩余时间
- 开始/暂停/重置按钮
- 倒计时结束时提醒

**UI 变化:**
- 时间数字变红色（最后 10 秒）
- 进度圆环显示剩余时间百分比
- 底部显示预设时间快捷按钮（1' 3' 5' 10'）

**动画:**
- 倒计时运行: 圆环逐渐减少（顺时针）
- 最后 10 秒: 数字闪烁（红色），每秒一次
- 结束时: 全屏闪烁 3 次 + 震动（如果支持）

**实现:**
```typescript
const useTimer = (initialDuration: number) => {
  const [remaining, setRemaining] = useState(initialDuration);
  const [isRunning, setIsRunning] = useState(false);
  const intervalRef = useRef<NodeJS.Timeout | null>(null);

  const start = () => {
    setIsRunning(true);
    intervalRef.current = setInterval(() => {
      setRemaining(prev => {
        if (prev <= 1) {
          // 倒计时结束
          stop();
          onComplete();
          return 0;
        }
        return prev - 1;
      });
    }, 1000);
  };

  const pause = () => {
    setIsRunning(false);
    if (intervalRef.current) clearInterval(intervalRef.current);
  };

  const reset = () => {
    setRemaining(initialDuration);
    setIsRunning(false);
    if (intervalRef.current) clearInterval(intervalRef.current);
  };

  const onComplete = () => {
    // 全屏闪烁
    // 震动提醒
    // 显示"倒计时结束"提示
  };

  return { remaining, isRunning, start, pause, reset };
};
```

---

## 技术实现细节

### 滑动切换实现

**核心代码结构:**
```typescript
const [currentFaceIndex, setCurrentFaceIndex] = useState(0);
const [dragOffset, setDragOffset] = useState(0);
const [isDragging, setIsDragging] = useState(false);
const [touchStart, setTouchStart] = useState<{x: number, y: number} | null>(null);

const watchFaces = [MinimalistFace, AnalogFace, SportFace, PixelFace];

// 鼠标/触摸事件处理（复用 PhotoFramePage 逻辑）
const handleMouseDown = (e: React.MouseEvent) => {
  if ((e.target as HTMLElement).closest('.mode-selector')) return;
  setTouchStart({ x: e.clientX, y: e.clientY });
  // 启动长按计时器
};

const handleMouseMove = (e: React.MouseEvent) => {
  if (!touchStart) return;
  const deltaX = e.clientX - touchStart.x;
  const deltaY = e.clientY - touchStart.y;

  if (Math.abs(deltaX) > Math.abs(deltaY) && Math.abs(deltaX) > 10) {
    setIsDragging(true);
    setDragOffset(deltaX);
    // 清除长按计时器
  }
};

const handleMouseUp = (e: React.MouseEvent) => {
  const threshold = 80;
  if (isDragging && Math.abs(dragOffset) > threshold) {
    if (dragOffset > 0) {
      // 切换到上一个表盘
      setCurrentFaceIndex(prev => (prev - 1 + watchFaces.length) % watchFaces.length);
    } else {
      // 切换到下一个表盘
      setCurrentFaceIndex(prev => (prev + 1) % watchFaces.length);
    }
  }
  // 重置状态
  setDragOffset(0);
  setIsDragging(false);
  setTouchStart(null);
};
```

**渲染逻辑:**
```typescript
// 只渲染当前表盘和相邻表盘（性能优化）
const prevIndex = (currentFaceIndex - 1 + watchFaces.length) % watchFaces.length;
const nextIndex = (currentFaceIndex + 1) % watchFaces.length;

return (
  <Box sx={{ position: 'relative', overflow: 'hidden' }}>
    {/* 前一个表盘 */}
    {dragOffset > 0 && (
      <Box sx={{ position: 'absolute', left: `${-100 + (dragOffset / 360) * 100}%` }}>
        {React.createElement(watchFaces[prevIndex], { mode, time })}
      </Box>
    )}

    {/* 当前表盘 */}
    <Box sx={{
      position: 'absolute',
      left: isDragging ? `${(dragOffset / 360) * 100}%` : '0',
      transition: isDragging ? 'none' : 'left 0.3s ease'
    }}>
      {React.createElement(watchFaces[currentFaceIndex], { mode, time })}
    </Box>

    {/* 后一个表盘 */}
    {dragOffset < 0 && (
      <Box sx={{ position: 'absolute', left: `${100 + (dragOffset / 360) * 100}%` }}>
        {React.createElement(watchFaces[nextIndex], { mode, time })}
      </Box>
    )}
  </Box>
);
```

### 性能优化

1. **组件优化:**
   - 使用 `React.memo` 包裹每个表盘组件
   - 只渲染当前表盘和相邻表盘（前/后）
   - 避免不必要的重新渲染

2. **动画优化:**
   - 使用 CSS transform 和 opacity（GPU 加速）
   - 避免使用 width/height/top/left 等触发 layout 的属性
   - 计时器使用 `requestAnimationFrame` 而不是 `setInterval`

3. **内存优化:**
   - 组件卸载时清理所有定时器和动画帧
   - 使用 useRef 存储不需要触发渲染的值

```typescript
useEffect(() => {
  return () => {
    // 清理定时器
    if (intervalRef.current) clearInterval(intervalRef.current);
    if (animationFrameRef.current) cancelAnimationFrame(animationFrameRef.current);
  };
}, []);
```

---

## 数据流与状态管理

### 数据流图

```
用户交互
    ↓
ClockPage (状态管理)
    ↓
├─→ WatchFaceContainer (处理滑动)
│       ↓
│   当前表盘组件 (接收 mode 和 time 数据)
│       ↓
│   根据 mode 渲染不同内容
│
└─→ ModeSelector (切换模式)
        ↓
    更新 currentMode 状态
        ↓
    表盘根据 mode 调整显示
```

### 状态持久化

使用 localStorage 保存用户偏好：

```typescript
// 保存状态
useEffect(() => {
  localStorage.setItem('clockSettings', JSON.stringify({
    lastWatchFace: currentFaceIndex,
    lastMode: currentMode,
    timerPresets: [60, 180, 300, 600], // 1分钟、3分钟、5分钟、10分钟
  }));
}, [currentFaceIndex, currentMode]);

// 加载状态
useEffect(() => {
  const saved = localStorage.getItem('clockSettings');
  if (saved) {
    const settings = JSON.parse(saved);
    setCurrentFaceIndex(settings.lastWatchFace || 0);
    setCurrentMode(settings.lastMode || 'clock');
  }
}, []);
```

---

## 边界情况处理

### 1. 计时器运行时切换表盘
**场景**: 用户在计时器运行时左右滑动切换表盘

**处理**:
- 计时器状态保持不变（继续运行）
- 所有表盘都能正确显示计时器时间
- 动画效果根据表盘风格调整
- 运动表盘的圆环显示计时进度

### 2. 计时器运行时长按返回
**场景**: 用户在计时器运行时长按屏幕返回主页

**处理方案 A（推荐）**:
- 弹出确认对话框："计时器正在运行，确定返回？"
- 用户确认后返回，计时器停止
- 用户取消则留在当前页面

**处理方案 B**:
- 自动暂停计时器并返回
- 下次进入时恢复暂停状态

**实现**:
```typescript
const handleLongPress = () => {
  if (currentMode !== 'clock' && (stopwatchRunning || timerRunning)) {
    // 显示确认对话框
    setShowConfirmDialog(true);
  } else {
    onBack();
  }
};
```

### 3. 倒计时结束时
**场景**: 倒计时到 0

**处理**:
- 全屏闪烁 3 次（红色，每次 300ms）
- 播放提示音（如果支持 Web Audio API）
- 震动提醒（如果支持 Vibration API）
- 显示"倒计时结束"提示 2 秒
- 自动切换回时钟模式

**实现**:
```typescript
const onTimerComplete = () => {
  // 全屏闪烁
  setFlashing(true);
  let flashCount = 0;
  const flashInterval = setInterval(() => {
    flashCount++;
    if (flashCount >= 6) { // 3次闪烁 = 6次切换
      clearInterval(flashInterval);
      setFlashing(false);
    }
  }, 300);

  // 震动
  if ('vibrate' in navigator) {
    navigator.vibrate([200, 100, 200, 100, 200]);
  }

  // 提示
  setShowCompletionMessage(true);
  setTimeout(() => {
    setShowCompletionMessage(false);
    setCurrentMode('clock');
  }, 2000);
};
```

### 4. 滑动冲突处理
**场景**: 用户在模式切换器上滑动

**处理**:
- 检测触摸起始位置
- 如果在模式切换器区域，不触发表盘切换
- 使用 `e.stopPropagation()` 阻止事件冒泡

**实现**:
```typescript
const handleMouseDown = (e: React.MouseEvent) => {
  // 检查是否点击在模式切换器上
  if ((e.target as HTMLElement).closest('.mode-selector')) {
    return; // 不处理滑动
  }
  // 正常处理滑动逻辑
};
```

### 5. 性能降级
**场景**: 在低性能设备上运行

**处理**:
- 检测帧率（使用 requestAnimationFrame）
- 如果平均帧率 < 30fps，自动禁用部分动画
- 保留核心功能，移除装饰性动画

**实现**:
```typescript
const [performanceMode, setPerformanceMode] = useState<'high' | 'low'>('high');

useEffect(() => {
  let frameCount = 0;
  let lastTime = performance.now();

  const checkPerformance = () => {
    frameCount++;
    const currentTime = performance.now();

    if (currentTime - lastTime >= 1000) {
      const fps = frameCount;
      frameCount = 0;
      lastTime = currentTime;

      if (fps < 30) {
        setPerformanceMode('low');
      }
    }

    requestAnimationFrame(checkPerformance);
  };

  const rafId = requestAnimationFrame(checkPerformance);
  return () => cancelAnimationFrame(rafId);
}, []);
```

---

## 文件结构

```
electron-app/src/renderer/components/simulator/
├── ClockPage.tsx                    # 主组件（重构）
├── clock/
│   ├── WatchFaceContainer.tsx       # 表盘容器（处理滑动）
│   ├── faces/
│   │   ├── MinimalistFace.tsx       # 极简数字表盘
│   │   ├── AnalogFace.tsx           # 模拟指针表盘
│   │   ├── SportFace.tsx            # 运动风格表盘
│   │   └── PixelFace.tsx            # 像素风格表盘
│   ├── ModeSelector.tsx             # 模式切换器
│   ├── modes/
│   │   ├── StopwatchControls.tsx    # 计时器控制按钮
│   │   └── TimerControls.tsx        # 倒计时控制按钮
│   └── animations/
│       ├── flipNumber.ts            # 数字翻页动画
│       ├── analogClock.ts           # 指针时钟动画
│       ├── circleProgress.ts        # 圆环进度动画
│       └── pixelEffects.ts          # 像素效果动画
└── hooks/
    ├── useStopwatch.ts              # 计时器逻辑
    ├── useTimer.ts                  # 倒计时逻辑
    └── useSwipeGesture.ts           # 滑动手势逻辑
```

---

## 实现计划

### Phase 1: 基础架构（2-3小时）
1. 重构 ClockPage.tsx，建立新的组件结构
2. 实现 WatchFaceContainer 和滑动切换逻辑
3. 创建 ModeSelector 组件
4. 实现 useStopwatch 和 useTimer hooks

### Phase 2: 表盘实现（4-5小时）
1. 实现 MinimalistFace（数字翻页动画）
2. 实现 AnalogFace（指针旋转动画）
3. 实现 SportFace（圆环进度动画）
4. 实现 PixelFace（像素效果动画）

### Phase 3: 模式功能（2-3小时）
1. 实现计时器控制（开始/暂停/重置）
2. 实现倒计时控制（预设时间选择）
3. 实现模式切换逻辑
4. 添加状态持久化

### Phase 4: 优化与测试（1-2小时）
1. 性能优化（React.memo、动画优化）
2. 边界情况处理
3. 测试所有交互场景
4. 调整动画细节

**总计**: 约 9-13 小时

---

## 成功标准

✅ 4 个表盘都能正常显示和切换
✅ 左右滑动切换表盘流畅（< 300ms）
✅ 每个表盘都有独特的动画效果
✅ 计时器和倒计时功能正常工作
✅ 模式切换器正常工作
✅ 长按返回功能保持正常
✅ 所有边界情况都有合理处理
✅ 性能良好（帧率 > 30fps）
✅ 状态持久化正常工作

---

## 未来扩展

- 更多表盘样式（天气表盘、日历表盘等）
- 自定义表盘颜色/主题
- 表盘商店（下载社区表盘）
- 闹钟功能
- 世界时钟（多时区）
- 表盘编辑器

---

**文档版本**: v1.0
**最后更新**: 2026-02-20
**状态**: 已批准，准备实现
