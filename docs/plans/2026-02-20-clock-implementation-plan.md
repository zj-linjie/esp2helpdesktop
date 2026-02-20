# 时钟页面增强 - 实现计划

**日期**: 2026-02-20
**参考设计**: [2026-02-20-clock-enhancement-design.md](./2026-02-20-clock-enhancement-design.md)

---

## 实现策略

采用**增量开发**策略，每个步骤都可以独立测试和验证：
1. 先搭建基础架构（容器、状态管理）
2. 实现一个简单表盘验证滑动切换
3. 逐个添加其他表盘
4. 添加模式切换功能
5. 优化和完善

---

## Phase 1: 基础架构搭建

### Step 1.1: 创建 hooks 和工具函数
**文件**: `src/renderer/hooks/useSwipeGesture.ts`

**任务**:
- 创建 `useSwipeGesture` hook，封装滑动手势逻辑
- 复用 PhotoFramePage 的滑动检测代码
- 支持鼠标和触摸事件
- 返回 `{ dragOffset, isDragging, handlers }`

**验证**:
- 在控制台输出滑动偏移量
- 确认水平滑动检测正常

**代码结构**:
```typescript
export const useSwipeGesture = (onSwipeLeft: () => void, onSwipeRight: () => void) => {
  const [dragOffset, setDragOffset] = useState(0);
  const [isDragging, setIsDragging] = useState(false);
  const [touchStart, setTouchStart] = useState<{x: number, y: number} | null>(null);

  // 鼠标/触摸事件处理
  const handlers = {
    onMouseDown,
    onMouseMove,
    onMouseUp,
    onMouseLeave,
    onTouchStart,
    onTouchMove,
    onTouchEnd
  };

  return { dragOffset, isDragging, handlers };
};
```

---

### Step 1.2: 创建计时器 hooks
**文件**:
- `src/renderer/hooks/useStopwatch.ts`
- `src/renderer/hooks/useTimer.ts`

**任务**:
- 实现 `useStopwatch` hook（计时器逻辑）
- 实现 `useTimer` hook（倒计时逻辑）
- 使用 `requestAnimationFrame` 获得精确计时
- 支持开始/暂停/重置功能

**验证**:
- 创建简单测试页面验证计时准确性
- 确认开始/暂停/重置功能正常

**useStopwatch 接口**:
```typescript
interface UseStopwatchReturn {
  time: number;              // 毫秒
  isRunning: boolean;
  start: () => void;
  pause: () => void;
  reset: () => void;
  formatTime: () => string;  // 格式化为 00:00:00.00
}
```

**useTimer 接口**:
```typescript
interface UseTimerReturn {
  remaining: number;         // 秒
  isRunning: boolean;
  progress: number;          // 0-100
  start: () => void;
  pause: () => void;
  reset: () => void;
  setDuration: (seconds: number) => void;
}
```

---

### Step 1.3: 创建 WatchFaceContainer 组件
**文件**: `src/renderer/components/simulator/clock/WatchFaceContainer.tsx`

**任务**:
- 创建表盘容器组件
- 集成 `useSwipeGesture` hook
- 实现表盘切换逻辑（前/当前/后三个表盘渲染）
- 添加滑动动画（transform + transition）

**Props**:
```typescript
interface WatchFaceContainerProps {
  faces: React.ComponentType<WatchFaceProps>[];
  currentIndex: number;
  onIndexChange: (index: number) => void;
  mode: 'clock' | 'stopwatch' | 'timer';
  time: Date;
  stopwatchTime?: number;
  timerRemaining?: number;
}
```

**验证**:
- 使用两个简单的占位表盘测试切换
- 确认滑动切换流畅（< 300ms）
- 确认循环切换正常（最后一个 → 第一个）

---

### Step 1.4: 创建 ModeSelector 组件
**文件**: `src/renderer/components/simulator/clock/ModeSelector.tsx`

**任务**:
- 创建底部模式切换器
- 三个按钮：时钟、计时器、倒计时
- 当前模式高亮显示
- 使用 `e.stopPropagation()` 防止触发滑动

**Props**:
```typescript
interface ModeSelectorProps {
  currentMode: 'clock' | 'stopwatch' | 'timer';
  onModeChange: (mode: 'clock' | 'stopwatch' | 'timer') => void;
}
```

**样式**:
- 半透明黑色背景（rgba(0,0,0,0.6)）
- 毛玻璃效果（backdrop-filter: blur(10px)）
- 圆角 20px，padding 8px
- 按钮间距 12px

**验证**:
- 点击按钮切换模式
- 确认不会触发表盘滑动
- 确认高亮状态正确

---

### Step 1.5: 重构 ClockPage 主组件
**文件**: `src/renderer/components/simulator/ClockPage.tsx`

**任务**:
- 重构现有 ClockPage
- 集成 WatchFaceContainer 和 ModeSelector
- 管理全局状态（currentFaceIndex, currentMode）
- 集成 useStopwatch 和 useTimer hooks
- 保持长按返回功能

**状态管理**:
```typescript
const [currentFaceIndex, setCurrentFaceIndex] = useState(0);
const [currentMode, setCurrentMode] = useState<'clock' | 'stopwatch' | 'timer'>('clock');
const [time, setTime] = useState(new Date());
const stopwatch = useStopwatch();
const timer = useTimer(60); // 默认 1 分钟
```

**验证**:
- 页面正常渲染
- 模式切换正常
- 长按返回功能正常

---

## Phase 2: 表盘实现

### Step 2.1: 实现极简数字表盘
**文件**: `src/renderer/components/simulator/clock/faces/MinimalistFace.tsx`

**任务**:
- 纯白色背景，黑色超大数字
- 显示时间、日期、星期
- 实现数字翻页动画（flipNumber）
- 根据 mode 显示不同内容（时钟/计时器/倒计时）

**动画文件**: `src/renderer/components/simulator/clock/animations/flipNumber.ts`
```typescript
export const flipNumberAnimation = keyframes`
  0% { transform: translateY(100%); opacity: 0; }
  50% { transform: translateY(-10%); }
  100% { transform: translateY(0); opacity: 1; }
`;
```

**Props**:
```typescript
interface WatchFaceProps {
  mode: 'clock' | 'stopwatch' | 'timer';
  time: Date;
  stopwatchTime?: number;
  timerRemaining?: number;
  timerProgress?: number;
}
```

**验证**:
- 时钟模式：显示当前时间
- 计时器模式：显示计时时间
- 倒计时模式：显示剩余时间
- 数字变化时有翻页动画

---

### Step 2.2: 实现模拟指针表盘
**文件**: `src/renderer/components/simulator/clock/faces/AnalogFace.tsx`

**任务**:
- 深色背景，金色/白色指针
- 12 个刻度点
- 时针、分针、秒针
- 指针平滑旋转动画
- 秒针弹跳效果

**动画文件**: `src/renderer/components/simulator/clock/animations/analogClock.ts`
```typescript
export const calculateAngles = (time: Date) => {
  const seconds = time.getSeconds();
  const minutes = time.getMinutes();
  const hours = time.getHours() % 12;

  return {
    secondAngle: (seconds / 60) * 360,
    minuteAngle: (minutes / 60) * 360 + (seconds / 60) * 6,
    hourAngle: (hours / 12) * 360 + (minutes / 60) * 30
  };
};
```

**验证**:
- 指针角度计算正确
- 指针旋转流畅
- 秒针有弹跳效果
- 计时器模式：秒针快速旋转

---

### Step 2.3: 实现运动风格表盘
**文件**: `src/renderer/components/simulator/clock/faces/SportFace.tsx`

**任务**:
- 黑色背景，橙色主题
- 中心显示时间
- 3 个圆环进度条（SVG）
- 底部显示心率、步数（模拟数据）
- 圆环动画

**动画文件**: `src/renderer/components/simulator/clock/animations/circleProgress.ts`
```typescript
export const calculateCircleProgress = (progress: number, radius: number) => {
  const circumference = 2 * Math.PI * radius;
  const offset = circumference - (progress / 100) * circumference;
  return { circumference, offset };
};
```

**模拟数据**:
```typescript
const activityProgress = (time.getHours() / 24) * 100;
const exerciseProgress = Math.random() * 60 + 20;
const standProgress = Math.random() * 80 + 10;
const heartRate = 72 + Math.floor(Math.random() * 20);
const steps = Math.floor(Math.random() * 5000) + 3000;
```

**验证**:
- 圆环正确显示
- 圆环动画流畅
- 计时器模式：圆环显示计时进度（每圈 1 分钟）
- 倒计时模式：圆环显示剩余时间百分比

---

### Step 2.4: 实现像素风格表盘
**文件**: `src/renderer/components/simulator/clock/faces/PixelFace.tsx`

**任务**:
- 深蓝色背景，绿色像素字体
- 使用像素字体（Press Start 2P 或 fallback）
- 复古游戏机风格边框
- 像素闪烁效果
- 扫描线动画

**动画文件**: `src/renderer/components/simulator/clock/animations/pixelEffects.ts`
```typescript
export const pixelFlickerAnimation = keyframes`
  0%, 100% { opacity: 1; }
  50% { opacity: 0.8; }
`;

export const scanlineAnimation = keyframes`
  0% { transform: translateY(-100%); }
  100% { transform: translateY(100%); }
`;
```

**字体加载**:
```typescript
// 在 index.html 中添加
<link href="https://fonts.googleapis.com/css2?family=Press+Start+2P&display=swap" rel="stylesheet">
```

**验证**:
- 像素字体正确显示
- 数字变化时有闪烁效果
- 扫描线动画流畅
- 边框呼吸效果正常

---

## Phase 3: 模式功能实现

### Step 3.1: 实现计时器控制
**文件**: `src/renderer/components/simulator/clock/modes/StopwatchControls.tsx`

**任务**:
- 创建计时器控制按钮组件
- 开始/暂停按钮（中心大按钮）
- 重置按钮（右下角小按钮）
- 集成到各个表盘

**Props**:
```typescript
interface StopwatchControlsProps {
  isRunning: boolean;
  onStart: () => void;
  onPause: () => void;
  onReset: () => void;
}
```

**布局**:
- 中心按钮：直径 60px，开始/暂停图标
- 重置按钮：右下角，直径 40px
- 半透明背景，毛玻璃效果

**验证**:
- 按钮点击响应正常
- 不会触发表盘滑动
- 在所有表盘上都能正常显示

---

### Step 3.2: 实现倒计时控制
**文件**: `src/renderer/components/simulator/clock/modes/TimerControls.tsx`

**任务**:
- 创建倒计时控制按钮组件
- 预设时间按钮（1分钟、3分钟、5分钟、10分钟）
- 开始/暂停/重置按钮
- 倒计时结束提醒

**Props**:
```typescript
interface TimerControlsProps {
  isRunning: boolean;
  remaining: number;
  onStart: () => void;
  onPause: () => void;
  onReset: () => void;
  onSetDuration: (seconds: number) => void;
}
```

**预设时间**:
- 1 分钟：60 秒
- 3 分钟：180 秒
- 5 分钟：300 秒
- 10 分钟：600 秒

**倒计时结束**:
```typescript
const onTimerComplete = () => {
  // 全屏闪烁 3 次
  setFlashing(true);
  setTimeout(() => setFlashing(false), 1800);

  // 震动（如果支持）
  if ('vibrate' in navigator) {
    navigator.vibrate([200, 100, 200, 100, 200]);
  }

  // 显示提示
  setShowMessage(true);
  setTimeout(() => {
    setShowMessage(false);
    setCurrentMode('clock');
  }, 2000);
};
```

**验证**:
- 预设时间按钮正常工作
- 倒计时准确
- 最后 10 秒数字变红并闪烁
- 结束时有提醒效果

---

### Step 3.3: 集成模式切换逻辑
**文件**: 更新 `ClockPage.tsx`

**任务**:
- 根据 mode 显示不同的控制按钮
- 计时器运行时切换表盘，状态保持
- 计时器运行时长按返回，显示确认对话框

**确认对话框**:
```typescript
const [showConfirmDialog, setShowConfirmDialog] = useState(false);

const handleLongPress = () => {
  if (currentMode !== 'clock' && (stopwatch.isRunning || timer.isRunning)) {
    setShowConfirmDialog(true);
  } else {
    onBack();
  }
};
```

**验证**:
- 模式切换正常
- 计时器状态在切换表盘时保持
- 长按返回时有确认提示

---

### Step 3.4: 实现状态持久化
**文件**: 更新 `ClockPage.tsx`

**任务**:
- 使用 localStorage 保存用户偏好
- 保存：上次表盘、上次模式、倒计时预设
- 页面加载时恢复状态

**实现**:
```typescript
// 保存
useEffect(() => {
  localStorage.setItem('clockSettings', JSON.stringify({
    lastWatchFace: currentFaceIndex,
    lastMode: currentMode,
    timerPresets: [60, 180, 300, 600]
  }));
}, [currentFaceIndex, currentMode]);

// 加载
useEffect(() => {
  const saved = localStorage.getItem('clockSettings');
  if (saved) {
    const settings = JSON.parse(saved);
    setCurrentFaceIndex(settings.lastWatchFace || 0);
    setCurrentMode(settings.lastMode || 'clock');
  }
}, []);
```

**验证**:
- 刷新页面后状态恢复
- 切换表盘后再次进入，显示上次的表盘

---

## Phase 4: 优化与完善

### Step 4.1: 性能优化
**任务**:
- 使用 `React.memo` 包裹表盘组件
- 优化动画性能（使用 transform 和 opacity）
- 只渲染当前表盘和相邻表盘
- 清理定时器和动画帧

**优化点**:
```typescript
// 1. 使用 React.memo
export const MinimalistFace = React.memo<WatchFaceProps>(({ mode, time, ... }) => {
  // ...
});

// 2. 清理副作用
useEffect(() => {
  return () => {
    if (intervalRef.current) clearInterval(intervalRef.current);
    if (rafRef.current) cancelAnimationFrame(rafRef.current);
  };
}, []);

// 3. 避免不必要的渲染
const memoizedAngles = useMemo(() => calculateAngles(time), [time]);
```

**验证**:
- 使用 React DevTools Profiler 检查渲染性能
- 确认帧率 > 30fps
- 确认内存不会持续增长

---

### Step 4.2: 边界情况处理
**任务**:
- 处理计时器运行时的各种场景
- 处理滑动冲突
- 添加性能降级逻辑（可选）

**测试场景**:
1. 计时器运行时切换表盘 ✓
2. 计时器运行时长按返回 ✓
3. 倒计时结束时的提醒 ✓
4. 在模式切换器上滑动 ✓
5. 快速连续滑动切换 ✓

**验证**:
- 所有边界情况都有合理处理
- 没有崩溃或异常行为

---

### Step 4.3: 动画细节调整
**任务**:
- 调整动画时长和缓动函数
- 确保所有动画流畅自然
- 添加微交互细节

**调整项**:
- 数字翻页动画：0.3s ease-out
- 指针旋转：0.5s cubic-bezier(0.68, -0.55, 0.265, 1.55)
- 圆环动画：1s ease
- 表盘切换：0.3s ease

**验证**:
- 所有动画流畅
- 没有卡顿或跳帧
- 动画时机合理

---

### Step 4.4: 最终测试
**任务**:
- 完整测试所有功能
- 测试所有表盘和模式组合
- 测试所有交互场景

**测试清单**:
- [ ] 4 个表盘都能正常显示
- [ ] 左右滑动切换表盘流畅
- [ ] 每个表盘都有独特动画
- [ ] 计时器功能正常（开始/暂停/重置）
- [ ] 倒计时功能正常（预设时间/结束提醒）
- [ ] 模式切换器正常工作
- [ ] 长按返回功能正常
- [ ] 计时器运行时长按有确认提示
- [ ] 状态持久化正常
- [ ] 性能良好（帧率 > 30fps）

---

## 实现顺序总结

```
Phase 1: 基础架构（2-3小时）
├─ Step 1.1: useSwipeGesture hook
├─ Step 1.2: useStopwatch & useTimer hooks
├─ Step 1.3: WatchFaceContainer 组件
├─ Step 1.4: ModeSelector 组件
└─ Step 1.5: 重构 ClockPage

Phase 2: 表盘实现（4-5小时）
├─ Step 2.1: MinimalistFace（极简数字）
├─ Step 2.2: AnalogFace（模拟指针）
├─ Step 2.3: SportFace（运动风格）
└─ Step 2.4: PixelFace（像素风格）

Phase 3: 模式功能（2-3小时）
├─ Step 3.1: StopwatchControls（计时器控制）
├─ Step 3.2: TimerControls（倒计时控制）
├─ Step 3.3: 集成模式切换逻辑
└─ Step 3.4: 状态持久化

Phase 4: 优化与完善（1-2小时）
├─ Step 4.1: 性能优化
├─ Step 4.2: 边界情况处理
├─ Step 4.3: 动画细节调整
└─ Step 4.4: 最终测试
```

**总计**: 约 9-13 小时

---

## 开发建议

1. **每完成一个 Step 就提交一次 git**
   - 便于回滚和追踪进度
   - 提交信息格式：`feat(clock): implement [step-name]`

2. **先实现功能，再优化动画**
   - 确保核心功能正常工作
   - 动画可以后续逐步完善

3. **使用占位数据测试**
   - 模拟数据（心率、步数）可以先用随机数
   - 后续可以接入真实数据源

4. **保持代码整洁**
   - 每个组件职责单一
   - 复用逻辑提取为 hook
   - 动画逻辑独立文件

5. **及时测试**
   - 每完成一个 Step 就测试
   - 不要等到全部完成再测试

---

## 下一步

准备好开始实现了吗？建议：

1. **创建 git worktree**：`git worktree add ../clock-enhancement -b feature/clock-enhancement`
2. **开始 Phase 1**：从 Step 1.1 开始逐步实现
3. **每完成一个 Step 就提交**：保持小步快跑

要不要我现在开始实现 Step 1.1？
