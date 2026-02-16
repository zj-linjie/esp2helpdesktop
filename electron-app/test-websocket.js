// Simple test script for WebSocket server
import { DeviceWebSocketServer } from './dist/main/websocket.js'

console.log('Starting WebSocket server test...')
const server = new DeviceWebSocketServer(8765)

// Keep the process running
setTimeout(() => {
  console.log('Test completed. Server is running.')
  console.log('Press Ctrl+C to exit.')
}, 2000)
