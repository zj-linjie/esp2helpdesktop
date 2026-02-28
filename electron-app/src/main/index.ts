import { app, BrowserWindow, ipcMain, shell } from 'electron'
import path from 'path'
import { fileURLToPath } from 'url'
import { DeviceWebSocketServer } from './websocket.js'
import { AISimulator } from './ai-simulator.js'
import fs from 'fs/promises'
import crypto from 'crypto'
import WebSocket from 'ws'
import dotenv from 'dotenv'

// 加载环境变量
dotenv.config({ path: path.join(process.cwd(), '.env') })

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

let wsServer: DeviceWebSocketServer
let aiSimulator: AISimulator | null = null
let mainWindow: BrowserWindow | null = null

interface LauncherAppConfig {
  id: string
  name: string
  path: string
}

const APP_LAUNCHER_SETTINGS_FILE = 'app-launcher-settings.json'
let cachedCustomLauncherApps: LauncherAppConfig[] = []

const normalizeLauncherApps = (apps: unknown): LauncherAppConfig[] => {
  if (!Array.isArray(apps)) return []

  const normalized: LauncherAppConfig[] = []
  const seenPaths = new Set<string>()

  for (const item of apps) {
    if (!item || typeof item !== 'object') continue
    const obj = item as Record<string, unknown>
    const name = typeof obj.name === 'string' ? obj.name.trim() : ''
    const appPath = typeof obj.path === 'string' ? obj.path.trim() : ''
    if (!name || !appPath || seenPaths.has(appPath)) continue

    seenPaths.add(appPath)
    normalized.push({
      id: typeof obj.id === 'string' && obj.id.trim() ? obj.id.trim() : `custom-${normalized.length + 1}`,
      name,
      path: appPath,
    })

    if (normalized.length >= 64) {
      break
    }
  }

  return normalized
}

const getAppLauncherSettingsPath = () => path.join(app.getPath('userData'), APP_LAUNCHER_SETTINGS_FILE)

const loadCustomLauncherApps = async (): Promise<LauncherAppConfig[]> => {
  try {
    const filePath = getAppLauncherSettingsPath()
    const raw = await fs.readFile(filePath, 'utf-8')
    const parsed = JSON.parse(raw) as { apps?: unknown }
    const apps = normalizeLauncherApps(parsed?.apps || [])
    console.log(`[AppLauncher] 已加载自定义应用 ${apps.length} 项`)
    return apps
  } catch (error) {
    // ignore file not found and start with empty configuration
    return []
  }
}

const saveCustomLauncherApps = async (apps: LauncherAppConfig[]): Promise<void> => {
  const filePath = getAppLauncherSettingsPath()
  const payload = {
    version: 1,
    updatedAt: new Date().toISOString(),
    apps,
  }
  await fs.writeFile(filePath, JSON.stringify(payload, null, 2), 'utf-8')
}

// ASR 相关变量
let asrSocket: WebSocket | null = null
let asrTaskId: string | null = null
let asrLastText = ''
let asrReady = false
const asrAudioQueue: Buffer[] = []
let audioChunkCount = 0

// 阿里云配置（从环境变量读取）
const aliyunAccessKeyId = process.env.ALIYUN_ACCESS_KEY_ID || ''
const aliyunAccessKeySecret = process.env.ALIYUN_ACCESS_KEY_SECRET || ''
const aliyunAppKey = process.env.ALIYUN_APP_KEY || ''
const aliyunToken = process.env.ALIYUN_NLS_TOKEN || ''
const aliyunTokenEndpoint = process.env.ALIYUN_NLS_TOKEN_URL || 'https://nls-meta.cn-shanghai.aliyuncs.com/'
const aliyunWsEndpoint = process.env.ALIYUN_NLS_WS_URL || 'wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1'

// 打印配置状态（不打印完整密钥）
console.log('[ASR] 配置状态:', {
  hasAccessKeyId: !!aliyunAccessKeyId,
  hasAccessKeySecret: !!aliyunAccessKeySecret,
  hasAppKey: !!aliyunAppKey,
  hasToken: !!aliyunToken,
  accessKeyIdPrefix: aliyunAccessKeyId.substring(0, 8) + '...',
  appKeyPrefix: aliyunAppKey.substring(0, 8) + '...',
})

// URL 编码
const percentEncode = (value: string) =>
  encodeURIComponent(value)
    .replace(/\+/g, '%20')
    .replace(/\*/g, '%2A')
    .replace(/%7E/g, '~')

// 签名参数
const signParams = (params: Record<string, string>, secret: string) => {
  const keys = Object.keys(params).sort()
  const query = keys.map((key) => `${percentEncode(key)}=${percentEncode(params[key])}`).join('&')
  const stringToSign = `GET&%2F&${percentEncode(query)}`
  const signature = crypto.createHmac('sha1', `${secret}&`).update(stringToSign).digest('base64')
  return signature
}

// 获取阿里云 Token
const fetchAliyunToken = async () => {
  if (aliyunToken) return aliyunToken
  if (!aliyunAccessKeyId || !aliyunAccessKeySecret) return null

  const params: Record<string, string> = {
    AccessKeyId: aliyunAccessKeyId,
    Action: 'CreateToken',
    Format: 'JSON',
    RegionId: 'cn-shanghai',
    SignatureMethod: 'HMAC-SHA1',
    SignatureNonce: `${Date.now()}${Math.random().toString(16).slice(2)}`,
    SignatureVersion: '1.0',
    Timestamp: new Date().toISOString(),
    Version: '2019-02-28',
  }

  const signature = signParams(params, aliyunAccessKeySecret)
  params.Signature = signature

  const query = Object.keys(params)
    .map((key) => `${percentEncode(key)}=${percentEncode(params[key])}`)
    .join('&')

  const url = `${aliyunTokenEndpoint}?${query}`
  const response = await fetch(url)
  const json = await response.json()
  return json?.Token?.Id || null
}

// 启动 ASR 会话
const startAsrSession = async (sampleRate: number) => {
  console.log('[ASR] 启动会话, 采样率:', sampleRate)
  audioChunkCount = 0; // 重置计数器

  const token = await fetchAliyunToken()
  if (!token || !aliyunAppKey) {
    const errorMsg = '缺少 Token 或 AppKey，请配置环境变量'
    console.error('[ASR] 错误:', errorMsg)
    mainWindow?.webContents.send('asr-error', { message: errorMsg })
    return
  }

  console.log('[ASR] Token 获取成功，开始连接 WebSocket')

  asrTaskId = crypto.randomUUID().replace(/-/g, '')
  asrLastText = ''
  asrReady = false
  asrAudioQueue.length = 0
  const url = `${aliyunWsEndpoint}?token=${encodeURIComponent(token)}&appkey=${encodeURIComponent(aliyunAppKey)}`
  asrSocket = new WebSocket(url)

  asrSocket.on('open', () => {
    console.log('[ASR] WebSocket 已连接')
    const start = {
      header: {
        appkey: aliyunAppKey,
        namespace: 'SpeechTranscriber',
        name: 'StartTranscription',
        task_id: asrTaskId,
        message_id: crypto.randomUUID().replace(/-/g, ''),
      },
      payload: {
        format: 'pcm',
        sample_rate: sampleRate,
        enable_intermediate_result: true,
        enable_punctuation_prediction: true,
        enable_inverse_text_normalization: true,
      },
    }
    asrSocket?.send(JSON.stringify(start))
    console.log('[ASR] 发送启动消息')
  })

  asrSocket.on('message', (data: WebSocket.RawData) => {
    try {
      const text = data.toString()
      const message = JSON.parse(text)
      const name = message?.header?.name

      console.log('[ASR] 收到消息:', name)

      if (name === 'TranscriptionStarted') {
        asrReady = true
        console.log('[ASR] 转录已启动，队列中有', asrAudioQueue.length, '个音频块')
        while (asrAudioQueue.length > 0 && asrSocket?.readyState === WebSocket.OPEN) {
          const chunk = asrAudioQueue.shift()
          if (chunk) asrSocket.send(chunk)
        }
      }

      const result =
        message?.payload?.result ||
        message?.payload?.output?.text ||
        message?.payload?.text ||
        ''

      const isFinal =
        name === 'SentenceEnd' ||
        name === 'TranscriptionCompleted' ||
        message?.payload?.is_final === true ||
        message?.payload?.final === true ||
        message?.payload?.sentence_end === true

      if (typeof result === 'string' && result) {
        console.log('[ASR] 识别结果:', result, 'isFinal:', isFinal)
        asrLastText = result
        mainWindow?.webContents.send('asr-result', { text: result, isFinal, name })
      }
    } catch (error) {
      console.error('[ASR] 解析消息失败:', error)
    }
  })

  asrSocket.on('close', () => {
    console.log('[ASR] WebSocket 已关闭')
    mainWindow?.webContents.send('asr-result', { text: asrLastText, isFinal: true })
  })

  asrSocket.on('error', (error: Error) => {
    console.error('[ASR] WebSocket 错误:', error.message)
    mainWindow?.webContents.send('asr-error', { message: error.message })
  })
}

// 停止 ASR 会话
const stopAsrSession = () => {
  if (!asrSocket || !asrTaskId) return
  const stop = {
    header: {
      appkey: aliyunAppKey,
      namespace: 'SpeechTranscriber',
      name: 'StopTranscription',
      task_id: asrTaskId,
      message_id: crypto.randomUUID().replace(/-/g, ''),
    },
    payload: {},
  }
  try {
    asrSocket.send(JSON.stringify(stop))
  } catch {
    // ignore
  }
  asrSocket.close()
  asrSocket = null
  asrTaskId = null
  asrReady = false
  asrAudioQueue.length = 0
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  })

  mainWindow = win

  if (process.env.NODE_ENV === 'development') {
    // 尝试多个端口，因为 Vite 可能使用不同端口
    const port = process.env.VITE_PORT || '5173'
    win.loadURL(`http://localhost:${port}`)
    win.webContents.openDevTools() // 打开开发者工具
  } else {
    win.loadFile(path.join(__dirname, '../dist-renderer/index.html'))
  }
}

app.whenReady().then(async () => {
  cachedCustomLauncherApps = await loadCustomLauncherApps()
  createWindow()
  wsServer = new DeviceWebSocketServer(8765)
  wsServer.setCustomLaunchApps(cachedCustomLauncherApps)

  // 注册 IPC 处理器
  setupIpcHandlers()
})

// IPC 处理器
function setupIpcHandlers() {
  // 启动 macOS 应用
  ipcMain.handle('launch-app', async (event, appPath: string) => {
    try {
      console.log('启动应用:', appPath)
      await shell.openPath(appPath)
      return { success: true }
    } catch (error) {
      console.error('启动应用失败:', error)
      return { success: false, error: String(error) }
    }
  })

  // 扫描 /Applications 文件夹
  ipcMain.handle('scan-applications', async () => {
    try {
      const appsDir = '/Applications'
      const files = await fs.readdir(appsDir)

      const apps = files
        .filter(file => file.endsWith('.app'))
        .map((file, index) => ({
          id: String(index + 1),
          name: file.replace('.app', ''),
          path: path.join(appsDir, file),
        }))

      console.log(`扫描到 ${apps.length} 个应用`)
      return { success: true, apps }
    } catch (error) {
      console.error('扫描应用失败:', error)
      return { success: false, error: String(error), apps: [] }
    }
  })

  ipcMain.handle('app-launcher-sync-settings', async (_event, payload: { apps?: unknown }) => {
    try {
      const apps = normalizeLauncherApps(payload?.apps || [])
      cachedCustomLauncherApps = apps
      await saveCustomLauncherApps(cachedCustomLauncherApps)
      wsServer?.setCustomLaunchApps(cachedCustomLauncherApps)
      console.log(`[AppLauncher] 已同步自定义应用 ${apps.length} 项`)
      return { success: true, apps }
    } catch (error) {
      console.error('[AppLauncher] 同步设置失败:', error)
      return { success: false, error: String(error) }
    }
  })

  ipcMain.handle('app-launcher-get-settings', async () => {
    try {
      if (cachedCustomLauncherApps.length === 0) {
        cachedCustomLauncherApps = await loadCustomLauncherApps()
        wsServer?.setCustomLaunchApps(cachedCustomLauncherApps)
      }
      return { success: true, apps: cachedCustomLauncherApps }
    } catch (error) {
      console.error('[AppLauncher] 读取设置失败:', error)
      return { success: false, error: String(error), apps: [] }
    }
  })

  // ASR 启动
  ipcMain.handle('asr-start', async (_event, sampleRate: number) => {
    await startAsrSession(sampleRate)
  })

  // ASR 音频数据
  let audioChunkCount = 0;
  ipcMain.on('asr-audio', (_event, audio: ArrayBuffer) => {
    audioChunkCount++;
    if (audioChunkCount % 50 === 0) {
      console.log('[ASR] 已接收音频块:', audioChunkCount, '大小:', audio.byteLength);
    }
    const chunk = Buffer.from(audio)
    if (asrSocket && asrSocket.readyState === WebSocket.OPEN && asrReady) {
      asrSocket.send(chunk)
      return
    }
    asrAudioQueue.push(chunk)
  })

  // ASR 停止
  ipcMain.on('asr-stop', () => {
    stopAsrSession()
  })

  // AI 模拟器控制
  ipcMain.handle('ai-simulator-start', async () => {
    try {
      if (!aiSimulator) {
        aiSimulator = new AISimulator('ws://localhost:8765')
      }
      aiSimulator.connect()
      return { success: true }
    } catch (error) {
      console.error('[AI Simulator] 启动失败:', error)
      return { success: false, error: String(error) }
    }
  })

  ipcMain.handle('ai-simulator-stop', async () => {
    try {
      if (aiSimulator) {
        aiSimulator.disconnect()
        aiSimulator = null
      }
      return { success: true }
    } catch (error) {
      console.error('[AI Simulator] 停止失败:', error)
      return { success: false, error: String(error) }
    }
  })

  ipcMain.handle('ai-simulator-trigger-conversation', async (_event, userText: string, assistantText: string) => {
    try {
      if (aiSimulator) {
        aiSimulator.triggerConversation(userText, assistantText)
        return { success: true }
      }
      return { success: false, error: '模拟器未运行' }
    } catch (error) {
      console.error('[AI Simulator] 触发对话失败:', error)
      return { success: false, error: String(error) }
    }
  })

  ipcMain.handle('ai-simulator-set-online', async (_event, online: boolean) => {
    try {
      if (aiSimulator) {
        aiSimulator.setOnline(online)
        return { success: true }
      }
      return { success: false, error: '模拟器未运行' }
    } catch (error) {
      console.error('[AI Simulator] 设置状态失败:', error)
      return { success: false, error: String(error) }
    }
  })
}

app.on('window-all-closed', () => {
  if (wsServer) {
    wsServer.close()
  }
  if (aiSimulator) {
    aiSimulator.disconnect()
  }
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
