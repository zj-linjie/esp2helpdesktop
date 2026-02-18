/**
 * 马赛克占位图组件
 * 用于照片加载失败时显示
 */

import React from 'react';
import { Box } from '@mui/material';
import { BrokenImage } from '@mui/icons-material';

interface MosaicPlaceholderProps {
  size?: number;
}

const MosaicPlaceholder: React.FC<MosaicPlaceholderProps> = ({ size = 360 }) => {
  // 生成马赛克图案
  const generateMosaic = () => {
    const tiles: React.ReactElement[] = [];
    const tileSize = 40; // 每个方块大小
    const cols = Math.ceil(size / tileSize);
    const rows = Math.ceil(size / tileSize);

    // 灰度色板
    const colors = [
      '#2a2a2a',
      '#3a3a3a',
      '#4a4a4a',
      '#5a5a5a',
      '#6a6a6a',
      '#7a7a7a',
    ];

    for (let row = 0; row < rows; row++) {
      for (let col = 0; col < cols; col++) {
        const randomColor = colors[Math.floor(Math.random() * colors.length)];
        tiles.push(
          <Box
            key={`${row}-${col}`}
            sx={{
              width: tileSize,
              height: tileSize,
              backgroundColor: randomColor,
              transition: 'background-color 0.3s ease',
              '&:hover': {
                backgroundColor: '#8a8a8a',
              },
            }}
          />
        );
      }
    }

    return tiles;
  };

  return (
    <Box
      sx={{
        width: '100%',
        height: '100%',
        position: 'relative',
        overflow: 'hidden',
        backgroundColor: '#1a1a1a',
      }}
    >
      {/* 马赛克背景 */}
      <Box
        sx={{
          width: '100%',
          height: '100%',
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fill, 40px)',
          gridTemplateRows: 'repeat(auto-fill, 40px)',
          opacity: 0.6,
        }}
      >
        {generateMosaic()}
      </Box>

      {/* 中心图标 */}
      <Box
        sx={{
          position: 'absolute',
          top: '50%',
          left: '50%',
          transform: 'translate(-50%, -50%)',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          gap: 1,
        }}
      >
        <BrokenImage
          sx={{
            fontSize: '4rem',
            color: 'rgba(255, 255, 255, 0.3)',
            filter: 'drop-shadow(0 4px 8px rgba(0, 0, 0, 0.5))',
          }}
        />
        <Box
          sx={{
            fontSize: '0.75rem',
            color: 'rgba(255, 255, 255, 0.5)',
            fontWeight: 500,
            textAlign: 'center',
            textShadow: '0 2px 4px rgba(0, 0, 0, 0.8)',
          }}
        >
          照片加载失败
        </Box>
      </Box>
    </Box>
  );
};

export default MosaicPlaceholder;
