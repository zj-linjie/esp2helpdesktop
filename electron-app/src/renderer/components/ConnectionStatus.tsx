import React from 'react';
import { Card, CardContent, Typography, Box, Chip } from '@mui/material';
import { Circle, WifiOff, Wifi } from '@mui/icons-material';

interface ConnectionStatusProps {
  isConnected: boolean;
  serverUrl: string;
  reconnectAttempts: number;
}

const ConnectionStatus: React.FC<ConnectionStatusProps> = ({
  isConnected,
  serverUrl,
  reconnectAttempts
}) => {
  return (
    <Card sx={{ bgcolor: 'background.paper' }}>
      <CardContent>
        <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <Box sx={{ display: 'flex', alignItems: 'center' }}>
            {isConnected ? (
              <Wifi sx={{ mr: 1, color: 'success.main' }} />
            ) : (
              <WifiOff sx={{ mr: 1, color: 'error.main' }} />
            )}
            <Typography variant="h6">WebSocket 连接状态</Typography>
          </Box>
          <Chip
            icon={<Circle sx={{ fontSize: 12 }} />}
            label={isConnected ? '已连接' : '未连接'}
            color={isConnected ? 'success' : 'error'}
            size="small"
          />
        </Box>
        <Box sx={{ mt: 2 }}>
          <Typography variant="body2" color="text.secondary">
            服务器地址: {serverUrl}
          </Typography>
          {!isConnected && reconnectAttempts > 0 && (
            <Typography variant="body2" color="warning.main" sx={{ mt: 1 }}>
              重连尝试次数: {reconnectAttempts}
            </Typography>
          )}
        </Box>
      </CardContent>
    </Card>
  );
};

export default ConnectionStatus;
