import { WebSocketServer, WebSocket } from 'ws'
import { SystemMonitor } from './system.js'

interface ClientInfo {
  ws: WebSocket
  type: 'control_panel' | 'esp32_device' | 'unknown'
  deviceId?: string
  connectedAt: number
  lastHeartbeat: number
}

export class DeviceWebSocketServer {
  private wss: WebSocketServer
  private clients: Map<WebSocket, ClientInfo> = new Map()
  private systemMonitor: SystemMonitor
  private systemBroadcastInterval: NodeJS.Timeout | null = null
  private heartbeatMonitorInterval: NodeJS.Timeout | null = null

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

      ws.on('message', (data: Buffer) => {
        try {
          const message = JSON.parse(data.toString())
          this.handleMessage(ws, message)
        } catch (error) {
          console.error('解析消息失败:', error)
        }
      })

      ws.on('close', () => {
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

    console.log('收到消息:', message.type, message.data || '')

    switch (message.type) {
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

      default:
        console.log('未知消息类型:', message.type)
    }
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
    if (this.systemBroadcastInterval) {
      clearInterval(this.systemBroadcastInterval)
    }
    if (this.heartbeatMonitorInterval) {
      clearInterval(this.heartbeatMonitorInterval)
    }
    this.wss.close()
  }
}
