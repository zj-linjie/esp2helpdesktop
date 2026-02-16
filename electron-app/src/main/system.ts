import si from 'systeminformation'
import os from 'os'

export interface SystemInfo {
  cpu: {
    usage: number
    temperature: number
  }
  memory: {
    used: number
    total: number
    percentage: number
  }
  network: {
    upload: number
    download: number
  }
  time: string
  date: string
}

export class SystemMonitor {
  private lastNetworkStats: any = null

  async getSystemInfo(): Promise<SystemInfo> {
    const [cpuLoad, mem, networkStats] = await Promise.all([
      si.currentLoad(),
      si.mem(),
      si.networkStats()
    ])

    // 计算网络速度
    let upload = 0
    let download = 0
    if (this.lastNetworkStats && networkStats[0]) {
      const timeDiff = (Date.now() - this.lastNetworkStats.timestamp) / 1000
      upload = (networkStats[0].tx_bytes - this.lastNetworkStats.tx_bytes) / timeDiff
      download = (networkStats[0].rx_bytes - this.lastNetworkStats.rx_bytes) / timeDiff
    }

    if (networkStats[0]) {
      this.lastNetworkStats = {
        tx_bytes: networkStats[0].tx_bytes,
        rx_bytes: networkStats[0].rx_bytes,
        timestamp: Date.now()
      }
    }

    const now = new Date()

    return {
      cpu: {
        usage: Math.round(cpuLoad.currentLoad),
        temperature: 0 // macOS 需要特殊权限获取温度
      },
      memory: {
        used: Math.round(mem.used / 1024 / 1024 / 1024 * 10) / 10,
        total: Math.round(mem.total / 1024 / 1024 / 1024 * 10) / 10,
        percentage: Math.round((mem.used / mem.total) * 100)
      },
      network: {
        upload: Math.round(upload / 1024),
        download: Math.round(download / 1024)
      },
      time: now.toLocaleTimeString('zh-CN', { hour12: false }),
      date: now.toLocaleDateString('zh-CN')
    }
  }
}
