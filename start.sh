#!/bin/bash

# ESP32 桌面助手启动脚本

echo "==================================="
echo "  ESP32 智能桌面助手"
echo "==================================="
echo ""

# 检查是否在项目根目录
if [ ! -d "electron-app" ]; then
    echo "❌ 错误：请在项目根目录运行此脚本"
    exit 1
fi

# 进入 electron-app 目录
cd electron-app

# 检查依赖是否已安装
if [ ! -d "node_modules" ]; then
    echo "📦 首次运行，正在安装依赖..."
    npm install
fi

echo "🚀 启动 Electron 应用..."
echo ""
echo "应用功能："
echo "  📊 控制中心 - 实时系统监控和设备管理"
echo "  📱 设备模拟器 - ESP32 圆形屏幕模拟器"
echo ""
echo "WebSocket 服务器: ws://localhost:8765"
echo ""
echo "按 Ctrl+C 停止应用"
echo "==================================="
echo ""

# 启动应用
npm start
