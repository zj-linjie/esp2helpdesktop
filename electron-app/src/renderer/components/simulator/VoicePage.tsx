import React, { useState, useEffect, useRef } from 'react';
import { Box, Typography, IconButton, FormControlLabel, Switch } from '@mui/material';
import { Mic, MicOff, ContentCopy, Send, Close } from '@mui/icons-material';

interface VoicePageProps {
  onBack: () => void;
  onNavigate: (page: string) => void;
}

interface VoiceDispatchAck {
  requestId: string;
  success: boolean;
  action?: 'navigate' | 'launch_app' | 'unknown';
  page?: string;
  appName?: string;
  appPath?: string;
  message?: string;
  reason?: string;
  command?: string;
  normalized?: string;
  deviceId?: string;
  timestamp?: number;
}

const VoicePage: React.FC<VoicePageProps> = ({ onBack, onNavigate }) => {
  const [isListening, setIsListening] = useState(false);
  const [voiceText, setVoiceText] = useState('');
  const [voiceFinal, setVoiceFinal] = useState('');
  const [voicePreview, setVoicePreview] = useState('');
  const [voiceStatus, setVoiceStatus] = useState<string | null>(null);
  const [voiceBridgeConnected, setVoiceBridgeConnected] = useState(false);
  const [autoExecuteOnFinal, setAutoExecuteOnFinal] = useState(true);

  const audioContextRef = useRef<AudioContext | null>(null);
  const mediaStreamRef = useRef<MediaStream | null>(null);
  const processorRef = useRef<ScriptProcessorNode | null>(null);
  const lastFinalRef = useRef<string>('');
  const audioChunkCountRef = useRef<number>(0);
  const executingCommandRef = useRef(false);
  const voiceBridgeWsRef = useRef<WebSocket | null>(null);
  const voiceBridgeReconnectTimerRef = useRef<number | null>(null);
  const voiceDispatchResolversRef = useRef<Map<string, (ack: VoiceDispatchAck) => void>>(new Map());

  // 获取 ipcRenderer - 使用 require 而不是 import
  const getIpcRenderer = () => {
    try {
      // @ts-ignore
      return window.require ? window.require('electron').ipcRenderer : null;
    } catch {
      return null;
    }
  };

  const ipcRenderer = getIpcRenderer();

  useEffect(() => {
    let disposed = false;

    const scheduleReconnect = () => {
      if (disposed || voiceBridgeReconnectTimerRef.current !== null) return;
      voiceBridgeReconnectTimerRef.current = window.setTimeout(() => {
        voiceBridgeReconnectTimerRef.current = null;
        connectBridge();
      }, 2500);
    };

    const connectBridge = () => {
      if (disposed) return;
      const existing = voiceBridgeWsRef.current;
      if (existing && (existing.readyState === WebSocket.OPEN || existing.readyState === WebSocket.CONNECTING)) {
        return;
      }

      const ws = new WebSocket('ws://localhost:8765');
      voiceBridgeWsRef.current = ws;

      ws.onopen = () => {
        if (disposed) return;
        setVoiceBridgeConnected(true);
        ws.send(JSON.stringify({
          type: 'handshake',
          clientType: 'control_panel',
        }));
      };

      ws.onmessage = (event) => {
        try {
          const packet = JSON.parse(event.data);
          if (packet?.type !== 'voice_command_dispatch_ack') return;

          const payload = (packet?.data || {}) as Partial<VoiceDispatchAck>;
          const requestId = typeof payload.requestId === 'string' ? payload.requestId : '';
          if (!requestId) return;

          const resolver = voiceDispatchResolversRef.current.get(requestId);
          if (!resolver) return;

          voiceDispatchResolversRef.current.delete(requestId);
          resolver({
            requestId,
            success: Boolean(payload.success),
            action: payload.action,
            page: payload.page,
            appName: payload.appName,
            appPath: payload.appPath,
            message: typeof payload.message === 'string' ? payload.message : '',
            reason: typeof payload.reason === 'string' ? payload.reason : '',
            command: typeof payload.command === 'string' ? payload.command : '',
            normalized: typeof payload.normalized === 'string' ? payload.normalized : '',
            deviceId: typeof payload.deviceId === 'string' ? payload.deviceId : '',
            timestamp: Number(payload.timestamp || Date.now()),
          });
        } catch {
          // ignore malformed packets
        }
      };

      ws.onclose = () => {
        if (disposed) return;
        if (voiceBridgeWsRef.current === ws) {
          voiceBridgeWsRef.current = null;
        }
        setVoiceBridgeConnected(false);
        scheduleReconnect();
      };

      ws.onerror = () => {
        // close will trigger reconnect
      };
    };

    connectBridge();

    return () => {
      disposed = true;
      if (voiceBridgeReconnectTimerRef.current !== null) {
        window.clearTimeout(voiceBridgeReconnectTimerRef.current);
        voiceBridgeReconnectTimerRef.current = null;
      }

      voiceDispatchResolversRef.current.forEach((resolve, requestId) => {
        resolve({
          requestId,
          success: false,
          reason: 'bridge closed',
        });
      });
      voiceDispatchResolversRef.current.clear();

      const ws = voiceBridgeWsRef.current;
      voiceBridgeWsRef.current = null;
      if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        ws.close();
      }
      setVoiceBridgeConnected(false);
    };
  }, []);

  // 监听 ASR 结果
  useEffect(() => {
    if (!ipcRenderer) return;

    const handleAsrResult = (_event: any, data: { text: string; isFinal?: boolean; name?: string }) => {
      console.log('[VoicePage] 收到 ASR 结果:', { text: data.text, isFinal: data.isFinal, name: data.name });

      if (data.isFinal) {
        console.log('[VoicePage] 处理 Final 结果');
        setVoicePreview('');

        const clean = data.text.trim();
        console.log('[VoicePage] clean:', clean, 'lastFinal:', lastFinalRef.current);

        if (!clean) return;
        if (clean === lastFinalRef.current) {
          console.log('[VoicePage] 重复的 final 结果，跳过');
          return;
        }

        lastFinalRef.current = clean;

        setVoiceFinal((prev) => {
          // 如果新文本以之前文本开头，说明是同一句话的延续，直接用新文本替换
          // 否则追加到后面（新句子）
          const next = prev && clean.startsWith(prev) ? clean : `${prev} ${clean}`.trim();
          console.log('[VoicePage] 更新 voiceFinal 从', prev, '到', next);
          return next;
        });

        if (autoExecuteOnFinal) {
          setTimeout(() => {
            executeCommandText(clean, 'auto').catch((error) => {
              setVoiceStatus(`自动执行失败: ${String(error)}`);
            });
          }, 120);
        }
        return;
      }
      setVoicePreview(data.text);
    };

    const handleAsrError = (_event: any, data: { message: string }) => {
      setVoiceStatus(`错误: ${data.message}`);
      setIsListening(false);
    };

    ipcRenderer.on('asr-result', handleAsrResult);
    ipcRenderer.on('asr-error', handleAsrError);

    return () => {
      ipcRenderer.removeListener('asr-result', handleAsrResult);
      ipcRenderer.removeListener('asr-error', handleAsrError);
    };
  }, [autoExecuteOnFinal]);

  // 同步 voiceFinal 到 voiceText
  useEffect(() => {
    if (voiceFinal) {
      console.log('[VoicePage] 同步 voiceFinal 到 voiceText:', voiceFinal);
      setVoiceText(voiceFinal);
    }
  }, [voiceFinal]);

  // 开始录音
  const startVoice = async () => {
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      const audioContext = new AudioContext({ sampleRate: 16000 });
      const source = audioContext.createMediaStreamSource(stream);
      const processor = audioContext.createScriptProcessor(4096, 1, 1);

      processor.onaudioprocess = (event) => {
        const input = event.inputBuffer.getChannelData(0);
        const buffer = new ArrayBuffer(input.length * 2);
        const view = new DataView(buffer);
        for (let i = 0; i < input.length; i++) {
          const s = Math.max(-1, Math.min(1, input[i]));
          view.setInt16(i * 2, s * 0x7fff, true);
        }
        ipcRenderer.send('asr-audio', buffer);
      };

      source.connect(processor);
      processor.connect(audioContext.destination);

      audioContextRef.current = audioContext;
      mediaStreamRef.current = stream;
      processorRef.current = processor;

      setIsListening(true);
      setVoiceText('');
      setVoiceFinal('');
      setVoicePreview('');
      setVoiceStatus('正在监听...');
      lastFinalRef.current = '';
      audioChunkCountRef.current = 0;

      console.log('[VoicePage] 准备启动 ASR, 采样率:', audioContext.sampleRate);
      console.log('[VoicePage] ipcRenderer 是否存在:', !!ipcRenderer);
      const result = await ipcRenderer.invoke('asr-start', audioContext.sampleRate);
      console.log('[VoicePage] ASR 启动结果:', result);
    } catch (error) {
      setVoiceStatus('麦克风访问失败');
      setIsListening(false);
    }
  };

  // 停止录音
  const stopVoice = () => {
    ipcRenderer.send('asr-stop');

    processorRef.current?.disconnect();
    mediaStreamRef.current?.getTracks().forEach((track) => track.stop());
    audioContextRef.current?.close();

    processorRef.current = null;
    mediaStreamRef.current = null;
    audioContextRef.current = null;

    setIsListening(false);
    setVoiceStatus(null);
  };

  // 切换录音状态
  const handleToggle = () => {
    if (isListening) {
      stopVoice();
    } else {
      startVoice();
    }
  };

  // 复制文本
  const handleCopy = async () => {
    if (!voiceText.trim()) return;
    try {
      await navigator.clipboard.writeText(voiceText);
      setVoiceStatus('已复制到剪贴板');
      setTimeout(() => setVoiceStatus(null), 2000);
    } catch (error) {
      setVoiceStatus('复制失败');
    }
  };

  const dispatchVoiceCommandToDevice = async (command: string): Promise<VoiceDispatchAck> => {
    const ws = voiceBridgeWsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      return {
        requestId: '',
        success: false,
        reason: 'bridge disconnected',
      };
    }

    const requestId = `voice-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`;
    const timeoutMs = 7000;

    try {
      return await new Promise<VoiceDispatchAck>((resolve) => {
        const timeout = window.setTimeout(() => {
          voiceDispatchResolversRef.current.delete(requestId);
          resolve({
            requestId,
            success: false,
            reason: `dispatch timeout (${timeoutMs}ms)`,
          });
        }, timeoutMs);

        voiceDispatchResolversRef.current.set(requestId, (ack) => {
          window.clearTimeout(timeout);
          resolve(ack);
        });

        ws.send(JSON.stringify({
          type: 'voice_command_from_panel',
          data: {
            requestId,
            text: command,
          },
        }));
      });
    } catch (error) {
      voiceDispatchResolversRef.current.delete(requestId);
      return {
        requestId,
        success: false,
        reason: String(error),
      };
    }
  };

  const executeLocalCommand = async (text: string) => {
    if (text.includes('返回') || text.includes('主页') || text.includes('回到主页')) {
      setVoiceStatus('返回主页');
      setTimeout(() => onBack(), 500);
    } else if (text.includes('监控') || text.includes('系统监控')) {
      setVoiceStatus('打开系统监控');
      setTimeout(() => onNavigate('monitor'), 500);
    } else if (text.includes('时钟') || text.includes('钟表')) {
      setVoiceStatus('打开时钟');
      setTimeout(() => onNavigate('clock'), 500);
    } else if (text.includes('番茄钟') || text.includes('计时器') || text.includes('定时器')) {
      setVoiceStatus('打开番茄钟');
      setTimeout(() => onNavigate('timer'), 500);
    } else if (text.includes('天气') || text.includes('天气预报')) {
      setVoiceStatus('打开天气');
      setTimeout(() => onNavigate('weather'), 500);
    } else if (text.includes('相框') || text.includes('照片') || text.includes('图片')) {
      setVoiceStatus('打开相框');
      setTimeout(() => onNavigate('photo'), 500);
    } else if (text.includes('应用') || text.includes('启动器') || text.includes('应用列表')) {
      setVoiceStatus('打开应用启动器');
      setTimeout(() => onNavigate('quick'), 500);
    } else if (text.includes('设置') || text.includes('快捷设置')) {
      setVoiceStatus('打开设置');
      setTimeout(() => onNavigate('settings'), 500);
    } else if (text.includes('打开') || text.includes('启动') || text.includes('运行')) {
      const appName = extractAppName(text);
      if (appName) {
        setVoiceStatus(`正在打开 ${appName}...`);
        try {
          if (ipcRenderer) {
            const result = await ipcRenderer.invoke('launch-app', `/Applications/${appName}.app`);
            if (result.success) {
              setVoiceStatus(`已打开 ${appName}`);
            } else {
              setVoiceStatus(`打开失败: ${appName}`);
            }
          }
        } catch {
          setVoiceStatus(`打开失败: ${appName}`);
        }
      } else {
        setVoiceStatus('未识别的应用名称');
      }
    } else {
      setVoiceStatus('未识别的命令');
    }

    setTimeout(() => setVoiceStatus(null), 2000);
  };

  async function executeCommandText(rawText: string, source: 'manual' | 'auto' = 'manual') {
    const text = rawText.trim().toLowerCase();
    if (!text) return;
    if (executingCommandRef.current) {
      if (source === 'manual') {
        setVoiceStatus('上一条命令执行中，请稍候...');
      }
      return;
    }

    executingCommandRef.current = true;
    try {
      if (source === 'auto') {
        setVoiceStatus(`自动执行: ${text}`);
      } else {
        setVoiceStatus('正在下发到 ESP32...');
      }

      const dispatch = await dispatchVoiceCommandToDevice(text);
      if (dispatch.success) {
        const detail = dispatch.message || '命令执行成功';
        const targetLabel = dispatch.deviceId ? ` (${dispatch.deviceId})` : '';
        setVoiceStatus(`ESP32${targetLabel}: ${detail}`);
        setTimeout(() => setVoiceStatus(null), 2600);
        return;
      }

      const reason = dispatch.reason || 'unknown error';
      const fallbackToLocal =
        reason.includes('no online esp32 device') ||
        reason.includes('device not online') ||
        reason.includes('bridge disconnected');

      if (fallbackToLocal) {
        setVoiceStatus('未检测到在线 ESP32，改为本地模拟执行');
        setTimeout(() => {
          executeLocalCommand(text).catch((error) => {
            setVoiceStatus(`本地执行失败: ${String(error)}`);
          });
        }, 200);
        return;
      }

      setVoiceStatus(`ESP32 执行失败: ${reason}`);
      setTimeout(() => setVoiceStatus(null), 3000);
    } finally {
      executingCommandRef.current = false;
    }
  }

  // 执行命令
  const handleCommand = async () => {
    if (!voiceText.trim()) return;
    console.log('[VoicePage] 执行命令:', voiceText);
    await executeCommandText(voiceText, 'manual');
  };

  // 从语音文本中提取应用名称
  const extractAppName = (text: string): string | null => {
    // 常见应用映射
    const appMap: Record<string, string> = {
      '微信': 'WeChat',
      '浏览器': 'Safari',
      '萨法里': 'Safari',
      'safari': 'Safari',
      '谷歌浏览器': 'Google Chrome',
      'chrome': 'Google Chrome',
      '火狐': 'Firefox',
      'firefox': 'Firefox',
      '终端': 'Terminal',
      'terminal': 'Terminal',
      '访达': 'Finder',
      'finder': 'Finder',
      '邮件': 'Mail',
      'mail': 'Mail',
      '日历': 'Calendar',
      'calendar': 'Calendar',
      '备忘录': 'Notes',
      'notes': 'Notes',
      '音乐': 'Music',
      'music': 'Music',
      '照片': 'Photos',
      'photos': 'Photos',
      '设置': 'System Settings',
      '系统设置': 'System Settings',
      'vscode': 'Visual Studio Code',
      'vs code': 'Visual Studio Code',
      '代码编辑器': 'Visual Studio Code',
    };

    // 移除"打开"、"启动"等动词和标点符号
    let cleanText = text
      .replace(/打开|启动|运行/g, '')
      .replace(/[。，、！？,.!?]/g, '')
      .trim();

    // 查找匹配的应用
    for (const [key, value] of Object.entries(appMap)) {
      if (cleanText.includes(key)) {
        return value;
      }
    }

    // 如果没有匹配到，尝试将清理后的文本作为应用名
    // 首字母大写
    if (cleanText) {
      return cleanText.charAt(0).toUpperCase() + cleanText.slice(1);
    }

    return null;
  };

  // 清空文本
  const handleClear = () => {
    setVoiceText('');
    setVoicePreview('');
    setVoiceStatus(null);
  };

  // 波形可视化（简化版，不需要实时音频分析）
  const renderWaveform = () => {
    const bars = 12;
    const barElements = [];

    for (let i = 0; i < bars; i++) {
      const height = isListening
        ? Math.max(20, 50 * (0.5 + Math.random() * 0.5))
        : 20;

      barElements.push(
        <Box
          key={i}
          sx={{
            width: 4,
            height: `${height}%`,
            backgroundColor: isListening ? '#1976d2' : 'rgba(255, 255, 255, 0.3)',
            borderRadius: 1,
            transition: 'all 0.1s ease',
          }}
        />
      );
    }

    return barElements;
  };

  return (
    <Box
      sx={{
        width: '100%',
        height: '100%',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 2,
        padding: 2,
      }}
    >
      {/* 标题 */}
      <Typography
        variant="body1"
        sx={{
          color: '#fff',
          fontWeight: 600,
        }}
      >
        语音控制
      </Typography>

      <Typography
        variant="caption"
        sx={{
          color: voiceBridgeConnected ? '#81c784' : 'rgba(255, 255, 255, 0.55)',
          fontSize: '0.72rem',
          marginTop: -1.2,
        }}
      >
        设备桥接: {voiceBridgeConnected ? '已连接（优先控制 ESP32）' : '未连接（执行将回退本地模拟）'}
      </Typography>

      <FormControlLabel
        sx={{ marginTop: -1.2 }}
        control={(
          <Switch
            size="small"
            checked={autoExecuteOnFinal}
            onChange={(event) => setAutoExecuteOnFinal(event.target.checked)}
          />
        )}
        label={(
          <Typography
            variant="caption"
            sx={{ color: 'rgba(255, 255, 255, 0.7)', fontSize: '0.72rem' }}
          >
            识别结束自动执行
          </Typography>
        )}
      />

      {/* 波形可视化 */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 0.5,
          height: 60,
          width: '80%',
        }}
      >
        {renderWaveform()}
      </Box>

      {/* 录音按钮 */}
      <IconButton
        onClick={handleToggle}
        sx={{
          width: 80,
          height: 80,
          backgroundColor: isListening ? '#f44336' : '#1976d2',
          color: '#fff',
          '&:hover': {
            backgroundColor: isListening ? '#d32f2f' : '#1565c0',
          },
          transition: 'all 0.3s ease',
          boxShadow: isListening
            ? '0 0 20px rgba(244, 67, 54, 0.6)'
            : '0 0 20px rgba(25, 118, 210, 0.4)',
        }}
      >
        {isListening ? <MicOff sx={{ fontSize: 40 }} /> : <Mic sx={{ fontSize: 40 }} />}
      </IconButton>

      {/* 状态文字 */}
      {voiceStatus && (
        <Typography
          variant="caption"
          sx={{
            color: 'rgba(255, 255, 255, 0.7)',
            fontSize: '0.75rem',
          }}
        >
          {voiceStatus}
        </Typography>
      )}

      {/* 识别文本显示 */}
      <Box
        sx={{
          width: '90%',
          minHeight: 60,
          maxHeight: 80,
          backgroundColor: 'rgba(255, 255, 255, 0.1)',
          borderRadius: 2,
          padding: 1.5,
          border: '1px solid rgba(255, 255, 255, 0.2)',
          overflowY: 'auto',
        }}
      >
        <Typography
          variant="body2"
          sx={{
            color: '#fff',
            fontSize: '0.85rem',
            lineHeight: 1.4,
          }}
        >
          {voiceText || '点击麦克风开始说话...'}
        </Typography>
        {voicePreview && (
          <Typography
            variant="body2"
            sx={{
              color: 'rgba(255, 255, 255, 0.5)',
              fontSize: '0.75rem',
              fontStyle: 'italic',
              marginTop: 0.5,
            }}
          >
            {voicePreview}
          </Typography>
        )}
      </Box>

      {/* 操作按钮 */}
      <Box
        sx={{
          display: 'flex',
          gap: 1,
          justifyContent: 'center',
        }}
      >
        <IconButton
          onClick={handleCopy}
          disabled={!voiceText.trim()}
          sx={{
            backgroundColor: 'rgba(255, 255, 255, 0.1)',
            color: '#fff',
            '&:hover': {
              backgroundColor: 'rgba(255, 255, 255, 0.2)',
            },
            '&:disabled': {
              color: 'rgba(255, 255, 255, 0.3)',
            },
          }}
        >
          <ContentCopy sx={{ fontSize: 20 }} />
        </IconButton>

        <IconButton
          onClick={handleCommand}
          disabled={!voiceText.trim()}
          sx={{
            backgroundColor: 'rgba(76, 175, 80, 0.2)',
            color: '#4caf50',
            '&:hover': {
              backgroundColor: 'rgba(76, 175, 80, 0.3)',
            },
            '&:disabled': {
              color: 'rgba(255, 255, 255, 0.3)',
            },
          }}
        >
          <Send sx={{ fontSize: 20 }} />
        </IconButton>

        <IconButton
          onClick={handleClear}
          disabled={!voiceText.trim()}
          sx={{
            backgroundColor: 'rgba(255, 255, 255, 0.1)',
            color: '#fff',
            '&:hover': {
              backgroundColor: 'rgba(255, 255, 255, 0.2)',
            },
            '&:disabled': {
              color: 'rgba(255, 255, 255, 0.3)',
            },
          }}
        >
          <Close sx={{ fontSize: 20 }} />
        </IconButton>
      </Box>
    </Box>
  );
};

export default VoicePage;
