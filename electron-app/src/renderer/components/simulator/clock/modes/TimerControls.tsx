import React from 'react';
import { Box, IconButton, Chip } from '@mui/material';
import { PlayArrow, Pause, Refresh } from '@mui/icons-material';

interface TimerControlsProps {
  isRunning: boolean;
  remaining: number;
  onStart: () => void;
  onPause: () => void;
  onReset: () => void;
  onSetDuration: (seconds: number) => void;
}

/**
 * 倒计时控制按钮组件
 * 预设时间按钮 + 开始/暂停/重置按钮
 */
const TimerControls: React.FC<TimerControlsProps> = ({
  isRunning,
  remaining,
  onStart,
  onPause,
  onReset,
  onSetDuration
}) => {
  const presets = [
    { label: '1分', seconds: 60 },
    { label: '3分', seconds: 180 },
    { label: '5分', seconds: 300 },
    { label: '10分', seconds: 600 }
  ];

  return (
    <>
      {/* 顶部预设时间按钮 */}
      <Box
        onClick={(e) => e.stopPropagation()}
        onMouseDown={(e) => e.stopPropagation()}
        onTouchStart={(e) => e.stopPropagation()}
        sx={{
          position: 'absolute',
          top: '30px',
          left: '50%',
          transform: 'translateX(-50%)',
          display: 'flex',
          gap: '8px',
          zIndex: 100
        }}
      >
        {presets.map((preset) => (
          <Chip
            key={preset.seconds}
            label={preset.label}
            onClick={() => {
              onSetDuration(preset.seconds);
              // 设置时长后自动开始倒计时
              setTimeout(() => onStart(), 100);
            }}
            disabled={isRunning}
            sx={{
              backgroundColor: 'rgba(255, 152, 0, 0.9)',
              backdropFilter: 'blur(10px)',
              color: '#fff',
              fontWeight: 'bold',
              fontSize: '0.75rem',
              height: '28px',
              border: '1px solid rgba(255, 255, 255, 0.3)',
              cursor: isRunning ? 'not-allowed' : 'pointer',
              opacity: isRunning ? 0.5 : 1,
              transition: 'all 0.2s ease',
              '&:hover': {
                backgroundColor: isRunning ? 'rgba(255, 152, 0, 0.9)' : 'rgba(255, 152, 0, 1)',
                transform: isRunning ? 'none' : 'scale(1.05)',
                boxShadow: isRunning ? 'none' : '0 2px 10px rgba(255, 152, 0, 0.5)'
              },
              '&:active': {
                transform: isRunning ? 'none' : 'scale(0.95)'
              }
            }}
          />
        ))}
      </Box>

      {/* 中心开始/暂停按钮 */}
      <Box
        onClick={(e) => e.stopPropagation()}
        onMouseDown={(e) => e.stopPropagation()}
        onTouchStart={(e) => e.stopPropagation()}
        sx={{
          position: 'absolute',
          left: '40%',
          bottom: '100px',
          transform: 'translateX(-50%)',
          zIndex: 100
        }}
      >
        <IconButton
          onClick={isRunning ? onPause : onStart}
          disabled={remaining <= 0}
          sx={{
            width: 64,
            height: 64,
            backgroundColor: isRunning ? 'rgba(244, 67, 54, 0.9)' : 'rgba(76, 175, 80, 0.9)',
            backdropFilter: 'blur(10px)',
            color: '#fff',
            border: '2px solid rgba(255, 255, 255, 0.3)',
            boxShadow: isRunning
              ? '0 4px 20px rgba(244, 67, 54, 0.5)'
              : '0 4px 20px rgba(76, 175, 80, 0.5)',
            opacity: remaining <= 0 ? 0.5 : 1,
            cursor: remaining <= 0 ? 'not-allowed' : 'pointer',
            transition: 'all 0.3s ease',
            '&:hover': {
              backgroundColor: remaining <= 0
                ? (isRunning ? 'rgba(244, 67, 54, 0.9)' : 'rgba(76, 175, 80, 0.9)')
                : (isRunning ? 'rgba(244, 67, 54, 1)' : 'rgba(76, 175, 80, 1)'),
              transform: remaining <= 0 ? 'none' : 'scale(1.1)',
              boxShadow: remaining <= 0
                ? (isRunning ? '0 4px 20px rgba(244, 67, 54, 0.5)' : '0 4px 20px rgba(76, 175, 80, 0.5)')
                : (isRunning ? '0 6px 30px rgba(244, 67, 54, 0.7)' : '0 6px 30px rgba(76, 175, 80, 0.7)')
            },
            '&:active': {
              transform: remaining <= 0 ? 'none' : 'scale(0.95)'
            }
          }}
        >
          {isRunning ? <Pause sx={{ fontSize: 32 }} /> : <PlayArrow sx={{ fontSize: 32 }} />}
        </IconButton>
      </Box>

      {/* 右下角重置按钮 */}
      <Box
        onClick={(e) => e.stopPropagation()}
        onMouseDown={(e) => e.stopPropagation()}
        onTouchStart={(e) => e.stopPropagation()}
        sx={{
          position: 'absolute',
          right: '30px',
          bottom: '100px',
          zIndex: 100
        }}
      >
        <IconButton
          onClick={onReset}
          sx={{
            width: 44,
            height: 44,
            backgroundColor: 'rgba(158, 158, 158, 0.9)',
            backdropFilter: 'blur(10px)',
            color: '#fff',
            border: '2px solid rgba(255, 255, 255, 0.2)',
            boxShadow: '0 2px 10px rgba(0, 0, 0, 0.3)',
            transition: 'all 0.2s ease',
            '&:hover': {
              backgroundColor: 'rgba(158, 158, 158, 1)',
              transform: 'scale(1.1)',
              boxShadow: '0 4px 15px rgba(0, 0, 0, 0.4)'
            },
            '&:active': {
              transform: 'scale(0.95)'
            }
          }}
        >
          <Refresh sx={{ fontSize: 24 }} />
        </IconButton>
      </Box>
    </>
  );
};

export default TimerControls;
