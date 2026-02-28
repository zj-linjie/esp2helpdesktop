import { app, BrowserWindow, ipcMain, shell, dialog } from 'electron'
import type { OpenDialogOptions } from 'electron'
import path from 'path'
import { fileURLToPath } from 'url'
import { DeviceWebSocketServer, type PhotoFrameSettings } from './websocket.js'
import { AISimulator } from './ai-simulator.js'
import fs from 'fs/promises'
import { existsSync } from 'fs'
import crypto from 'crypto'
import WebSocket from 'ws'
import dotenv from 'dotenv'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

const loadEnvFiles = () => {
  const candidates = [
    path.resolve(process.cwd(), '.env'),
    path.resolve(process.cwd(), '../.env'),
    path.resolve(__dirname, '../../.env'),
    path.resolve(__dirname, '../../../.env'),
  ]

  const loaded: string[] = []
  const visited = new Set<string>()
  for (const filePath of candidates) {
    if (visited.has(filePath)) continue
    visited.add(filePath)
    if (!existsSync(filePath)) continue

    const result = dotenv.config({ path: filePath, override: false })
    if (!result.error) {
      loaded.push(filePath)
    }
  }

  if (loaded.length === 0) {
    console.warn('[Env] 未找到 .env 文件（已尝试 electron-app 与仓库根目录）')
  } else {
    console.log('[Env] 已加载 .env:', loaded.join(' | '))
  }
}

// 加载环境变量（支持 electron-app/.env 与仓库根目录 .env）
loadEnvFiles()

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
const PHOTO_FRAME_SETTINGS_FILE = 'photo-frame-settings.json'
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
let cachedPhotoFrameSettings: PhotoFrameSettings = { ...DEFAULT_PHOTO_FRAME_SETTINGS }

type SdFileType = 'image' | 'audio' | 'video' | 'other'

interface SdFileItem {
  name: string
  path: string
  relativePath: string
  extension: string
  type: SdFileType
  size: number
  modifiedAt: number
}

const SD_FILE_TYPES: SdFileType[] = ['image', 'audio', 'video', 'other']
const IMAGE_EXTENSIONS = new Set(['.jpg', '.jpeg', '.png', '.bmp', '.gif', '.webp', '.sjpg'])
const AUDIO_EXTENSIONS = new Set(['.mp3', '.wav', '.aac', '.m4a', '.flac', '.ogg'])
const VIDEO_EXTENSIONS = new Set(['.mp4', '.mov', '.mkv', '.avi', '.webm', '.mjpeg', '.mjpg'])

const classifySdFileType = (ext: string): SdFileType => {
  const lower = ext.toLowerCase()
  if (IMAGE_EXTENSIONS.has(lower)) return 'image'
  if (AUDIO_EXTENSIONS.has(lower)) return 'audio'
  if (VIDEO_EXTENSIONS.has(lower)) return 'video'
  return 'other'
}

const SD_SAFE_FILENAME_MAX_LEN = 63

const toDeviceSafeFileName = (rawFileName: string): { fileName: string; renamed: boolean } => {
  const original = rawFileName.trim()
  if (!original) {
    return { fileName: 'file.bin', renamed: true }
  }

  const normalized = original.normalize('NFKD').replace(/[\u0300-\u036f]/g, '')
  const extRaw = path.posix.extname(normalized).toLowerCase()
  const ext = extRaw.startsWith('.') ? extRaw.replace(/[^.a-z0-9]/g, '') : ''
  const baseRaw = normalized.slice(0, normalized.length - extRaw.length)
  let base = baseRaw
    .replace(/[^A-Za-z0-9._-]+/g, '_')
    .replace(/_+/g, '_')
    .replace(/^[_\-.]+|[_\-.]+$/g, '')

  if (!base) {
    base = 'file'
  }

  const needsRename = `${base}${ext}` !== original
  if (needsRename) {
    const shortHash = crypto.createHash('sha1').update(original).digest('hex').slice(0, 6)
    base = `${base}_${shortHash}`
  }

  const maxBaseLen = Math.max(1, SD_SAFE_FILENAME_MAX_LEN - ext.length)
  if (base.length > maxBaseLen) {
    base = base.slice(0, maxBaseLen)
  }

  const finalName = `${base}${ext}`
  return {
    fileName: finalName,
    renamed: finalName !== original,
  }
}

const normalizeSdDevicePath = (rawPath: unknown, fallback: string = '/'): string => {
  if (typeof rawPath !== 'string') {
    return fallback
  }

  const trimmed = rawPath.trim()
  if (!trimmed.startsWith('/')) {
    return fallback
  }

  const collapsed = trimmed.replace(/\/{2,}/g, '/')
  if (collapsed.length > 1 && collapsed.endsWith('/')) {
    return collapsed.slice(0, -1)
  }
  return collapsed || '/'
}

const getDefaultSdRootPath = (): string => {
  return normalizeSdDevicePath(cachedPhotoFrameSettings.folderPath, '/')
}

const isPathInsideDeviceRoot = (rootPath: string, targetPath: string): boolean => {
  const normalizedRoot = normalizeSdDevicePath(rootPath, '/')
  const normalizedTarget = normalizeSdDevicePath(targetPath, '')
  if (!normalizedTarget) {
    return false
  }
  if (normalizedRoot === '/') {
    return true
  }
  return normalizedTarget === normalizedRoot || normalizedTarget.startsWith(`${normalizedRoot}/`)
}

const toRelativePath = (rootPath: string, targetPath: string): string => {
  const normalizedRoot = normalizeSdDevicePath(rootPath, '/')
  const normalizedTarget = normalizeSdDevicePath(targetPath, '')
  if (!normalizedTarget) {
    return ''
  }
  if (normalizedRoot === '/') {
    return normalizedTarget.replace(/^\/+/, '')
  }
  if (normalizedTarget === normalizedRoot) {
    return path.posix.basename(normalizedTarget)
  }
  return normalizedTarget.slice(normalizedRoot.length + 1)
}

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
const getPhotoFrameSettingsPath = () => path.join(app.getPath('userData'), PHOTO_FRAME_SETTINGS_FILE)

const parseJsonWithRecovery = <T>(raw: string): { value: T | null; recovered: boolean } => {
  try {
    return { value: JSON.parse(raw) as T, recovered: false }
  } catch {
    // Try trimming trailing broken chars (common for partially-written JSON)
    for (let end = raw.lastIndexOf('}'); end > 0; end = raw.lastIndexOf('}', end - 1)) {
      const candidate = raw.slice(0, end + 1)
      try {
        return { value: JSON.parse(candidate) as T, recovered: true }
      } catch {
        // continue
      }
    }
  }
  return { value: null, recovered: false }
}

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

const normalizePhotoFrameSettings = (settings: unknown): PhotoFrameSettings => {
  if (!settings || typeof settings !== 'object') {
    return { ...DEFAULT_PHOTO_FRAME_SETTINGS }
  }

  const source = settings as Record<string, unknown>
  const normalizeWallpaperPath = (value: unknown): string => {
    const normalized = normalizeSdDevicePath(value, '')
    if (!normalized) {
      return ''
    }
    const lower = normalized.toLowerCase()
    if (!lower.endsWith('.mjpeg') && !lower.endsWith('.mjpg')) {
      return ''
    }
    return normalized
  }
  const slideshowIntervalRaw = Number(source.slideshowInterval)
  const maxFileSizeRaw = Number(source.maxFileSize)
  const maxPhotoCountRaw = Number(source.maxPhotoCount)

  return {
    folderPath: typeof source.folderPath === 'string' && source.folderPath.trim().length > 0
      ? source.folderPath.trim()
      : DEFAULT_PHOTO_FRAME_SETTINGS.folderPath,
    slideshowInterval: Number.isFinite(slideshowIntervalRaw)
      ? Math.max(3, Math.min(30, Math.round(slideshowIntervalRaw)))
      : DEFAULT_PHOTO_FRAME_SETTINGS.slideshowInterval,
    autoPlay: source.autoPlay !== undefined ? Boolean(source.autoPlay) : DEFAULT_PHOTO_FRAME_SETTINGS.autoPlay,
    theme: typeof source.theme === 'string' && source.theme.trim().length > 0
      ? source.theme.trim()
      : DEFAULT_PHOTO_FRAME_SETTINGS.theme,
    maxFileSize: Number.isFinite(maxFileSizeRaw)
      ? Math.max(1, Math.min(5, Math.round(maxFileSizeRaw * 2) / 2))
      : DEFAULT_PHOTO_FRAME_SETTINGS.maxFileSize,
    autoCompress: source.autoCompress !== undefined
      ? Boolean(source.autoCompress)
      : DEFAULT_PHOTO_FRAME_SETTINGS.autoCompress,
    maxPhotoCount: Number.isFinite(maxPhotoCountRaw)
      ? Math.max(1, Math.min(100, Math.round(maxPhotoCountRaw)))
      : DEFAULT_PHOTO_FRAME_SETTINGS.maxPhotoCount,
    homeWallpaperPath: normalizeWallpaperPath(source.homeWallpaperPath ?? source.home_wallpaper_path),
    clockWallpaperPath: normalizeWallpaperPath(source.clockWallpaperPath ?? source.clock_wallpaper_path),
  }
}

const loadPhotoFrameSettings = async (): Promise<PhotoFrameSettings> => {
  try {
    const filePath = getPhotoFrameSettingsPath()
    const raw = await fs.readFile(filePath, 'utf-8')
    const parsedResult = parseJsonWithRecovery<{ settings?: unknown }>(raw)
    if (!parsedResult.value) {
      console.warn('[PhotoFrame] 配置文件损坏，回退默认设置')
      return { ...DEFAULT_PHOTO_FRAME_SETTINGS }
    }
    const parsed = parsedResult.value
    const settings = normalizePhotoFrameSettings(parsed?.settings ?? parsed)

    if (parsedResult.recovered) {
      await savePhotoFrameSettings(settings)
      console.log('[PhotoFrame] 已自动修复配置文件')
    }

    console.log(
      `[PhotoFrame] 已加载设置 interval=${settings.slideshowInterval}s autoPlay=${settings.autoPlay} theme=${settings.theme}`
    )
    return settings
  } catch (error) {
    console.warn('[PhotoFrame] 读取设置失败，回退默认:', error)
    return { ...DEFAULT_PHOTO_FRAME_SETTINGS }
  }
}

const savePhotoFrameSettings = async (settings: PhotoFrameSettings): Promise<void> => {
  const filePath = getPhotoFrameSettingsPath()
  const payload = {
    version: 1,
    updatedAt: new Date().toISOString(),
    settings,
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
  cachedPhotoFrameSettings = await loadPhotoFrameSettings()
  createWindow()
  wsServer = new DeviceWebSocketServer(8765)
  wsServer.setCustomLaunchApps(cachedCustomLauncherApps)
  wsServer.setPhotoFrameSettings(cachedPhotoFrameSettings)

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

  ipcMain.handle('photo-frame-sync-settings', async (_event, payload: { settings?: unknown }) => {
    try {
      const settings = normalizePhotoFrameSettings(payload?.settings)
      cachedPhotoFrameSettings = settings
      await savePhotoFrameSettings(cachedPhotoFrameSettings)
      wsServer?.setPhotoFrameSettings(cachedPhotoFrameSettings)
      wsServer?.broadcastPhotoFrameSettings()
      console.log(
        `[PhotoFrame] 已同步设置 interval=${settings.slideshowInterval}s autoPlay=${settings.autoPlay} theme=${settings.theme}`
      )
      return { success: true, settings }
    } catch (error) {
      console.error('[PhotoFrame] 同步设置失败:', error)
      return { success: false, error: String(error), settings: cachedPhotoFrameSettings }
    }
  })

  ipcMain.handle('photo-frame-get-settings', async () => {
    try {
      cachedPhotoFrameSettings = await loadPhotoFrameSettings()
      wsServer?.setPhotoFrameSettings(cachedPhotoFrameSettings)
      return { success: true, settings: cachedPhotoFrameSettings }
    } catch (error) {
      console.error('[PhotoFrame] 读取设置失败:', error)
      return { success: false, error: String(error), settings: cachedPhotoFrameSettings }
    }
  })

  ipcMain.handle('sd-manager-list-files', async (_event, payload: { rootPath?: unknown; deviceId?: unknown } | undefined) => {
    const rootPath = normalizeSdDevicePath(payload?.rootPath, getDefaultSdRootPath())
    const deviceId = typeof payload?.deviceId === 'string' ? payload.deviceId.trim() : undefined

    try {
      const pageLimit = 24
      const maxPages = 10
      const sourceFiles: Array<Record<string, unknown>> = []
      let page = 0
      let requestOffset = 0
      let truncated = true
      let totalFromDevice = 0
      let activeDeviceId: string | undefined = undefined

      while (truncated && page < maxPages) {
        const result = await wsServer.requestSdFileList(deviceId, 12000, requestOffset, pageLimit)
        if (!result?.success) {
          return {
            success: false,
            rootPath,
            files: [] as SdFileItem[],
            exists: false,
            error: result?.reason || '读取设备 SD 列表失败',
          }
        }

        if (result.sdMounted === false) {
          return {
            success: false,
            rootPath,
            files: [] as SdFileItem[],
            exists: false,
            error: `设备 SD 未挂载: ${result.reason || 'unknown'}`,
          }
        }

        if (typeof result.deviceId === 'string' && result.deviceId.trim().length > 0) {
          activeDeviceId = result.deviceId
        }

        const batchFiles = Array.isArray(result.files) ? (result.files as Array<Record<string, unknown>>) : []
        sourceFiles.push(...batchFiles)

        const returnedRaw = Number(result.returned)
        const returned = Number.isFinite(returnedRaw) && returnedRaw >= 0
          ? Math.floor(returnedRaw)
          : batchFiles.length
        if (returned <= 0 && batchFiles.length === 0) {
          break
        }

        requestOffset += returned > 0 ? returned : batchFiles.length
        const totalRaw = Number(result.total)
        if (Number.isFinite(totalRaw) && totalRaw >= 0) {
          totalFromDevice = Math.max(totalFromDevice, Math.floor(totalRaw))
        }

        truncated = Boolean(result.truncated)
          || (totalFromDevice > 0 && requestOffset < totalFromDevice)
        page += 1
      }

      const normalizedFiles: SdFileItem[] = []
      const seenPaths = new Set<string>()
      for (const item of sourceFiles) {
        if (!item || typeof item !== 'object') {
          continue
        }

        const rawPath = normalizeSdDevicePath(item.path, '')
        if (!rawPath || !isPathInsideDeviceRoot(rootPath, rawPath)) {
          continue
        }
        if (seenPaths.has(rawPath)) {
          continue
        }
        seenPaths.add(rawPath)

        const name = typeof item.name === 'string'
          ? String(item.name).trim()
          : path.posix.basename(rawPath)
        const extension = path.posix.extname(name).toLowerCase()
        const rawType = typeof item.type === 'string'
          ? String(item.type).toLowerCase()
          : ''
        const type = SD_FILE_TYPES.includes(rawType as SdFileType)
          ? rawType as SdFileType
          : classifySdFileType(extension)
        const sizeRaw = Number(item.size)
        const modifiedAtRaw = Number(item.modifiedAt)

        normalizedFiles.push({
          name: name || path.posix.basename(rawPath),
          path: rawPath,
          relativePath: toRelativePath(rootPath, rawPath),
          extension,
          type,
          size: Number.isFinite(sizeRaw) && sizeRaw >= 0 ? Math.round(sizeRaw) : 0,
          modifiedAt: Number.isFinite(modifiedAtRaw) && modifiedAtRaw > 0 ? modifiedAtRaw : 0,
        })
      }

      normalizedFiles.sort((a, b) => a.relativePath.localeCompare(b.relativePath, 'zh-Hans-CN'))
      const counts = normalizedFiles.reduce(
        (acc, file) => {
          acc[file.type] += 1
          return acc
        },
        { image: 0, audio: 0, video: 0, other: 0 }
      )

      return {
        success: true,
        rootPath,
        files: normalizedFiles,
        exists: true,
        truncated: totalFromDevice > 0 ? totalFromDevice > normalizedFiles.length : truncated,
        counts,
        deviceId: activeDeviceId || deviceId,
      }
    } catch (error) {
      console.error('[SD Manager] 列设备文件失败:', error)
      return {
        success: false,
        rootPath,
        error: String(error),
        files: [] as SdFileItem[],
        exists: false,
      }
    }
  })

  ipcMain.handle('sd-manager-upload-files', async (event, payload: { rootPath?: unknown; sourcePaths?: unknown; deviceId?: unknown } | undefined) => {
    const rootPath = normalizeSdDevicePath(payload?.rootPath, getDefaultSdRootPath())
    const deviceId = typeof payload?.deviceId === 'string' ? payload.deviceId.trim() : undefined
    const emitUploadProgress = (data: Record<string, unknown>) => {
      try {
        event.sender.send('sd-upload-progress', {
          ...data,
          timestamp: Date.now(),
        })
      } catch {
        // ignore renderer progress delivery errors
      }
    }

    let sourcePaths: string[] = []
    if (Array.isArray(payload?.sourcePaths)) {
      sourcePaths = payload.sourcePaths
        .filter((item): item is string => typeof item === 'string' && item.trim().length > 0)
        .map((item) => path.resolve(item.trim()))
    }

    if (sourcePaths.length === 0) {
      const pickerOptions: OpenDialogOptions = {
        title: '选择上传到 ESP32 SD 的文件',
        properties: ['openFile', 'multiSelections'],
      }
      const picker = mainWindow
        ? await dialog.showOpenDialog(mainWindow, pickerOptions)
        : await dialog.showOpenDialog(pickerOptions)
      if (picker.canceled || picker.filePaths.length === 0) {
        emitUploadProgress({ status: 'canceled' })
        return {
          success: true,
          canceled: true,
          rootPath,
          uploaded: [] as string[],
          uploadedCount: 0,
          skippedCount: 0,
        }
      }
      sourcePaths = picker.filePaths.map((filePath) => path.resolve(filePath))
    }

    const uploaded: string[] = []
    const skipped: Array<{ path: string; reason: string }> = []
    const renamed: Array<{ sourceName: string; targetName: string; targetPath: string }> = []
    const uploadCandidates: Array<{
      sourcePath: string
      sourceName: string
      targetPath: string
      size: number
    }> = []

    for (const sourcePath of sourcePaths.slice(0, 30)) {
      const sourceName = path.basename(sourcePath)
      const safeNameResult = toDeviceSafeFileName(sourceName)
      const targetPath = normalizeSdDevicePath(path.posix.join(rootPath, safeNameResult.fileName), '/')

      if (safeNameResult.renamed) {
        console.warn(`[SD Upload] 文件名兼容性重命名: "${sourceName}" -> "${safeNameResult.fileName}"`)
        renamed.push({
          sourceName,
          targetName: safeNameResult.fileName,
          targetPath,
        })
      }

      try {
        const stat = await fs.stat(sourcePath)
        if (!stat.isFile()) {
          skipped.push({
            path: sourcePath,
            reason: 'source is not file',
          })
          continue
        }
        uploadCandidates.push({
          sourcePath,
          sourceName,
          targetPath,
          size: stat.size,
        })
      } catch (error) {
        skipped.push({
          path: sourcePath,
          reason: `source stat failed: ${String(error)}`,
        })
      }
    }

    const totalFiles = uploadCandidates.length
    const totalBytes = uploadCandidates.reduce((sum, item) => sum + item.size, 0)
    let completedBytes = 0

    emitUploadProgress({
      status: 'start',
      totalFiles,
      totalBytes,
      overallBytesSent: 0,
      overallPercent: 0,
    })

    for (let i = 0; i < uploadCandidates.length; i++) {
      const candidate = uploadCandidates[i]
      try {
        const uploadResult = await wsServer.uploadFileToSd({
          sourcePath: candidate.sourcePath,
          targetPath: candidate.targetPath,
          targetDeviceId: deviceId,
          chunkSize: 4096,
          overwrite: true,
          timeoutMs: 12000,
          onProgress: (progress) => {
            const fileTotal = progress.totalBytes > 0 ? progress.totalBytes : candidate.size
            const fileBytes = Math.max(0, Math.min(progress.bytesSent, fileTotal))
            const overallBytes = Math.max(0, Math.min(totalBytes, completedBytes + fileBytes))
            emitUploadProgress({
              status: 'progress',
              fileIndex: i + 1,
              fileCount: totalFiles,
              fileName: candidate.sourceName,
              targetPath: candidate.targetPath,
              fileBytesSent: fileBytes,
              fileTotalBytes: fileTotal,
              filePercent: fileTotal > 0 ? Math.round((fileBytes / fileTotal) * 100) : 0,
              overallBytesSent: overallBytes,
              overallTotalBytes: totalBytes,
              overallPercent: totalBytes > 0 ? Math.round((overallBytes / totalBytes) * 100) : 0,
            })
          },
        })

        if (uploadResult?.success) {
          uploaded.push(candidate.targetPath)
          completedBytes += candidate.size
          emitUploadProgress({
            status: 'file_complete',
            fileIndex: i + 1,
            fileCount: totalFiles,
            fileName: candidate.sourceName,
            targetPath: candidate.targetPath,
            fileBytesSent: candidate.size,
            fileTotalBytes: candidate.size,
            filePercent: 100,
            overallBytesSent: completedBytes,
            overallTotalBytes: totalBytes,
            overallPercent: totalBytes > 0 ? Math.round((completedBytes / totalBytes) * 100) : 100,
          })
        } else {
          console.error(`[SD Upload] 上传失败: ${candidate.sourceName} -> ${candidate.targetPath} | ${uploadResult?.reason || 'upload failed'}`)
          skipped.push({
            path: candidate.sourcePath,
            reason: uploadResult?.reason || 'upload failed',
          })
          emitUploadProgress({
            status: 'file_error',
            fileIndex: i + 1,
            fileCount: totalFiles,
            fileName: candidate.sourceName,
            targetPath: candidate.targetPath,
            reason: uploadResult?.reason || 'upload failed',
            overallBytesSent: completedBytes,
            overallTotalBytes: totalBytes,
            overallPercent: totalBytes > 0 ? Math.round((completedBytes / totalBytes) * 100) : 0,
          })
        }
      } catch (error) {
        console.error(`[SD Upload] 上传异常: ${candidate.sourceName} -> ${candidate.targetPath}`, error)
        skipped.push({
          path: candidate.sourcePath,
          reason: String(error),
        })
        emitUploadProgress({
          status: 'file_error',
          fileIndex: i + 1,
          fileCount: totalFiles,
          fileName: candidate.sourceName,
          targetPath: candidate.targetPath,
          reason: String(error),
          overallBytesSent: completedBytes,
          overallTotalBytes: totalBytes,
          overallPercent: totalBytes > 0 ? Math.round((completedBytes / totalBytes) * 100) : 0,
        })
      }
    }

    emitUploadProgress({
      status: 'done',
      totalFiles,
      totalBytes,
      uploadedCount: uploaded.length,
      skippedCount: skipped.length,
      overallBytesSent: completedBytes,
      overallPercent: totalBytes > 0 ? Math.round((completedBytes / totalBytes) * 100) : 100,
    })

    return {
      success: uploaded.length > 0 && skipped.length === 0,
      rootPath,
      uploaded,
      skipped,
      renamed,
      renamedCount: renamed.length,
      uploadedCount: uploaded.length,
      skippedCount: skipped.length,
      deviceId,
      error: skipped.length > 0 && uploaded.length === 0 ? skipped[0].reason : undefined,
    }
  })

  ipcMain.handle('sd-manager-delete-file', async (_event, payload: { rootPath?: unknown; filePath?: unknown; deviceId?: unknown } | undefined) => {
    const rootPath = normalizeSdDevicePath(payload?.rootPath, getDefaultSdRootPath())
    const filePath = normalizeSdDevicePath(payload?.filePath, '')
    const deviceId = typeof payload?.deviceId === 'string' ? payload.deviceId.trim() : undefined

    if (!filePath) {
      return { success: false, rootPath, error: 'filePath 为空' }
    }
    if (!isPathInsideDeviceRoot(rootPath, filePath)) {
      return { success: false, rootPath, error: '仅允许删除当前 SD 目录下的文件' }
    }

    try {
      const result = await wsServer.requestSdDelete(filePath, deviceId)
      if (!result?.success) {
        return {
          success: false,
          rootPath,
          error: result?.reason || '设备删除失败',
        }
      }
      return {
        success: true,
        rootPath,
        deleted: typeof result.path === 'string' ? result.path : filePath,
        deviceId: result.deviceId,
      }
    } catch (error) {
      console.error('[SD Manager] 删除设备文件失败:', error)
      return { success: false, rootPath, error: String(error) }
    }
  })

  ipcMain.handle('sd-manager-preview-file', async (_event, payload: { rootPath?: unknown; filePath?: unknown; deviceId?: unknown } | undefined) => {
    const rootPath = normalizeSdDevicePath(payload?.rootPath, getDefaultSdRootPath())
    const filePath = normalizeSdDevicePath(payload?.filePath, '')
    const deviceId = typeof payload?.deviceId === 'string' ? payload.deviceId.trim() : undefined

    if (!filePath) {
      return { success: false, rootPath, error: 'filePath 为空' }
    }
    if (!isPathInsideDeviceRoot(rootPath, filePath)) {
      return { success: false, rootPath, error: '仅允许预览当前 SD 目录下的文件' }
    }

    const ext = path.posix.extname(filePath).toLowerCase()
    if (ext !== '.mjpeg' && ext !== '.mjpg') {
      return { success: false, rootPath, error: '仅支持 MJPEG 文件预览' }
    }

    try {
      const result = await wsServer.requestSdPreview(filePath, deviceId, 10000)
      if (!result?.success) {
        return {
          success: false,
          rootPath,
          error: result?.reason || '设备预览失败',
        }
      }

      const mime = typeof result.mime === 'string' && result.mime.trim().length > 0
        ? result.mime.trim()
        : 'image/jpeg'
      const binary = Buffer.isBuffer(result.buffer)
        ? result.buffer
        : Buffer.from(result.buffer ?? [])
      if (binary.length <= 0) {
        return { success: false, rootPath, error: '设备未返回预览数据' }
      }

      return {
        success: true,
        rootPath,
        filePath,
        mime,
        bytes: binary.length,
        previewDataUrl: `data:${mime};base64,${binary.toString('base64')}`,
        deviceId: result.deviceId || deviceId,
      }
    } catch (error) {
      console.error('[SD Manager] 预览设备文件失败:', error)
      return { success: false, rootPath, error: String(error) }
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
