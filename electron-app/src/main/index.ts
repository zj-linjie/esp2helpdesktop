import { app, BrowserWindow, ipcMain, shell } from 'electron'
import path from 'path'
import { fileURLToPath } from 'url'
import { DeviceWebSocketServer } from './websocket.js'
import fs from 'fs/promises'
import crypto from 'crypto'
import WebSocket from 'ws'
import dotenv from 'dotenv'

// 加载环境变量
dotenv.config({ path: path.join(process.cwd(), '.env') })

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

let wsServer: DeviceWebSocketServer
let mainWindow: BrowserWindow | null = null

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

app.whenReady().then(() => {
  createWindow()
  wsServer = new DeviceWebSocketServer(8765)

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
}

app.on('window-all-closed', () => {
  if (wsServer) {
    wsServer.close()
  }
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
