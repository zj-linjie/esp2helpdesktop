import React from 'react';
import { Card, CardContent, Typography, Box, List, ListItem, ListItemText, Chip } from '@mui/material';
import { Message, Info, Warning, Error as ErrorIcon } from '@mui/icons-material';

interface LogMessage {
  id: string;
  timestamp: string;
  type: 'info' | 'warning' | 'error' | 'success';
  message: string;
}

interface MessageLogProps {
  messages: LogMessage[];
  maxMessages?: number;
}

const MessageLog: React.FC<MessageLogProps> = ({ messages, maxMessages = 50 }) => {
  const displayMessages = messages.slice(-maxMessages).reverse();

  const getIcon = (type: string) => {
    switch (type) {
      case 'error':
        return <ErrorIcon sx={{ fontSize: 16, color: 'error.main' }} />;
      case 'warning':
        return <Warning sx={{ fontSize: 16, color: 'warning.main' }} />;
      case 'success':
        return <Message sx={{ fontSize: 16, color: 'success.main' }} />;
      default:
        return <Info sx={{ fontSize: 16, color: 'info.main' }} />;
    }
  };

  const getChipColor = (type: string): 'error' | 'warning' | 'success' | 'info' => {
    switch (type) {
      case 'error':
        return 'error';
      case 'warning':
        return 'warning';
      case 'success':
        return 'success';
      default:
        return 'info';
    }
  };

  return (
    <Card sx={{ bgcolor: 'background.paper', height: '100%' }}>
      <CardContent>
        <Typography variant="h6" sx={{ mb: 2 }}>
          消息日志 ({messages.length})
        </Typography>
        <Box sx={{ maxHeight: 400, overflow: 'auto' }}>
          <List dense>
            {displayMessages.length === 0 ? (
              <ListItem>
                <ListItemText
                  primary="暂无消息"
                  secondary="等待 WebSocket 连接..."
                  primaryTypographyProps={{ color: 'text.secondary' }}
                />
              </ListItem>
            ) : (
              displayMessages.map((msg) => (
                <ListItem
                  key={msg.id}
                  sx={{
                    borderLeft: 3,
                    borderColor: `${getChipColor(msg.type)}.main`,
                    mb: 1,
                    bgcolor: 'background.default',
                    borderRadius: 1
                  }}
                >
                  <Box sx={{ display: 'flex', alignItems: 'flex-start', width: '100%' }}>
                    <Box sx={{ mr: 1, mt: 0.5 }}>{getIcon(msg.type)}</Box>
                    <ListItemText
                      primary={msg.message}
                      secondary={msg.timestamp}
                      primaryTypographyProps={{ fontSize: '0.9rem' }}
                      secondaryTypographyProps={{ fontSize: '0.75rem' }}
                    />
                    <Chip
                      label={msg.type}
                      size="small"
                      color={getChipColor(msg.type)}
                      sx={{ ml: 1, height: 20, fontSize: '0.7rem' }}
                    />
                  </Box>
                </ListItem>
              ))
            )}
          </List>
        </Box>
      </CardContent>
    </Card>
  );
};

export default MessageLog;
