import React, { useState, useEffect, useRef } from 'react';
import { ThemeProvider, createTheme, CssBaseline, Container, Box, AppBar, Toolbar, Typography, Grid, Tabs, Tab } from '@mui/material';
import { Dashboard, Devices, Settings } from '@mui/icons-material';
import SystemMonitor from './components/SystemMonitor';
import ConnectionStatus from './components/ConnectionStatus';
import MessageLog from './components/MessageLog';
import DeviceList from './components/DeviceList';
import ESP32Simulator from './components/ESP32Simulator';
import SettingsPanel from './components/SettingsPanel';

const darkTheme = createTheme({
  palette: {
    mode: 'dark',
    primary: {
      main: '#1976d2',
    },
    secondary: {
      main: '#9c27b0',
    },
    background: {
      default: '#121212',
      paper: '#1e1e1e',
    },
  },
});

interface SystemData {
  timestamp: string;
  cpu: number;
  memory: number;
  network: number;
}

interface LogMessage {
  id: string;
  timestamp: string;
  type: 'info' | 'warning' | 'error' | 'success';
  message: string;
}

interface ESP32Device {
  deviceId: string;
  status: 'online' | 'offline';
  uptime: number;
  wifiSignal: number;
  lastHeartbeat: number;
  connectedAt: number;
}

function App() {
  const [isConnected, setIsConnected] = useState(false);
  const [reconnectAttempts, setReconnectAttempts] = useState(0);

  // macOS 系统数据
  const [macosSystemData, setMacosSystemData] = useState<SystemData[]>([]);
  const [currentMacosStats, setCurrentMacosStats] = useState({
    cpu: 0,
    memory: 0,
    network: 0
  });

  // ESP32 设备数据
  const [esp32Devices, setEsp32Devices] = useState<Map<string, ESP32Device>>(new Map());

  const [messages, setMessages] = useState<LogMessage[]>([]);
  const [currentTab, setCurrentTab] = useState(0);
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  const WS_URL = 'ws://localhost:8765';
  const MAX_DATA_POINTS = 30;

  const addMessage = (type: LogMessage['type'], message: string) => {
    const newMessage: LogMessage = {
      id: Date.now().toString() + Math.random(),
      timestamp: new Date().toLocaleTimeString('zh-CN'),
      type,
      message,
    };
    setMessages(prev => [...prev, newMessage]);
  };

  const connectWebSocket = () => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      return;
    }

    try {
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setIsConnected(true);
        setReconnectAttempts(0);
        addMessage('success', 'WebSocket 连接成功');
        console.log('[Control Panel] WebSocket connected');

        // 发送握手消息，标识为控制面板
        ws.send(JSON.stringify({
          type: 'handshake',
          clientType: 'control_panel'
        }));
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          const type = data?.type as string;
          if (type !== 'system_stats' && type !== 'device_heartbeat' && type !== 'photo_state') {
            console.log('[Control Panel] 收到消息:', type);
          }

          switch (type) {
            case 'handshake_ack':
              addMessage('success', `握手成功 - 服务器版本: ${data.data.serverVersion}`);
              break;

            case 'system_stats':
              // macOS 系统数据（注意：这个消息只发给 ESP32 设备，控制面板不应该收到）
              // 但为了兼容性，我们也处理一下
              handleSystemStats(data.data);
              break;

            case 'device_heartbeat':
              // ESP32 设备心跳数据
              handleDeviceHeartbeat(data.data);
              break;

            case 'device_connected':
              // 设备连接通知
              handleDeviceConnected(data.data);
              break;

            case 'device_disconnected':
              // 设备断开通知
              handleDeviceDisconnected(data.data);
              break;

            case 'connected_devices':
              // 当前在线设备列表
              handleConnectedDevices(data.data);
              break;

            case 'photo_state':
            case 'voice_command_event':
            case 'photo_control_ack':
            case 'sd_list_response':
            case 'sd_delete_response':
              // 控制面板可接收这些消息，但目前无需在主面板展示
              break;

            default:
              console.log('[Control Panel] 未处理消息类型:', type);
              addMessage('info', `收到消息: ${type}`);
          }
        } catch (error) {
          console.error('Error parsing message:', error);
          addMessage('error', `解析消息失败: ${error}`);
        }
      };

      ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        addMessage('error', 'WebSocket 连接错误');
      };

      ws.onclose = () => {
        setIsConnected(false);
        addMessage('warning', 'WebSocket 连接已关闭');
        console.log('[Control Panel] WebSocket disconnected');

        // Auto reconnect
        setReconnectAttempts(prev => prev + 1);
        reconnectTimeoutRef.current = setTimeout(() => {
          addMessage('info', '尝试重新连接...');
          connectWebSocket();
        }, 5000);
      };
    } catch (error) {
      console.error('Failed to connect:', error);
      addMessage('error', `连接失败: ${error}`);
      setReconnectAttempts(prev => prev + 1);
      reconnectTimeoutRef.current = setTimeout(connectWebSocket, 5000);
    }
  };

  const handleSystemStats = (data: any) => {
    const timestamp = new Date().toLocaleTimeString('zh-CN', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    });

    const networkTotal = ((data.network?.upload || 0) + (data.network?.download || 0)) / 1024;

    const newDataPoint: SystemData = {
      timestamp,
      cpu: data.cpu || 0,
      memory: data.memory || 0,
      network: networkTotal,
    };

    setMacosSystemData(prev => {
      const updated = [...prev, newDataPoint];
      return updated.slice(-MAX_DATA_POINTS);
    });

    setCurrentMacosStats({
      cpu: data.cpu || 0,
      memory: data.memory || 0,
      network: networkTotal,
    });
  };

  const handleDeviceHeartbeat = (data: any) => {
    const { deviceId, uptime, wifiSignal } = data;

    setEsp32Devices(prev => {
      const updated = new Map(prev);
      const existing = updated.get(deviceId);

      if (existing) {
        updated.set(deviceId, {
          ...existing,
          status: 'online',
          uptime,
          wifiSignal,
          lastHeartbeat: Date.now()
        });
      } else {
        // 如果设备不存在，创建新设备
        updated.set(deviceId, {
          deviceId,
          status: 'online',
          uptime,
          wifiSignal,
          lastHeartbeat: Date.now(),
          connectedAt: Date.now()
        });
      }

      return updated;
    });
  };

  const handleDeviceConnected = (data: any) => {
    const { deviceId } = data;

    setEsp32Devices(prev => {
      const updated = new Map(prev);
      updated.set(deviceId, {
        deviceId,
        status: 'online',
        uptime: 0,
        wifiSignal: -50,
        lastHeartbeat: Date.now(),
        connectedAt: Date.now()
      });
      return updated;
    });

    addMessage('success', `ESP32 设备已连接: ${deviceId}`);
  };

  const handleDeviceDisconnected = (data: any) => {
    const { deviceId } = data;

    setEsp32Devices(prev => {
      const updated = new Map(prev);
      const device = updated.get(deviceId);
      if (device) {
        updated.set(deviceId, {
          ...device,
          status: 'offline'
        });
      }
      return updated;
    });

    addMessage('warning', `ESP32 设备已断开: ${deviceId}`);
  };

  const handleConnectedDevices = (data: any) => {
    const { devices } = data;

    devices.forEach((device: any) => {
      setEsp32Devices(prev => {
        const updated = new Map(prev);
        updated.set(device.deviceId, {
          deviceId: device.deviceId,
          status: 'online',
          uptime: 0,
          wifiSignal: -50,
          lastHeartbeat: device.lastHeartbeat,
          connectedAt: device.connectedAt
        });
        return updated;
      });
    });

    addMessage('info', `收到在线设备列表: ${devices.length} 个设备`);
  };

  useEffect(() => {
    connectWebSocket();

    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, []);

  return (
    <ThemeProvider theme={darkTheme}>
      <CssBaseline />
      <Box sx={{ flexGrow: 1, minHeight: '100vh', bgcolor: 'background.default' }}>
        <AppBar position="static" elevation={0}>
          <Toolbar>
            <Dashboard sx={{ mr: 2 }} />
            <Typography variant="h6" component="div" sx={{ flexGrow: 1 }}>
              ESP32 桌面助手控制中心
            </Typography>
            <Typography variant="body2" color="inherit">
              {new Date().toLocaleDateString('zh-CN')}
            </Typography>
          </Toolbar>
          <Tabs
            value={currentTab}
            onChange={(_, newValue) => setCurrentTab(newValue)}
            sx={{
              bgcolor: 'background.paper',
              '& .MuiTab-root': { color: 'rgba(255, 255, 255, 0.7)' },
              '& .Mui-selected': { color: '#fff' },
            }}
          >
            <Tab label="控制中心" icon={<Dashboard />} iconPosition="start" />
            <Tab label="设备模拟器" icon={<Devices />} iconPosition="start" />
            <Tab label="设置" icon={<Settings />} iconPosition="start" />
          </Tabs>
        </AppBar>

        <Container maxWidth="xl" sx={{ mt: 3, mb: 3 }}>
          {currentTab === 0 && (
            <Grid container spacing={3}>
              {/* Connection Status */}
              <Grid size={{ xs: 12 }}>
                <ConnectionStatus
                  isConnected={isConnected}
                  serverUrl={WS_URL}
                  reconnectAttempts={reconnectAttempts}
                />
              </Grid>

              {/* macOS System Monitor */}
              <Grid size={{ xs: 12 }}>
                <Typography variant="h5" sx={{ mb: 2, fontWeight: 600 }}>
                  macOS 系统状态
                </Typography>
                <SystemMonitor
                  data={macosSystemData}
                  currentStats={{
                    cpu: currentMacosStats.cpu,
                    memory: currentMacosStats.memory,
                    network: currentMacosStats.network
                  }}
                />
              </Grid>

              {/* ESP32 Devices Section */}
              <Grid size={{ xs: 12 }}>
                <Typography variant="h5" sx={{ mb: 2, fontWeight: 600 }}>
                  连接的 ESP32 设备
                </Typography>
              </Grid>

              {/* ESP32 Device List */}
              <Grid size={{ xs: 12, md: 6 }}>
                <DeviceList
                  devices={Array.from(esp32Devices.values()).map(device => ({
                    id: device.deviceId,
                    name: device.deviceId,
                    type: 'ESP32',
                    status: device.status,
                    lastSeen: new Date(device.lastHeartbeat).toLocaleTimeString('zh-CN'),
                    ip: undefined
                  }))}
                />
              </Grid>

              {/* Message Log */}
              <Grid size={{ xs: 12, md: 6 }}>
                <MessageLog messages={messages} maxMessages={50} />
              </Grid>
            </Grid>
          )}

          {currentTab === 1 && (
            <Box sx={{ display: 'flex', justifyContent: 'center', mt: 4 }}>
              <ESP32Simulator />
            </Box>
          )}

          {currentTab === 2 && (
            <SettingsPanel />
          )}
        </Container>
      </Box>
    </ThemeProvider>
  );
}

export default App;
