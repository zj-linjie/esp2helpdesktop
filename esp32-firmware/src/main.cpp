#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <Preferences.h>
#include <stdint.h>
#include <time.h>
#include "config.h"
#include "display/scr_st77916.h"

WebSocketsClient webSocket;
Preferences settingsStore;
bool isConnected = false;

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);

enum UiPage {
  UI_PAGE_HOME = 0,
  UI_PAGE_MONITOR = 1,
  UI_PAGE_CLOCK = 2,
  UI_PAGE_SETTINGS = 3,
  UI_PAGE_INBOX = 4,
  UI_PAGE_POMODORO = 5,
  UI_PAGE_WEATHER = 6,
  UI_PAGE_COUNT = 7
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

static lv_obj_t *diagWifiLabel = nullptr;
static lv_obj_t *diagWsLabel = nullptr;
static lv_obj_t *diagNtpLabel = nullptr;
static lv_obj_t *diagIpLabel = nullptr;
static lv_obj_t *diagRssiLabel = nullptr;
static lv_obj_t *diagUptimeLabel = nullptr;
static lv_obj_t *diagServerLabel = nullptr;
static lv_obj_t *diagActionLabel = nullptr;
static lv_obj_t *brightnessSlider = nullptr;
static lv_obj_t *brightnessValueLabel = nullptr;

static lv_obj_t *inboxTypeLabel = nullptr;
static lv_obj_t *inboxIndexLabel = nullptr;
static lv_obj_t *inboxTitleLabel = nullptr;
static lv_obj_t *inboxBodyLabel = nullptr;
static lv_obj_t *inboxMetaLabel = nullptr;
static lv_obj_t *inboxActionLabel = nullptr;
static lv_obj_t *inboxAckBtn = nullptr;
static lv_obj_t *inboxDoneBtn = nullptr;

static lv_obj_t *pomodoroArc = nullptr;
static lv_obj_t *pomodoroTimeLabel = nullptr;
static lv_obj_t *pomodoroModeLabel = nullptr;
static lv_obj_t *pomodoroCountLabel = nullptr;
static lv_obj_t *pomodoroStatusLabel = nullptr;

static lv_obj_t *weatherTempLabel = nullptr;
static lv_obj_t *weatherConditionLabel = nullptr;
static lv_obj_t *weatherCityLabel = nullptr;
static lv_obj_t *weatherHumidityLabel = nullptr;
static lv_obj_t *weatherFeelsLikeLabel = nullptr;
static lv_obj_t *weatherUpdateLabel = nullptr;
static lv_obj_t *weatherIconLabel = nullptr;

static bool ntpConfigured = false;
static bool ntpSynced = false;
static uint32_t lastNtpSyncAttemptMs = 0;
static bool settingsStoreReady = false;
static uint8_t screenBrightness = 100;
static bool aiStatusInitialized = false;
static bool lastAiOnline = false;
static bool lastAiTalking = false;

enum PomodoroMode {
  POMODORO_WORK = 0,
  POMODORO_SHORT_BREAK = 1,
  POMODORO_LONG_BREAK = 2
};

enum PomodoroState {
  POMODORO_IDLE = 0,
  POMODORO_RUNNING = 1,
  POMODORO_PAUSED = 2
};

static PomodoroMode pomodoroMode = POMODORO_WORK;
static PomodoroState pomodoroState = POMODORO_IDLE;
static uint32_t pomodoroStartMs = 0;
static uint32_t pomodoroElapsedMs = 0;
static uint32_t pomodoroDurationMs = 25 * 60 * 1000; // 25 minutes
static int pomodoroCompletedCount = 0;

struct WeatherData {
  float temperature = 0.0f;
  float feelsLike = 0.0f;
  int humidity = 0;
  char condition[32] = "Loading...";
  char city[32] = "Beijing";
  char updateTime[32] = "";
  bool valid = false;
};

static WeatherData currentWeather;
static uint32_t lastWeatherUpdateMs = 0;
static constexpr uint32_t WEATHER_UPDATE_INTERVAL_MS = 30 * 60 * 1000; // 30 minutes
static const char *WEATHER_API_KEY = "598a41cf8b404383a148d15a41fa0b55";
static const char *WEATHER_CITY_ID = "101010100"; // Beijing default

static constexpr uint32_t NTP_RETRY_INTERVAL_MS = 30000;
static const char *NTP_TZ_INFO = "UTC-8";
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.nist.gov";
static const char *NTP_SERVER_3 = "ntp.aliyun.com";
static const char *WEEKDAY_SHORT[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static constexpr const char *PREF_NAMESPACE = "desktop";
static constexpr const char *PREF_KEY_BRIGHTNESS = "brightness";

enum SettingsAction {
  SETTINGS_ACTION_NONE = 0,
  SETTINGS_ACTION_WIFI_RECONNECT = 1,
  SETTINGS_ACTION_WS_RECONNECT = 2,
  SETTINGS_ACTION_NTP_SYNC = 3,
  SETTINGS_ACTION_REBOOT = 4
};

static volatile SettingsAction pendingAction = SETTINGS_ACTION_NONE;

enum InboxAction {
  INBOX_ACTION_NONE = 0,
  INBOX_ACTION_PREV = 1,
  INBOX_ACTION_NEXT = 2,
  INBOX_ACTION_ACK = 3,
  INBOX_ACTION_DONE = 4
};

struct InboxMessage {
  char category[12];
  char title[32];
  char body[120];
  char taskId[32];
  uint32_t createdMs;
  bool actionable;
  bool done;
};

static constexpr int INBOX_MAX_MESSAGES = 12;
static InboxMessage inboxMessages[INBOX_MAX_MESSAGES];
static int inboxCount = 0;
static int inboxStart = 0;
static int inboxSelected = 0;

static void updateClockDisplay();
static void setWifiStatus(const String &text);
static void setWsStatus(const String &text);
static void gestureEventCallback(lv_event_t *e);
static void refreshInboxView();

static int clampPercent(float value) {
  int v = (int)(value + 0.5f);
  if (v < 0) return 0;
  if (v > 100) return 100;
  return v;
}

static void copyText(char *dst, size_t dstSize, const char *src) {
  if (dst == nullptr || dstSize == 0) {
    return;
  }
  if (src == nullptr) {
    src = "";
  }
  snprintf(dst, dstSize, "%s", src);
}

static int inboxPhysicalIndex(int logicalIndex) {
  if (logicalIndex < 0 || logicalIndex >= inboxCount) {
    return -1;
  }
  return (inboxStart + logicalIndex) % INBOX_MAX_MESSAGES;
}

static InboxMessage *selectedInboxMessage() {
  int idx = inboxPhysicalIndex(inboxSelected);
  if (idx < 0) {
    return nullptr;
  }
  return &inboxMessages[idx];
}

static void pushInboxMessage(
  const char *category,
  const char *title,
  const char *body,
  const char *taskId = nullptr,
  bool actionable = false
) {
  int logicalIndex = 0;
  if (inboxCount < INBOX_MAX_MESSAGES) {
    logicalIndex = inboxCount;
    inboxCount++;
  } else {
    inboxStart = (inboxStart + 1) % INBOX_MAX_MESSAGES;
    logicalIndex = inboxCount - 1;
  }

  int physicalIndex = (inboxStart + logicalIndex) % INBOX_MAX_MESSAGES;
  InboxMessage &msg = inboxMessages[physicalIndex];
  copyText(msg.category, sizeof(msg.category), category);
  copyText(msg.title, sizeof(msg.title), title);
  copyText(msg.body, sizeof(msg.body), body);
  copyText(msg.taskId, sizeof(msg.taskId), taskId);
  msg.createdMs = millis();
  msg.actionable = actionable;
  msg.done = false;
  inboxSelected = inboxCount - 1;

  Serial.printf("[Inbox] [%s] %s - %s\n", msg.category, msg.title, msg.body);
  refreshInboxView();
}

static void sendInboxTaskAction(const char *action, const InboxMessage &msg) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<320> doc;
  doc["type"] = "task_action";
  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["action"] = action;
  if (msg.taskId[0] != '\0') {
    data["taskId"] = msg.taskId;
  }
  data["title"] = msg.title;
  data["timestamp"] = millis();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void refreshInboxView() {
  if (inboxTypeLabel == nullptr || inboxTitleLabel == nullptr || inboxBodyLabel == nullptr || inboxMetaLabel == nullptr || inboxIndexLabel == nullptr) {
    return;
  }

  if (inboxCount <= 0) {
    lv_label_set_text(inboxTypeLabel, "[info]");
    lv_label_set_text(inboxIndexLabel, "0/0");
    lv_label_set_text(inboxTitleLabel, "No messages");
    lv_label_set_text(inboxBodyLabel, "Incoming notifications and tasks\nwill appear here.");
    lv_label_set_text(inboxMetaLabel, "waiting for events");
    if (inboxAckBtn != nullptr) lv_obj_add_flag(inboxAckBtn, LV_OBJ_FLAG_HIDDEN);
    if (inboxDoneBtn != nullptr) lv_obj_add_flag(inboxDoneBtn, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (inboxSelected < 0) inboxSelected = 0;
  if (inboxSelected >= inboxCount) inboxSelected = inboxCount - 1;

  InboxMessage *msg = selectedInboxMessage();
  if (msg == nullptr) {
    return;
  }

  lv_label_set_text_fmt(inboxTypeLabel, "[%s]", msg->category);
  lv_label_set_text_fmt(inboxIndexLabel, "%d/%d", inboxSelected + 1, inboxCount);
  lv_label_set_text(inboxTitleLabel, msg->title);
  lv_label_set_text(inboxBodyLabel, msg->body);

  uint32_t ageSec = (millis() - msg->createdMs) / 1000;
  if (ageSec < 60) {
    lv_label_set_text_fmt(inboxMetaLabel, "%lus ago | %s", ageSec, msg->done ? "done" : (msg->actionable ? "pending" : "info"));
  } else if (ageSec < 3600) {
    lv_label_set_text_fmt(inboxMetaLabel, "%lum ago | %s", ageSec / 60, msg->done ? "done" : (msg->actionable ? "pending" : "info"));
  } else {
    lv_label_set_text_fmt(inboxMetaLabel, "%luh ago | %s", ageSec / 3600, msg->done ? "done" : (msg->actionable ? "pending" : "info"));
  }

  if (inboxAckBtn != nullptr) {
    lv_obj_clear_flag(inboxAckBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (inboxDoneBtn != nullptr) {
    if (msg->actionable && !msg->done) {
      lv_obj_clear_flag(inboxDoneBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(inboxDoneBtn, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void inboxActionEventCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  intptr_t raw = (intptr_t)lv_event_get_user_data(e);
  InboxAction action = (InboxAction)raw;

  if (action == INBOX_ACTION_PREV) {
    if (inboxCount > 0 && inboxSelected > 0) {
      inboxSelected--;
      refreshInboxView();
    }
    return;
  }

  if (action == INBOX_ACTION_NEXT) {
    if (inboxCount > 0 && inboxSelected < inboxCount - 1) {
      inboxSelected++;
      refreshInboxView();
    }
    return;
  }

  InboxMessage *msg = selectedInboxMessage();
  if (msg == nullptr) {
    return;
  }

  if (action == INBOX_ACTION_ACK) {
    sendInboxTaskAction("ack", *msg);
    if (inboxActionLabel != nullptr) {
      lv_label_set_text_fmt(inboxActionLabel, "ACK sent: %s", msg->title);
    }
    return;
  }

  if (action == INBOX_ACTION_DONE) {
    msg->done = true;
    sendInboxTaskAction("done", *msg);
    if (inboxActionLabel != nullptr) {
      lv_label_set_text_fmt(inboxActionLabel, "Done: %s", msg->title);
    }
    refreshInboxView();
    return;
  }
}

static lv_obj_t *createInboxButton(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w, InboxAction action) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, 34);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E2E2E), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
  lv_obj_add_event_cb(btn, inboxActionEventCallback, LV_EVENT_CLICKED, (void *)((intptr_t)action));

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

static void setupNtpTime() {
  configTzTime(NTP_TZ_INFO, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
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

static void setActionStatus(const String &text) {
  if (diagActionLabel != nullptr) {
    lv_label_set_text(diagActionLabel, text.c_str());
  }
}

static void applyBrightness(uint8_t brightness, bool persist) {
  if (brightness < 5) brightness = 5;
  if (brightness > 100) brightness = 100;

  screenBrightness = brightness;
  set_brightness(screenBrightness);

  if (brightnessSlider != nullptr && lv_slider_get_value(brightnessSlider) != screenBrightness) {
    lv_slider_set_value(brightnessSlider, screenBrightness, LV_ANIM_OFF);
  }
  if (brightnessValueLabel != nullptr) {
    lv_label_set_text_fmt(brightnessValueLabel, "%u%%", screenBrightness);
  }
  if (persist && settingsStoreReady) {
    settingsStore.putUChar(PREF_KEY_BRIGHTNESS, screenBrightness);
  }
}

static void beginWebSocketClient() {
  setWsStatus("WS: connecting...");
  webSocket.begin(WS_SERVER_HOST, WS_SERVER_PORT, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

static void updateDiagnosticStatus() {
  if (diagNtpLabel != nullptr) {
    lv_label_set_text(diagNtpLabel, ntpSynced ? "NTP: synced" : "NTP: syncing");
  }

  if (diagIpLabel != nullptr) {
    if (WiFi.status() == WL_CONNECTED) {
      lv_label_set_text_fmt(diagIpLabel, "IP: %s", WiFi.localIP().toString().c_str());
    } else {
      lv_label_set_text(diagIpLabel, "IP: --");
    }
  }

  if (diagRssiLabel != nullptr) {
    if (WiFi.status() == WL_CONNECTED) {
      lv_label_set_text_fmt(diagRssiLabel, "RSSI: %d dBm", WiFi.RSSI());
    } else {
      lv_label_set_text(diagRssiLabel, "RSSI: --");
    }
  }

  if (diagUptimeLabel != nullptr) {
    uint32_t total = millis() / 1000;
    uint32_t h = total / 3600;
    uint32_t m = (total / 60) % 60;
    uint32_t s = total % 60;
    lv_label_set_text_fmt(diagUptimeLabel, "Uptime: %02lu:%02lu:%02lu", h, m, s);
  }

  if (diagServerLabel != nullptr) {
    lv_label_set_text_fmt(diagServerLabel, "Server: %s:%d", WS_SERVER_HOST, WS_SERVER_PORT);
  }
}

static void diagnosticsTimerCallback(lv_timer_t *timer) {
  (void)timer;
  updateDiagnosticStatus();
  refreshInboxView();
}

static void reconnectWifiNow() {
  setActionStatus("Wi-Fi reconnecting...");
  pushInboxMessage("event", "Wi-Fi reconnect", "Trying to reconnect Wi-Fi");
  setWifiStatus("WiFi: reconnecting...");
  setWsStatus("WS: disconnected");
  isConnected = false;
  webSocket.disconnect();

  WiFi.disconnect(true);
  delay(120);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < 12000) {
    lv_timer_handler();
    delay(80);
  }

  if (WiFi.status() == WL_CONNECTED) {
    setWifiStatus(String("WiFi: ") + WiFi.localIP().toString());
    setActionStatus("Wi-Fi reconnect OK");
    pushInboxMessage("event", "Wi-Fi reconnect", "Wi-Fi reconnect succeeded");
    beginWebSocketClient();
  } else {
    setWifiStatus("WiFi: reconnect failed");
    setActionStatus("Wi-Fi reconnect failed");
    pushInboxMessage("alert", "Wi-Fi reconnect", "Wi-Fi reconnect failed");
  }
  updateDiagnosticStatus();
}

static void reconnectWsNow() {
  if (WiFi.status() != WL_CONNECTED) {
    setWsStatus("WS: waiting WiFi");
    setActionStatus("WS reconnect blocked: no Wi-Fi");
    pushInboxMessage("alert", "WS reconnect", "Blocked: Wi-Fi is disconnected");
    return;
  }

  setWsStatus("WS: reconnecting...");
  setActionStatus("WS reconnect requested");
  pushInboxMessage("event", "WS reconnect", "Reconnecting to WebSocket server");
  isConnected = false;
  webSocket.disconnect();
  delay(60);
  beginWebSocketClient();
}

static void syncNtpNow() {
  setActionStatus("NTP syncing...");
  pushInboxMessage("event", "NTP sync", "Manual NTP sync requested");
  ntpSynced = false;
  setupNtpTime();
  if (trySyncNtpTime(2500)) {
    setActionStatus("NTP sync OK");
    pushInboxMessage("event", "NTP sync", "NTP sync succeeded");
  } else {
    setActionStatus("NTP sync pending");
    pushInboxMessage("alert", "NTP sync", "NTP sync pending");
  }
  updateClockDisplay();
  updateDiagnosticStatus();
}

static void rebootNow() {
  setActionStatus("Rebooting...");
  pushInboxMessage("task", "Device reboot", "Reboot command accepted");
  delay(300);
  ESP.restart();
}

static void processPendingAction() {
  SettingsAction action = pendingAction;
  if (action == SETTINGS_ACTION_NONE) {
    return;
  }
  pendingAction = SETTINGS_ACTION_NONE;

  switch (action) {
    case SETTINGS_ACTION_WIFI_RECONNECT:
      reconnectWifiNow();
      break;
    case SETTINGS_ACTION_WS_RECONNECT:
      reconnectWsNow();
      break;
    case SETTINGS_ACTION_NTP_SYNC:
      syncNtpNow();
      break;
    case SETTINGS_ACTION_REBOOT:
      rebootNow();
      break;
    default:
      break;
  }
}

static void settingsActionEventCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  intptr_t raw = (intptr_t)lv_event_get_user_data(e);
  SettingsAction action = (SettingsAction)raw;
  pendingAction = action;

  switch (action) {
    case SETTINGS_ACTION_WIFI_RECONNECT:
      setActionStatus("Queue: Wi-Fi reconnect");
      break;
    case SETTINGS_ACTION_WS_RECONNECT:
      setActionStatus("Queue: WS reconnect");
      break;
    case SETTINGS_ACTION_NTP_SYNC:
      setActionStatus("Queue: NTP sync");
      break;
    case SETTINGS_ACTION_REBOOT:
      setActionStatus("Queue: reboot");
      break;
    default:
      break;
  }
}

static void brightnessSliderEventCallback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
    return;
  }

  lv_obj_t *slider = lv_event_get_target(e);
  uint8_t value = (uint8_t)lv_slider_get_value(slider);
  applyBrightness(value, code == LV_EVENT_RELEASED);
  if (code == LV_EVENT_RELEASED) {
    setActionStatus(String("Brightness saved: ") + value + "%");
  }
}

static lv_obj_t *createSettingsButton(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, SettingsAction action) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 136, 40);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1F1F1F), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B2B2B), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
  lv_obj_add_event_cb(btn, settingsActionEventCallback, LV_EVENT_CLICKED, (void *)((intptr_t)action));

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
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
  lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(page, gestureEventCallback, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(page, gestureEventCallback, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(page, gestureEventCallback, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(page, gestureEventCallback, LV_EVENT_PRESS_LOST, nullptr);
  return page;
}

static uint32_t getPomodoroModeDuration(PomodoroMode mode) {
  switch (mode) {
    case POMODORO_WORK: return 25 * 60 * 1000; // 25 minutes
    case POMODORO_SHORT_BREAK: return 5 * 60 * 1000; // 5 minutes
    case POMODORO_LONG_BREAK: return 15 * 60 * 1000; // 15 minutes
    default: return 25 * 60 * 1000;
  }
}

static const char* getPomodoroModeText(PomodoroMode mode) {
  switch (mode) {
    case POMODORO_WORK: return "Work";
    case POMODORO_SHORT_BREAK: return "Short Break";
    case POMODORO_LONG_BREAK: return "Long Break";
    default: return "Work";
  }
}

static uint32_t getPomodoroColor(PomodoroMode mode) {
  switch (mode) {
    case POMODORO_WORK: return 0xEF5350; // Red
    case POMODORO_SHORT_BREAK: return 0x66BB6A; // Green
    case POMODORO_LONG_BREAK: return 0x42A5F5; // Blue
    default: return 0xEF5350;
  }
}

static void updatePomodoroDisplay() {
  if (pomodoroTimeLabel == nullptr || pomodoroArc == nullptr) {
    return;
  }

  uint32_t remainingMs = 0;
  if (pomodoroState == POMODORO_RUNNING) {
    uint32_t elapsed = millis() - pomodoroStartMs + pomodoroElapsedMs;
    if (elapsed >= pomodoroDurationMs) {
      // Timer finished
      pomodoroState = POMODORO_IDLE;
      pomodoroElapsedMs = 0;

      // Auto switch mode
      if (pomodoroMode == POMODORO_WORK) {
        pomodoroCompletedCount++;
        if (pomodoroCompletedCount % 4 == 0) {
          pomodoroMode = POMODORO_LONG_BREAK;
        } else {
          pomodoroMode = POMODORO_SHORT_BREAK;
        }
      } else {
        pomodoroMode = POMODORO_WORK;
      }

      pomodoroDurationMs = getPomodoroModeDuration(pomodoroMode);

      if (pomodoroModeLabel != nullptr) {
        lv_label_set_text(pomodoroModeLabel, getPomodoroModeText(pomodoroMode));
      }
      if (pomodoroStatusLabel != nullptr) {
        lv_label_set_text(pomodoroStatusLabel, "Tap to Start");
      }

      // Update arc color
      lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(getPomodoroColor(pomodoroMode)), LV_PART_INDICATOR);

      remainingMs = pomodoroDurationMs;
    } else {
      remainingMs = pomodoroDurationMs - elapsed;
    }
  } else if (pomodoroState == POMODORO_PAUSED) {
    remainingMs = pomodoroDurationMs - pomodoroElapsedMs;
  } else {
    remainingMs = pomodoroDurationMs;
  }

  // Update time display
  uint32_t remainingSec = remainingMs / 1000;
  uint32_t minutes = remainingSec / 60;
  uint32_t seconds = remainingSec % 60;
  lv_label_set_text_fmt(pomodoroTimeLabel, "%02lu:%02lu", minutes, seconds);

  // Update arc progress
  int progress = 100 - (int)((remainingMs * 100) / pomodoroDurationMs);
  lv_arc_set_value(pomodoroArc, progress);

  // Update count label
  if (pomodoroCountLabel != nullptr) {
    lv_label_set_text_fmt(pomodoroCountLabel, "Completed: %d", pomodoroCompletedCount);
  }

  // Update status label
  if (pomodoroStatusLabel != nullptr && pomodoroState != POMODORO_IDLE) {
    if (pomodoroState == POMODORO_RUNNING) {
      lv_label_set_text(pomodoroStatusLabel, "Running...");
    } else {
      lv_label_set_text(pomodoroStatusLabel, "Paused");
    }
  }
}

static void pomodoroTimerCallback(lv_timer_t *timer) {
  (void)timer;
  updatePomodoroDisplay();
}

static void pomodoroControlCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  intptr_t action = (intptr_t)lv_event_get_user_data(e);

  if (action == 0) { // Start/Pause
    if (pomodoroState == POMODORO_IDLE) {
      pomodoroState = POMODORO_RUNNING;
      pomodoroStartMs = millis();
      pomodoroElapsedMs = 0;
    } else if (pomodoroState == POMODORO_RUNNING) {
      pomodoroState = POMODORO_PAUSED;
      pomodoroElapsedMs += millis() - pomodoroStartMs;
    } else if (pomodoroState == POMODORO_PAUSED) {
      pomodoroState = POMODORO_RUNNING;
      pomodoroStartMs = millis();
    }
  } else if (action == 1) { // Reset
    pomodoroState = POMODORO_IDLE;
    pomodoroElapsedMs = 0;
    if (pomodoroStatusLabel != nullptr) {
      lv_label_set_text(pomodoroStatusLabel, "Tap to Start");
    }
  } else if (action == 2) { // Skip
    pomodoroState = POMODORO_IDLE;
    pomodoroElapsedMs = 0;

    // Switch to next mode
    if (pomodoroMode == POMODORO_WORK) {
      pomodoroCompletedCount++;
      if (pomodoroCompletedCount % 4 == 0) {
        pomodoroMode = POMODORO_LONG_BREAK;
      } else {
        pomodoroMode = POMODORO_SHORT_BREAK;
      }
    } else {
      pomodoroMode = POMODORO_WORK;
    }

    pomodoroDurationMs = getPomodoroModeDuration(pomodoroMode);

    if (pomodoroModeLabel != nullptr) {
      lv_label_set_text(pomodoroModeLabel, getPomodoroModeText(pomodoroMode));
    }
    if (pomodoroStatusLabel != nullptr) {
      lv_label_set_text(pomodoroStatusLabel, "Tap to Start");
    }

    // Update arc color
    lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(getPomodoroColor(pomodoroMode)), LV_PART_INDICATOR);
  }

  updatePomodoroDisplay();
}

static const char* getWeatherIcon(const char* condition) {
  // 根据天气状况返回emoji图标
  if (strstr(condition, "晴") || strstr(condition, "Sunny")) return "☀️";
  if (strstr(condition, "云") || strstr(condition, "Cloud")) return "☁️";
  if (strstr(condition, "雨") || strstr(condition, "Rain")) return "🌧️";
  if (strstr(condition, "雪") || strstr(condition, "Snow")) return "❄️";
  if (strstr(condition, "雾") || strstr(condition, "Fog")) return "🌫️";
  if (strstr(condition, "雷") || strstr(condition, "Thunder")) return "⚡";
  return "🌤️";
}

static void updateWeatherDisplay() {
  if (weatherTempLabel == nullptr) {
    return;
  }

  if (currentWeather.valid) {
    lv_label_set_text_fmt(weatherTempLabel, "%.1f°C", currentWeather.temperature);
    lv_label_set_text(weatherConditionLabel, currentWeather.condition);
    lv_label_set_text(weatherCityLabel, currentWeather.city);
    lv_label_set_text_fmt(weatherHumidityLabel, "Humidity: %d%%", currentWeather.humidity);
    lv_label_set_text_fmt(weatherFeelsLikeLabel, "Feels like: %.1f°C", currentWeather.feelsLike);

    if (weatherIconLabel != nullptr) {
      lv_label_set_text(weatherIconLabel, getWeatherIcon(currentWeather.condition));
    }

    if (weatherUpdateLabel != nullptr && currentWeather.updateTime[0] != '\0') {
      lv_label_set_text_fmt(weatherUpdateLabel, "Updated: %s", currentWeather.updateTime);
    }
  } else {
    lv_label_set_text(weatherTempLabel, "--°C");
    lv_label_set_text(weatherConditionLabel, "Loading...");
    lv_label_set_text(weatherCityLabel, currentWeather.city);
    lv_label_set_text(weatherHumidityLabel, "Humidity: --%");
    lv_label_set_text(weatherFeelsLikeLabel, "Feels like: --°C");

    if (weatherIconLabel != nullptr) {
      lv_label_set_text(weatherIconLabel, "🌤️");
    }
  }
}

static void fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Weather] WiFi not connected");
    return;
  }

  HTTPClient http;
  String url = String("https://devapi.qweather.com/v7/weather/now?location=") +
               WEATHER_CITY_ID + "&key=" + WEATHER_API_KEY;

  Serial.printf("[Weather] Fetching: %s\n", url.c_str());

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.printf("[Weather] Response: %s\n", payload.c_str());

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      const char* code = doc["code"];
      if (strcmp(code, "200") == 0) {
        JsonObject now = doc["now"];
        currentWeather.temperature = now["temp"].as<float>();
        currentWeather.feelsLike = now["feelsLike"].as<float>();
        currentWeather.humidity = now["humidity"].as<int>();

        const char* text = now["text"];
        if (text) {
          snprintf(currentWeather.condition, sizeof(currentWeather.condition), "%s", text);
        }

        const char* obsTime = now["obsTime"];
        if (obsTime) {
          snprintf(currentWeather.updateTime, sizeof(currentWeather.updateTime), "%s", obsTime);
        }

        currentWeather.valid = true;
        lastWeatherUpdateMs = millis();

        Serial.printf("[Weather] Updated: %.1f°C, %s, %d%%\n",
                     currentWeather.temperature,
                     currentWeather.condition,
                     currentWeather.humidity);

        updateWeatherDisplay();
        pushInboxMessage("weather", "Weather Updated", currentWeather.condition);
      } else {
        Serial.printf("[Weather] API error code: %s\n", code);
      }
    } else {
      Serial.printf("[Weather] JSON parse error: %s\n", error.c_str());
    }
  } else {
    Serial.printf("[Weather] HTTP error: %d\n", httpCode);
  }

  http.end();
}

static void weatherTimerCallback(lv_timer_t *timer) {
  (void)timer;

  // 检查是否需要更新天气
  if (!currentWeather.valid || (millis() - lastWeatherUpdateMs) >= WEATHER_UPDATE_INTERVAL_MS) {
    fetchWeatherData();
  }
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

  // Page 4: Settings & Diagnostics
  pages[UI_PAGE_SETTINGS] = createBasePage();
  lv_obj_t *settingsTitle = lv_label_create(pages[UI_PAGE_SETTINGS]);
  lv_label_set_text(settingsTitle, "Settings & Diagnostics");
  lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *diagPanel = lv_obj_create(pages[UI_PAGE_SETTINGS]);
  lv_obj_set_size(diagPanel, 300, 98);
  lv_obj_align(diagPanel, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_set_style_radius(diagPanel, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(diagPanel, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(diagPanel, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
  lv_obj_set_style_border_width(diagPanel, 1, LV_PART_MAIN);
  lv_obj_clear_flag(diagPanel, LV_OBJ_FLAG_SCROLLABLE);

  diagWifiLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagWifiLabel, "WiFi: connecting...");
  lv_obj_align(diagWifiLabel, LV_ALIGN_TOP_LEFT, 10, 8);

  diagWsLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagWsLabel, "WS: disconnected");
  lv_obj_align(diagWsLabel, LV_ALIGN_TOP_LEFT, 10, 26);

  diagNtpLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagNtpLabel, "NTP: syncing");
  lv_obj_align(diagNtpLabel, LV_ALIGN_TOP_LEFT, 10, 42);

  diagIpLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagIpLabel, "IP: --");
  lv_obj_align(diagIpLabel, LV_ALIGN_TOP_LEFT, 10, 58);

  diagRssiLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagRssiLabel, "RSSI: --");
  lv_obj_align(diagRssiLabel, LV_ALIGN_TOP_LEFT, 10, 74);

  diagServerLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagServerLabel, "Server: --");
  lv_obj_align(diagServerLabel, LV_ALIGN_TOP_LEFT, 148, 74);

  createSettingsButton(pages[UI_PAGE_SETTINGS], "WiFi Reconnect", 34, 142, SETTINGS_ACTION_WIFI_RECONNECT);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "WS Reconnect", 190, 142, SETTINGS_ACTION_WS_RECONNECT);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "NTP Sync", 34, 182, SETTINGS_ACTION_NTP_SYNC);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "Reboot", 190, 182, SETTINGS_ACTION_REBOOT);

  lv_obj_t *brightnessPanel = lv_obj_create(pages[UI_PAGE_SETTINGS]);
  lv_obj_set_size(brightnessPanel, 300, 54);
  lv_obj_align(brightnessPanel, LV_ALIGN_TOP_MID, 0, 222);
  lv_obj_set_style_radius(brightnessPanel, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightnessPanel, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(brightnessPanel, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
  lv_obj_set_style_border_width(brightnessPanel, 1, LV_PART_MAIN);
  lv_obj_clear_flag(brightnessPanel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *brightnessText = lv_label_create(brightnessPanel);
  lv_label_set_text(brightnessText, "Brightness");
  lv_obj_align(brightnessText, LV_ALIGN_TOP_LEFT, 10, 6);

  brightnessSlider = lv_slider_create(brightnessPanel);
  lv_obj_set_size(brightnessSlider, 192, 12);
  lv_obj_align(brightnessSlider, LV_ALIGN_TOP_LEFT, 90, 10);
  lv_slider_set_range(brightnessSlider, 5, 100);
  lv_slider_set_value(brightnessSlider, screenBrightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(brightnessSlider, brightnessSliderEventCallback, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(brightnessSlider, brightnessSliderEventCallback, LV_EVENT_RELEASED, nullptr);

  brightnessValueLabel = lv_label_create(brightnessPanel);
  lv_label_set_text(brightnessValueLabel, "100%");
  lv_obj_align(brightnessValueLabel, LV_ALIGN_TOP_RIGHT, -10, 4);

  diagActionLabel = lv_label_create(brightnessPanel);
  lv_label_set_text(diagActionLabel, "Ready");
  lv_obj_set_style_text_color(diagActionLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_align(diagActionLabel, LV_ALIGN_BOTTOM_LEFT, 10, -4);

  diagUptimeLabel = lv_label_create(pages[UI_PAGE_SETTINGS]);
  lv_label_set_text(diagUptimeLabel, "Uptime: 00:00:00");
  lv_obj_align(diagUptimeLabel, LV_ALIGN_TOP_MID, 0, 282);

  // Page 5: Inbox & Tasks
  pages[UI_PAGE_INBOX] = createBasePage();
  lv_obj_t *inboxPageTitle = lv_label_create(pages[UI_PAGE_INBOX]);
  lv_label_set_text(inboxPageTitle, "Inbox & Tasks");
  lv_obj_align(inboxPageTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *inboxCard = lv_obj_create(pages[UI_PAGE_INBOX]);
  lv_obj_set_size(inboxCard, 300, 170);
  lv_obj_align(inboxCard, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_set_style_radius(inboxCard, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(inboxCard, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(inboxCard, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
  lv_obj_set_style_border_width(inboxCard, 1, LV_PART_MAIN);
  lv_obj_clear_flag(inboxCard, LV_OBJ_FLAG_SCROLLABLE);

  inboxTypeLabel = lv_label_create(inboxCard);
  lv_label_set_text(inboxTypeLabel, "[info]");
  lv_obj_set_style_text_color(inboxTypeLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(inboxTypeLabel, LV_ALIGN_TOP_LEFT, 10, 8);

  inboxIndexLabel = lv_label_create(inboxCard);
  lv_label_set_text(inboxIndexLabel, "0/0");
  lv_obj_set_style_text_color(inboxIndexLabel, lv_color_hex(0x9E9E9E), LV_PART_MAIN);
  lv_obj_align(inboxIndexLabel, LV_ALIGN_TOP_RIGHT, -10, 8);

  inboxTitleLabel = lv_label_create(inboxCard);
  lv_label_set_text(inboxTitleLabel, "No messages");
  lv_obj_set_style_text_font(inboxTitleLabel, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_align(inboxTitleLabel, LV_ALIGN_TOP_LEFT, 10, 30);

  inboxBodyLabel = lv_label_create(inboxCard);
  lv_obj_set_width(inboxBodyLabel, 280);
  lv_label_set_long_mode(inboxBodyLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(inboxBodyLabel, "Incoming notifications and tasks\nwill appear here.");
  lv_obj_align(inboxBodyLabel, LV_ALIGN_TOP_LEFT, 10, 66);

  inboxMetaLabel = lv_label_create(inboxCard);
  lv_label_set_text(inboxMetaLabel, "waiting for events");
  lv_obj_set_style_text_color(inboxMetaLabel, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
  lv_obj_align(inboxMetaLabel, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  createInboxButton(pages[UI_PAGE_INBOX], "Prev", 34, 214, 96, INBOX_ACTION_PREV);
  createInboxButton(pages[UI_PAGE_INBOX], "Next", 230, 214, 96, INBOX_ACTION_NEXT);
  inboxAckBtn = createInboxButton(pages[UI_PAGE_INBOX], "Acknowledge", 34, 254, 136, INBOX_ACTION_ACK);
  inboxDoneBtn = createInboxButton(pages[UI_PAGE_INBOX], "Mark Done", 190, 254, 136, INBOX_ACTION_DONE);

  inboxActionLabel = lv_label_create(pages[UI_PAGE_INBOX]);
  lv_label_set_text(inboxActionLabel, "Swipe to browse");
  lv_obj_set_style_text_color(inboxActionLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_align(inboxActionLabel, LV_ALIGN_TOP_MID, 0, 296);

  // Page 6: Pomodoro Timer
  pages[UI_PAGE_POMODORO] = createBasePage();
  lv_obj_t *pomodoroTitle = lv_label_create(pages[UI_PAGE_POMODORO]);
  lv_label_set_text(pomodoroTitle, "Pomodoro Timer");
  lv_obj_align(pomodoroTitle, LV_ALIGN_TOP_MID, 0, 14);

  pomodoroModeLabel = lv_label_create(pages[UI_PAGE_POMODORO]);
  lv_label_set_text(pomodoroModeLabel, getPomodoroModeText(pomodoroMode));
  lv_obj_set_style_text_font(pomodoroModeLabel, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_align(pomodoroModeLabel, LV_ALIGN_TOP_MID, 0, 40);

  pomodoroArc = lv_arc_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(pomodoroArc, 200, 200);
  lv_obj_align(pomodoroArc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(pomodoroArc, 270);
  lv_arc_set_bg_angles(pomodoroArc, 0, 360);
  lv_arc_set_range(pomodoroArc, 0, 100);
  lv_arc_set_value(pomodoroArc, 0);
  lv_obj_set_style_arc_width(pomodoroArc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_arc_width(pomodoroArc, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(getPomodoroColor(pomodoroMode)), LV_PART_INDICATOR);
  lv_obj_set_style_opa(pomodoroArc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(pomodoroArc, LV_OBJ_FLAG_CLICKABLE);

  pomodoroTimeLabel = lv_label_create(pages[UI_PAGE_POMODORO]);
  lv_label_set_text(pomodoroTimeLabel, "25:00");
  lv_obj_set_style_text_font(pomodoroTimeLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(pomodoroTimeLabel, LV_ALIGN_CENTER, 0, 0);

  pomodoroStatusLabel = lv_label_create(pages[UI_PAGE_POMODORO]);
  lv_label_set_text(pomodoroStatusLabel, "Tap to Start");
  lv_obj_set_style_text_color(pomodoroStatusLabel, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
  lv_obj_align(pomodoroStatusLabel, LV_ALIGN_CENTER, 0, 50);

  pomodoroCountLabel = lv_label_create(pages[UI_PAGE_POMODORO]);
  lv_label_set_text(pomodoroCountLabel, "Completed: 0");
  lv_obj_set_style_text_color(pomodoroCountLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(pomodoroCountLabel, LV_ALIGN_BOTTOM_MID, 0, -80);

  // Control buttons
  lv_obj_t *startBtn = lv_btn_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(startBtn, 90, 36);
  lv_obj_align(startBtn, LV_ALIGN_BOTTOM_LEFT, 30, -36);
  lv_obj_set_style_radius(startBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(startBtn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_add_event_cb(startBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)0);
  lv_obj_t *startLabel = lv_label_create(startBtn);
  lv_label_set_text(startLabel, "Start");
  lv_obj_center(startLabel);

  lv_obj_t *resetBtn = lv_btn_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(resetBtn, 90, 36);
  lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 0, -36);
  lv_obj_set_style_radius(resetBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_add_event_cb(resetBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)1);
  lv_obj_t *resetLabel = lv_label_create(resetBtn);
  lv_label_set_text(resetLabel, "Reset");
  lv_obj_center(resetLabel);

  lv_obj_t *skipBtn = lv_btn_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(skipBtn, 90, 36);
  lv_obj_align(skipBtn, LV_ALIGN_BOTTOM_RIGHT, -30, -36);
  lv_obj_set_style_radius(skipBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(skipBtn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_add_event_cb(skipBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)2);
  lv_obj_t *skipLabel = lv_label_create(skipBtn);
  lv_label_set_text(skipLabel, "Skip");
  lv_obj_center(skipLabel);

  // Page 7: Weather
  pages[UI_PAGE_WEATHER] = createBasePage();
  lv_obj_t *weatherTitle = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherTitle, "Weather");
  lv_obj_align(weatherTitle, LV_ALIGN_TOP_MID, 0, 14);

  weatherCityLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherCityLabel, currentWeather.city);
  lv_obj_set_style_text_color(weatherCityLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(weatherCityLabel, LV_ALIGN_TOP_MID, 0, 38);

  weatherIconLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherIconLabel, "🌤️");
  lv_obj_set_style_text_font(weatherIconLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(weatherIconLabel, LV_ALIGN_CENTER, 0, -40);

  weatherTempLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherTempLabel, "--°C");
  lv_obj_set_style_text_font(weatherTempLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(weatherTempLabel, LV_ALIGN_CENTER, 0, 20);

  weatherConditionLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherConditionLabel, "Loading...");
  lv_obj_set_style_text_color(weatherConditionLabel, lv_color_hex(0xA0A0A0), LV_PART_MAIN);
  lv_obj_align(weatherConditionLabel, LV_ALIGN_CENTER, 0, 55);

  lv_obj_t *weatherInfoPanel = lv_obj_create(pages[UI_PAGE_WEATHER]);
  lv_obj_set_size(weatherInfoPanel, 280, 70);
  lv_obj_align(weatherInfoPanel, LV_ALIGN_BOTTOM_MID, 0, -50);
  lv_obj_set_style_radius(weatherInfoPanel, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(weatherInfoPanel, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
  lv_obj_set_style_border_width(weatherInfoPanel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(weatherInfoPanel, LV_OBJ_FLAG_SCROLLABLE);

  weatherHumidityLabel = lv_label_create(weatherInfoPanel);
  lv_label_set_text(weatherHumidityLabel, "Humidity: --%");
  lv_obj_align(weatherHumidityLabel, LV_ALIGN_TOP_LEFT, 12, 10);

  weatherFeelsLikeLabel = lv_label_create(weatherInfoPanel);
  lv_label_set_text(weatherFeelsLikeLabel, "Feels like: --°C");
  lv_obj_align(weatherFeelsLikeLabel, LV_ALIGN_TOP_LEFT, 12, 32);

  weatherUpdateLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherUpdateLabel, "Tap to refresh");
  lv_obj_set_style_text_color(weatherUpdateLabel, lv_color_hex(0x707070), LV_PART_MAIN);
  lv_obj_align(weatherUpdateLabel, LV_ALIGN_BOTTOM_MID, 0, -20);

  // Global page indicator
  pageIndicatorLabel = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_color(pageIndicatorLabel, lv_color_hex(0x8F8F8F), LV_PART_MAIN);
  lv_obj_align(pageIndicatorLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_move_foreground(pageIndicatorLabel);
  showPage(UI_PAGE_HOME);
  applyBrightness(screenBrightness, false);
  updateDiagnosticStatus();
  refreshInboxView();
  updateClockDisplay();
  lv_timer_create(clockTimerCallback, 1000, nullptr);
  lv_timer_create(diagnosticsTimerCallback, 1000, nullptr);
  lv_timer_create(pomodoroTimerCallback, 100, nullptr);
  lv_timer_create(weatherTimerCallback, 60000, nullptr); // Check every minute

  // Initialize weather display
  updateWeatherDisplay();
}

static void setWifiStatus(const String &text) {
  if (homeWifiLabel != nullptr) {
    lv_label_set_text(homeWifiLabel, text.c_str());
  }
  if (wifiLabel != nullptr) {
    lv_label_set_text(wifiLabel, text.c_str());
  }
  if (diagWifiLabel != nullptr) {
    lv_label_set_text(diagWifiLabel, text.c_str());
  }
}

static void setWsStatus(const String &text) {
  if (homeWsLabel != nullptr) {
    lv_label_set_text(homeWsLabel, text.c_str());
  }
  if (wsLabel != nullptr) {
    lv_label_set_text(wsLabel, text.c_str());
  }
  if (diagWsLabel != nullptr) {
    lv_label_set_text(diagWsLabel, text.c_str());
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
      pushInboxMessage("alert", "WebSocket", "Connection lost");
      break;

    case WStype_CONNECTED:
      Serial.println("[WebSocket] connected");
      isConnected = true;
      setWsStatus("WS: connected");
      pushInboxMessage("event", "WebSocket", "Connected to server");
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
        char body[96];
        snprintf(body, sizeof(body), "Server %s, interval %ldms", serverVersion, updateInterval);
        pushInboxMessage("event", "Handshake OK", body);
      } else if (strcmp(messageType, "system_stats") == 0) {
        handleSystemStats(doc["data"].as<JsonObjectConst>());
      } else if (strcmp(messageType, "system_info") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        float cpuUsage = data["cpu"]["usage"] | 0;
        float memPercentage = data["memory"]["percentage"] | 0;
        setStats(cpuUsage, memPercentage, 0, 0);
      } else if (strcmp(messageType, "ai_conversation") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *role = data["role"] | "assistant";
        const char *text = data["text"] | data["message"] | "AI message";
        const char *title = (strcmp(role, "user") == 0) ? "AI user" : "AI assistant";
        pushInboxMessage("chat", title, text);
      } else if (strcmp(messageType, "ai_status") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool online = data["online"] | false;
        bool talking = data["talking"] | false;
        if (!aiStatusInitialized || online != lastAiOnline || talking != lastAiTalking) {
          aiStatusInitialized = true;
          lastAiOnline = online;
          lastAiTalking = talking;
          char body[96];
          snprintf(body, sizeof(body), "online=%s talking=%s", online ? "true" : "false", talking ? "true" : "false");
          pushInboxMessage("ai", "AI status", body);
        }
      } else if (
        strcmp(messageType, "task_card") == 0 ||
        strcmp(messageType, "task") == 0 ||
        strcmp(messageType, "todo") == 0 ||
        strcmp(messageType, "notification") == 0 ||
        strcmp(messageType, "reminder") == 0
      ) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *title = data["title"] | data["taskTitle"] | messageType;
        const char *body = data["body"] | data["description"] | data["text"] | "New task received";
        const char *taskId = data["taskId"] | data["id"] | "";
        bool actionable = data["actionable"] | true;
        pushInboxMessage(actionable ? "task" : "info", title, body, taskId, actionable);
      } else if (
        strcmp(messageType, "app_launched") == 0 ||
        strcmp(messageType, "launch_app_response") == 0 ||
        strcmp(messageType, "command_result") == 0
      ) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool success = data["success"] | false;
        const char *title = success ? "Command success" : "Command failed";
        const char *body = data["message"] | data["appName"] | data["reason"] | messageType;
        pushInboxMessage(success ? "event" : "alert", title, body);
      } else {
        Serial.printf("[WebSocket] unhandled type: %s\n", messageType);
        char body[96];
        snprintf(body, sizeof(body), "Unhandled message type: %s", messageType);
        pushInboxMessage("info", "Unhandled message", body);
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

  settingsStoreReady = settingsStore.begin(PREF_NAMESPACE, false);
  if (settingsStoreReady) {
    screenBrightness = settingsStore.getUChar(PREF_KEY_BRIGHTNESS, 100);
  }
  if (screenBrightness < 5 || screenBrightness > 100) {
    screenBrightness = 100;
  }

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
  setActionStatus("Wi-Fi connected");
  pushInboxMessage("event", "Wi-Fi connected", WiFi.localIP().toString().c_str());

  setupNtpTime();
  if (trySyncNtpTime(2500)) {
    Serial.println("NTP time sync OK");
    setActionStatus("NTP sync OK");
    pushInboxMessage("event", "NTP sync", "NTP time synchronized");
  } else {
    Serial.println("NTP time sync pending (will retry in background)");
    setActionStatus("NTP sync pending");
    pushInboxMessage("alert", "NTP sync", "NTP sync pending");
  }

  Serial.printf("Connecting WebSocket: %s:%d\n", WS_SERVER_HOST, WS_SERVER_PORT);
  beginWebSocketClient();
  updateDiagnosticStatus();

  // Fetch initial weather data
  Serial.println("Fetching initial weather data...");
  fetchWeatherData();
}

void loop() {
  webSocket.loop();
  processPendingAction();

  static unsigned long lastHeartbeat = 0;
  if (isConnected && millis() - lastHeartbeat > 5000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  if (!ntpSynced && WiFi.status() == WL_CONNECTED && (millis() - lastNtpSyncAttemptMs > NTP_RETRY_INTERVAL_MS)) {
    lastNtpSyncAttemptMs = millis();
    if (trySyncNtpTime(300)) {
      Serial.println("NTP time sync OK");
      pushInboxMessage("event", "NTP sync", "Background NTP sync succeeded");
    } else {
      Serial.println("NTP retry failed");
    }
  }

  lv_timer_handler();
  delay(5);
}
