import { WebSocketServer, WebSocket, type RawData } from 'ws'
import { SystemMonitor } from './system.js'
import { exec, execFile } from 'child_process'
import { promisify } from 'util'
import { access, open as openFile, stat as statFile } from 'fs/promises'
import { constants as fsConstants } from 'fs'
import crypto from 'crypto'

const execAsync = promisify(exec)
const execFileAsync = promisify(execFile)

interface ClientInfo {
  ws: WebSocket
  type: 'control_panel' | 'esp32_device' | 'unknown'
  deviceId?: string
  connectedAt: number
  lastHeartbeat: number
}

interface LaunchAppConfig {
  id: string
  name: string
  path: string
}

interface PendingRequest<T> {
  resolve: (value: T) => void
  reject: (reason?: unknown) => void
  timeout: NodeJS.Timeout
}

interface PendingSdPreviewBinary {
  requestId: string
  expectedLen: number
  path: string
  mime: string
}

type PhotoControlAction = 'prev' | 'next' | 'reload' | 'play' | 'pause' | 'set_interval'

export interface PhotoFrameSettings {
  folderPath: string
  slideshowInterval: number
  autoPlay: boolean
  theme: string
  maxFileSize: number
  autoCompress: boolean
  maxPhotoCount: number
  homeWallpaperPath: string
  clockWallpaperPath: string
}

const DEFAULT_PHOTO_FRAME_SETTINGS: PhotoFrameSettings = {
  folderPath: '/photos',
  slideshowInterval: 5,
  autoPlay: true,
  theme: 'dark-gallery',
  maxFileSize: 2,
  autoCompress: true,
  maxPhotoCount: 20,
  homeWallpaperPath: '',
  clockWallpaperPath: '',
}

type VoiceCommandAction = 'navigate' | 'launch_app' | 'unknown'

interface ParsedVoiceCommand {
  action: VoiceCommandAction
  page?: string
  appName?: string
  normalized: string
}

interface VoiceCommandExecutionResult {
  success: boolean
  action: VoiceCommandAction
  page?: string
  appName?: string
  appPath?: string
  message: string
  reason?: string
  command: string
  normalized: string
  timestamp: number
}

const VOICE_OPEN_KEYWORDS = ['open', 'launch', 'start', 'run', '打开', '启动', '运行']

const VOICE_PAGE_RULES: Array<{ page: string; keywords: string[] }> = [
  { page: 'home', keywords: ['主页', '返回', '回到主页', 'home', 'go home'] },
  { page: 'monitor', keywords: ['监控', '系统监控', 'monitor'] },
  { page: 'clock', keywords: ['时钟', '钟表', 'clock'] },
  { page: 'pomodoro', keywords: ['番茄钟', '计时器', '定时器', 'pomodoro', 'timer'] },
  { page: 'weather', keywords: ['天气', '天气预报', 'weather'] },
  { page: 'photo', keywords: ['相框', '照片', '图片', 'photo', 'photos'] },
  { page: 'apps', keywords: ['应用', '启动器', '应用列表', 'app launcher', 'apps', 'launcher'] },
  { page: 'settings', keywords: ['设置', '快捷设置', 'settings', 'setting'] },
  { page: 'music', keywords: ['音乐', '播放器', 'music', 'audio'] },
  { page: 'inbox', keywords: ['消息', '任务', '待办', 'inbox', 'task'] },
  { page: 'voice', keywords: ['语音', 'voice'] },
]

const VOICE_APP_ALIAS: Array<[string, string]> = [
  ['微信', 'WeChat'],
  ['wechat', 'WeChat'],
  ['浏览器', 'Safari'],
  ['safari', 'Safari'],
  ['谷歌浏览器', 'Google Chrome'],
  ['chrome', 'Google Chrome'],
  ['firefox', 'Firefox'],
  ['火狐', 'Firefox'],
  ['终端', 'Terminal'],
  ['terminal', 'Terminal'],
  ['访达', 'Finder'],
  ['finder', 'Finder'],
  ['邮件', 'Mail'],
  ['mail', 'Mail'],
  ['日历', 'Calendar'],
  ['calendar', 'Calendar'],
  ['备忘录', 'Notes'],
  ['notes', 'Notes'],
  ['音乐', 'Music'],
  ['music', 'Music'],
  ['照片', 'Photos'],
  ['photos', 'Photos'],
  ['系统设置', 'System Settings'],
  ['设置', 'System Settings'],
  ['vscode', 'Visual Studio Code'],
  ['vs code', 'Visual Studio Code'],
  ['visual studio code', 'Visual Studio Code'],
  ['xcode', 'Xcode'],
  ['cursor', 'Cursor'],
]

const includesAny = (text: string, keywords: string[]): boolean => {
  for (const keyword of keywords) {
    if (keyword && text.includes(keyword)) {
      return true
    }
  }
  return false
}

const extractVoiceAppName = (normalizedText: string): string | null => {
  let cleaned = normalizedText
    .replace(/打开|启动|运行/g, ' ')
    .replace(/\b(open|launch|start|run)\b/g, ' ')
    .replace(/[。，、！？,.!?]/g, ' ')
    .trim()

  if (!cleaned) {
    return null
  }

  for (const [alias, appName] of VOICE_APP_ALIAS) {
    if (cleaned.includes(alias)) {
      return appName
    }
  }

  cleaned = cleaned.replace(/\b(app|application|应用)\b/g, ' ').trim()
  if (!cleaned) {
    return null
  }

  if (!/[a-z0-9]/i.test(cleaned)) {
    return null
  }

  const candidate = cleaned
    .split(/\s+/)
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(' ')
    .trim()

  return candidate.length > 0 ? candidate : null
}

const parseVoiceCommand = (rawText: string): ParsedVoiceCommand => {
  const normalized = rawText.trim().toLowerCase()
  if (!normalized) {
    return { action: 'unknown', normalized }
  }

  for (const rule of VOICE_PAGE_RULES) {
    if (includesAny(normalized, rule.keywords)) {
      return {
        action: 'navigate',
        page: rule.page,
        normalized,
      }
    }
  }

  if (includesAny(normalized, VOICE_OPEN_KEYWORDS)) {
    const appName = extractVoiceAppName(normalized)
    if (appName) {
      return {
        action: 'launch_app',
        appName,
        normalized,
      }
    }
  }

  return { action: 'unknown', normalized }
}

interface VoiceStreamChunkMeta {
  seq: number
  len: number
  level: number
  timestamp: number
}

interface VoiceStreamState {
  deviceId: string
  streamId: string
  sampleRate: number
  startedAt: number
  expectedSeq: number
  pendingMeta: VoiceStreamChunkMeta | null
  chunksReceived: number
  bytesReceived: number
  lastLevel: number
  lastFinalText: string
  asr: AliyunSpeechTranscriber | null
}

interface VoiceRecognitionPacket {
  text: string
  isFinal: boolean
  name: string
}

const aliyunPercentEncode = (value: string): string =>
  encodeURIComponent(value)
    .replace(/\+/g, '%20')
    .replace(/\*/g, '%2A')
    .replace(/%7E/g, '~')

const aliyunSignParams = (params: Record<string, string>, secret: string): string => {
  const keys = Object.keys(params).sort()
  const query = keys.map((key) => `${aliyunPercentEncode(key)}=${aliyunPercentEncode(params[key])}`).join('&')
  const stringToSign = `GET&%2F&${aliyunPercentEncode(query)}`
  return crypto.createHmac('sha1', `${secret}&`).update(stringToSign).digest('base64')
}

const fetchAliyunToken = async (): Promise<string | null> => {
  const staticToken = process.env.ALIYUN_NLS_TOKEN?.trim() || ''
  if (staticToken) {
    return staticToken
  }

  const accessKeyId = process.env.ALIYUN_ACCESS_KEY_ID?.trim() || ''
  const accessKeySecret = process.env.ALIYUN_ACCESS_KEY_SECRET?.trim() || ''
  if (!accessKeyId || !accessKeySecret) {
    return null
  }

  const tokenEndpoint = process.env.ALIYUN_NLS_TOKEN_URL?.trim() || 'https://nls-meta.cn-shanghai.aliyuncs.com/'
  const params: Record<string, string> = {
    AccessKeyId: accessKeyId,
    Action: 'CreateToken',
    Format: 'JSON',
    RegionId: 'cn-shanghai',
    SignatureMethod: 'HMAC-SHA1',
    SignatureNonce: `${Date.now()}${Math.random().toString(16).slice(2)}`,
    SignatureVersion: '1.0',
    Timestamp: new Date().toISOString(),
    Version: '2019-02-28',
  }
  params.Signature = aliyunSignParams(params, accessKeySecret)

  const query = Object.keys(params)
    .map((key) => `${aliyunPercentEncode(key)}=${aliyunPercentEncode(params[key])}`)
    .join('&')
  const response = await fetch(`${tokenEndpoint}?${query}`)
  if (!response.ok) {
    throw new Error(`fetch token failed: HTTP ${response.status}`)
  }
  const json = (await response.json()) as {
    Token?: { Id?: string }
    Message?: string
    Code?: string
  }
  const token = typeof json?.Token?.Id === 'string' ? json.Token.Id : ''
  if (!token) {
    const reason = typeof json?.Message === 'string' ? json.Message : (typeof json?.Code === 'string' ? json.Code : 'token missing')
    throw new Error(`fetch token failed: ${reason}`)
  }
  return token
}

class AliyunSpeechTranscriber {
  private socket: WebSocket | null = null
  private taskId = ''
  private ready = false
  private stopped = false
  private queue: Buffer[] = []
  private readonly queueLimit = 240
  private readonly appKey: string
  private readonly wsEndpoint: string
  private readonly sampleRate: number
  private readonly onResult: (packet: VoiceRecognitionPacket) => void
  private readonly onError: (reason: string) => void
  private readonly onClosed: () => void

  constructor(options: {
    sampleRate: number
    onResult: (packet: VoiceRecognitionPacket) => void
    onError: (reason: string) => void
    onClosed: () => void
  }) {
    this.sampleRate = options.sampleRate
    this.onResult = options.onResult
    this.onError = options.onError
    this.onClosed = options.onClosed
    this.appKey = process.env.ALIYUN_APP_KEY?.trim() || ''
    this.wsEndpoint = process.env.ALIYUN_NLS_WS_URL?.trim() || 'wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1'
  }

  public async start(): Promise<void> {
    if (this.stopped) {
      throw new Error('asr session already stopped')
    }
    if (!this.appKey) {
      throw new Error('ALIYUN_APP_KEY missing')
    }

    const token = await fetchAliyunToken()
    if (!token) {
      throw new Error('aliyun token unavailable')
    }

    this.taskId = crypto.randomUUID().replace(/-/g, '')
    const wsUrl = `${this.wsEndpoint}?token=${encodeURIComponent(token)}&appkey=${encodeURIComponent(this.appKey)}`

    await new Promise<void>((resolve, reject) => {
      let settled = false
      const settleOk = () => {
        if (settled) return
        settled = true
        clearTimeout(startTimeout)
        resolve()
      }
      const settleErr = (reason: string) => {
        if (settled) return
        settled = true
        clearTimeout(startTimeout)
        reject(new Error(reason))
      }
      const startTimeout = setTimeout(() => {
        settleErr('asr start timeout')
      }, 8000)

      const ws = new WebSocket(wsUrl)
      this.socket = ws

      ws.on('open', () => {
        const startMessage = {
          header: {
            appkey: this.appKey,
            namespace: 'SpeechTranscriber',
            name: 'StartTranscription',
            task_id: this.taskId,
            message_id: crypto.randomUUID().replace(/-/g, ''),
          },
          payload: {
            format: 'pcm',
            sample_rate: this.sampleRate,
            enable_intermediate_result: true,
            enable_punctuation_prediction: true,
            enable_inverse_text_normalization: true,
          },
        }
        ws.send(JSON.stringify(startMessage))
      })

      ws.on('message', (rawData: RawData) => {
        let payload = ''
        try {
          payload = AliyunSpeechTranscriber.rawDataToText(rawData)
          const message = JSON.parse(payload) as Record<string, any>
          const name = typeof message?.header?.name === 'string' ? message.header.name : ''

          if (name === 'TranscriptionStarted') {
            this.ready = true
            this.flushQueue()
            settleOk()
            return
          }

          if (name === 'TaskFailed') {
            const reason =
              message?.payload?.message ||
              message?.payload?.status_text ||
              message?.header?.status_text ||
              'asr task failed'
            this.onError(String(reason))
            return
          }

          const resultText =
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

          if (typeof resultText === 'string' && resultText.trim().length > 0) {
            this.onResult({
              text: resultText.trim(),
              isFinal,
              name,
            })
          }
        } catch (error) {
          const reason = error instanceof Error ? error.message : String(error)
          this.onError(`asr parse failed: ${reason}`)
        }
      })

      ws.on('close', () => {
        this.ready = false
        this.socket = null
        if (!settled) {
          settleErr('asr socket closed before ready')
        }
        this.onClosed()
      })

      ws.on('error', (error: Error) => {
        const reason = error?.message || 'asr websocket error'
        this.onError(reason)
        if (!settled) {
          settleErr(reason)
        }
      })
    })
  }

  public pushAudio(chunk: Buffer): boolean {
    if (this.stopped) {
      return false
    }
    if (this.ready && this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(chunk)
      return true
    }
    if (this.queue.length >= this.queueLimit) {
      this.queue.shift()
    }
    this.queue.push(Buffer.from(chunk))
    return true
  }

  public stop(): void {
    if (this.stopped) {
      return
    }
    this.stopped = true
    const ws = this.socket
    if (ws && ws.readyState === WebSocket.OPEN && this.taskId) {
      const stopMessage = {
        header: {
          appkey: this.appKey,
          namespace: 'SpeechTranscriber',
          name: 'StopTranscription',
          task_id: this.taskId,
          message_id: crypto.randomUUID().replace(/-/g, ''),
        },
        payload: {},
      }
      try {
        ws.send(JSON.stringify(stopMessage))
      } catch {
        // ignore send error on shutdown
      }
    }
    try {
      ws?.close()
    } catch {
      // ignore close errors
    }
    this.ready = false
    this.queue.length = 0
    this.socket = null
  }

  private flushQueue() {
    if (!this.ready || !this.socket || this.socket.readyState !== WebSocket.OPEN) {
      return
    }
    while (this.queue.length > 0 && this.socket.readyState === WebSocket.OPEN) {
      const chunk = this.queue.shift()
      if (chunk) {
        this.socket.send(chunk)
      }
    }
  }

  private static rawDataToText(rawData: RawData): string {
    if (typeof rawData === 'string') {
      return rawData
    }
    if (Buffer.isBuffer(rawData)) {
      return rawData.toString('utf8')
    }
    if (Array.isArray(rawData)) {
      return Buffer.concat(rawData).toString('utf8')
    }
    return Buffer.from(rawData as ArrayBufferLike).toString('utf8')
  }
}

export class DeviceWebSocketServer {
  private wss: WebSocketServer
  private clients: Map<WebSocket, ClientInfo> = new Map()
  private voiceStreams: Map<WebSocket, VoiceStreamState> = new Map()
  private systemMonitor: SystemMonitor
  private systemBroadcastInterval: NodeJS.Timeout | null = null
  private heartbeatMonitorInterval: NodeJS.Timeout | null = null
  private customLaunchApps: LaunchAppConfig[] = []
  private photoFrameSettings: PhotoFrameSettings = { ...DEFAULT_PHOTO_FRAME_SETTINGS }
  private pendingSdListRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdDeleteRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdUploadBeginRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdUploadChunkRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdUploadCommitRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdPreviewRequests: Map<string, PendingRequest<any>> = new Map()
  private pendingSdPreviewRequestSockets: Map<string, WebSocket> = new Map()
  private pendingSdPreviewBinaryBySocket: Map<WebSocket, PendingSdPreviewBinary> = new Map()

  constructor(port: number = 8765) {
    this.wss = new WebSocketServer({ port })
    this.systemMonitor = new SystemMonitor()
    this.setupServer()
    this.startSystemBroadcast()
    this.startHeartbeatMonitor()
  }

  private setupServer() {
    this.wss.on('connection', (ws: WebSocket) => {
      console.log('新客户端连接，等待握手...')

      // 初始化客户端信息（类型未知）
      const clientInfo: ClientInfo = {
        ws,
        type: 'unknown',
        connectedAt: Date.now(),
        lastHeartbeat: Date.now()
      }
      this.clients.set(ws, clientInfo)

      ws.on('message', (rawData: RawData, isBinary: boolean) => {
        if (isBinary) {
          this.handleBinaryMessage(ws, this.rawDataToBuffer(rawData))
          return
        }

        try {
          const message = JSON.parse(this.rawDataToBuffer(rawData).toString('utf8'))
          this.handleMessage(ws, message)
        } catch (error) {
          console.error('解析消息失败:', error)
        }
      })

      ws.on('close', () => {
        this.stopVoiceStreamSession(ws, 'socket closed', false)
        const relatedPreviewRequestIds: string[] = []
        this.pendingSdPreviewRequestSockets.forEach((socket, requestId) => {
          if (socket === ws) {
            relatedPreviewRequestIds.push(requestId)
          }
        })
        for (const requestId of relatedPreviewRequestIds) {
          this.pendingSdPreviewRequestSockets.delete(requestId)
          const pending = this.pendingSdPreviewRequests.get(requestId)
          if (pending) {
            clearTimeout(pending.timeout)
            this.pendingSdPreviewRequests.delete(requestId)
            pending.resolve({
              success: false,
              requestId,
              reason: 'device disconnected',
            })
          }
        }
        this.pendingSdPreviewBinaryBySocket.delete(ws)
        const client = this.clients.get(ws)
        if (client) {
          if (client.type === 'esp32_device' && client.deviceId) {
            console.log(`ESP32 设备已断开: ${client.deviceId}`)
            this.notifyDeviceDisconnected(client.deviceId)
          } else if (client.type === 'control_panel') {
            console.log('控制面板已断开')
          }
          this.clients.delete(ws)
        }
      })

      ws.on('error', (error) => {
        console.error('WebSocket 错误:', error)
      })
    })

    console.log(`WebSocket 服务器运行在端口 ${this.wss.options.port}`)
  }

  public setCustomLaunchApps(apps: Array<Partial<LaunchAppConfig>>): void {
    const normalized: LaunchAppConfig[] = []
    const seenPaths = new Set<string>()

    for (const item of apps) {
      const name = typeof item.name === 'string' ? item.name.trim() : ''
      const path = typeof item.path === 'string' ? item.path.trim() : ''
      if (!name || !path || seenPaths.has(path)) {
        continue
      }
      seenPaths.add(path)
      normalized.push({
        id: typeof item.id === 'string' && item.id.trim() ? item.id.trim() : `custom-${normalized.length + 1}`,
        name,
        path
      })
      if (normalized.length >= 64) {
        break
      }
    }

    this.customLaunchApps = normalized
    console.log(`[应用列表配置] 自定义应用已更新: ${this.customLaunchApps.length}`)
  }

  public setPhotoFrameSettings(settings: Partial<PhotoFrameSettings> | null | undefined): void {
    if (!settings || typeof settings !== 'object') {
      this.photoFrameSettings = { ...DEFAULT_PHOTO_FRAME_SETTINGS }
      return
    }

    const normalizeWallpaperPath = (value: unknown): string => {
      if (typeof value !== 'string') {
        return ''
      }
      const trimmed = value.trim()
      if (!trimmed.startsWith('/')) {
        return ''
      }
      const lower = trimmed.toLowerCase()
      if (!lower.endsWith('.mjpeg') && !lower.endsWith('.mjpg')) {
        return ''
      }
      return trimmed
    }

    const slideshowIntervalRaw = Number(settings.slideshowInterval)
    const maxFileSizeRaw = Number(settings.maxFileSize)
    const maxPhotoCountRaw = Number(settings.maxPhotoCount)

    this.photoFrameSettings = {
      folderPath: typeof settings.folderPath === 'string' && settings.folderPath.trim().length > 0
        ? settings.folderPath.trim()
        : DEFAULT_PHOTO_FRAME_SETTINGS.folderPath,
      slideshowInterval: Number.isFinite(slideshowIntervalRaw)
        ? Math.max(3, Math.min(30, Math.round(slideshowIntervalRaw)))
        : DEFAULT_PHOTO_FRAME_SETTINGS.slideshowInterval,
      autoPlay: settings.autoPlay !== undefined ? Boolean(settings.autoPlay) : DEFAULT_PHOTO_FRAME_SETTINGS.autoPlay,
      theme: typeof settings.theme === 'string' && settings.theme.trim().length > 0
        ? settings.theme.trim()
        : DEFAULT_PHOTO_FRAME_SETTINGS.theme,
      maxFileSize: Number.isFinite(maxFileSizeRaw)
        ? Math.max(1, Math.min(5, Math.round(maxFileSizeRaw * 2) / 2))
        : DEFAULT_PHOTO_FRAME_SETTINGS.maxFileSize,
      autoCompress: settings.autoCompress !== undefined ? Boolean(settings.autoCompress) : DEFAULT_PHOTO_FRAME_SETTINGS.autoCompress,
      maxPhotoCount: Number.isFinite(maxPhotoCountRaw)
        ? Math.max(1, Math.min(100, Math.round(maxPhotoCountRaw)))
        : DEFAULT_PHOTO_FRAME_SETTINGS.maxPhotoCount,
      homeWallpaperPath: normalizeWallpaperPath(settings.homeWallpaperPath),
      clockWallpaperPath: normalizeWallpaperPath(settings.clockWallpaperPath),
    }

    console.log(
      `[相册设置] 已更新: interval=${this.photoFrameSettings.slideshowInterval}s autoPlay=${this.photoFrameSettings.autoPlay} theme=${this.photoFrameSettings.theme} home=${this.photoFrameSettings.homeWallpaperPath || 'auto'} clock=${this.photoFrameSettings.clockWallpaperPath || 'auto'}`
    )
  }

  public getPhotoFrameSettings(): PhotoFrameSettings {
    return { ...this.photoFrameSettings }
  }

  public broadcastPhotoFrameSettings(): void {
    this.broadcastToDevices({
      type: 'photo_settings',
      data: {
        ...this.photoFrameSettings,
        timestamp: Date.now(),
      },
    })
  }

  private findEsp32Client(targetDeviceId?: string): ClientInfo | null {
    for (const client of this.clients.values()) {
      if (client.type !== 'esp32_device' || !client.deviceId) {
        continue
      }
      if (targetDeviceId && client.deviceId !== targetDeviceId) {
        continue
      }
      return client
    }
    return null
  }

  private buildSdChunkRequestKey(uploadId: string, seq: number): string {
    return `${uploadId}#${seq}`
  }

  public async requestSdFileList(
    targetDeviceId?: string,
    timeoutMs: number = 12000,
    offset: number = 0,
    limit: number = 24
  ): Promise<any> {
    const targetClient = this.findEsp32Client(targetDeviceId)
    if (!targetClient || !targetClient.ws || targetClient.ws.readyState !== WebSocket.OPEN) {
      return {
        success: false,
        reason: targetDeviceId ? `device not online: ${targetDeviceId}` : 'no online esp32 device',
      }
    }

    const normalizedOffset = Number.isFinite(offset) ? Math.max(0, Math.floor(offset)) : 0
    const normalizedLimit = Number.isFinite(limit) ? Math.max(1, Math.min(24, Math.floor(limit))) : 24
    const requestId = `sd-list-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`
    console.log(
      `[SD] request list -> device=${targetClient.deviceId} requestId=${requestId} offset=${normalizedOffset} limit=${normalizedLimit} timeout=${timeoutMs}ms`
    )
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingSdListRequests.delete(requestId)
        resolve({
          success: false,
          reason: `sd list timeout (${timeoutMs}ms)`,
          requestId,
          deviceId: targetClient.deviceId,
        })
      }, timeoutMs)

      this.pendingSdListRequests.set(requestId, { resolve, reject, timeout })
      this.sendMessage(targetClient.ws, {
        type: 'sd_list_request',
        data: {
          requestId,
          deviceId: targetClient.deviceId,
          offset: normalizedOffset,
          limit: normalizedLimit,
          timestamp: Date.now(),
        },
      })
    })
  }

  public async requestSdDelete(filePath: string, targetDeviceId?: string, timeoutMs: number = 6000): Promise<any> {
    const targetClient = this.findEsp32Client(targetDeviceId)
    if (!targetClient || !targetClient.ws || targetClient.ws.readyState !== WebSocket.OPEN) {
      return {
        success: false,
        reason: targetDeviceId ? `device not online: ${targetDeviceId}` : 'no online esp32 device',
      }
    }

    const trimmedPath = typeof filePath === 'string' ? filePath.trim() : ''
    if (!trimmedPath.startsWith('/')) {
      return {
        success: false,
        reason: 'invalid path',
      }
    }

    const requestId = `sd-del-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingSdDeleteRequests.delete(requestId)
        resolve({
          success: false,
          reason: `sd delete timeout (${timeoutMs}ms)`,
          requestId,
          path: trimmedPath,
          deviceId: targetClient.deviceId,
        })
      }, timeoutMs)

      this.pendingSdDeleteRequests.set(requestId, { resolve, reject, timeout })
      this.sendMessage(targetClient.ws, {
        type: 'sd_delete_request',
        data: {
          requestId,
          deviceId: targetClient.deviceId,
          path: trimmedPath,
          timestamp: Date.now(),
        },
      })
    })
  }

  public async requestSdPreview(filePath: string, targetDeviceId?: string, timeoutMs: number = 9000): Promise<any> {
    const targetClient = this.findEsp32Client(targetDeviceId)
    if (!targetClient || !targetClient.ws || targetClient.ws.readyState !== WebSocket.OPEN) {
      return {
        success: false,
        reason: targetDeviceId ? `device not online: ${targetDeviceId}` : 'no online esp32 device',
      }
    }
    if (this.voiceStreams.has(targetClient.ws)) {
      return {
        success: false,
        reason: 'preview unavailable while voice streaming is active',
      }
    }

    const trimmedPath = typeof filePath === 'string' ? filePath.trim() : ''
    const lowerPath = trimmedPath.toLowerCase()
    if (!trimmedPath.startsWith('/') || (!lowerPath.endsWith('.mjpeg') && !lowerPath.endsWith('.mjpg'))) {
      return {
        success: false,
        reason: 'invalid mjpeg path',
      }
    }

    const requestId = `sd-preview-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingSdPreviewRequests.delete(requestId)
        this.pendingSdPreviewRequestSockets.delete(requestId)
        const previewState = this.pendingSdPreviewBinaryBySocket.get(targetClient.ws)
        if (previewState && previewState.requestId === requestId) {
          this.pendingSdPreviewBinaryBySocket.delete(targetClient.ws)
        }
        resolve({
          success: false,
          reason: `sd preview timeout (${timeoutMs}ms)`,
          requestId,
          path: trimmedPath,
          deviceId: targetClient.deviceId,
        })
      }, timeoutMs)

      this.pendingSdPreviewRequests.set(requestId, { resolve, reject, timeout })
      this.pendingSdPreviewRequestSockets.set(requestId, targetClient.ws)
      this.sendMessage(targetClient.ws, {
        type: 'sd_preview_request',
        data: {
          requestId,
          deviceId: targetClient.deviceId,
          path: trimmedPath,
          timestamp: Date.now(),
        },
      })
    })
  }

  public async uploadFileToSd(options: {
    sourcePath: string
    targetPath: string
    targetDeviceId?: string
    chunkSize?: number
    overwrite?: boolean
    timeoutMs?: number
    onProgress?: (progress: {
      uploadId: string
      deviceId?: string
      targetPath: string
      bytesSent: number
      totalBytes: number
      seq: number
    }) => void
  }): Promise<any> {
    const sourcePath = options.sourcePath
    const targetPath = typeof options.targetPath === 'string' ? options.targetPath.trim() : ''
    const timeoutMs = Number.isFinite(options.timeoutMs) ? Number(options.timeoutMs) : 12000
    const overwrite = options.overwrite !== undefined ? Boolean(options.overwrite) : true

    if (!sourcePath || !targetPath.startsWith('/')) {
      return { success: false, reason: 'invalid sourcePath/targetPath' }
    }

    const targetClient = this.findEsp32Client(options.targetDeviceId)
    if (!targetClient || !targetClient.ws || targetClient.ws.readyState !== WebSocket.OPEN) {
      return {
        success: false,
        reason: options.targetDeviceId ? `device not online: ${options.targetDeviceId}` : 'no online esp32 device',
      }
    }

    let sourceStat
    try {
      sourceStat = await statFile(sourcePath)
    } catch (error) {
      return { success: false, reason: `source stat failed: ${String(error)}` }
    }
    if (!sourceStat.isFile()) {
      return { success: false, reason: 'source is not file' }
    }

    const fileSize = sourceStat.size
    const chunkSizeRaw = Number(options.chunkSize)
    const chunkSize = Number.isFinite(chunkSizeRaw)
      ? Math.max(512, Math.min(4096, Math.floor(chunkSizeRaw)))
      : 4096

    const uploadId = `upload-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`
    const beginPromise = new Promise<any>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingSdUploadBeginRequests.delete(uploadId)
        resolve({
          success: false,
          reason: `sd upload begin timeout (${timeoutMs}ms)`,
          uploadId,
        })
      }, timeoutMs)
      this.pendingSdUploadBeginRequests.set(uploadId, { resolve, reject, timeout })
    })

    this.sendMessage(targetClient.ws, {
      type: 'sd_upload_begin',
      data: {
        uploadId,
        deviceId: targetClient.deviceId,
        path: targetPath,
        size: fileSize,
        chunkSize,
        overwrite,
        timestamp: Date.now(),
      },
    })

    const beginAck = await beginPromise
    if (!beginAck?.success) {
      console.error(
        `[SD upload] begin failed device=${targetClient.deviceId} path=${targetPath} reason=${beginAck?.reason || 'unknown'}`
      )
      return {
        success: false,
        reason: beginAck?.reason || 'sd upload begin failed',
        uploadId,
      }
    }

    const fileHandle = await openFile(sourcePath, 'r')
    let position = 0
    let seq = 0
    let lastProgressEmitMs = 0

    try {
      options.onProgress?.({
        uploadId,
        deviceId: targetClient.deviceId,
        targetPath,
        bytesSent: 0,
        totalBytes: fileSize,
        seq: 0,
      })
    } catch {
      // ignore callback errors
    }

    try {
      while (position < fileSize) {
        const expectedBytes = Math.min(chunkSize, fileSize - position)
        const chunkBuffer = Buffer.allocUnsafe(expectedBytes)
        const { bytesRead } = await fileHandle.read(chunkBuffer, 0, expectedBytes, position)
        if (bytesRead <= 0) {
          throw new Error('read failed before reaching expected file size')
        }

        const requestKey = this.buildSdChunkRequestKey(uploadId, seq)
        const chunkAckPromise = new Promise<any>((resolve, reject) => {
          const timeout = setTimeout(() => {
            this.pendingSdUploadChunkRequests.delete(requestKey)
            resolve({
              success: false,
              reason: `sd upload chunk timeout (${timeoutMs}ms)`,
              uploadId,
              seq,
            })
          }, timeoutMs)
          this.pendingSdUploadChunkRequests.set(requestKey, { resolve, reject, timeout })
        })

        this.sendMessage(targetClient.ws, {
          type: 'sd_upload_chunk_meta',
          data: {
            uploadId,
            seq,
            len: bytesRead,
            timestamp: Date.now(),
          },
        })
        targetClient.ws.send(chunkBuffer.subarray(0, bytesRead), { binary: true })

        const chunkAck = await chunkAckPromise
        if (!chunkAck?.success) {
          console.error(
            `[SD upload] chunk failed device=${targetClient.deviceId} path=${targetPath} seq=${seq} reason=${chunkAck?.reason || 'unknown'}`
          )
          throw new Error(chunkAck?.reason || `chunk ${seq} failed`)
        }

        position += bytesRead
        seq += 1

        const now = Date.now()
        const shouldEmit = position >= fileSize || (now - lastProgressEmitMs) >= 120
        if (shouldEmit) {
          lastProgressEmitMs = now
          try {
            options.onProgress?.({
              uploadId,
              deviceId: targetClient.deviceId,
              targetPath,
              bytesSent: position,
              totalBytes: fileSize,
              seq,
            })
          } catch {
            // ignore callback errors
          }
        }
      }
    } catch (error) {
      try {
        this.sendMessage(targetClient.ws, {
          type: 'sd_upload_abort',
          data: {
            uploadId,
            reason: String(error),
            timestamp: Date.now(),
          },
        })
      } catch {
        // ignore abort send errors
      }

      return {
        success: false,
        reason: String(error),
        uploadId,
      }
    } finally {
      await fileHandle.close()
    }

    const commitPromise = new Promise<any>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingSdUploadCommitRequests.delete(uploadId)
        resolve({
          success: false,
          reason: `sd upload commit timeout (${timeoutMs}ms)`,
          uploadId,
        })
      }, timeoutMs)
      this.pendingSdUploadCommitRequests.set(uploadId, { resolve, reject, timeout })
    })

    this.sendMessage(targetClient.ws, {
      type: 'sd_upload_commit',
      data: {
        uploadId,
        expectedSize: fileSize,
        timestamp: Date.now(),
      },
    })

    const commitAck = await commitPromise
    if (!commitAck?.success) {
      console.error(
        `[SD upload] commit failed device=${targetClient.deviceId} path=${targetPath} reason=${commitAck?.reason || 'unknown'}`
      )
      return {
        success: false,
        reason: commitAck?.reason || 'sd upload commit failed',
        uploadId,
      }
    }

    return {
      success: true,
      uploadId,
      targetPath: commitAck.path || targetPath,
      bytes: fileSize,
      deviceId: targetClient.deviceId,
    }
  }

  private async startSystemBroadcast() {
    // 每 5 秒广播系统数据到所有客户端（ESP32 设备和控制面板）
    this.systemBroadcastInterval = setInterval(async () => {
      const systemInfo = await this.systemMonitor.getSystemInfo()

      const message = {
        type: 'system_stats',
        data: {
          cpu: systemInfo.cpu.usage,
          memory: systemInfo.memory.percentage,
          network: {
            upload: systemInfo.network.upload,
            download: systemInfo.network.download
          },
          timestamp: Date.now()
        }
      }

      // 广播到所有客户端（ESP32 设备和控制面板）
      console.log(`[广播] system_stats - CPU: ${systemInfo.cpu.usage.toFixed(1)}%, 内存: ${systemInfo.memory.percentage.toFixed(1)}%, 客户端数: ${this.clients.size}`)
      this.broadcast(message)
    }, 5000)
  }

  private startHeartbeatMonitor() {
    // 每 10 秒检查一次设备心跳
    this.heartbeatMonitorInterval = setInterval(() => {
      const now = Date.now()
      this.clients.forEach((client, ws) => {
        if (client.type === 'esp32_device' && client.deviceId) {
          // 15 秒无心跳，标记为离线
          if (now - client.lastHeartbeat > 15000) {
            console.log(`设备心跳超时: ${client.deviceId}`)
            this.notifyDeviceDisconnected(client.deviceId)
            ws.close()
          }
        }
      })
    }, 10000)
  }

  private handleMessage(ws: WebSocket, message: any) {
    const client = this.clients.get(ws)
    if (!client) return

    const messageType = typeof message?.type === 'string' ? message.type : 'unknown'
    if (messageType !== 'heartbeat' && messageType !== 'photo_state') {
      console.log('收到消息:', messageType, message.data || '')
    }

    switch (messageType) {
      case 'handshake':
        this.handleHandshake(ws, client, message)
        break

      case 'heartbeat':
        this.handleHeartbeat(ws, client, message)
        break

      case 'ai_status':
        this.handleAIStatus(ws, client, message)
        break

      case 'ai_conversation':
        this.handleAIConversation(ws, client, message)
        break

      case 'ai_config':
        this.handleAIConfig(ws, client, message)
        break

      case 'task_action':
        this.handleTaskAction(ws, client, message)
        break

      case 'weather_request':
        this.handleWeatherRequest(ws, client, message)
        break

      case 'app_list_request':
        this.handleAppListRequest(ws, client, message)
        break

      case 'launch_app':
        this.handleLaunchApp(ws, client, message)
        break

      case 'voice_command':
        this.handleVoiceCommand(ws, client, message)
        break

      case 'voice_command_from_panel':
        this.handleVoiceCommandFromControlPanel(ws, client, message)
        break

      case 'voice_stream_start':
        this.handleVoiceStreamStart(ws, client, message)
        break

      case 'voice_stream_chunk_meta':
        this.handleVoiceStreamChunkMeta(ws, client, message)
        break

      case 'voice_stream_stop':
        this.handleVoiceStreamStop(ws, client, message)
        break

      case 'photo_settings_request':
        this.handlePhotoSettingsRequest(ws, client)
        break

      case 'photo_control':
        this.handlePhotoControl(ws, client, message)
        break

      case 'photo_state':
        this.handlePhotoState(ws, client, message)
        break

      case 'sd_list_response':
        this.handleSdListResponse(client, message)
        break

      case 'sd_delete_response':
        this.handleSdDeleteResponse(client, message)
        break

      case 'sd_upload_begin_ack':
        this.handleSdUploadBeginAck(client, message)
        break

      case 'sd_upload_chunk_ack':
        this.handleSdUploadChunkAck(client, message)
        break

      case 'sd_upload_commit_ack':
        this.handleSdUploadCommitAck(client, message)
        break

      case 'sd_preview_response':
        this.handleSdPreviewResponse(client, message)
        break

      default:
        console.log('未知消息类型:', message.type)
    }
  }

  private rawDataToBuffer(rawData: RawData): Buffer {
    if (Buffer.isBuffer(rawData)) {
      return rawData
    }
    if (Array.isArray(rawData)) {
      return Buffer.concat(rawData)
    }
    if (rawData instanceof ArrayBuffer) {
      return Buffer.from(rawData)
    }
    return Buffer.from(rawData as ArrayBufferLike)
  }

  private sendVoiceStreamAck(
    ws: WebSocket,
    payload: {
      streamId: string
      success: boolean
      status: 'accepted' | 'ready' | 'stopped' | 'error'
      reason?: string
      seq?: number
      expectedLen?: number
      actualLen?: number
      chunksReceived?: number
      bytesReceived?: number
      text?: string
      isFinal?: boolean
    }
  ) {
    this.sendMessage(ws, {
      type: 'voice_stream_ack',
      data: {
        streamId: payload.streamId,
        success: payload.success,
        status: payload.status,
        reason: payload.reason || '',
        seq: payload.seq,
        expectedLen: payload.expectedLen,
        actualLen: payload.actualLen,
        chunksReceived: payload.chunksReceived,
        bytesReceived: payload.bytesReceived,
        text: payload.text,
        isFinal: payload.isFinal,
        timestamp: Date.now(),
      },
    })
  }

  private handleBinaryMessage(ws: WebSocket, data: Buffer) {
    const pendingPreview = this.pendingSdPreviewBinaryBySocket.get(ws)
    if (pendingPreview) {
      this.pendingSdPreviewBinaryBySocket.delete(ws)
      this.pendingSdPreviewRequestSockets.delete(pendingPreview.requestId)
      const pending = this.pendingSdPreviewRequests.get(pendingPreview.requestId)
      if (!pending) {
        return
      }
      clearTimeout(pending.timeout)
      this.pendingSdPreviewRequests.delete(pendingPreview.requestId)

      if (data.length !== pendingPreview.expectedLen) {
        pending.resolve({
          success: false,
          requestId: pendingPreview.requestId,
          path: pendingPreview.path,
          reason: `preview binary length mismatch expected=${pendingPreview.expectedLen} actual=${data.length}`,
        })
        return
      }

      pending.resolve({
        success: true,
        requestId: pendingPreview.requestId,
        path: pendingPreview.path,
        mime: pendingPreview.mime,
        bytes: data.length,
        buffer: data,
      })
      return
    }

    const state = this.voiceStreams.get(ws)
    if (!state || !state.pendingMeta) {
      return
    }

    const pendingMeta = state.pendingMeta
    state.pendingMeta = null

    if (data.length !== pendingMeta.len) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId: state.streamId,
          seq: pendingMeta.seq,
          success: false,
          reason: 'binary length mismatch',
          expectedLen: pendingMeta.len,
          actualLen: data.length,
          timestamp: Date.now(),
        },
      })
      this.stopVoiceStreamSession(ws, 'binary length mismatch', true)
      return
    }

    if (!state.asr || !state.asr.pushAudio(data)) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId: state.streamId,
          seq: pendingMeta.seq,
          success: false,
          reason: 'asr stream unavailable',
          timestamp: Date.now(),
        },
      })
      this.stopVoiceStreamSession(ws, 'asr unavailable', true)
      return
    }

    state.expectedSeq = pendingMeta.seq + 1
    state.lastLevel = pendingMeta.level
    state.bytesReceived += data.length
    state.chunksReceived += 1

    this.sendMessage(ws, {
      type: 'voice_stream_chunk_ack',
      data: {
        streamId: state.streamId,
        seq: pendingMeta.seq,
        success: true,
        len: data.length,
        level: pendingMeta.level,
        chunksReceived: state.chunksReceived,
        bytesReceived: state.bytesReceived,
        timestamp: Date.now(),
      },
    })
  }

  private handleVoiceStreamStart(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') {
      return
    }

    const data = message?.data ?? {}
    const streamId = typeof data.streamId === 'string' ? data.streamId.trim() : ''
    const sampleRateRaw = Number(data.sampleRate)
    const sampleRate = Number.isFinite(sampleRateRaw) ? Math.max(8000, Math.min(48000, Math.floor(sampleRateRaw))) : 16000

    if (!streamId) {
      this.sendVoiceStreamAck(ws, {
        streamId: '',
        success: false,
        status: 'error',
        reason: 'streamId missing',
      })
      return
    }

    if (this.voiceStreams.has(ws)) {
      this.stopVoiceStreamSession(ws, 'replaced by new stream', false)
    }

    const state: VoiceStreamState = {
      deviceId: client.deviceId || 'esp32-device',
      streamId,
      sampleRate,
      startedAt: Date.now(),
      expectedSeq: 0,
      pendingMeta: null,
      chunksReceived: 0,
      bytesReceived: 0,
      lastLevel: 0,
      lastFinalText: '',
      asr: null,
    }
    this.voiceStreams.set(ws, state)
    this.sendVoiceStreamAck(ws, {
      streamId,
      success: true,
      status: 'accepted',
    })

    const asr = new AliyunSpeechTranscriber({
      sampleRate,
      onResult: (packet) => {
        void this.handleVoiceStreamAsrPacket(ws, streamId, packet)
      },
      onError: (reason) => {
        const current = this.voiceStreams.get(ws)
        if (!current || current.streamId !== streamId) {
          return
        }
        this.sendVoiceStreamAck(ws, {
          streamId,
          success: false,
          status: 'error',
          reason,
          chunksReceived: current.chunksReceived,
          bytesReceived: current.bytesReceived,
        })
        this.stopVoiceStreamSession(ws, `asr error: ${reason}`, false)
      },
      onClosed: () => {
        const current = this.voiceStreams.get(ws)
        if (!current || current.streamId !== streamId) {
          return
        }
        this.stopVoiceStreamSession(ws, 'asr closed', true)
      },
    })
    state.asr = asr

    void asr.start()
      .then(() => {
        const current = this.voiceStreams.get(ws)
        if (!current || current.streamId !== streamId) {
          return
        }
        this.sendVoiceStreamAck(ws, {
          streamId,
          success: true,
          status: 'ready',
        })
      })
      .catch((error) => {
        const reason = error instanceof Error ? error.message : String(error)
        const current = this.voiceStreams.get(ws)
        if (!current || current.streamId !== streamId) {
          return
        }
        this.sendVoiceStreamAck(ws, {
          streamId,
          success: false,
          status: 'error',
          reason,
        })
        this.stopVoiceStreamSession(ws, `asr start failed: ${reason}`, false)
      })
  }

  private handleVoiceStreamChunkMeta(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') {
      return
    }

    const state = this.voiceStreams.get(ws)
    const data = message?.data ?? {}
    const streamId = typeof data.streamId === 'string' ? data.streamId.trim() : ''
    const seqRaw = Number(data.seq)
    const lenRaw = Number(data.len)
    const levelRaw = Number(data.level)
    const seq = Number.isFinite(seqRaw) ? Math.floor(seqRaw) : -1
    const len = Number.isFinite(lenRaw) ? Math.floor(lenRaw) : -1
    const level = Number.isFinite(levelRaw) ? Math.max(0, Math.min(100, Math.floor(levelRaw))) : 0

    if (!state || !streamId || streamId !== state.streamId) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId,
          seq,
          success: false,
          reason: 'voice stream not active',
          timestamp: Date.now(),
        },
      })
      return
    }

    if (state.pendingMeta) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId: state.streamId,
          seq,
          success: false,
          reason: 'binary pending',
          timestamp: Date.now(),
        },
      })
      this.stopVoiceStreamSession(ws, 'binary pending', true)
      return
    }

    if (seq !== state.expectedSeq) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId: state.streamId,
          seq,
          success: false,
          reason: `seq mismatch expected=${state.expectedSeq}`,
          timestamp: Date.now(),
        },
      })
      this.stopVoiceStreamSession(ws, 'seq mismatch', true)
      return
    }

    if (len <= 0 || len > 8192) {
      this.sendMessage(ws, {
        type: 'voice_stream_chunk_ack',
        data: {
          streamId: state.streamId,
          seq,
          success: false,
          reason: 'invalid chunk length',
          timestamp: Date.now(),
        },
      })
      this.stopVoiceStreamSession(ws, 'invalid chunk length', true)
      return
    }

    state.pendingMeta = {
      seq,
      len,
      level,
      timestamp: Date.now(),
    }
  }

  private handleVoiceStreamStop(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') {
      return
    }

    const state = this.voiceStreams.get(ws)
    if (!state) {
      this.sendVoiceStreamAck(ws, {
        streamId: '',
        success: false,
        status: 'error',
        reason: 'voice stream not active',
      })
      return
    }

    const streamId = typeof message?.data?.streamId === 'string' ? message.data.streamId.trim() : ''
    if (streamId && streamId !== state.streamId) {
      this.sendVoiceStreamAck(ws, {
        streamId,
        success: false,
        status: 'error',
        reason: 'streamId mismatch',
      })
      return
    }

    const reason = typeof message?.data?.reason === 'string' ? message.data.reason : 'device stop'
    this.stopVoiceStreamSession(ws, reason, true)
  }

  private async handleVoiceStreamAsrPacket(ws: WebSocket, streamId: string, packet: VoiceRecognitionPacket) {
    const state = this.voiceStreams.get(ws)
    if (!state || state.streamId !== streamId) {
      return
    }
    const recognizedText = packet.text.trim()
    if (!recognizedText) {
      return
    }

    this.sendVoiceStreamAck(ws, {
      streamId,
      success: true,
      status: 'ready',
      text: recognizedText,
      isFinal: packet.isFinal,
      chunksReceived: state.chunksReceived,
      bytesReceived: state.bytesReceived,
    })

    if (!packet.isFinal) {
      return
    }

    if (recognizedText === state.lastFinalText) {
      return
    }
    state.lastFinalText = recognizedText

    const result = await this.executeVoiceCommand(recognizedText)
    this.sendMessage(ws, {
      type: 'voice_command_result',
      data: {
        ...result,
        source: 'esp32_mic',
      },
    })
    this.broadcastVoiceCommandEvent(state.deviceId, result)
  }

  private stopVoiceStreamSession(ws: WebSocket, reason: string, notifyClient: boolean) {
    const state = this.voiceStreams.get(ws)
    if (!state) {
      return
    }

    this.voiceStreams.delete(ws)
    state.pendingMeta = null
    state.asr?.stop()
    state.asr = null

    console.log(
      `[VoiceStream] stop device=${state.deviceId} streamId=${state.streamId} chunks=${state.chunksReceived} bytes=${state.bytesReceived} reason=${reason}`
    )

    if (notifyClient) {
      this.sendVoiceStreamAck(ws, {
        streamId: state.streamId,
        success: true,
        status: 'stopped',
        reason,
        chunksReceived: state.chunksReceived,
        bytesReceived: state.bytesReceived,
      })
    }
  }

  private handlePhotoSettingsRequest(ws: WebSocket, client: ClientInfo) {
    if (client.type !== 'esp32_device') return

    console.log(`[相册设置请求] 设备: ${client.deviceId}`)
    this.sendMessage(ws, {
      type: 'photo_settings',
      data: {
        ...this.photoFrameSettings,
        timestamp: Date.now(),
      },
    })
  }

  private handlePhotoControl(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'control_panel') return

    const data = message?.data ?? {}
    const actionRaw = typeof data.action === 'string' ? data.action.trim() : ''
    const targetDeviceId = typeof data.deviceId === 'string' ? data.deviceId.trim() : ''
    const allowedActions = new Set<PhotoControlAction>(['prev', 'next', 'reload', 'play', 'pause', 'set_interval'])
    if (!allowedActions.has(actionRaw as PhotoControlAction)) {
      this.sendMessage(ws, {
        type: 'photo_control_ack',
        data: {
          success: false,
          reason: `unsupported action: ${actionRaw || 'empty'}`,
        },
      })
      return
    }
    const action = actionRaw as PhotoControlAction

    let targetClient: ClientInfo | undefined
    for (const candidate of this.clients.values()) {
      if (candidate.type !== 'esp32_device' || !candidate.deviceId) {
        continue
      }
      if (targetDeviceId.length > 0 && candidate.deviceId !== targetDeviceId) {
        continue
      }
      targetClient = candidate
      break
    }

    if (!targetClient) {
      this.sendMessage(ws, {
        type: 'photo_control_ack',
        data: {
          success: false,
          action,
          reason: targetDeviceId ? `device not found: ${targetDeviceId}` : 'no online esp32 device',
        },
      })
      return
    }

    const payload: Record<string, unknown> = {
      deviceId: targetClient.deviceId,
      action,
      timestamp: Date.now(),
    }

    if (action === 'set_interval') {
      const intervalRaw = Number(data.intervalSec ?? data.interval ?? data.slideshowInterval)
      if (!Number.isFinite(intervalRaw)) {
        this.sendMessage(ws, {
          type: 'photo_control_ack',
          data: {
            success: false,
            action,
            reason: 'intervalSec required for set_interval',
          },
        })
        return
      }
      payload.intervalSec = Math.max(3, Math.min(30, Math.round(intervalRaw)))
    }

    this.sendMessage(targetClient.ws, {
      type: 'photo_control',
      data: payload,
    })

    this.sendMessage(ws, {
      type: 'photo_control_ack',
      data: {
        success: true,
        action,
        deviceId: targetClient.deviceId,
        intervalSec: payload.intervalSec,
      },
    })
  }

  private handlePhotoState(_ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}

    this.broadcastToControlPanels({
      type: 'photo_state',
      data: {
        ...data,
        deviceId: client.deviceId,
        timestamp: Date.now(),
      },
    })
  }

  private handleSdListResponse(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}
    const requestId = typeof data.requestId === 'string' ? data.requestId : ''

    if (requestId && this.pendingSdListRequests.has(requestId)) {
      const pending = this.pendingSdListRequests.get(requestId)
      if (pending) {
        clearTimeout(pending.timeout)
        this.pendingSdListRequests.delete(requestId)
        console.log(
          `[SD] list response <- device=${client.deviceId} requestId=${requestId} total=${data.total ?? '-'} returned=${data.returned ?? (Array.isArray(data.files) ? data.files.length : '-')}`
        )
        pending.resolve({
          success: true,
          ...data,
          deviceId: client.deviceId,
        })
      }
    }

    this.broadcastToControlPanels({
      type: 'sd_list_response',
      data: {
        ...data,
        deviceId: client.deviceId,
        timestamp: Date.now(),
      },
    })
  }

  private handleSdDeleteResponse(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}
    const requestId = typeof data.requestId === 'string' ? data.requestId : ''

    if (requestId && this.pendingSdDeleteRequests.has(requestId)) {
      const pending = this.pendingSdDeleteRequests.get(requestId)
      if (pending) {
        clearTimeout(pending.timeout)
        this.pendingSdDeleteRequests.delete(requestId)
        pending.resolve({
          ...data,
          deviceId: client.deviceId,
        })
      }
    }

    this.broadcastToControlPanels({
      type: 'sd_delete_response',
      data: {
        ...data,
        deviceId: client.deviceId,
        timestamp: Date.now(),
      },
    })
  }

  private handleSdUploadBeginAck(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}
    const uploadId = typeof data.uploadId === 'string' ? data.uploadId : ''
    if (!uploadId) return

    const pending = this.pendingSdUploadBeginRequests.get(uploadId)
    if (!pending) return
    clearTimeout(pending.timeout)
    this.pendingSdUploadBeginRequests.delete(uploadId)
    pending.resolve({
      ...data,
      success: Boolean(data.success),
      deviceId: client.deviceId,
    })
  }

  private handleSdUploadChunkAck(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}
    const uploadId = typeof data.uploadId === 'string' ? data.uploadId : ''
    const seqRaw = Number(data.seq)
    const seq = Number.isFinite(seqRaw) ? Math.floor(seqRaw) : -1
    if (!uploadId || seq < 0) return

    const requestKey = this.buildSdChunkRequestKey(uploadId, seq)
    const pending = this.pendingSdUploadChunkRequests.get(requestKey)
    if (!pending) return
    clearTimeout(pending.timeout)
    this.pendingSdUploadChunkRequests.delete(requestKey)
    pending.resolve({
      ...data,
      success: Boolean(data.success),
      deviceId: client.deviceId,
    })
  }

  private handleSdUploadCommitAck(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return
    const data = message?.data ?? {}
    const uploadId = typeof data.uploadId === 'string' ? data.uploadId : ''
    if (!uploadId) return

    const pending = this.pendingSdUploadCommitRequests.get(uploadId)
    if (!pending) return
    clearTimeout(pending.timeout)
    this.pendingSdUploadCommitRequests.delete(uploadId)
    pending.resolve({
      ...data,
      success: Boolean(data.success),
      deviceId: client.deviceId,
    })
  }

  private handleSdPreviewResponse(client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    const data = message?.data ?? {}
    const requestId = typeof data.requestId === 'string' ? data.requestId : ''
    if (!requestId) return

    const pending = this.pendingSdPreviewRequests.get(requestId)
    if (!pending) return

    const success = Boolean(data.success)
    if (!success) {
      clearTimeout(pending.timeout)
      this.pendingSdPreviewRequests.delete(requestId)
      this.pendingSdPreviewRequestSockets.delete(requestId)
      pending.resolve({
        ...data,
        success: false,
        deviceId: client.deviceId,
      })
      return
    }

    const expectedLenRaw = Number(data.len ?? data.bytes)
    const expectedLen = Number.isFinite(expectedLenRaw) ? Math.max(0, Math.floor(expectedLenRaw)) : 0
    const path = typeof data.path === 'string' ? data.path : ''
    const mime = typeof data.mime === 'string' && data.mime.trim().length > 0
      ? data.mime.trim()
      : 'image/jpeg'

    if (expectedLen <= 0) {
      clearTimeout(pending.timeout)
      this.pendingSdPreviewRequests.delete(requestId)
      this.pendingSdPreviewRequestSockets.delete(requestId)
      pending.resolve({
        success: false,
        requestId,
        path,
        reason: 'invalid preview length',
        deviceId: client.deviceId,
      })
      return
    }

    const existing = this.pendingSdPreviewBinaryBySocket.get(client.ws)
    if (existing && existing.requestId !== requestId) {
      const stale = this.pendingSdPreviewRequests.get(existing.requestId)
      if (stale) {
        clearTimeout(stale.timeout)
        this.pendingSdPreviewRequests.delete(existing.requestId)
        this.pendingSdPreviewRequestSockets.delete(existing.requestId)
        stale.resolve({
          success: false,
          requestId: existing.requestId,
          path: existing.path,
          reason: 'preview superseded by newer request',
          deviceId: client.deviceId,
        })
      }
    }

    this.pendingSdPreviewBinaryBySocket.set(client.ws, {
      requestId,
      expectedLen,
      path,
      mime,
    })
  }

  private handleAIStatus(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    // 转发AI状态到所有控制面板
    this.broadcastToControlPanels({
      type: 'ai_status',
      data: {
        deviceId: client.deviceId,
        ...message.data,
        timestamp: Date.now()
      }
    })
  }

  private handleAIConversation(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    // 转发对话内容到所有控制面板
    this.broadcastToControlPanels({
      type: 'ai_conversation',
      data: {
        deviceId: client.deviceId,
        ...message.data,
        timestamp: Date.now()
      }
    })
  }

  private handleAIConfig(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'control_panel') return

    // 转发配置到指定的ESP32设备
    const targetDeviceId = message.data.deviceId
    this.clients.forEach((c, clientWs) => {
      if (c.type === 'esp32_device' && c.deviceId === targetDeviceId) {
        this.sendMessage(clientWs, {
          type: 'ai_config',
          data: message.data
        })
      }
    })
  }

  private handleTaskAction(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    const { action, taskId, title, timestamp } = message.data
    console.log(`[任务操作] 设备: ${client.deviceId}, 动作: ${action}, 任务: ${title}${taskId ? ` (ID: ${taskId})` : ''}`)

    // 转发任务操作到所有控制面板
    this.broadcastToControlPanels({
      type: 'task_action',
      data: {
        deviceId: client.deviceId,
        action,
        taskId,
        title,
        timestamp: timestamp || Date.now()
      }
    })

    // 可选：根据action类型执行特定逻辑
    if (action === 'done') {
      console.log(`  → 任务已完成: ${title}`)
    } else if (action === 'ack') {
      console.log(`  → 任务已确认: ${title}`)
    }
  }

  private async handleWeatherRequest(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    console.log(`[天气请求] 设备: ${client.deviceId}`)

    try {
      // 使用和风天气API获取天气数据
      const apiKey = '598a41cf8b404383a148d15a41fa0b55'
      const cityId = message.data.cityId || '101010100' // 默认北京
      const url = `https://devapi.qweather.com/v7/weather/now?location=${cityId}&key=${apiKey}`

      const response = await fetch(url)
      const data = await response.json()

      if (data.code === '200' && data.now) {
        const weatherData = {
          temperature: parseFloat(data.now.temp),
          feelsLike: parseFloat(data.now.feelsLike),
          humidity: parseInt(data.now.humidity),
          condition: data.now.text,
          city: 'Beijing',
          updateTime: data.now.obsTime
        }

        console.log(`[天气数据] ${weatherData.temperature}°C, ${weatherData.condition}`)

        // 发送天气数据给请求的设备
        this.sendMessage(ws, {
          type: 'weather_data',
          data: weatherData
        })
      } else {
        console.error('[天气API] 错误:', data.code)
        // 发送默认数据
        this.sendMessage(ws, {
          type: 'weather_data',
          data: {
            temperature: 22,
            feelsLike: 20,
            humidity: 65,
            condition: 'Unknown',
            city: 'Beijing',
            updateTime: new Date().toISOString()
          }
        })
      }
    } catch (error) {
      console.error('[天气请求] 失败:', error)
      // 发送默认数据
      this.sendMessage(ws, {
        type: 'weather_data',
        data: {
          temperature: 22,
          feelsLike: 20,
          humidity: 65,
          condition: 'Error',
          city: 'Beijing',
          updateTime: new Date().toISOString()
        }
      })
    }
  }

  private async handleAppListRequest(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    console.log(`[应用列表请求] 设备: ${client.deviceId}`)

    if (this.customLaunchApps.length > 0) {
      const apps = this.customLaunchApps.slice(0, 12)
      console.log(`[应用列表] 使用自定义配置 ${apps.length} 项`)
      this.sendMessage(ws, {
        type: 'app_list',
        data: { apps }
      })
      return
    }

    try {
      // 扫描 /Applications 目录
      const { stdout } = await execAsync('ls -1 /Applications | grep ".app$"')
      const appNames = stdout.trim().split('\n').filter((name: string) => name.length > 0)

      const apps = appNames.slice(0, 12).map((name: string, index: number) => ({
        id: `app-${index}`,
        name: name.replace('.app', ''),
        path: `/Applications/${name}`
      }))

      console.log(`[应用列表] 找到 ${apps.length} 个应用`)
      console.log(`[应用列表] 应用:`, apps.map(a => a.name).join(', '))

      // 发送应用列表给设备
      this.sendMessage(ws, {
        type: 'app_list',
        data: {
          apps
        }
      })
    } catch (error) {
      console.error('[应用列表请求] 失败:', error)
      // 发送默认应用列表
      this.sendMessage(ws, {
        type: 'app_list',
        data: {
          apps: [
            { id: 'app-1', name: 'Safari', path: '/Applications/Safari.app' },
            { id: 'app-2', name: 'Mail', path: '/Applications/Mail.app' },
            { id: 'app-3', name: 'Calendar', path: '/Applications/Calendar.app' },
            { id: 'app-4', name: 'Notes', path: '/Applications/Notes.app' },
            { id: 'app-5', name: 'Music', path: '/Applications/Music.app' },
            { id: 'app-6', name: 'Photos', path: '/Applications/Photos.app' }
          ]
        }
      })
    }
  }

  private async launchAppPath(rawPath: string): Promise<{ success: boolean; appName: string; reason?: string }> {
    const appName = rawPath.split('/').pop()?.replace(/\.app$/i, '') || 'App'

    if (!rawPath) {
      return { success: false, appName, reason: 'appPath missing' }
    }
    if (!rawPath.startsWith('/Applications/') || !rawPath.toLowerCase().endsWith('.app')) {
      return { success: false, appName, reason: `invalid app path: ${rawPath}` }
    }

    try {
      await access(rawPath, fsConstants.F_OK)
    } catch {
      return { success: false, appName, reason: `app not found: ${rawPath}` }
    }

    try {
      const { stdout, stderr } = await execFileAsync('open', [rawPath])
      if (stdout.trim().length > 0) {
        console.log(`[启动应用] stdout: ${stdout.trim()}`)
      }
      if (stderr.trim().length > 0) {
        console.log(`[启动应用] stderr: ${stderr.trim()}`)
      }
      return { success: true, appName }
    } catch (error) {
      const err = error as Error & { stderr?: string; stdout?: string; code?: number | string }
      const stderrText = typeof err.stderr === 'string' ? err.stderr.trim() : ''
      const reason = stderrText.length > 0
        ? `${err.message}${err.code !== undefined ? ` (code ${err.code})` : ''} | ${stderrText}`
        : `${err.message}${err.code !== undefined ? ` (code ${err.code})` : ''}`
      return { success: false, appName, reason }
    }
  }

  private resolveVoiceAppPath(appNameRaw: string): string {
    const appName = appNameRaw.replace(/\.app$/i, '').trim()
    if (!appName) {
      return ''
    }

    const appNameLower = appName.toLowerCase()
    for (const item of this.customLaunchApps) {
      const customName = item.name.trim().toLowerCase()
      if (!customName) continue
      if (customName === appNameLower || customName.includes(appNameLower) || appNameLower.includes(customName)) {
        return item.path
      }
    }

    return `/Applications/${appName}.app`
  }

  private async executeVoiceCommand(rawText: string): Promise<VoiceCommandExecutionResult> {
    const parsed = parseVoiceCommand(rawText)
    const base: VoiceCommandExecutionResult = {
      success: false,
      action: parsed.action,
      message: 'Unsupported voice command',
      reason: rawText,
      command: rawText,
      normalized: parsed.normalized,
      timestamp: Date.now(),
    }

    if (!rawText) {
      return {
        ...base,
        action: 'unknown',
        message: 'empty voice command',
        reason: 'text missing',
      }
    }

    if (parsed.action === 'navigate' && parsed.page) {
      const pageTitle = parsed.page.charAt(0).toUpperCase() + parsed.page.slice(1)
      return {
        ...base,
        success: true,
        action: 'navigate',
        page: parsed.page,
        message: `Navigate to ${pageTitle}`,
        reason: undefined,
      }
    }

    if (parsed.action === 'launch_app' && parsed.appName) {
      const appPath = this.resolveVoiceAppPath(parsed.appName)
      if (!appPath) {
        return {
          ...base,
          action: 'launch_app',
          appName: parsed.appName,
          message: 'failed to resolve app path',
          reason: 'invalid app name',
        }
      }

      const launchResult = await this.launchAppPath(appPath)
      if (!launchResult.success) {
        return {
          ...base,
          action: 'launch_app',
          appName: launchResult.appName,
          appPath,
          message: 'Failed to launch app',
          reason: launchResult.reason,
        }
      }

      return {
        ...base,
        success: true,
        action: 'launch_app',
        appName: launchResult.appName,
        appPath,
        message: `App launched: ${launchResult.appName}`,
        reason: undefined,
      }
    }

    return base
  }

  private broadcastVoiceCommandEvent(deviceId: string | undefined, result: VoiceCommandExecutionResult) {
    if (!deviceId || !result.success) {
      return
    }

    const payload: Record<string, unknown> = {
      deviceId,
      command: result.command,
      action: result.action,
      success: true,
      timestamp: Date.now(),
    }

    if (result.action === 'navigate' && result.page) {
      payload.page = result.page
    } else if (result.action === 'launch_app') {
      payload.appName = result.appName
      payload.appPath = result.appPath
    }

    this.broadcastToControlPanels({
      type: 'voice_command_event',
      data: payload,
    })
  }

  private async handleVoiceCommand(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    const rawText = typeof message?.data?.text === 'string' ? message.data.text.trim() : ''
    const result = await this.executeVoiceCommand(rawText)
    this.sendMessage(ws, {
      type: 'voice_command_result',
      data: result,
    })
    this.broadcastVoiceCommandEvent(client.deviceId, result)
  }

  private async handleVoiceCommandFromControlPanel(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'control_panel') return

    const data = message?.data ?? {}
    const rawText = typeof data.text === 'string' ? data.text.trim() : ''
    const requestId = typeof data.requestId === 'string' ? data.requestId : ''
    const targetDeviceId = typeof data.targetDeviceId === 'string' ? data.targetDeviceId.trim() : ''
    const targetClient = this.findEsp32Client(targetDeviceId || undefined)

    if (!targetClient || !targetClient.ws || targetClient.ws.readyState !== WebSocket.OPEN) {
      this.sendMessage(ws, {
        type: 'voice_command_dispatch_ack',
        data: {
          requestId,
          success: false,
          reason: targetDeviceId ? `device not online: ${targetDeviceId}` : 'no online esp32 device',
          command: rawText,
          timestamp: Date.now(),
        },
      })
      return
    }

    const result = await this.executeVoiceCommand(rawText)
    const resultForDevice: VoiceCommandExecutionResult & { source: string } = {
      ...result,
      source: 'control_panel',
    }

    this.sendMessage(targetClient.ws, {
      type: 'voice_command_result',
      data: resultForDevice,
    })

    this.sendMessage(ws, {
      type: 'voice_command_dispatch_ack',
      data: {
        requestId,
        deviceId: targetClient.deviceId,
        ...result,
      },
    })

    this.broadcastVoiceCommandEvent(targetClient.deviceId, result)
  }

  private async handleLaunchApp(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    const rawPath = typeof message?.data?.appPath === 'string' ? message.data.appPath.trim() : ''
    const launchResult = await this.launchAppPath(rawPath)
    const compactReason = launchResult.reason && launchResult.reason.length > 180
      ? `${launchResult.reason.slice(0, 180)}...`
      : launchResult.reason

    console.log(`[启动应用] 设备: ${client.deviceId}, 应用: ${rawPath}, 成功: ${launchResult.success ? 'yes' : 'no'}`)

    if (!launchResult.success) {
      this.sendMessage(ws, {
        type: 'launch_app_response',
        data: {
          success: false,
          appPath: rawPath,
          appName: launchResult.appName,
          message: 'Failed to launch app',
          reason: compactReason || 'unknown error',
        },
      })
      return
    }

    this.sendMessage(ws, {
      type: 'launch_app_response',
      data: {
        success: true,
        appPath: rawPath,
        appName: launchResult.appName,
        message: `App launched: ${launchResult.appName}`,
      },
    })

    this.broadcastToControlPanels({
      type: 'app_launched',
      data: {
        deviceId: client.deviceId,
        appPath: rawPath,
        appName: launchResult.appName,
        success: true,
      },
    })
  }

  private handleHandshake(ws: WebSocket, client: ClientInfo, message: any) {
    const { clientType, deviceId } = message

    if (clientType === 'control_panel') {
      client.type = 'control_panel'
      console.log('控制面板已连接')

      this.sendMessage(ws, {
        type: 'handshake_ack',
        data: {
          serverVersion: '4.0.0',
          updateInterval: 5000,
          clientType: 'control_panel'
        }
      })

      // 发送当前所有在线设备列表
      this.sendConnectedDevicesList(ws)

    } else if (clientType === 'esp32_device') {
      client.type = 'esp32_device'
      client.deviceId = deviceId || `esp32-${Date.now()}`
      client.lastHeartbeat = Date.now()

      console.log(`ESP32 设备已连接: ${client.deviceId}`)

      this.sendMessage(ws, {
        type: 'handshake_ack',
        data: {
          serverVersion: '4.0.0',
          updateInterval: 5000,
          clientType: 'esp32_device',
          deviceId: client.deviceId
        }
      })

      // 通知所有控制面板有新设备连接
      if (client.deviceId) {
        this.notifyDeviceConnected(client.deviceId)
      }
    }
  }

  private handleHeartbeat(ws: WebSocket, client: ClientInfo, message: any) {
    if (client.type !== 'esp32_device') return

    client.lastHeartbeat = Date.now()
    const { deviceId, uptime, wifiSignal } = message.data

    // 转发心跳数据到所有控制面板
    this.broadcastToControlPanels({
      type: 'device_heartbeat',
      data: {
        deviceId: deviceId || client.deviceId,
        uptime,
        wifiSignal,
        timestamp: Date.now()
      }
    })
  }

  private sendConnectedDevicesList(ws: WebSocket) {
    const devices: any[] = []
    this.clients.forEach((client) => {
      if (client.type === 'esp32_device' && client.deviceId) {
        devices.push({
          deviceId: client.deviceId,
          connectedAt: client.connectedAt,
          lastHeartbeat: client.lastHeartbeat
        })
      }
    })

    this.sendMessage(ws, {
      type: 'connected_devices',
      data: { devices }
    })
  }

  private notifyDeviceConnected(deviceId: string) {
    this.broadcastToControlPanels({
      type: 'device_connected',
      data: {
        deviceId,
        timestamp: Date.now()
      }
    })
  }

  private notifyDeviceDisconnected(deviceId: string) {
    this.broadcastToControlPanels({
      type: 'device_disconnected',
      data: {
        deviceId,
        timestamp: Date.now()
      }
    })
  }

  private broadcastToDevices(message: any) {
    this.clients.forEach((client, ws) => {
      if (client.type === 'esp32_device') {
        this.sendMessage(ws, message)
      }
    })
  }

  private broadcastToControlPanels(message: any) {
    this.clients.forEach((client, ws) => {
      if (client.type === 'control_panel') {
        this.sendMessage(ws, message)
      }
    })
  }

  public sendMessage(ws: WebSocket, message: any) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(message))
    }
  }

  public broadcast(message: any) {
    this.clients.forEach((client, ws) => {
      this.sendMessage(ws, message)
    })
  }

  public close() {
    this.voiceStreams.forEach((_state, ws) => {
      this.stopVoiceStreamSession(ws, 'server shutdown', false)
    })
    this.voiceStreams.clear()

    this.pendingSdListRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdListRequests.clear()
    this.pendingSdDeleteRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdDeleteRequests.clear()
    this.pendingSdUploadBeginRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdUploadBeginRequests.clear()
    this.pendingSdUploadChunkRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdUploadChunkRequests.clear()
    this.pendingSdUploadCommitRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdUploadCommitRequests.clear()
    this.pendingSdPreviewRequests.forEach((pending) => clearTimeout(pending.timeout))
    this.pendingSdPreviewRequests.clear()
    this.pendingSdPreviewRequestSockets.clear()
    this.pendingSdPreviewBinaryBySocket.clear()

    if (this.systemBroadcastInterval) {
      clearInterval(this.systemBroadcastInterval)
    }
    if (this.heartbeatMonitorInterval) {
      clearInterval(this.heartbeatMonitorInterval)
    }
    this.wss.close()
  }
}
