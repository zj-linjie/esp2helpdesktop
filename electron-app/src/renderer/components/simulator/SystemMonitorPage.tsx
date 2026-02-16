import React from 'react';
import { Box, Typography, IconButton } from '@mui/material';
import { ArrowBack } from '@mui/icons-material';

interface SystemMonitorPageProps {
  cpu: number;
  memory: number;
  network: number;
  onBack: () => void;
}

const SystemMonitorPage: React.FC<SystemMonitorPageProps> = ({ cpu, memory, network, onBack }) => {
  const CircularProgress = ({ value, label, color }: { value: number; label: string; color: string }) => {
    const radius = 45;
    const circumference = 2 * Math.PI * radius;
    const offset = circumference - (value / 100) * circumference;

    return (
      <Box sx={{ position: 'relative', display: 'inline-flex', flexDirection: 'column', alignItems: 'center' }}>
        <svg width="120" height="120">
          <circle
            cx="60"
            cy="60"
            r={radius}
            fill="none"
            stroke="rgba(255, 255, 255, 0.1)"
            strokeWidth="8"
          />
          <circle
            cx="60"
            cy="60"
            r={radius}
            fill="none"
            stroke={color}
            strokeWidth="8"
            strokeDasharray={circumference}
            strokeDashoffset={offset}
            strokeLinecap="round"
            transform="rotate(-90 60 60)"
            style={{ transition: 'stroke-dashoffset 0.5s ease' }}
          />
        </svg>
        <Box
          sx={{
            position: 'absolute',
            top: '50%',
            left: '50%',
            transform: 'translate(-50%, -50%)',
            textAlign: 'center',
          }}
        >
          <Typography variant="h5" sx={{ color: '#fff', fontWeight: 'bold' }}>
            {value.toFixed(1)}%
          </Typography>
        </Box>
        <Typography variant="caption" sx={{ color: 'rgba(255, 255, 255, 0.7)', mt: 1 }}>
          {label}
        </Typography>
      </Box>
    );
  };

  return (
    <Box
      sx={{
        width: '100%',
        height: '100%',
        background: 'linear-gradient(135deg, #1a1a2e 0%, #16213e 100%)',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        position: 'relative',
        padding: 2,
      }}
    >
      {/* Back Button */}
      <IconButton
        onClick={onBack}
        sx={{
          position: 'absolute',
          top: 10,
          left: 10,
          color: '#fff',
          backgroundColor: 'rgba(255, 255, 255, 0.1)',
          '&:hover': {
            backgroundColor: 'rgba(255, 255, 255, 0.2)',
          },
        }}
      >
        <ArrowBack />
      </IconButton>

      {/* Title */}
      <Typography
        variant="h6"
        sx={{
          color: '#fff',
          mb: 3,
          fontWeight: 'bold',
        }}
      >
        系统监控
      </Typography>

      {/* CPU and Memory */}
      <Box sx={{ display: 'flex', gap: 3, mb: 3 }}>
        <CircularProgress value={cpu} label="CPU" color="#1976d2" />
        <CircularProgress value={memory} label="内存" color="#9c27b0" />
      </Box>

      {/* Network Speed */}
      <Box
        sx={{
          backgroundColor: 'rgba(255, 255, 255, 0.1)',
          borderRadius: 2,
          padding: 2,
          textAlign: 'center',
          minWidth: 200,
        }}
      >
        <Typography variant="body2" sx={{ color: 'rgba(255, 255, 255, 0.7)' }}>
          网络速率
        </Typography>
        <Typography variant="h4" sx={{ color: '#2e7d32', fontWeight: 'bold', mt: 1 }}>
          {network.toFixed(1)}
        </Typography>
        <Typography variant="caption" sx={{ color: 'rgba(255, 255, 255, 0.7)' }}>
          MB/s
        </Typography>
      </Box>
    </Box>
  );
};

export default SystemMonitorPage;
