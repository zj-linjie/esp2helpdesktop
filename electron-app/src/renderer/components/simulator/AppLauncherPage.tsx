/**
 * Apple Watch 风格的应用启动器页面
 * 蜂窝布局，支持拖拽查看
 */

import React, { useState, useEffect, useRef } from 'react';
import { Box, Typography } from '@mui/material';
import { appLauncherService, MacApp } from '../../services/appLauncherService';

interface AppLauncherPageProps {
  onBack: () => void;
}

const AppLauncherPage: React.FC<AppLauncherPageProps> = ({ onBack }) => {
  const [apps, setApps] = useState<MacApp[]>([]);
  const [offset, setOffset] = useState({ x: 0, y: 0 }); // 当前偏移量（持久化）
  const [isDragging, setIsDragging] = useState(false);
  const [dragStart, setDragStart] = useState<{ x: number; y: number; offsetX: number; offsetY: number } | null>(null);
  const [longPressTimer, setLongPressTimer] = useState<NodeJS.Timeout | null>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  // Load apps on mount
  useEffect(() => {
    loadApps();
  }, []);

  const loadApps = async () => {
    const savedApps = appLauncherService.getApps();
    if (savedApps.length === 0) {
      // 尝试扫描真实应用
      const scannedApps = await appLauncherService.scanApplications();
      console.log('加载的应用数据:', scannedApps.map(app => ({
        name: app.name,
        hasIcon: !!app.icon,
        iconType: app.icon?.startsWith('data:') ? 'base64' : 'emoji',
        iconLength: app.icon?.length || 0,
      })));
      setApps(scannedApps.slice(0, 12)); // 最多12个
    } else {
      setApps(savedApps);
    }
  };

  // Handle app launch
  const handleAppClick = async (app: MacApp, e: React.MouseEvent) => {
    e.stopPropagation();
    if (isDragging) return;

    console.log('启动应用:', app.name);
    const success = await appLauncherService.launchApp(app.path);

    if (!success) {
      alert(`启动 ${app.name} 失败`);
    }
  };

  // Handle mouse/touch drag
  const handleMouseDown = (e: React.MouseEvent) => {
    if ((e.target as HTMLElement).closest('.app-icon')) {
      return;
    }

    setDragStart({
      x: e.clientX,
      y: e.clientY,
      offsetX: offset.x,
      offsetY: offset.y,
    });
    setIsDragging(false);

    const timer = setTimeout(() => {
      if (!isDragging) {
        onBack();
      }
    }, 800);
    setLongPressTimer(timer);
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!dragStart) return;

    const deltaX = e.clientX - dragStart.x;
    const deltaY = e.clientY - dragStart.y;

    // 开始拖拽
    if (Math.abs(deltaX) > 5 || Math.abs(deltaY) > 5) {
      setIsDragging(true);

      // 更新偏移量（保持拖拽位置）
      const newOffsetX = dragStart.offsetX + deltaX;
      const newOffsetY = dragStart.offsetY + deltaY;

      // 边界限制（可选）
      const maxOffset = 200;
      const minOffset = -200;

      setOffset({
        x: Math.max(minOffset, Math.min(maxOffset, newOffsetX)),
        y: Math.max(minOffset, Math.min(maxOffset, newOffsetY)),
      });

      // 取消长按
      if (longPressTimer) {
        clearTimeout(longPressTimer);
        setLongPressTimer(null);
      }
    }
  };

  const handleMouseUp = () => {
    setDragStart(null);
    setIsDragging(false);

    if (longPressTimer) {
      clearTimeout(longPressTimer);
      setLongPressTimer(null);
    }
  };

  // Touch events
  const handleTouchStart = (e: React.TouchEvent) => {
    if ((e.target as HTMLElement).closest('.app-icon')) {
      return;
    }

    const touch = e.touches[0];
    setDragStart({
      x: touch.clientX,
      y: touch.clientY,
      offsetX: offset.x,
      offsetY: offset.y,
    });
    setIsDragging(false);

    const timer = setTimeout(() => {
      if (!isDragging) {
        onBack();
      }
    }, 800);
    setLongPressTimer(timer);
  };

  const handleTouchMove = (e: React.TouchEvent) => {
    if (!dragStart) return;

    const touch = e.touches[0];
    const deltaX = touch.clientX - dragStart.x;
    const deltaY = touch.clientY - dragStart.y;

    if (Math.abs(deltaX) > 5 || Math.abs(deltaY) > 5) {
      setIsDragging(true);

      const newOffsetX = dragStart.offsetX + deltaX;
      const newOffsetY = dragStart.offsetY + deltaY;

      const maxOffset = 200;
      const minOffset = -200;

      setOffset({
        x: Math.max(minOffset, Math.min(maxOffset, newOffsetX)),
        y: Math.max(minOffset, Math.min(maxOffset, newOffsetY)),
      });

      if (longPressTimer) {
        clearTimeout(longPressTimer);
        setLongPressTimer(null);
      }
    }
  };

  const handleTouchEnd = () => {
    setDragStart(null);
    setIsDragging(false);

    if (longPressTimer) {
      clearTimeout(longPressTimer);
      setLongPressTimer(null);
    }
  };

  // Apple Watch 蜂窝布局算法
  const getHoneycombPosition = (index: number) => {
    const centerX = 180;
    const centerY = 180;
    const iconSize = 52;
    const spacing = 16; // 增加间距从 8 到 16
    const radius = iconSize + spacing;

    // 中心位置
    if (index === 0) {
      return { x: centerX, y: centerY };
    }

    // 计算在第几环
    let ring = 1;
    let posInRing = index - 1;
    let appsInPrevRings = 0;

    while (posInRing >= ring * 6) {
      appsInPrevRings += ring * 6;
      posInRing -= ring * 6;
      ring++;
    }

    // 六边形环形布局
    const appsInRing = ring * 6;
    const angle = (posInRing / appsInRing) * Math.PI * 2 - Math.PI / 2;
    const ringRadius = ring * radius * 1.0; // 增加环形半径系数从 0.866 到 1.0

    const x = centerX + Math.cos(angle) * ringRadius;
    const y = centerY + Math.sin(angle) * ringRadius;

    return { x, y };
  };

  return (
    <Box
      ref={containerRef}
      onMouseDown={handleMouseDown}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
      onTouchStart={handleTouchStart}
      onTouchMove={handleTouchMove}
      onTouchEnd={handleTouchEnd}
      sx={{
        width: '100%',
        height: '100%',
        background: 'radial-gradient(circle at center, #1a1a1a 0%, #000000 100%)',
        position: 'relative',
        overflow: 'hidden',
        cursor: isDragging ? 'grabbing' : 'grab',
        userSelect: 'none',
      }}
    >
      {/* Apps Container */}
      <Box
        sx={{
          position: 'absolute',
          top: 0,
          left: 0,
          width: '100%',
          height: '100%',
          transform: `translate(${offset.x}px, ${offset.y}px)`,
          transition: isDragging ? 'none' : 'transform 0.2s ease-out',
        }}
      >
        {apps.map((app, index) => {
          const pos = getHoneycombPosition(index);

          return (
            <Box
              key={app.id}
              className="app-icon"
              onClick={(e) => handleAppClick(app, e)}
              sx={{
                position: 'absolute',
                left: pos.x,
                top: pos.y,
                transform: 'translate(-50%, -50%)',
                width: '52px',
                height: '70px',
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                justifyContent: 'flex-start',
                gap: '4px',
                cursor: 'pointer',
                transition: 'transform 0.2s ease',
                '&:hover': {
                  transform: 'translate(-50%, -50%) scale(1.1)',
                },
                '&:active': {
                  transform: 'translate(-50%, -50%) scale(0.95)',
                },
              }}
            >
              {/* App Icon */}
              <Box
                sx={{
                  width: '52px',
                  height: '52px',
                  borderRadius: '50%',
                  backgroundColor: app.icon?.startsWith('LETTER:')
                    ? app.icon.split(':')[2]
                    : '#2a2a2a',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  boxShadow: '0 4px 12px rgba(0, 0, 0, 0.5), inset 0 1px 0 rgba(255, 255, 255, 0.1)',
                  border: '2px solid rgba(255, 255, 255, 0.1)',
                  transition: 'all 0.2s ease',
                  overflow: 'hidden',
                  position: 'relative',
                }}
              >
                {app.icon?.startsWith('LETTER:') ? (
                  <span style={{
                    fontSize: '1.5rem',
                    fontWeight: 'bold',
                    color: '#ffffff',
                    textShadow: '0 2px 4px rgba(0, 0, 0, 0.3)',
                  }}>
                    {app.icon.split(':')[1]}
                  </span>
                ) : (
                  <span style={{ fontSize: '1.8rem' }}>{app.icon || '📱'}</span>
                )}
              </Box>

              {/* App Name */}
              <Typography
                sx={{
                  fontSize: '0.6rem',
                  color: 'rgba(255, 255, 255, 0.9)',
                  fontWeight: 500,
                  textAlign: 'center',
                  maxWidth: '60px',
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap',
                  textShadow: '0 1px 2px rgba(0, 0, 0, 0.8)',
                }}
              >
                {app.name}
              </Typography>
            </Box>
          );
        })}
      </Box>

      {/* Long Press Hint */}
      <Typography
        sx={{
          position: 'absolute',
          bottom: '8px',
          left: '50%',
          transform: 'translateX(-50%)',
          fontSize: '0.6rem',
          color: 'rgba(255, 255, 255, 0.4)',
          fontWeight: 500,
          textShadow: '0 1px 2px rgba(0, 0, 0, 0.8)',
        }}
      >
        长按屏幕返回
      </Typography>
    </Box>
  );
};

export default AppLauncherPage;
