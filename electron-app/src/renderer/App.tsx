import React, { useState, useEffect, useRef } from 'react';
import { ThemeProvider, createTheme, CssBaseline, Container, Box, AppBar, Toolbar, Typography, Grid, Tabs, Tab } from '@mui/material';
import { Dashboard, Devices } from '@mui/icons-material';
import SystemMonitor from './components/SystemMonitor';
import ConnectionStatus from './components/ConnectionStatus';
import MessageLog from './components/MessageLog';
import DeviceList from './components/DeviceList';
import ESP32Simulator from './components/ESP32Simulator';

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

interface Device {
  id: string;
  name: string;
  type: string;
  status: 'online' | 'offline';
  lastSeen: string;
  ip?: string;
}

function App() {
  const [isConnected, setIsConnected] = useState(false);
  const [reconnectAttempts, setReconnectAttempts] = useState(0);
  const [systemData, setSystemData] = useState<SystemData[]>([]);
  const [currentStats, setCurrentStats] = useState({ cpu: 0, memory: 0, network: 0 });
  const [messages, setMessages] = useState<LogMessage[]>([]);
  const [devices, setDevices] = useState<Device[]>([]);
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
        console.log('WebSocket connected');
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          console.log('Received data:', data);

          if (data.type === 'system_info') {
            const timestamp = new Date().toLocaleTimeString('zh-CN', {
              hour: '2-digit',
              minute: '2-digit',
              second: '2-digit'
            });

            const newDataPoint: SystemData = {
              timestamp,
              cpu: data.cpu || 0,
              memory: data.memory || 0,
              network: data.network || 0,
            };

            setSystemData(prev => {
              const updated = [...prev, newDataPoint];
              return updated.slice(-MAX_DATA_POINTS);
            });

            setCurrentStats({
              cpu: data.cpu || 0,
              memory: data.memory || 0,
              network: data.network || 0,
            });

            addMessage('info', `系统信息更新 - CPU: ${data.cpu?.toFixed(1)}%, 内存: ${data.memory?.toFixed(1)}%`);
          } else if (data.type === 'device_connected') {
            const newDevice: Device = {
              id: data.deviceId || Date.now().toString(),
              name: data.deviceName || '未知设备',
              type: data.deviceType || 'ESP32',
              status: 'online',
              lastSeen: new Date().toLocaleTimeString('zh-CN'),
              ip: data.ip,
            };
            setDevices(prev => {
              const existing = prev.find(d => d.id === newDevice.id);
              if (existing) {
                return prev.map(d => d.id === newDevice.id ? { ...d, status: 'online', lastSeen: newDevice.lastSeen } : d);
              }
              return [...prev, newDevice];
            });
            addMessage('success', `设备已连接: ${newDevice.name}`);
          } else if (data.type === 'device_disconnected') {
            setDevices(prev => prev.map(d =>
              d.id === data.deviceId
                ? { ...d, status: 'offline', lastSeen: new Date().toLocaleTimeString('zh-CN') }
                : d
            ));
            addMessage('warning', `设备已断开: ${data.deviceName || data.deviceId}`);
          } else {
            addMessage('info', `收到消息: ${JSON.stringify(data)}`);
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
        console.log('WebSocket disconnected');

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

  useEffect(() => {
    connectWebSocket();

    // Simulate initial device for demo
    setTimeout(() => {
      setDevices([
        {
          id: 'demo-1',
          name: 'ESP32 开发板',
          type: 'ESP32',
          status: 'offline',
          lastSeen: new Date().toLocaleTimeString('zh-CN'),
          ip: '192.168.1.100',
        },
      ]);
    }, 1000);

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

              {/* System Monitor */}
              <Grid size={{ xs: 12 }}>
                <SystemMonitor data={systemData} currentStats={currentStats} />
              </Grid>

              {/* Device List and Message Log */}
              <Grid size={{ xs: 12, md: 6 }}>
                <DeviceList devices={devices} />
              </Grid>
              <Grid size={{ xs: 12, md: 6 }}>
                <MessageLog messages={messages} maxMessages={50} />
              </Grid>
            </Grid>
          )}

          {currentTab === 1 && (
            <Box sx={{ display: 'flex', justifyContent: 'center', mt: 4 }}>
              <ESP32Simulator systemStats={currentStats} />
            </Box>
          )}
        </Container>
      </Box>
    </ThemeProvider>
  );
}

export default App;
