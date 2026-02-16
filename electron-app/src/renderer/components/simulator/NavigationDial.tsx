import React from 'react';
import { Box, Typography, IconButton } from '@mui/material';
import {
  PhotoCamera,
  WbSunny,
  AccessTime,
  Mic,
  SmartToy,
  BarChart,
  Timer,
  FlashOn,
} from '@mui/icons-material';

interface NavigationDialProps {
  onNavigate: (page: string) => void;
}

const NavigationDial: React.FC<NavigationDialProps> = ({ onNavigate }) => {
  const currentTime = new Date();
  const timeString = currentTime.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' });
  const dateString = currentTime.toLocaleDateString('zh-CN', { month: '2-digit', day: '2-digit' });

  const icons = [
    { icon: <PhotoCamera />, label: '相框', page: 'photo', angle: 0 },
    { icon: <WbSunny />, label: '天气', page: 'weather', angle: 45 },
    { icon: <AccessTime />, label: '时钟', page: 'clock', angle: 90 },
    { icon: <Mic />, label: '语音', page: 'voice', angle: 135 },
    { icon: <SmartToy />, label: 'AI', page: 'ai', angle: 180 },
    { icon: <BarChart />, label: '监控', page: 'monitor', angle: 225 },
    { icon: <Timer />, label: '番茄钟', page: 'timer', angle: 270 },
    { icon: <FlashOn />, label: '快捷', page: 'quick', angle: 315 },
  ];

  const radius = 120;
  const centerSize = 100;

  return (
    <Box
      sx={{
        width: '100%',
        height: '100%',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        position: 'relative',
        background: 'linear-gradient(135deg, #1a1a2e 0%, #16213e 100%)',
      }}
    >
      {/* Center Circle */}
      <Box
        sx={{
          width: centerSize,
          height: centerSize,
          borderRadius: '50%',
          background: 'linear-gradient(135deg, #0f3460 0%, #16213e 100%)',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          boxShadow: '0 4px 20px rgba(0, 0, 0, 0.5)',
          border: '2px solid rgba(255, 255, 255, 0.1)',
          zIndex: 10,
        }}
      >
        <Typography
          variant="h4"
          sx={{
            color: '#fff',
            fontWeight: 'bold',
            fontFamily: 'monospace',
          }}
        >
          {timeString}
        </Typography>
        <Typography
          variant="body2"
          sx={{
            color: 'rgba(255, 255, 255, 0.7)',
            mt: 0.5,
          }}
        >
          {dateString}
        </Typography>
      </Box>

      {/* Icon Ring */}
      {icons.map((item, index) => {
        const angleRad = (item.angle * Math.PI) / 180;
        const x = Math.cos(angleRad) * radius;
        const y = Math.sin(angleRad) * radius;

        return (
          <Box
            key={index}
            sx={{
              position: 'absolute',
              left: '50%',
              top: '50%',
              transform: `translate(calc(-50% + ${x}px), calc(-50% + ${y}px))`,
            }}
          >
            <IconButton
              onClick={() => onNavigate(item.page)}
              sx={{
                width: 56,
                height: 56,
                background: 'linear-gradient(135deg, #533483 0%, #7b2cbf 100%)',
                color: '#fff',
                boxShadow: '0 4px 15px rgba(123, 44, 191, 0.4)',
                border: '2px solid rgba(255, 255, 255, 0.2)',
                transition: 'all 0.3s ease',
                '&:hover': {
                  background: 'linear-gradient(135deg, #7b2cbf 0%, #9d4edd 100%)',
                  transform: 'scale(1.1)',
                  boxShadow: '0 6px 20px rgba(123, 44, 191, 0.6)',
                },
                '&:active': {
                  transform: 'scale(0.95)',
                },
              }}
            >
              {item.icon}
            </IconButton>
            <Typography
              variant="caption"
              sx={{
                position: 'absolute',
                left: '50%',
                top: '100%',
                transform: 'translateX(-50%)',
                color: 'rgba(255, 255, 255, 0.8)',
                mt: 0.5,
                fontSize: '0.7rem',
                whiteSpace: 'nowrap',
              }}
            >
              {item.label}
            </Typography>
          </Box>
        );
      })}
    </Box>
  );
};

export default NavigationDial;
