import React from 'react';
import { Card, CardContent, Typography, Box, List, ListItem, ListItemText, Chip, Avatar } from '@mui/material';
import { Devices, CheckCircle, Cancel } from '@mui/icons-material';

interface Device {
  id: string;
  name: string;
  type: string;
  status: 'online' | 'offline';
  lastSeen: string;
  ip?: string;
}

interface DeviceListProps {
  devices: Device[];
}

const DeviceList: React.FC<DeviceListProps> = ({ devices }) => {
  const onlineDevices = devices.filter(d => d.status === 'online');
  const offlineDevices = devices.filter(d => d.status === 'offline');

  return (
    <Card sx={{ bgcolor: 'background.paper', height: '100%' }}>
      <CardContent>
        <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center' }}>
            <Devices sx={{ mr: 1 }} />
            <Typography variant="h6">已连接设备</Typography>
          </Box>
          <Chip
            label={`${onlineDevices.length} 在线`}
            color="success"
            size="small"
          />
        </Box>
        <Box sx={{ maxHeight: 400, overflow: 'auto' }}>
          <List dense>
            {devices.length === 0 ? (
              <ListItem>
                <ListItemText
                  primary="暂无设备"
                  secondary="等待设备连接..."
                  primaryTypographyProps={{ color: 'text.secondary' }}
                />
              </ListItem>
            ) : (
              <>
                {onlineDevices.length > 0 && (
                  <>
                    <Typography variant="subtitle2" color="success.main" sx={{ px: 2, py: 1 }}>
                      在线设备
                    </Typography>
                    {onlineDevices.map((device) => (
                      <ListItem
                        key={device.id}
                        sx={{
                          bgcolor: 'background.default',
                          mb: 1,
                          borderRadius: 1,
                          border: 1,
                          borderColor: 'success.main'
                        }}
                      >
                        <Avatar sx={{ bgcolor: 'success.main', mr: 2 }}>
                          <CheckCircle />
                        </Avatar>
                        <ListItemText
                          primary={device.name}
                          secondary={
                            <>
                              <Typography component="span" variant="body2" color="text.secondary">
                                类型: {device.type}
                              </Typography>
                              {device.ip && (
                                <>
                                  <br />
                                  <Typography component="span" variant="body2" color="text.secondary">
                                    IP: {device.ip}
                                  </Typography>
                                </>
                              )}
                              <br />
                              <Typography component="span" variant="body2" color="text.secondary">
                                最后活动: {device.lastSeen}
                              </Typography>
                            </>
                          }
                        />
                        <Chip label="在线" color="success" size="small" />
                      </ListItem>
                    ))}
                  </>
                )}
                {offlineDevices.length > 0 && (
                  <>
                    <Typography variant="subtitle2" color="error.main" sx={{ px: 2, py: 1, mt: 2 }}>
                      离线设备
                    </Typography>
                    {offlineDevices.map((device) => (
                      <ListItem
                        key={device.id}
                        sx={{
                          bgcolor: 'background.default',
                          mb: 1,
                          borderRadius: 1,
                          border: 1,
                          borderColor: 'error.main',
                          opacity: 0.6
                        }}
                      >
                        <Avatar sx={{ bgcolor: 'error.main', mr: 2 }}>
                          <Cancel />
                        </Avatar>
                        <ListItemText
                          primary={device.name}
                          secondary={
                            <>
                              <Typography component="span" variant="body2" color="text.secondary">
                                类型: {device.type}
                              </Typography>
                              <br />
                              <Typography component="span" variant="body2" color="text.secondary">
                                最后活动: {device.lastSeen}
                              </Typography>
                            </>
                          }
                        />
                        <Chip label="离线" color="error" size="small" />
                      </ListItem>
                    ))}
                  </>
                )}
              </>
            )}
          </List>
        </Box>
      </CardContent>
    </Card>
  );
};

export default DeviceList;
