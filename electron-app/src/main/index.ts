import { app, BrowserWindow, ipcMain, shell } from 'electron'
import path from 'path'
import { fileURLToPath } from 'url'
import { DeviceWebSocketServer } from './websocket.js'
import fs from 'fs/promises'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

let wsServer: DeviceWebSocketServer

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  })

  if (process.env.NODE_ENV === 'development') {
    // 尝试多个端口，因为 Vite 可能使用不同端口
    const port = process.env.VITE_PORT || '5174'
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
}

app.on('window-all-closed', () => {
  if (wsServer) {
    wsServer.close()
  }
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
