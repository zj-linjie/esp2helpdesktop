#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "display/scr_st77916.h"

WebSocketsClient webSocket;
bool isConnected = false;

enum UiPage {
  UI_PAGE_HOME = 0,
  UI_PAGE_MONITOR = 1,
  UI_PAGE_CLOCK = 2,
  UI_PAGE_COUNT = 3
};

struct TouchGestureState {
  bool pressed = false;
  bool longPressHandled = false;
  lv_point_t startPoint = {0, 0};
  uint32_t startMs = 0;
};

static constexpr int16_t SWIPE_THRESHOLD = 55;
static constexpr int16_t SWIPE_DIRECTION_MARGIN = 10;
static constexpr uint32_t LONG_PRESS_MS = 800;
static constexpr int16_t CENTER_RADIUS = 72;

static lv_obj_t *pages[UI_PAGE_COUNT] = {nullptr};
static int currentPage = UI_PAGE_HOME;
static TouchGestureState gestureState;

static lv_obj_t *pageIndicatorLabel = nullptr;

static lv_obj_t *homeWifiLabel = nullptr;
static lv_obj_t *homeWsLabel = nullptr;

static lv_obj_t *wifiLabel = nullptr;
static lv_obj_t *wsLabel = nullptr;
static lv_obj_t *statsLabel = nullptr;
static lv_obj_t *cpuArc = nullptr;
static lv_obj_t *memArc = nullptr;
static lv_obj_t *cpuValueLabel = nullptr;
static lv_obj_t *memValueLabel = nullptr;
static lv_obj_t *upValueLabel = nullptr;
static lv_obj_t *downValueLabel = nullptr;

static lv_obj_t *clockLabel = nullptr;
static lv_obj_t *clockSecondLabel = nullptr;
static lv_obj_t *clockDateLabel = nullptr;
static lv_obj_t *clockSecondArc = nullptr;

static bool ntpConfigured = false;
static bool ntpSynced = false;
static uint32_t lastNtpSyncAttemptMs = 0;

static constexpr uint32_t NTP_RETRY_INTERVAL_MS = 30000;
static const char *NTP_TZ_INFO = "CST-8";
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.nist.gov";
static const char *NTP_SERVER_3 = "ntp.aliyun.com";
static const char *WEEKDAY_SHORT[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int clampPercent(float value) {
  int v = (int)(value + 0.5f);
  if (v < 0) return 0;
  if (v > 100) return 100;
  return v;
}

static void setupNtpTime() {
  setenv("TZ", NTP_TZ_INFO, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  ntpConfigured = true;
  lastNtpSyncAttemptMs = millis();
}

static bool trySyncNtpTime(uint32_t waitMs) {
  if (!ntpConfigured) {
    return false;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, waitMs)) {
    ntpSynced = true;
    return true;
  }
  return false;
}

static bool getActiveTouchPoint(lv_point_t *point) {
  lv_indev_t *indev = lv_indev_get_act();
  if (indev == nullptr || point == nullptr) {
    return false;
  }

  lv_indev_get_point(indev, point);
  return true;
}

static bool isInsideCenter(const lv_point_t &point) {
  lv_disp_t *disp = lv_disp_get_default();
  if (disp == nullptr) {
    return false;
  }

  int16_t cx = lv_disp_get_hor_res(disp) / 2;
  int16_t cy = lv_disp_get_ver_res(disp) / 2;
  int32_t dx = (int32_t)point.x - cx;
  int32_t dy = (int32_t)point.y - cy;
  return (dx * dx + dy * dy) <= (CENTER_RADIUS * CENTER_RADIUS);
}

static void updatePageIndicator() {
  if (pageIndicatorLabel != nullptr) {
    lv_label_set_text_fmt(pageIndicatorLabel, "%d/%d", currentPage + 1, UI_PAGE_COUNT);
  }
}

static void showPage(int pageIndex) {
  if (pageIndex < 0) {
    pageIndex += UI_PAGE_COUNT;
  }
  if (pageIndex >= UI_PAGE_COUNT) {
    pageIndex %= UI_PAGE_COUNT;
  }

  currentPage = pageIndex;
  for (int i = 0; i < UI_PAGE_COUNT; ++i) {
    if (pages[i] == nullptr) {
      continue;
    }
    if (i == currentPage) {
      lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  updatePageIndicator();
}

static lv_obj_t *createBasePage() {
  lv_obj_t *page = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(page);
  lv_obj_set_size(page, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  return page;
}

static void updateClockDisplay() {
  if (clockLabel == nullptr) {
    return;
  }

  if (ntpConfigured) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      ntpSynced = true;
      lv_label_set_text_fmt(clockLabel, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

      if (clockSecondLabel != nullptr) {
        lv_label_set_text_fmt(clockSecondLabel, ":%02d", timeinfo.tm_sec);
      }
      if (clockDateLabel != nullptr) {
        int wday = (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) ? timeinfo.tm_wday : 0;
        lv_label_set_text_fmt(
          clockDateLabel,
          "%04d-%02d-%02d %s",
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          WEEKDAY_SHORT[wday]
        );
      }
      if (clockSecondArc != nullptr) {
        lv_arc_set_value(clockSecondArc, timeinfo.tm_sec);
      }
      return;
    }
  }

  uint32_t totalSeconds = millis() / 1000;
  uint32_t days = totalSeconds / 86400;
  uint32_t h = (totalSeconds / 3600) % 24;
  uint32_t m = (totalSeconds / 60) % 60;
  uint32_t s = totalSeconds % 60;
  lv_label_set_text_fmt(clockLabel, "%02lu:%02lu", h, m);

  if (clockSecondLabel != nullptr) {
    lv_label_set_text_fmt(clockSecondLabel, ":%02lu", s);
  }
  if (clockDateLabel != nullptr) {
    lv_label_set_text_fmt(clockDateLabel, "NTP syncing... Uptime %lud %02luh", days, h);
  }
  if (clockSecondArc != nullptr) {
    lv_arc_set_value(clockSecondArc, (int)s);
  }
}

static void clockTimerCallback(lv_timer_t *timer) {
  (void)timer;
  updateClockDisplay();
}

static void gestureEventCallback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    lv_point_t point = {0, 0};
    if (getActiveTouchPoint(&point)) {
      gestureState.pressed = true;
      gestureState.longPressHandled = false;
      gestureState.startPoint = point;
      gestureState.startMs = millis();
    }
    return;
  }

  if (code == LV_EVENT_PRESSING) {
    if (!gestureState.pressed || gestureState.longPressHandled) {
      return;
    }

    if ((millis() - gestureState.startMs) < LONG_PRESS_MS) {
      return;
    }

    lv_point_t point = {0, 0};
    if (!getActiveTouchPoint(&point)) {
      return;
    }

    if (isInsideCenter(point)) {
      gestureState.longPressHandled = true;
      if (currentPage != UI_PAGE_HOME) {
        showPage(UI_PAGE_HOME);
      }
    }
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    if (!gestureState.pressed) {
      return;
    }

    if (!gestureState.longPressHandled) {
      lv_point_t endPoint = gestureState.startPoint;
      getActiveTouchPoint(&endPoint);

      int32_t dx = (int32_t)endPoint.x - gestureState.startPoint.x;
      int32_t dy = (int32_t)endPoint.y - gestureState.startPoint.y;
      int32_t adx = (dx >= 0) ? dx : -dx;
      int32_t ady = (dy >= 0) ? dy : -dy;

      if (adx >= SWIPE_THRESHOLD && adx > (ady + SWIPE_DIRECTION_MARGIN)) {
        if (dx < 0) {
          showPage(currentPage + 1);
        } else {
          showPage(currentPage - 1);
        }
      }
    }

    gestureState = TouchGestureState();
    return;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    gestureState = TouchGestureState();
  }
}

static void createUi() {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_text_color(lv_scr_act(), lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  // Page 1: Home
  pages[UI_PAGE_HOME] = createBasePage();
  lv_obj_t *homeTitle = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeTitle, "ESP32 Desktop");
  lv_obj_align(homeTitle, LV_ALIGN_TOP_MID, 0, 18);

  homeWifiLabel = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeWifiLabel, "WiFi: connecting...");
  lv_obj_align(homeWifiLabel, LV_ALIGN_CENTER, 0, -18);

  homeWsLabel = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeWsLabel, "WS: disconnected");
  lv_obj_align(homeWsLabel, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *homeHint = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeHint, "Swipe Left/Right to switch\nLong-press center to go Home");
  lv_obj_set_style_text_align(homeHint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(homeHint, LV_ALIGN_BOTTOM_MID, 0, -34);

  // Page 2: Monitor
  pages[UI_PAGE_MONITOR] = createBasePage();
  lv_obj_t *monitorTitle = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(monitorTitle, "System Monitor");
  lv_obj_align(monitorTitle, LV_ALIGN_TOP_MID, 0, 18);

  wifiLabel = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(wifiLabel, "WiFi: connecting...");
  lv_obj_align(wifiLabel, LV_ALIGN_TOP_MID, 0, 44);

  wsLabel = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(wsLabel, "WS: disconnected");
  lv_obj_align(wsLabel, LV_ALIGN_TOP_MID, 0, 62);

  cpuArc = lv_arc_create(pages[UI_PAGE_MONITOR]);
  lv_obj_set_size(cpuArc, 120, 120);
  lv_obj_align(cpuArc, LV_ALIGN_TOP_LEFT, 34, 88);
  lv_arc_set_rotation(cpuArc, 135);
  lv_arc_set_bg_angles(cpuArc, 0, 270);
  lv_arc_set_range(cpuArc, 0, 100);
  lv_arc_set_value(cpuArc, 0);
  lv_obj_set_style_arc_width(cpuArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(cpuArc, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_arc_width(cpuArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(cpuArc, lv_color_hex(0x26C6DA), LV_PART_INDICATOR);
  lv_obj_set_style_opa(cpuArc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(cpuArc, LV_OBJ_FLAG_CLICKABLE);

  cpuValueLabel = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(cpuValueLabel, "CPU\n0%");
  lv_obj_set_style_text_align(cpuValueLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(cpuValueLabel, LV_ALIGN_TOP_LEFT, 76, 128);

  memArc = lv_arc_create(pages[UI_PAGE_MONITOR]);
  lv_obj_set_size(memArc, 120, 120);
  lv_obj_align(memArc, LV_ALIGN_TOP_RIGHT, -34, 88);
  lv_arc_set_rotation(memArc, 135);
  lv_arc_set_bg_angles(memArc, 0, 270);
  lv_arc_set_range(memArc, 0, 100);
  lv_arc_set_value(memArc, 0);
  lv_obj_set_style_arc_width(memArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(memArc, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_arc_width(memArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(memArc, lv_color_hex(0x66BB6A), LV_PART_INDICATOR);
  lv_obj_set_style_opa(memArc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(memArc, LV_OBJ_FLAG_CLICKABLE);

  memValueLabel = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(memValueLabel, "MEM\n0%");
  lv_obj_set_style_text_align(memValueLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(memValueLabel, LV_ALIGN_TOP_RIGHT, -76, 128);

  lv_obj_t *netPanel = lv_obj_create(pages[UI_PAGE_MONITOR]);
  lv_obj_set_size(netPanel, 292, 62);
  lv_obj_align(netPanel, LV_ALIGN_BOTTOM_MID, 0, -38);
  lv_obj_set_style_radius(netPanel, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(netPanel, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(netPanel, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
  lv_obj_set_style_border_width(netPanel, 1, LV_PART_MAIN);
  lv_obj_clear_flag(netPanel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *upLabel = lv_label_create(netPanel);
  lv_label_set_text(upLabel, "UP");
  lv_obj_set_style_text_color(upLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(upLabel, LV_ALIGN_LEFT_MID, 14, -10);

  upValueLabel = lv_label_create(netPanel);
  lv_label_set_text(upValueLabel, "-- KB/s");
  lv_obj_align(upValueLabel, LV_ALIGN_LEFT_MID, 44, -10);

  lv_obj_t *downLabel = lv_label_create(netPanel);
  lv_label_set_text(downLabel, "DOWN");
  lv_obj_set_style_text_color(downLabel, lv_color_hex(0xA5D6A7), LV_PART_MAIN);
  lv_obj_align(downLabel, LV_ALIGN_LEFT_MID, 14, 14);

  downValueLabel = lv_label_create(netPanel);
  lv_label_set_text(downValueLabel, "-- KB/s");
  lv_obj_align(downValueLabel, LV_ALIGN_LEFT_MID, 64, 14);

  statsLabel = lv_label_create(pages[UI_PAGE_MONITOR]);
  lv_label_set_text(statsLabel, "");
  lv_obj_add_flag(statsLabel, LV_OBJ_FLAG_HIDDEN);

  // Page 3: Clock
  pages[UI_PAGE_CLOCK] = createBasePage();
  lv_obj_t *clockTitle = lv_label_create(pages[UI_PAGE_CLOCK]);
  lv_label_set_text(clockTitle, "Clock");
  lv_obj_align(clockTitle, LV_ALIGN_TOP_MID, 0, 20);

  clockSecondArc = lv_arc_create(pages[UI_PAGE_CLOCK]);
  lv_obj_set_size(clockSecondArc, 232, 232);
  lv_obj_align(clockSecondArc, LV_ALIGN_CENTER, 0, 8);
  lv_arc_set_rotation(clockSecondArc, 270);
  lv_arc_set_bg_angles(clockSecondArc, 0, 360);
  lv_arc_set_range(clockSecondArc, 0, 59);
  lv_arc_set_value(clockSecondArc, 0);
  lv_obj_set_style_arc_width(clockSecondArc, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_color(clockSecondArc, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_set_style_arc_width(clockSecondArc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(clockSecondArc, lv_color_hex(0xFFA726), LV_PART_INDICATOR);
  lv_obj_set_style_opa(clockSecondArc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(clockSecondArc, LV_OBJ_FLAG_CLICKABLE);

  clockLabel = lv_label_create(pages[UI_PAGE_CLOCK]);
  lv_label_set_text(clockLabel, "00:00");
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, -6);

  clockSecondLabel = lv_label_create(pages[UI_PAGE_CLOCK]);
  lv_label_set_text(clockSecondLabel, ":00");
  lv_obj_set_style_text_color(clockSecondLabel, lv_color_hex(0xFFCC80), LV_PART_MAIN);
  lv_obj_align(clockSecondLabel, LV_ALIGN_CENTER, 0, 36);

  clockDateLabel = lv_label_create(pages[UI_PAGE_CLOCK]);
  lv_label_set_text(clockDateLabel, "Uptime 0d 00h 00m");
  lv_obj_align(clockDateLabel, LV_ALIGN_BOTTOM_MID, 0, -54);

  lv_obj_t *clockHint = lv_label_create(pages[UI_PAGE_CLOCK]);
  lv_label_set_text(clockHint, "Long-press center to return Home");
  lv_obj_align(clockHint, LV_ALIGN_BOTTOM_MID, 0, -30);

  // Global page indicator
  pageIndicatorLabel = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(pageIndicatorLabel, lv_color_hex(0x8F8F8F), LV_PART_MAIN);
  lv_obj_align(pageIndicatorLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

  // Gesture overlay
  lv_obj_t *gestureLayer = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(gestureLayer);
  lv_obj_set_size(gestureLayer, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(gestureLayer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(gestureLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gestureLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gestureLayer, gestureEventCallback, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(gestureLayer, gestureEventCallback, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(gestureLayer, gestureEventCallback, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(gestureLayer, gestureEventCallback, LV_EVENT_PRESS_LOST, nullptr);

  lv_obj_move_foreground(pageIndicatorLabel);
  showPage(UI_PAGE_HOME);
  updateClockDisplay();
  lv_timer_create(clockTimerCallback, 1000, nullptr);
}

static void setWifiStatus(const String &text) {
  if (homeWifiLabel != nullptr) {
    lv_label_set_text(homeWifiLabel, text.c_str());
  }
  if (wifiLabel != nullptr) {
    lv_label_set_text(wifiLabel, text.c_str());
  }
}

static void setWsStatus(const String &text) {
  if (homeWsLabel != nullptr) {
    lv_label_set_text(homeWsLabel, text.c_str());
  }
  if (wsLabel != nullptr) {
    lv_label_set_text(wsLabel, text.c_str());
  }
}

static void setStats(float cpu, float memory, float upload, float download) {
  int cpuPercent = clampPercent(cpu);
  int memPercent = clampPercent(memory);

  if (cpuArc != nullptr) {
    lv_arc_set_value(cpuArc, cpuPercent);
  }
  if (memArc != nullptr) {
    lv_arc_set_value(memArc, memPercent);
  }
  if (cpuValueLabel != nullptr) {
    lv_label_set_text_fmt(cpuValueLabel, "CPU\n%d%%", cpuPercent);
  }
  if (memValueLabel != nullptr) {
    lv_label_set_text_fmt(memValueLabel, "MEM\n%d%%", memPercent);
  }
  if (upValueLabel != nullptr) {
    lv_label_set_text_fmt(upValueLabel, "%.1f KB/s", upload);
  }
  if (downValueLabel != nullptr) {
    lv_label_set_text_fmt(downValueLabel, "%.1f KB/s", download);
  }

  if (statsLabel != nullptr) {
    lv_label_set_text_fmt(
      statsLabel,
      "CPU : %.1f%%\nMEM : %.1f%%\nUP  : %.1f KB/s\nDOWN: %.1f KB/s",
      cpu,
      memory,
      upload,
      download
    );
  }
}

static void sendHandshake() {
  StaticJsonDocument<384> doc;
  doc["type"] = "handshake";
  doc["clientType"] = "esp32_device";
  doc["deviceId"] = DEVICE_ID;

  JsonObject data = doc.createNestedObject("data");
  data["firmwareVersion"] = FIRMWARE_VERSION;
  data["screenResolution"] = "360x360";
  data["screenShape"] = "circular";

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["type"] = "heartbeat";

  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["uptime"] = millis() / 1000;
  data["wifiSignal"] = WiFi.RSSI();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void handleSystemStats(const JsonObjectConst &data) {
  float cpu = data["cpu"] | 0;
  float memory = data["memory"] | 0;
  float upload = data["network"]["upload"] | 0;
  float download = data["network"]["download"] | 0;

  setStats(cpu, memory, upload, download);
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] disconnected");
      isConnected = false;
      setWsStatus("WS: disconnected");
      break;

    case WStype_CONNECTED:
      Serial.println("[WebSocket] connected");
      isConnected = true;
      setWsStatus("WS: connected");
      sendHandshake();
      break;

    case WStype_TEXT: {
      Serial.printf("[WebSocket] message: %s\n", payload);

      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.printf("[WebSocket] JSON parse failed: %s\n", error.c_str());
        break;
      }

      const char *messageType = doc["type"] | "";
      if (strcmp(messageType, "handshake_ack") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *serverVersion = data["serverVersion"] | data["server_version"] | "unknown";
        long updateInterval = data["updateInterval"] | data["update_interval"] | 0;
        Serial.printf("[WebSocket] handshake ok: serverVersion=%s updateInterval=%ldms\n", serverVersion, updateInterval);
      } else if (strcmp(messageType, "system_stats") == 0) {
        handleSystemStats(doc["data"].as<JsonObjectConst>());
      } else if (strcmp(messageType, "system_info") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        float cpuUsage = data["cpu"]["usage"] | 0;
        float memPercentage = data["memory"]["percentage"] | 0;
        setStats(cpuUsage, memPercentage, 0, 0);
      } else {
        Serial.printf("[WebSocket] unhandled type: %s\n", messageType);
      }
      break;
    }
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n=== ESP32-S3 Desktop Assistant ===");
  scr_lvgl_init();
  createUi();

  Serial.printf("Connecting WiFi: %s\n", WIFI_SSID);
  setWifiStatus("WiFi: connecting...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    lv_timer_handler();
    delay(200);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  setWifiStatus(String("WiFi: ") + WiFi.localIP().toString());

  setupNtpTime();
  if (trySyncNtpTime(2500)) {
    Serial.println("NTP time sync OK");
  } else {
    Serial.println("NTP time sync pending (will retry in background)");
  }

  Serial.printf("Connecting WebSocket: %s:%d\n", WS_SERVER_HOST, WS_SERVER_PORT);
  webSocket.begin(WS_SERVER_HOST, WS_SERVER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();

  static unsigned long lastHeartbeat = 0;
  if (isConnected && millis() - lastHeartbeat > 5000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  if (!ntpSynced && WiFi.status() == WL_CONNECTED && (millis() - lastNtpSyncAttemptMs > NTP_RETRY_INTERVAL_MS)) {
    lastNtpSyncAttemptMs = millis();
    if (trySyncNtpTime(300)) {
      Serial.println("NTP time sync OK");
    } else {
      Serial.println("NTP retry failed");
    }
  }

  lv_timer_handler();
  delay(5);
}
