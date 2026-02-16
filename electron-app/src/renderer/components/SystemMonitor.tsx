import React from 'react';
import { Card, CardContent, Typography, Box, Grid } from '@mui/material';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';
import { Memory, Speed, NetworkCheck } from '@mui/icons-material';

interface SystemData {
  timestamp: string;
  cpu: number;
  memory: number;
  network: number;
}

interface SystemMonitorProps {
  data: SystemData[];
  currentStats: {
    cpu: number;
    memory: number;
    network: number;
  };
}

const SystemMonitor: React.FC<SystemMonitorProps> = ({ data, currentStats }) => {
  return (
    <Box>
      <Grid container spacing={2} sx={{ mb: 2 }}>
        <Grid size={{ xs: 12, md: 4 }}>
          <Card sx={{ bgcolor: 'background.paper' }}>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <Speed sx={{ mr: 1, color: 'primary.main' }} />
                <Typography variant="h6">CPU 使用率</Typography>
              </Box>
              <Typography variant="h3" color="primary">
                {currentStats.cpu.toFixed(1)}%
              </Typography>
            </CardContent>
          </Card>
        </Grid>
        <Grid size={{ xs: 12, md: 4 }}>
          <Card sx={{ bgcolor: 'background.paper' }}>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <Memory sx={{ mr: 1, color: 'secondary.main' }} />
                <Typography variant="h6">内存使用率</Typography>
              </Box>
              <Typography variant="h3" color="secondary">
                {currentStats.memory.toFixed(1)}%
              </Typography>
            </CardContent>
          </Card>
        </Grid>
        <Grid size={{ xs: 12, md: 4 }}>
          <Card sx={{ bgcolor: 'background.paper' }}>
            <CardContent>
              <Box sx={{ display: 'flex', alignItems: 'center', mb: 1 }}>
                <NetworkCheck sx={{ mr: 1, color: 'success.main' }} />
                <Typography variant="h6">网络速率</Typography>
              </Box>
              <Typography variant="h3" color="success.main">
                {currentStats.network.toFixed(1)} MB/s
              </Typography>
            </CardContent>
          </Card>
        </Grid>
      </Grid>

      <Card sx={{ bgcolor: 'background.paper' }}>
        <CardContent>
          <Typography variant="h6" sx={{ mb: 2 }}>系统性能趋势</Typography>
          <ResponsiveContainer width="100%" height={300}>
            <LineChart data={data}>
              <CartesianGrid strokeDasharray="3 3" stroke="#444" />
              <XAxis dataKey="timestamp" stroke="#888" />
              <YAxis stroke="#888" />
              <Tooltip
                contentStyle={{ backgroundColor: '#1e1e1e', border: '1px solid #444' }}
                labelStyle={{ color: '#fff' }}
              />
              <Legend />
              <Line type="monotone" dataKey="cpu" stroke="#1976d2" name="CPU %" strokeWidth={2} />
              <Line type="monotone" dataKey="memory" stroke="#9c27b0" name="内存 %" strokeWidth={2} />
              <Line type="monotone" dataKey="network" stroke="#2e7d32" name="网络 MB/s" strokeWidth={2} />
            </LineChart>
          </ResponsiveContainer>
        </CardContent>
      </Card>
    </Box>
  );
};

export default SystemMonitor;
