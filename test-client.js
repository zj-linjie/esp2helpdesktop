#!/usr/bin/env node

/**
 * ESP32 WebSocket 测试客户端
 * 模拟 ESP32 设备连接到 Electron 服务器
 */

const WebSocket = require('ws');

const WS_SERVER = 'ws://localhost:8765';

console.log('=== ESP32 WebSocket 测试客户端 ===\n');
console.log(`连接到服务器: ${WS_SERVER}`);

const ws = new WebSocket(WS_SERVER);

ws.on('open', () => {
  console.log('✓ WebSocket 连接成功\n');

  // 发送握手消息
  const handshake = {
    type: 'handshake',
    data: {
      device_id: 'test_client_001',
      firmware_version: '3.0.0',
      screen_resolution: '360x360',
      screen_shape: 'circular',
      charging_status: 'full',
      sd_card_status: 'mounted',
      psram_size: 8388608,
      features: ['photo_frame', 'weather', 'clock', 'voice', 'system_monitor', 'pomodoro', 'shortcuts']
    }
  };

  console.log('→ 发送握手消息');
  ws.send(JSON.stringify(handshake));

  // 每 5 秒发送心跳
  setInterval(() => {
    const ping = {
      type: 'ping',
      data: {
        charging_status: 'full',
        signal_strength: -45,
        current_page: 'system_monitor'
      }
    };
    console.log('→ 发送心跳包');
    ws.send(JSON.stringify(ping));
  }, 5000);
});

ws.on('message', (data) => {
  try {
    const message = JSON.parse(data.toString());

    console.log(`\n← 收到消息: ${message.type}`);

    if (message.type === 'handshake_ack') {
      console.log('  服务器版本:', message.data.server_version);
      console.log('  更新间隔:', message.data.update_interval, 'ms');
    } else if (message.type === 'system_info') {
      const { cpu, memory, network, time, date } = message.data;
      console.log('  === 系统信息 ===');
      console.log(`  CPU: ${cpu.usage}%`);
      console.log(`  内存: ${memory.percentage}% (${memory.used}GB / ${memory.total}GB)`);
      console.log(`  网络: ↑${network.upload}KB/s ↓${network.download}KB/s`);
      console.log(`  时间: ${time} ${date}`);
    } else {
      console.log('  数据:', JSON.stringify(message.data, null, 2));
    }
  } catch (error) {
    console.error('解析消息失败:', error.message);
  }
});

ws.on('close', () => {
  console.log('\n✗ WebSocket 连接已关闭');
  process.exit(0);
});

ws.on('error', (error) => {
  console.error('\n✗ WebSocket 错误:', error.message);
  process.exit(1);
});

// 优雅退出
process.on('SIGINT', () => {
  console.log('\n\n正在关闭连接...');
  ws.close();
});
