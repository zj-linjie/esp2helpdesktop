#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"

WebSocketsClient webSocket;
bool isConnected = false;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] 已断开连接");
      isConnected = false;
      break;

    case WStype_CONNECTED:
      Serial.println("[WebSocket] 已连接到服务器");
      isConnected = true;

      // 发送握手消息
      {
        StaticJsonDocument<512> doc;
        doc["type"] = "handshake";
        JsonObject data = doc.createNestedObject("data");
        data["device_id"] = DEVICE_ID;
        data["firmware_version"] = FIRMWARE_VERSION;
        data["screen_resolution"] = "360x360";
        data["screen_shape"] = "circular";

        String output;
        serializeJson(doc, output);
        webSocket.sendTXT(output);
      }
      break;

    case WStype_TEXT:
      Serial.printf("[WebSocket] 收到消息: %s\n", payload);

      {
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
          const char* type = doc["type"];

          if (strcmp(type, "system_info") == 0) {
            JsonObject data = doc["data"];

            float cpuUsage = data["cpu"]["usage"];
            float memPercentage = data["memory"]["percentage"];
            const char* time = data["time"];

            Serial.println("=== 系统信息 ===");
            Serial.printf("CPU: %.1f%%\n", cpuUsage);
            Serial.printf("内存: %.1f%%\n", memPercentage);
            Serial.printf("时间: %s\n", time);
          }
        }
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32-S3 桌面助手启动 ===");

  // 连接 WiFi
  Serial.printf("连接到 WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi 已连接");
  Serial.print("IP 地址: ");
  Serial.println(WiFi.localIP());

  // 连接 WebSocket
  Serial.printf("连接到 WebSocket 服务器: %s:%d\n", WS_SERVER_HOST, WS_SERVER_PORT);
  webSocket.begin(WS_SERVER_HOST, WS_SERVER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();

  // 每 5 秒发送心跳
  static unsigned long lastHeartbeat = 0;
  if (isConnected && millis() - lastHeartbeat > 5000) {
    StaticJsonDocument<256> doc;
    doc["type"] = "ping";
    JsonObject data = doc.createNestedObject("data");
    data["signal_strength"] = WiFi.RSSI();

    String output;
    serializeJson(doc, output);
    webSocket.sendTXT(output);

    lastHeartbeat = millis();
  }
}
