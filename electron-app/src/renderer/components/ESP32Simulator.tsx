import React, { useState, useEffect, useRef } from 'react';
import { Box, Chip } from '@mui/material';
import CircularScreen from './simulator/CircularScreen';
import NavigationDial from './simulator/NavigationDial';
import SystemMonitorPage from './simulator/SystemMonitorPage';
import ClockPage from './simulator/ClockPage';
import PomodoroPage from './simulator/PomodoroPage';
import WeatherPage from './simulator/WeatherPage';
import PhotoFramePage from './simulator/PhotoFramePage';
import AppLauncherPage from './simulator/AppLauncherPage';
import QuickSettingsPage from './simulator/QuickSettingsPage';
import VoicePage from './simulator/VoicePage';
import AIPage from './simulator/AIPage';

const ESP32Simulator: React.FC = () => {
  const DEBUG_LOG = false;
  const [currentPage, setCurrentPage] = useState<string>('home');
  const [wsConnected, setWsConnected] = useState(false);
  const [systemStats, setSystemStats] = useState({
    cpu: 0,
    memory: 0,
    network: { upload: 0, download: 0 }
  });
  const [deviceStatus, setDeviceStatus] = useState({
    uptime: 0,
    wifiSignal: -50,
    deviceId: 'esp32-simulator'
  });
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const heartbeatIntervalRef = useRef<NodeJS.Timeout | null>(null);

  // WebSocket 连接初始化
  useEffect(() => {
    const log = (...args: unknown[]) => {
      if (DEBUG_LOG) {
        console.log('[ESP32 Simulator]', ...args);
      }
    };

    const connectWebSocket = () => {
      log('正在连接 WebSocket...');
      const ws = new WebSocket('ws://localhost:8765');
      wsRef.current = ws;

      ws.onopen = () => {
        log('WebSocket 已连接');
        setWsConnected(true);

        // 发送握手消息
        ws.send(JSON.stringify({
          type: 'handshake',
          clientType: 'esp32_device',
          deviceId: deviceStatus.deviceId
        }));
      };

      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          log('收到消息:', data.type);

          if (data.type === 'system_stats') {
            setSystemStats({
              cpu: data.data.cpu,
              memory: data.data.memory,
              network: data.data.network
            });
          } else if (data.type === 'handshake_ack') {
            log('握手成功:', data.data);
          }
        } catch (error) {
          console.error('[ESP32 Simulator] 解析消息失败:', error);
        }
      };

      ws.onclose = () => {
        log('WebSocket 已断开');
        setWsConnected(false);

        // 清理心跳定时器
        if (heartbeatIntervalRef.current) {
          clearInterval(heartbeatIntervalRef.current);
          heartbeatIntervalRef.current = null;
        }

        // 5 秒后重连
        reconnectTimeoutRef.current = setTimeout(() => {
          log('尝试重新连接...');
          connectWebSocket();
        }, 5000);
      };

      ws.onerror = (error) => {
        console.error('[ESP32 Simulator] WebSocket 错误:', error);
      };
    };

    connectWebSocket();

    return () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
      }
      if (heartbeatIntervalRef.current) {
        clearInterval(heartbeatIntervalRef.current);
      }
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [deviceStatus.deviceId]);

  // 心跳发送机制
  useEffect(() => {
    if (!wsConnected) return;

    heartbeatIntervalRef.current = setInterval(() => {
      setDeviceStatus(prev => {
        const newStatus = {
          ...prev,
          uptime: prev.uptime + 5,
          wifiSignal: -45 + Math.random() * 10 - 5 // 模拟信号波动 -50 到 -40
        };

        // 发送心跳
        if (wsRef.current?.readyState === WebSocket.OPEN) {
          wsRef.current.send(JSON.stringify({
            type: 'heartbeat',
            data: {
              deviceId: newStatus.deviceId,
              uptime: newStatus.uptime,
              wifiSignal: Math.round(newStatus.wifiSignal)
            }
          }));
        }

        return newStatus;
      });
    }, 5000);

    return () => {
      if (heartbeatIntervalRef.current) {
        clearInterval(heartbeatIntervalRef.current);
      }
    };
  }, [wsConnected]);

  const handleNavigate = (page: string) => {
    setCurrentPage(page);
  };

  const handleBack = () => {
    setCurrentPage('home');
  };

  const renderPage = () => {
    switch (currentPage) {
      case 'monitor':
        return (
          <SystemMonitorPage
            cpu={systemStats.cpu}
            memory={systemStats.memory}
            network={(systemStats.network.upload + systemStats.network.download) / 1024}
            onBack={handleBack}
          />
        );
      case 'clock':
        return <ClockPage onBack={handleBack} />;
      case 'timer':
        return <PomodoroPage onBack={handleBack} />;
      case 'weather':
        return <WeatherPage onBack={handleBack} />;
      case 'photo':
        return <PhotoFramePage onBack={handleBack} />;
      case 'quick':
        return <AppLauncherPage onBack={handleBack} />;
      case 'settings':
        return <QuickSettingsPage onBack={handleBack} />;
      case 'voice':
        return <VoicePage onBack={handleBack} onNavigate={handleNavigate} />;
      case 'ai':
        return <AIPage onBack={handleBack} />;
      case 'home':
      default:
        return <NavigationDial onNavigate={handleNavigate} />;
    }
  };

  return (
    <Box
      sx={{
        display: 'flex',
        justifyContent: 'center',
        alignItems: 'center',
        padding: 4,
        backgroundColor: '#0a0a0a',
        borderRadius: 2,
        minHeight: 500,
        position: 'relative',
      }}
    >
      {/* WebSocket 连接状态指示器 */}
      <Chip
        label={wsConnected ? 'WebSocket 已连接' : 'WebSocket 断开'}
        color={wsConnected ? 'success' : 'error'}
        size="small"
        sx={{
          position: 'absolute',
          top: 16,
          right: 16,
          zIndex: 10,
        }}
      />

      {/* 设备状态信息 */}
      <Box
        sx={{
          position: 'absolute',
          top: 16,
          left: 16,
          zIndex: 10,
          display: 'flex',
          flexDirection: 'column',
          gap: 0.5,
        }}
      >
        <Chip
          label={`运行时间: ${Math.floor(deviceStatus.uptime / 60)}分${deviceStatus.uptime % 60}秒`}
          size="small"
          variant="outlined"
          sx={{ fontSize: '0.7rem' }}
        />
        <Chip
          label={`WiFi: ${deviceStatus.wifiSignal.toFixed(0)} dBm`}
          size="small"
          variant="outlined"
          sx={{ fontSize: '0.7rem' }}
        />
      </Box>

      <CircularScreen size={360} onLongPress={currentPage !== 'home' ? handleBack : undefined}>
        {renderPage()}
      </CircularScreen>
    </Box>
  );
};

export default ESP32Simulator;
