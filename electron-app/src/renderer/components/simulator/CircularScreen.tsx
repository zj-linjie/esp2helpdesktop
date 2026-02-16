import React from 'react';
import { Box } from '@mui/material';

interface CircularScreenProps {
  children: React.ReactNode;
  size?: number;
}

const CircularScreen: React.FC<CircularScreenProps> = ({ children, size = 360 }) => {
  return (
    <Box
      sx={{
        width: size,
        height: size,
        borderRadius: '50%',
        backgroundColor: '#000',
        position: 'relative',
        overflow: 'hidden',
        boxShadow: '0 0 30px rgba(0, 0, 0, 0.8), inset 0 0 20px rgba(255, 255, 255, 0.1)',
        border: '8px solid #1a1a1a',
      }}
    >
      <Box
        sx={{
          width: '100%',
          height: '100%',
          clipPath: 'circle(50%)',
          position: 'relative',
        }}
      >
        {children}
      </Box>
    </Box>
  );
};

export default CircularScreen;
