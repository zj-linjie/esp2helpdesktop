import React from 'react';
import { Box, IconButton } from '@mui/material';
import { PlayArrow, Pause, Refresh } from '@mui/icons-material';

interface StopwatchControlsProps {
  isRunning: boolean;
  onStart: () => void;
  onPause: () => void;
  onReset: () => void;
}

/**
 * 计时器控制按钮组件
 * 中心大按钮（开始/暂停），右下角重置按钮
 */
const StopwatchControls: React.FC<StopwatchControlsProps> = ({
  isRunning,
  onStart,
  onPause,
  onReset
}) => {
  return (
    <>
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
            transition: 'all 0.3s ease',
            '&:hover': {
              backgroundColor: isRunning ? 'rgba(244, 67, 54, 1)' : 'rgba(76, 175, 80, 1)',
              transform: 'scale(1.1)',
              boxShadow: isRunning
                ? '0 6px 30px rgba(244, 67, 54, 0.7)'
                : '0 6px 30px rgba(76, 175, 80, 0.7)'
            },
            '&:active': {
              transform: 'scale(0.95)'
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

export default StopwatchControls;
