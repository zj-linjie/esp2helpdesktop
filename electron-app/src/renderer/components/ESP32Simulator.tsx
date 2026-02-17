import React, { useState } from 'react';
import { Box } from '@mui/material';
import CircularScreen from './simulator/CircularScreen';
import NavigationDial from './simulator/NavigationDial';
import SystemMonitorPage from './simulator/SystemMonitorPage';
import ClockPage from './simulator/ClockPage';
import PomodoroPage from './simulator/PomodoroPage';
import WeatherPage from './simulator/WeatherPage';
import PhotoFramePage from './simulator/PhotoFramePage';
import AppLauncherPage from './simulator/AppLauncherPage';

interface ESP32SimulatorProps {
  systemStats: {
    cpu: number;
    memory: number;
    network: number;
  };
}

const ESP32Simulator: React.FC<ESP32SimulatorProps> = ({ systemStats }) => {
  const [currentPage, setCurrentPage] = useState<string>('home');

  const handleNavigate = (page: string) => {
    setCurrentPage(page);
  };

  const handleBack = () => {
    setCurrentPage('home');
  };

  const renderPage = () => {
    switch (currentPage) {
      case 'monitor':
        return (
          <SystemMonitorPage
            cpu={systemStats.cpu}
            memory={systemStats.memory}
            network={systemStats.network}
            onBack={handleBack}
          />
        );
      case 'clock':
        return <ClockPage onBack={handleBack} />;
      case 'timer':
        return <PomodoroPage onBack={handleBack} />;
      case 'weather':
        return <WeatherPage onBack={handleBack} />;
      case 'photo':
        return <PhotoFramePage onBack={handleBack} />;
      case 'quick':
        return <AppLauncherPage onBack={handleBack} />;
      case 'home':
      default:
        return <NavigationDial onNavigate={handleNavigate} />;
    }
  };

  return (
    <Box
      sx={{
        display: 'flex',
        justifyContent: 'center',
        alignItems: 'center',
        padding: 4,
        backgroundColor: '#0a0a0a',
        borderRadius: 2,
        minHeight: 500,
      }}
    >
      <CircularScreen size={360} onLongPress={currentPage !== 'home' ? handleBack : undefined}>
        {renderPage()}
      </CircularScreen>
    </Box>
  );
};

export default ESP32Simulator;
