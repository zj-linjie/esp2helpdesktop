# 语音识别功能配置指南

## 阿里云语音识别 (ASR) 配置

语音功能使用阿里云实时语音识别服务。需要配置以下环境变量：

### 1. 获取阿里云凭证

1. 登录 [阿里云控制台](https://www.aliyun.com/)
2. 开通 [智能语音交互服务](https://nls.console.aliyun.com/)
3. 创建项目并获取 AppKey
4. 在 AccessKey 管理页面获取 AccessKeyId 和 AccessKeySecret

### 2. 配置环境变量

在项目根目录创建 `.env` 文件（或在 `~/.zshrc` / `~/.bashrc` 中配置）：

```bash
# 阿里云 AccessKey
export ALIYUN_ACCESS_KEY_ID="your_access_key_id"
export ALIYUN_ACCESS_KEY_SECRET="your_access_key_secret"

# 阿里云智能语音 AppKey
export ALIYUN_APP_KEY="your_app_key"

# 可选：直接使用 Token（如果已有）
# export ALIYUN_NLS_TOKEN="your_token"

# 可选：自定义服务端点
# export ALIYUN_NLS_TOKEN_URL="https://nls-meta.cn-shanghai.aliyuncs.com/"
# export ALIYUN_NLS_WS_URL="wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1"
```

### 3. 启动应用

```bash
# 加载环境变量
source .env  # 或 source ~/.zshrc

# 启动应用
npm start
```

## 使用说明

### 语音控制页面

1. 在设备模拟器中点击"语音"图标
2. 点击麦克风按钮开始录音
3. 说话时会实时显示识别结果
4. 点击停止按钮结束录音

### 支持的命令

当前支持的语音命令：

- **返回主页**: "返回" / "主页"
- **打开天气**: "天气"
- **打开时钟**: "时钟"

更多命令正在开发中...

### 功能按钮

- **复制**: 复制识别的文本到剪贴板
- **执行**: 执行语音命令
- **清空**: 清空识别文本

## 技术细节

### 音频参数

- **采样率**: 16kHz
- **格式**: PCM
- **位深**: 16-bit
- **声道**: 单声道

### 识别特性

- ✅ 实时流式识别
- ✅ 中间结果显示
- ✅ 标点符号预测
- ✅ 文本规范化
- ✅ 音频可视化

## 故障排除

### 1. 麦克风访问失败

- 检查浏览器是否允许麦克风权限
- 在 macOS 系统偏好设置中检查麦克风权限

### 2. ASR 连接失败

- 检查环境变量是否正确配置
- 检查阿里云账号是否开通智能语音服务
- 检查网络连接是否正常

### 3. 识别结果为空

- 确保麦克风正常工作
- 说话时靠近麦克风
- 检查音量是否足够

## 未来计划

- [ ] 支持更多语音命令
- [ ] 添加 Whisper.cpp 离线识别
- [ ] 支持自定义命令
- [ ] 添加 TTS 语音反馈
- [ ] ESP32 硬件集成

## 参考资料

- [阿里云智能语音文档](https://help.aliyun.com/product/30413.html)
- [实时语音识别 API](https://help.aliyun.com/document_detail/84428.html)
