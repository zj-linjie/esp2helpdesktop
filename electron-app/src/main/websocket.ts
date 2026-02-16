import { WebSocketServer, WebSocket } from 'ws'
import { SystemMonitor } from './system.js'

export class DeviceWebSocketServer {
  private wss: WebSocketServer
  private clients: Set<WebSocket> = new Set()
  private systemMonitor: SystemMonitor
  private updateInterval: NodeJS.Timeout | null = null

  constructor(port: number = 8765) {
    this.wss = new WebSocketServer({ port })
    this.systemMonitor = new SystemMonitor()
    this.setupServer()
  }

  private setupServer() {
    this.wss.on('connection', (ws: WebSocket) => {
      console.log('ESP32 设备已连接')
      this.clients.add(ws)

      // 开始发送系统信息
      this.startSystemInfoUpdates(ws)

      ws.on('message', (data: Buffer) => {
        try {
          const message = JSON.parse(data.toString())
          this.handleMessage(ws, message)
        } catch (error) {
          console.error('解析消息失败:', error)
        }
      })

      ws.on('close', () => {
        console.log('ESP32 设备已断开')
        this.clients.delete(ws)
      })

      ws.on('error', (error) => {
        console.error('WebSocket 错误:', error)
      })
    })

    console.log(`WebSocket 服务器运行在端口 ${this.wss.options.port}`)
  }

  private async startSystemInfoUpdates(ws: WebSocket) {
    const sendUpdate = async () => {
      if (ws.readyState === WebSocket.OPEN) {
        const systemInfo = await this.systemMonitor.getSystemInfo()
        this.sendMessage(ws, {
          type: 'system_info',
          data: systemInfo
        })
      }
    }

    // 立即发送一次
    await sendUpdate()

    // 每秒更新
    const interval = setInterval(sendUpdate, 1000)

    ws.on('close', () => {
      clearInterval(interval)
    })
  }

  private handleMessage(ws: WebSocket, message: any) {
    console.log('收到消息:', message)

    if (message.type === 'handshake') {
      this.sendMessage(ws, {
        type: 'handshake_ack',
        data: {
          server_version: '3.0.0',
          update_interval: 1000
        }
      })
    }
  }

  public sendMessage(ws: WebSocket, message: any) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(message))
    }
  }

  public broadcast(message: any) {
    this.clients.forEach(client => {
      this.sendMessage(client, message)
    })
  }

  public close() {
    this.wss.close()
  }
}
