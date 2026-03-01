#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <driver/i2s.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "config.h"
#include "display/scr_st77916.h"
#include <AudioFileSourceFS.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>

#if LV_USE_SJPG
extern "C" void lv_split_jpeg_init(void);
#include <extra/libs/sjpg/tjpgd.h>
#endif

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
  UI_PAGE_APP_LAUNCHER = 7,
  UI_PAGE_PHOTO_FRAME = 8,
  UI_PAGE_AUDIO_PLAYER = 9,
  UI_PAGE_VIDEO_PLAYER = 10,
  UI_PAGE_VOICE = 11,
  UI_PAGE_COUNT = 12
};

struct TouchGestureState {
  bool pressed = false;
  bool longPressHandled = false;
  lv_point_t startPoint = {0, 0};
  uint32_t startMs = 0;
};

static constexpr uint32_t LONG_PRESS_MS = 800;
static constexpr int16_t SWIPE_HOME_THRESHOLD = 60;
static constexpr int16_t SWIPE_HOME_DIRECTION_MARGIN = 12;
static constexpr int16_t SWIPE_HOME_MAX_X_DRIFT = 120;
static constexpr uint32_t CLICK_SUPPRESS_MS_AFTER_HOME = 320;
static constexpr uint32_t CLICK_SUPPRESS_MS_AFTER_CAROUSEL = 220;
static constexpr int16_t HOME_CAROUSEL_SWIPE_THRESHOLD = 42;
static constexpr int16_t HOME_CAROUSEL_DIRECTION_MARGIN = 12;
static constexpr float HOME_DEG_TO_RAD = 0.01745329252f;
static uint32_t suppressClickUntilMs = 0;

static lv_obj_t *pages[UI_PAGE_COUNT] = {nullptr};
static int currentPage = UI_PAGE_HOME;
static TouchGestureState gestureState;

static lv_obj_t *pageIndicatorLabel = nullptr;

static lv_obj_t *homeWifiLabel = nullptr;
static lv_obj_t *homeWsLabel = nullptr;
static lv_obj_t *homeClockLabel = nullptr;
static lv_obj_t *homeDateLabel = nullptr;

struct HomeShortcutConfig {
  const char *label;
  const char *icon;
  uint8_t iconKind;
  UiPage page;
  uint32_t accentColor;
};

enum HomeIconKind : uint8_t {
  HOME_ICON_SYMBOL = 0,
  HOME_ICON_MONITOR = 1,
  HOME_ICON_POMODORO = 2,
  HOME_ICON_CLOCK = 3,
  HOME_ICON_WEATHER = 4,
  HOME_ICON_APPS = 5
};

static const HomeShortcutConfig HOME_SHORTCUTS[] = {
  {"Monitor", LV_SYMBOL_VIDEO, HOME_ICON_MONITOR, UI_PAGE_MONITOR, 0x3949AB},
  {"Pomodoro", LV_SYMBOL_BELL, HOME_ICON_POMODORO, UI_PAGE_POMODORO, 0xE53935},
  {"Settings", LV_SYMBOL_SETTINGS, HOME_ICON_SYMBOL, UI_PAGE_SETTINGS, 0x5E35B1},
  {"Photo", LV_SYMBOL_IMAGE, HOME_ICON_SYMBOL, UI_PAGE_PHOTO_FRAME, 0x8E24AA},
  {"Weather", LV_SYMBOL_TINT, HOME_ICON_WEATHER, UI_PAGE_WEATHER, 0x039BE5},
  {"Clock", LV_SYMBOL_REFRESH, HOME_ICON_CLOCK, UI_PAGE_CLOCK, 0x1E88E5},
  {"Apps", LV_SYMBOL_BARS, HOME_ICON_APPS, UI_PAGE_APP_LAUNCHER, 0x5E35B1},
  {"Music", LV_SYMBOL_AUDIO, HOME_ICON_SYMBOL, UI_PAGE_AUDIO_PLAYER, 0x43A047},
  {"Video", LV_SYMBOL_PLAY, HOME_ICON_SYMBOL, UI_PAGE_VIDEO_PLAYER, 0xFB8C00},
  {"Voice", LV_SYMBOL_CALL, HOME_ICON_SYMBOL, UI_PAGE_VOICE, 0x00897B},
  {"Inbox", LV_SYMBOL_LIST, HOME_ICON_SYMBOL, UI_PAGE_INBOX, 0x546E7A},
};

static constexpr uint8_t HOME_SHORTCUT_COUNT = sizeof(HOME_SHORTCUTS) / sizeof(HOME_SHORTCUTS[0]);
static constexpr uint8_t HOME_VISIBLE_SLOT_COUNT = 8;
static lv_obj_t *homeShortcutSlots[HOME_VISIBLE_SLOT_COUNT] = {nullptr};
static lv_obj_t *homeShortcutButtons[HOME_VISIBLE_SLOT_COUNT] = {nullptr};
static lv_obj_t *homeShortcutIcons[HOME_VISIBLE_SLOT_COUNT] = {nullptr};
static int8_t homeSlotToShortcut[HOME_VISIBLE_SLOT_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1};
static lv_obj_t *homeCurrentShortcutLabel = nullptr;
static lv_obj_t *homeSwipeHintLabel = nullptr;
static int homeCarouselOffset = 0;

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
static lv_obj_t *diagSdLabel = nullptr;
static lv_obj_t *diagSdRootLabel = nullptr;
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

static lv_obj_t *appLauncherList = nullptr;
static lv_obj_t *appLauncherTitle = nullptr;
static lv_obj_t *appLauncherPageLabel = nullptr;
static lv_obj_t *appLauncherStatusLabel = nullptr;
static lv_obj_t *appLauncherPrevBtn = nullptr;
static lv_obj_t *appLauncherNextBtn = nullptr;
static lv_timer_t *appLauncherStatusTimer = nullptr;

static lv_obj_t *photoFrameRootLabel = nullptr;
static lv_obj_t *photoFrameViewport = nullptr;
static lv_obj_t *photoFrameStatusLabel = nullptr;
static lv_obj_t *photoFrameImage = nullptr;
static lv_obj_t *photoFrameNameLabel = nullptr;
static lv_obj_t *photoFrameIndexLabel = nullptr;
static lv_obj_t *photoFramePrevBtn = nullptr;
static lv_obj_t *photoFrameReloadBtn = nullptr;
static lv_obj_t *photoFrameNextBtn = nullptr;

static lv_obj_t *audioStatusLabel = nullptr;
static lv_obj_t *audioTrackLabel = nullptr;
static lv_obj_t *audioTimeLabel = nullptr;
static lv_obj_t *audioIndexLabel = nullptr;
static lv_obj_t *audioPrevBtn = nullptr;
static lv_obj_t *audioPlayBtn = nullptr;
static lv_obj_t *audioPlayBtnLabel = nullptr;
static lv_obj_t *audioNextBtn = nullptr;

static lv_obj_t *homeWallpaperImage = nullptr;
static lv_obj_t *clockWallpaperImage = nullptr;
static lv_obj_t *homeWallpaperShade = nullptr;
static lv_obj_t *clockWallpaperShade = nullptr;
static lv_obj_t *bootSplashOverlay = nullptr;

static lv_obj_t *videoStatusLabel = nullptr;
static lv_obj_t *videoTrackLabel = nullptr;
static lv_obj_t *videoHintLabel = nullptr;
static lv_obj_t *videoIndexLabel = nullptr;
static lv_obj_t *videoViewport = nullptr;
static lv_obj_t *videoImage = nullptr;
static lv_obj_t *videoPrevBtn = nullptr;
static lv_obj_t *videoPlayBtn = nullptr;
static lv_obj_t *videoPlayBtnLabel = nullptr;
static lv_obj_t *videoNextBtn = nullptr;

static lv_obj_t *voiceStatusLabel = nullptr;
static lv_obj_t *voiceResultLabel = nullptr;
static lv_obj_t *voiceMicToggleBtn = nullptr;
static lv_obj_t *voiceMicToggleLabel = nullptr;

static constexpr i2s_port_t VOICE_I2S_PORT = I2S_NUM_1;
static constexpr uint32_t VOICE_SAMPLE_RATE = 16000;
static constexpr size_t VOICE_SAMPLES_PER_CHUNK = 640; // 40ms @ 16kHz
static constexpr size_t VOICE_PCM_BYTES_PER_CHUNK = VOICE_SAMPLES_PER_CHUNK * sizeof(int16_t);
static bool voiceMicInitialized = false;
static bool voiceMicStreaming = false;
static bool voiceStreamStartAcked = false;
static char voiceActiveStreamId[40] = "";
static uint32_t voiceChunkSeq = 0;
static uint32_t voiceChunksSent = 0;
static uint32_t voiceBytesSent = 0;
static uint32_t voiceLastChunkMs = 0;
static uint32_t voiceLastStartSentMs = 0;
static uint8_t voiceLastLevelPercent = 0;
static int32_t voiceRawChunk[VOICE_SAMPLES_PER_CHUNK * 2];
static int32_t voiceMonoRaw[VOICE_SAMPLES_PER_CHUNK];
static int16_t voicePcmChunk[VOICE_SAMPLES_PER_CHUNK];

struct VoicePresetCommand {
  const char *label;
  const char *text;
};

static const VoicePresetCommand VOICE_PRESET_COMMANDS[] = {
  {"Home", "go home"},
  {"Monitor", "open monitor"},
  {"Weather", "open weather"},
  {"Clock", "open clock"},
  {"Apps", "open apps"},
  {"Safari", "open safari"},
};

struct MacApp {
  char name[32];
  char path[128];
  char letter;
  uint32_t color;
};

static MacApp appList[12];
static int appCount = 0;
static int appPage = 0;
static const int APPS_PER_PAGE = 4;

static bool ntpConfigured = false;
static bool ntpSynced = false;
static uint32_t lastNtpSyncAttemptMs = 0;
static bool settingsStoreReady = false;
static uint8_t screenBrightness = 100;
static bool aiStatusInitialized = false;
static bool lastAiOnline = false;
static bool lastAiTalking = false;
static bool sdInitAttempted = false;
static bool sdMounted = false;
static bool sdMode1Bit = false;
static uint8_t sdCardType = CARD_NONE;
static uint64_t sdTotalBytes = 0;
static uint64_t sdUsedBytes = 0;
static uint32_t sdRootDirCount = 0;
static uint32_t sdRootFileCount = 0;
static char sdRootPreview[96] = "--";
static char sdMountReason[64] = "not checked";

struct SdPhotoFile {
  char path[192];
  char name[64];
};

static SdPhotoFile sdPhotoFiles[64];
static int sdPhotoCount = 0;
static int sdPhotoIndex = 0;
static bool photoDecoderReady = false;
static uint8_t *photoRawData = nullptr;
static size_t photoRawDataSize = 0;
static lv_img_dsc_t photoRawDsc;
static uint8_t *photoDecodedData = nullptr;
static size_t photoDecodedDataSize = 0;
static lv_img_dsc_t photoDecodedDsc;

struct SdAudioFile {
  char path[192];
  char name[64];
  uint32_t size;
  uint32_t durationSec;
  bool durationChecked;
  bool durationEstimated;
};

static SdAudioFile sdAudioFiles[96];
static int sdAudioCount = 0;
static int sdAudioIndex = 0;
static AudioFileSourceFS *audioFileSource = nullptr;
static AudioFileSourceBuffer *audioBufferedSource = nullptr;
static AudioGeneratorMP3 *audioMp3 = nullptr;
static AudioGeneratorWAV *audioWav = nullptr;
static AudioOutputI2S *audioOutput = nullptr;
static bool audioOutputReady = false;
static bool audioPaused = false;
static uint32_t audioLastControlMs = 0;
static uint32_t audioElapsedAccumMs = 0;
static uint32_t audioPlaybackResumeMs = 0;
static uint32_t audioLastTimeLabelRefreshMs = 0;
static uint32_t audioShownElapsedSec = 0xFFFFFFFF;
static uint32_t audioShownDurationSec = 0xFFFFFFFF;
static constexpr uint32_t AUDIO_CONTROL_COOLDOWN_MS = 220;

enum AudioControlAction {
  AUDIO_CONTROL_NONE = -1,
  AUDIO_CONTROL_PREV = 0,
  AUDIO_CONTROL_TOGGLE = 1,
  AUDIO_CONTROL_NEXT = 2,
  AUDIO_CONTROL_RESCAN = 3,
};

static volatile int pendingAudioControlAction = AUDIO_CONTROL_NONE;

struct SdVideoFile {
  char path[192];
  char name[64];
  uint32_t size;
};

static SdVideoFile sdVideoFiles[64];
static int sdVideoCount = 0;
static int sdVideoIndex = 0;
static File videoFile;
static bool videoPlaying = false;
static bool videoPaused = false;
static uint8_t *videoFrameData = nullptr;
static size_t videoFrameDataSize = 0;
static uint8_t *videoDecodedData = nullptr;
static size_t videoDecodedCapacity = 0;
static lv_img_dsc_t videoDecodedDsc;
static uint32_t videoFrameIntervalMs = 100; // 10 FPS default
static uint32_t videoLastFrameMs = 0;
static uint32_t videoLastControlMs = 0;
static constexpr uint32_t VIDEO_CONTROL_COOLDOWN_MS = 220;
static constexpr size_t VIDEO_FRAME_MAX_BYTES = 512 * 1024;
static constexpr uint8_t VIDEO_SCAN_MAX_DEPTH = 4;

enum VideoControlAction {
  VIDEO_CONTROL_NONE = -1,
  VIDEO_CONTROL_PREV = 0,
  VIDEO_CONTROL_TOGGLE = 1,
  VIDEO_CONTROL_NEXT = 2,
  VIDEO_CONTROL_RESCAN = 3,
};

static volatile int pendingVideoControlAction = VIDEO_CONTROL_NONE;

struct DynamicWallpaperPlayer {
  char path[192];
  File file;
  lv_obj_t *imageObj;
  bool enabled;
  bool opened;
  uint16_t baseIntervalMs;
  uint16_t intervalMs;
  uint32_t lastFrameMs;
  uint8_t slowScore;
  uint8_t fastScore;
  uint8_t failCount;
};

static DynamicWallpaperPlayer homeWallpaper = {
  "", File(), nullptr, false, false, 140, 140, 0, 0, 0, 0
};
static DynamicWallpaperPlayer clockWallpaper = {
  "", File(), nullptr, false, false, 160, 160, 0, 0, 0, 0
};

static uint8_t *wallpaperFrameData = nullptr;
static size_t wallpaperFrameDataSize = 0;
static uint32_t dynamicWallpaperPauseUntilMs = 0;
static constexpr uint32_t DYNAMIC_WALLPAPER_TOUCH_PAUSE_MS = 1200;
static constexpr uint16_t DYNAMIC_WALLPAPER_MIN_INTERVAL_MS = 333; // ~3 FPS

struct PhotoFrameRemoteSettings {
  uint16_t slideshowIntervalSec = 5;
  bool autoPlay = true;
  char theme[24] = "dark-gallery";
  float maxFileSizeMb = 2.0f;
  bool autoCompress = true;
  uint16_t maxPhotoCount = 20;
  char homeWallpaperPath[192] = "";
  char clockWallpaperPath[192] = "";
  bool valid = false;
};

struct SdBrowserFile {
  char path[192];
  char name[64];
  char type[8];
  uint32_t size;
};

static SdBrowserFile sdBrowserFiles[120];
static int sdBrowserFileCount = 0;
static constexpr int SD_BROWSER_RESPONSE_MAX_FILES = 24;
static StaticJsonDocument<8192> sdListResponseDoc;

struct SdUploadSession {
  bool active;
  bool waitingBinary;
  bool overwrite;
  char uploadId[48];
  char targetPath[192];
  char tempPath[208];
  uint32_t expectedSize;
  uint32_t receivedSize;
  int expectedSeq;
  int pendingSeq;
  int pendingLen;
  File file;
};

static SdUploadSession sdUploadSession;

static PhotoFrameRemoteSettings photoFrameSettings;
static uint32_t lastPhotoSettingsRequestMs = 0;
static uint32_t lastPhotoSettingsApplyMs = 0;
static uint32_t lastPhotoAutoAdvanceMs = 0;
static constexpr uint32_t PHOTO_SETTINGS_POLL_INTERVAL_MS = 10000;
static uint32_t lastPhotoStateReportMs = 0;
static uint32_t lastPhotoStateEventMs = 0;
static constexpr uint32_t PHOTO_STATE_REPORT_INTERVAL_MS = 15000;
static constexpr uint32_t PHOTO_STATE_EVENT_MIN_GAP_MS = 400;
static char currentPhotoName[64] = "";
static char currentPhotoPath[192] = "";
static char currentPhotoDecoder[16] = "-";
static bool currentPhotoValid = false;
static uint16_t sdPhotoLimitSkipped = 0;

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
static void attachGestureHandlers(lv_obj_t *obj);
static void refreshInboxView();
static void loadSdPhotoList();
static void showCurrentPhotoFrame();
static bool ensurePhotoDecoderReady();
static void requestPhotoFrameSettings(bool force = false);
static void processPhotoFrameAutoPlay();
static void sendPhotoFrameState(const char *reason, bool force = false);
static void handlePhotoControlCommand(const JsonObjectConst &data);
static void loadSdAudioList();
static void showCurrentAudioTrack();
static void processAudioPlayback();
static void processPendingAudioControl();
static bool startAudioPlayback(int index);
static void stopAudioPlayback(bool keepStatus = false);
static void refreshAudioTimeLabel(bool force = false);
static bool isAudioRunning();
static void loadSdVideoList();
static void showCurrentVideoTrack();
static void processVideoPlayback();
static void processPendingVideoControl();
static bool startVideoPlayback(int index);
static void stopVideoPlayback(bool keepStatus = false);
static bool readNextMjpegFrame(File &file, uint8_t *dst, size_t dstMaxLen, size_t *outLen, char *reason, size_t reasonSize);
static bool decodeVideoJpegToTrueColor(const uint8_t *jpegData, size_t jpegSize, lv_img_header_t *header, char *reason, size_t reasonSize);
static void processDynamicWallpapers();
static void refreshDynamicWallpaperSources();
static void prepareDynamicWallpaperForPage(int pageIndex, bool forceFrame = false);
static void pauseDynamicWallpapersForMs(uint32_t durationMs);
static bool showBootSplashFromSd(uint32_t holdMs);
static void clearBootSplashOverlay();
static void sendSdListResponse(const char *requestId, int offset, int limit);
static void sendSdDeleteResponse(const char *requestId, const char *targetPath, bool success, const char *reason);
static void sendSdPreviewResponse(const char *requestId, const char *targetPath, bool success, uint32_t len, const char *reason);
static void resetSdUploadSession(bool removeTempFile);
static bool ensureSdParentDirectories(const char *targetPath, char *reason, size_t reasonSize);
static void sendSdUploadBeginAck(const char *uploadId, bool success, const char *reason);
static void sendSdUploadChunkAck(const char *uploadId, int seq, bool success, const char *reason);
static void sendSdUploadCommitAck(const char *uploadId, bool success, const char *finalPath, const char *reason);
static void sendVoiceCommand(const char *text);
static bool ensureVoiceMicReady(char *reason, size_t reasonSize);
static void releaseVoiceMic();
static void setVoiceMicStreaming(bool enabled, const char *reason, bool notifyServer = true);
static void sendVoiceStreamStart();
static void sendVoiceStreamStop(const char *reason);
static void sendVoiceStreamChunkMeta(size_t byteLen, uint8_t levelPercent);
static void processVoiceMicStreaming();
static int parseUiPageFromVoiceName(const char *name);
static bool shouldSuppressClick();
static void suppressClicksForMs(uint32_t durationMs);
static void suppressClicksAfterHome();
static void homeShortcutEventCallback(lv_event_t *e);
static void voiceCommandButtonCallback(lv_event_t *e);
static void voiceMicToggleCallback(lv_event_t *e);
static void videoControlCallback(lv_event_t *e);
static void renderHomeShortcutIcon(uint8_t slotIdx, const HomeShortcutConfig &item);
static void refreshHomeShortcutSlots();
static void shiftHomeCarousel(int delta);
static void layoutHomeShortcuts();

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
  if (shouldSuppressClick()) {
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
  lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(btn);
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

static const char *sdCardTypeToText(uint8_t cardType) {
  switch (cardType) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC";
    default: return "NONE";
  }
}

static void formatStorageSize(uint64_t bytes, char *dst, size_t dstSize) {
  if (dst == nullptr || dstSize == 0) {
    return;
  }

  const char *units[] = {"B", "KB", "MB", "GB"};
  double value = (double)bytes;
  int unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }

  if (unit == 0) {
    snprintf(dst, dstSize, "%llu%s", (unsigned long long)bytes, units[unit]);
  } else {
    snprintf(dst, dstSize, "%.1f%s", value, units[unit]);
  }
}

static void scanSdRootDirectory() {
  sdRootDirCount = 0;
  sdRootFileCount = 0;
  copyText(sdRootPreview, sizeof(sdRootPreview), "(empty)");

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    copyText(sdRootPreview, sizeof(sdRootPreview), "root open failed");
    return;
  }

  char preview[96];
  preview[0] = '\0';
  int previewItems = 0;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }

    if (entry.isDirectory()) {
      sdRootDirCount++;
    } else {
      sdRootFileCount++;
    }

    if (previewItems < 3) {
      const char *name = entry.name();
      if (name != nullptr && name[0] != '\0') {
        while (*name == '/') {
          name++;
        }

        char shortName[32];
        if (strlen(name) > 18) {
          snprintf(shortName, sizeof(shortName), "%.18s...", name);
        } else {
          snprintf(shortName, sizeof(shortName), "%s", name);
        }

        size_t offset = strlen(preview);
        if (previewItems > 0 && offset < sizeof(preview) - 1) {
          snprintf(preview + offset, sizeof(preview) - offset, ", ");
          offset = strlen(preview);
        }
        if (offset < sizeof(preview) - 1) {
          snprintf(preview + offset, sizeof(preview) - offset, "%s", shortName);
        }
        previewItems++;
      }
    }

    entry.close();
  }

  root.close();

  if (previewItems > 0) {
    copyText(sdRootPreview, sizeof(sdRootPreview), preview);
  }
}

static void detectAndScanSdCard() {
  sdInitAttempted = true;
  sdMounted = false;
  sdMode1Bit = false;
  sdCardType = CARD_NONE;
  sdTotalBytes = 0;
  sdUsedBytes = 0;
  sdRootDirCount = 0;
  sdRootFileCount = 0;
  copyText(sdRootPreview, sizeof(sdRootPreview), "--");
  copyText(sdMountReason, sizeof(sdMountReason), "mounting");

  SD_MMC.end();

  bool mounted = false;
  if (SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN, SD_MMC_D1_PIN, SD_MMC_D2_PIN, SD_MMC_D3_PIN)) {
    mounted = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT);
    sdMode1Bit = false;
  }

  if (!mounted) {
    SD_MMC.end();
    if (SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)) {
      mounted = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT);
      sdMode1Bit = true;
    }
  }

  if (!mounted) {
    copyText(sdMountReason, sizeof(sdMountReason), "mount failed");
    Serial.println("[SD] mount failed");
    refreshDynamicWallpaperSources();
    if (pages[UI_PAGE_HOME] != nullptr) {
      prepareDynamicWallpaperForPage(currentPage, true);
    }
    return;
  }

  sdCardType = SD_MMC.cardType();
  if (sdCardType == CARD_NONE) {
    SD_MMC.end();
    copyText(sdMountReason, sizeof(sdMountReason), "no card");
    Serial.println("[SD] no card");
    refreshDynamicWallpaperSources();
    if (pages[UI_PAGE_HOME] != nullptr) {
      prepareDynamicWallpaperForPage(currentPage, true);
    }
    return;
  }

  sdMounted = true;
  sdTotalBytes = SD_MMC.totalBytes();
  sdUsedBytes = SD_MMC.usedBytes();
  scanSdRootDirectory();
  copyText(sdMountReason, sizeof(sdMountReason), "ok");

  char totalText[16];
  char usedText[16];
  formatStorageSize(sdTotalBytes, totalText, sizeof(totalText));
  formatStorageSize(sdUsedBytes, usedText, sizeof(usedText));

  Serial.printf(
    "[SD] mounted mode=%s type=%s used=%s total=%s dirs=%lu files=%lu root=%s\n",
    sdMode1Bit ? "1-bit" : "4-bit",
    sdCardTypeToText(sdCardType),
    usedText,
    totalText,
    (unsigned long)sdRootDirCount,
    (unsigned long)sdRootFileCount,
    sdRootPreview
  );
  refreshDynamicWallpaperSources();
  if (pages[UI_PAGE_HOME] != nullptr) {
    prepareDynamicWallpaperForPage(currentPage, true);
  }
}

static bool equalsIgnoreCase(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }

  while (*a != '\0' && *b != '\0') {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) {
      return false;
    }
    a++;
    b++;
  }

  return *a == '\0' && *b == '\0';
}

static bool hasPhotoExtension(const char *path) {
  if (path == nullptr) {
    return false;
  }

  const char *dot = strrchr(path, '.');
  if (dot == nullptr) {
    return false;
  }

  return equalsIgnoreCase(dot, ".jpg") || equalsIgnoreCase(dot, ".jpeg") || equalsIgnoreCase(dot, ".sjpg");
}

static bool hasAudioExtension(const char *path) {
  if (path == nullptr) {
    return false;
  }

  const char *dot = strrchr(path, '.');
  if (dot == nullptr) {
    return false;
  }

  return equalsIgnoreCase(dot, ".mp3") || equalsIgnoreCase(dot, ".wav");
}

static bool hasVideoExtension(const char *path) {
  if (path == nullptr) {
    return false;
  }

  const char *dot = strrchr(path, '.');
  if (dot == nullptr) {
    return false;
  }

  return equalsIgnoreCase(dot, ".mp4") ||
         equalsIgnoreCase(dot, ".mov") ||
         equalsIgnoreCase(dot, ".mkv") ||
         equalsIgnoreCase(dot, ".avi") ||
         equalsIgnoreCase(dot, ".mjpeg") ||
         equalsIgnoreCase(dot, ".mjpg");
}

static bool hasMjpegPlaybackExtension(const char *path) {
  if (path == nullptr) {
    return false;
  }

  const char *dot = strrchr(path, '.');
  if (dot == nullptr) {
    return false;
  }

  return equalsIgnoreCase(dot, ".mjpeg") || equalsIgnoreCase(dot, ".mjpg");
}

static bool pickFirstExistingPath(const char *const *candidates, size_t count, char *outPath, size_t outSize) {
  if (outPath == nullptr || outSize == 0) {
    return false;
  }
  outPath[0] = '\0';

  if (!sdMounted || candidates == nullptr || count == 0) {
    return false;
  }

  for (size_t i = 0; i < count; ++i) {
    const char *candidate = candidates[i];
    if (candidate == nullptr || candidate[0] == '\0') {
      continue;
    }
    if (SD_MMC.exists(candidate)) {
      copyText(outPath, outSize, candidate);
      return true;
    }
  }
  return false;
}

static bool findFirstMjpegInDirectory(const char *dirPath, int depth, char *outPath, size_t outSize) {
  if (outPath == nullptr || outSize == 0) {
    return false;
  }
  outPath[0] = '\0';
  if (!sdMounted || dirPath == nullptr || depth > 3) {
    return false;
  }

  File dir = SD_MMC.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return false;
  }

  bool found = false;
  while (!found) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    const char *entryPath = entry.path();
    if (entryPath != nullptr && entryPath[0] != '\0') {
      char childPath[192];
      if (entryPath[0] == '/') {
        copyText(childPath, sizeof(childPath), entryPath);
      } else if (strcmp(dirPath, "/") == 0) {
        snprintf(childPath, sizeof(childPath), "/%s", entryPath);
      } else {
        snprintf(childPath, sizeof(childPath), "%s/%s", dirPath, entryPath);
      }

      if (entry.isDirectory()) {
        if (depth < 3 && findFirstMjpegInDirectory(childPath, depth + 1, outPath, outSize)) {
          found = true;
        }
      } else if (hasMjpegPlaybackExtension(childPath)) {
        copyText(outPath, outSize, childPath);
        found = true;
      }
    }
    entry.close();
  }

  dir.close();
  return found;
}

static void closeDynamicWallpaper(DynamicWallpaperPlayer &player) {
  if (player.file) {
    player.file.close();
  }
  player.opened = false;
}

static bool ensureWallpaperFrameBuffer() {
  if (wallpaperFrameData != nullptr) {
    return true;
  }

  wallpaperFrameData = (uint8_t *)heap_caps_malloc(VIDEO_FRAME_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (wallpaperFrameData == nullptr) {
    wallpaperFrameData = (uint8_t *)malloc(VIDEO_FRAME_MAX_BYTES);
  }
  if (wallpaperFrameData == nullptr) {
    return false;
  }
  wallpaperFrameDataSize = 0;
  return true;
}

static bool openDynamicWallpaper(DynamicWallpaperPlayer &player) {
  if (!player.enabled || player.path[0] == '\0' || !sdMounted) {
    return false;
  }
  if (player.opened && player.file) {
    return true;
  }

  closeDynamicWallpaper(player);
  player.file = SD_MMC.open(player.path, FILE_READ);
  if (!player.file) {
    player.opened = false;
    player.failCount = 0;
    return false;
  }

  player.opened = true;
  player.lastFrameMs = 0;
  player.slowScore = 0;
  player.fastScore = 0;
  player.failCount = 0;
  player.intervalMs = player.baseIntervalMs;
  return true;
}

static bool renderNextDynamicWallpaperFrame(DynamicWallpaperPlayer &player, bool allowLoop, char *reason, size_t reasonSize) {
  if (reason != nullptr && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (!player.enabled || player.imageObj == nullptr) {
    copyText(reason, reasonSize, "wallpaper disabled");
    return false;
  }
  if (!openDynamicWallpaper(player)) {
    copyText(reason, reasonSize, "open wallpaper failed");
    return false;
  }
  if (!ensurePhotoDecoderReady()) {
    copyText(reason, reasonSize, "jpeg decoder not ready");
    return false;
  }
  if (!ensureWallpaperFrameBuffer()) {
    copyText(reason, reasonSize, "frame buffer OOM");
    return false;
  }

  size_t frameSize = 0;
  char frameReason[48];
  if (!readNextMjpegFrame(player.file, wallpaperFrameData, VIDEO_FRAME_MAX_BYTES, &frameSize, frameReason, sizeof(frameReason))) {
    if (allowLoop) {
      player.file.seek(0);
      if (!readNextMjpegFrame(player.file, wallpaperFrameData, VIDEO_FRAME_MAX_BYTES, &frameSize, frameReason, sizeof(frameReason))) {
        copyText(reason, reasonSize, frameReason);
        return false;
      }
    } else {
      copyText(reason, reasonSize, frameReason);
      return false;
    }
  }

  lv_img_header_t frameHeader;
  if (!decodeVideoJpegToTrueColor(wallpaperFrameData, frameSize, &frameHeader, frameReason, sizeof(frameReason))) {
    copyText(reason, reasonSize, frameReason);
    return false;
  }

  wallpaperFrameDataSize = frameSize;
  lv_img_set_src(player.imageObj, nullptr);
  lv_img_set_src(player.imageObj, (const void *)&videoDecodedDsc);

  int32_t viewportW = 360;
  int32_t viewportH = 360;
  int32_t zoomW = (viewportW * 256) / frameHeader.w;
  int32_t zoomH = (viewportH * 256) / frameHeader.h;
  int32_t zoom = (zoomW > zoomH) ? zoomW : zoomH; // cover
  if (zoom > 512) zoom = 512;
  if (zoom < 16) zoom = 16;

  lv_obj_set_size(player.imageObj, frameHeader.w, frameHeader.h);
  lv_img_set_pivot(player.imageObj, frameHeader.w / 2, frameHeader.h / 2);
  lv_img_set_zoom(player.imageObj, (uint16_t)zoom);
  lv_obj_center(player.imageObj);
  return true;
}

static void resetDynamicWallpaperPlayer(DynamicWallpaperPlayer &player) {
  closeDynamicWallpaper(player);
  player.lastFrameMs = 0;
  player.slowScore = 0;
  player.fastScore = 0;
  player.failCount = 0;
  player.intervalMs = player.baseIntervalMs;
}

static const char *classifySdFileType(const char *path) {
  if (hasPhotoExtension(path)) {
    return "image";
  }
  if (hasAudioExtension(path)) {
    return "audio";
  }
  if (hasVideoExtension(path)) {
    return "video";
  }
  return "other";
}

static const char *baseNameFromPath(const char *path) {
  if (path == nullptr) {
    return "";
  }
  const char *base = strrchr(path, '/');
  if (base == nullptr) {
    return path;
  }
  return base + 1;
}

static int getPhotoScanLimit() {
  int hardLimit = (int)(sizeof(sdPhotoFiles) / sizeof(sdPhotoFiles[0]));
  int configuredLimit = (int)photoFrameSettings.maxPhotoCount;
  if (configuredLimit <= 0) {
    return hardLimit;
  }
  return min(hardLimit, configuredLimit);
}

static void freePhotoRawBytes() {
  if (photoRawData != nullptr) {
    free(photoRawData);
    photoRawData = nullptr;
  }
  photoRawDataSize = 0;
  memset(&photoRawDsc, 0, sizeof(photoRawDsc));
}

static void freePhotoDecodedData() {
  if (photoDecodedData != nullptr) {
    free(photoDecodedData);
    photoDecodedData = nullptr;
  }
  photoDecodedDataSize = 0;
  memset(&photoDecodedDsc, 0, sizeof(photoDecodedDsc));
}

static void freePhotoRawData() {
  freePhotoRawBytes();
  freePhotoDecodedData();
}

static bool isSplitJpegData(const uint8_t *data, size_t size) {
  static const char kSjpgMagic[] = "_SJPG__";
  if (data == nullptr || size < sizeof(kSjpgMagic) - 1) {
    return false;
  }
  return memcmp(data, kSjpgMagic, sizeof(kSjpgMagic) - 1) == 0;
}

static bool ensurePhotoDecoderReady() {
#if LV_USE_SJPG
  if (!photoDecoderReady) {
    lv_split_jpeg_init();
    photoDecoderReady = true;
    Serial.println("[Photo] LVGL SJPG decoder initialized");
  }
  return true;
#else
  return false;
#endif
}

#if LV_USE_SJPG
struct PhotoJpegDecodeContext {
  const uint8_t *source = nullptr;
  size_t sourceSize = 0;
  size_t sourcePos = 0;
  lv_color_t *target = nullptr;
  uint16_t targetW = 0;
  uint16_t targetH = 0;
};

static size_t photoJpegInputCallback(JDEC *jd, uint8_t *buff, size_t ndata) {
  if (jd == nullptr) {
    return 0;
  }
  PhotoJpegDecodeContext *ctx = (PhotoJpegDecodeContext *)jd->device;
  if (ctx == nullptr || ctx->source == nullptr || ctx->sourcePos >= ctx->sourceSize) {
    return 0;
  }

  size_t remain = ctx->sourceSize - ctx->sourcePos;
  size_t readSize = (ndata < remain) ? ndata : remain;
  if (buff != nullptr) {
    memcpy(buff, ctx->source + ctx->sourcePos, readSize);
  }
  ctx->sourcePos += readSize;
  return readSize;
}

static int photoJpegOutputCallback(JDEC *jd, void *bitmap, JRECT *rect) {
  if (jd == nullptr || bitmap == nullptr || rect == nullptr) {
    return 0;
  }
  PhotoJpegDecodeContext *ctx = (PhotoJpegDecodeContext *)jd->device;
  if (ctx == nullptr || ctx->target == nullptr) {
    return 0;
  }

#if JD_FORMAT != 0
  return 0;
#else
  const uint8_t *src = (const uint8_t *)bitmap;
  for (uint16_t y = rect->top; y <= rect->bottom; ++y) {
    if (y >= ctx->targetH) {
      src += (size_t)(rect->right - rect->left + 1) * 3;
      continue;
    }
    size_t dstBase = (size_t)y * ctx->targetW;
    for (uint16_t x = rect->left; x <= rect->right; ++x) {
      if (x < ctx->targetW) {
        ctx->target[dstBase + x] = lv_color_make(src[0], src[1], src[2]);
      }
      src += 3;
    }
  }
  return 1;
#endif
}
#endif

static uint8_t choosePhotoJpegScale(uint16_t srcW, uint16_t srcH) {
  static constexpr uint16_t kMaxDim = 720;
  static constexpr uint32_t kMaxPixels = 450000UL;
  uint8_t scale = 0;
  while (scale < 3) {
    uint16_t scaledW = (uint16_t)((srcW + ((1U << scale) - 1U)) >> scale);
    uint16_t scaledH = (uint16_t)((srcH + ((1U << scale) - 1U)) >> scale);
    if (scaledW <= kMaxDim && scaledH <= kMaxDim && (uint32_t)scaledW * scaledH <= kMaxPixels) {
      break;
    }
    scale++;
  }
  return scale;
}

static bool decodePhotoJpegToTrueColor(lv_img_header_t *header, char *reason, size_t reasonSize) {
  if (header == nullptr) {
    copyText(reason, reasonSize, "invalid header");
    return false;
  }

#if LV_USE_SJPG
  if (photoRawData == nullptr || photoRawDataSize == 0) {
    copyText(reason, reasonSize, "jpeg bytes missing");
    return false;
  }
  if (isSplitJpegData(photoRawData, photoRawDataSize)) {
    copyText(reason, reasonSize, "split jpeg");
    return false;
  }

#if JD_FORMAT != 0
  copyText(reason, reasonSize, "JD_FORMAT unsupported");
  return false;
#else
  static constexpr size_t kWorkBufSize = 4096;
  uint8_t *workBuf = (uint8_t *)heap_caps_malloc(kWorkBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (workBuf == nullptr) {
    workBuf = (uint8_t *)malloc(kWorkBufSize);
  }
  if (workBuf == nullptr) {
    copyText(reason, reasonSize, "jpeg workbuf OOM");
    return false;
  }

  PhotoJpegDecodeContext ctx;
  ctx.source = photoRawData;
  ctx.sourceSize = photoRawDataSize;
  ctx.sourcePos = 0;

  JDEC decoder;
  JRESULT rc = jd_prepare(&decoder, photoJpegInputCallback, workBuf, kWorkBufSize, &ctx);
  if (rc != JDR_OK) {
    free(workBuf);
    char text[48];
    snprintf(text, sizeof(text), "jpeg prepare failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  uint8_t scale = choosePhotoJpegScale(decoder.width, decoder.height);
  uint16_t scaledW = (uint16_t)((decoder.width + ((1U << scale) - 1U)) >> scale);
  uint16_t scaledH = (uint16_t)((decoder.height + ((1U << scale) - 1U)) >> scale);
  if (scaledW == 0 || scaledH == 0) {
    free(workBuf);
    copyText(reason, reasonSize, "jpeg size invalid");
    return false;
  }
  if ((uint32_t)scaledW * scaledH > 800000UL) {
    free(workBuf);
    copyText(reason, reasonSize, "jpeg too large");
    return false;
  }

  freePhotoDecodedData();
  photoDecodedDataSize = (size_t)scaledW * scaledH * sizeof(lv_color_t);
  photoDecodedData = (uint8_t *)heap_caps_malloc(photoDecodedDataSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (photoDecodedData == nullptr) {
    photoDecodedData = (uint8_t *)malloc(photoDecodedDataSize);
  }
  if (photoDecodedData == nullptr) {
    free(workBuf);
    copyText(reason, reasonSize, "jpeg framebuf OOM");
    return false;
  }
  memset(photoDecodedData, 0, photoDecodedDataSize);

  ctx.sourcePos = 0;
  ctx.target = (lv_color_t *)photoDecodedData;
  ctx.targetW = scaledW;
  ctx.targetH = scaledH;
  rc = jd_prepare(&decoder, photoJpegInputCallback, workBuf, kWorkBufSize, &ctx);
  if (rc != JDR_OK) {
    free(workBuf);
    freePhotoDecodedData();
    char text[48];
    snprintf(text, sizeof(text), "jpeg reopen failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  rc = jd_decomp(&decoder, photoJpegOutputCallback, scale);
  free(workBuf);
  if (rc != JDR_OK) {
    freePhotoDecodedData();
    char text[48];
    snprintf(text, sizeof(text), "jpeg decomp failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  memset(&photoDecodedDsc, 0, sizeof(photoDecodedDsc));
  photoDecodedDsc.header.always_zero = 0;
  photoDecodedDsc.header.w = scaledW;
  photoDecodedDsc.header.h = scaledH;
  photoDecodedDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  photoDecodedDsc.data_size = (uint32_t)photoDecodedDataSize;
  photoDecodedDsc.data = photoDecodedData;

  header->always_zero = 0;
  header->w = scaledW;
  header->h = scaledH;
  header->cf = LV_IMG_CF_TRUE_COLOR;
  return true;
#endif
#else
  copyText(reason, reasonSize, "sjpg disabled");
  return false;
#endif
}

static bool validatePhotoRawSource(lv_img_header_t *header, char *reason, size_t reasonSize) {
  if (header == nullptr) {
    copyText(reason, reasonSize, "invalid header");
    return false;
  }
  lv_img_header_t localHeader;
  if (lv_img_decoder_get_info((const void *)&photoRawDsc, &localHeader) != LV_RES_OK || localHeader.w <= 0 || localHeader.h <= 0) {
    copyText(reason, reasonSize, "decode header failed");
    return false;
  }

  lv_img_decoder_dsc_t decoderDsc;
  memset(&decoderDsc, 0, sizeof(decoderDsc));
  if (lv_img_decoder_open(&decoderDsc, (const void *)&photoRawDsc, lv_color_black(), 0) != LV_RES_OK) {
    copyText(reason, reasonSize, "decoder open failed");
    return false;
  }
  lv_img_decoder_close(&decoderDsc);
  *header = localHeader;
  return true;
}

static void setPhotoFrameStatus(const char *text, lv_color_t color) {
  if (photoFrameStatusLabel == nullptr) {
    return;
  }
  lv_label_set_text(photoFrameStatusLabel, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(photoFrameStatusLabel, color, LV_PART_MAIN);
}

static void updatePhotoFrameNavButtons() {
  bool canPrev = sdPhotoCount > 1;
  bool canNext = sdPhotoCount > 1;
  bool canReload = sdMounted;

  if (photoFramePrevBtn != nullptr) {
    if (canPrev) {
      lv_obj_add_flag(photoFramePrevBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFramePrevBtn, LV_OPA_COVER, LV_PART_MAIN);
    } else {
      lv_obj_clear_flag(photoFramePrevBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFramePrevBtn, LV_OPA_50, LV_PART_MAIN);
    }
  }

  if (photoFrameNextBtn != nullptr) {
    if (canNext) {
      lv_obj_add_flag(photoFrameNextBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFrameNextBtn, LV_OPA_COVER, LV_PART_MAIN);
    } else {
      lv_obj_clear_flag(photoFrameNextBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFrameNextBtn, LV_OPA_50, LV_PART_MAIN);
    }
  }

  if (photoFrameReloadBtn != nullptr) {
    if (canReload) {
      lv_obj_add_flag(photoFrameReloadBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFrameReloadBtn, LV_OPA_COVER, LV_PART_MAIN);
    } else {
      lv_obj_clear_flag(photoFrameReloadBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(photoFrameReloadBtn, LV_OPA_50, LV_PART_MAIN);
    }
  }
}

static void addPhotoCandidate(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  int limit = getPhotoScanLimit();
  if (sdPhotoCount >= limit) {
    sdPhotoLimitSkipped++;
    return;
  }

  SdPhotoFile &target = sdPhotoFiles[sdPhotoCount];
  copyText(target.path, sizeof(target.path), path);
  copyText(target.name, sizeof(target.name), baseNameFromPath(path));
  sdPhotoCount++;
}

static void scanPhotoDirectoryRecursive(const char *dirPath, int depth) {
  if (dirPath == nullptr || depth > 4) {
    return;
  }
  int limit = getPhotoScanLimit();
  if (sdPhotoCount >= limit) {
    return;
  }

  File dir = SD_MMC.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  while (sdPhotoCount < limit) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    const char *entryPath = entry.name();
    if (entryPath != nullptr && entryPath[0] != '\0') {
      char childPath[192];
      if (entryPath[0] == '/') {
        copyText(childPath, sizeof(childPath), entryPath);
      } else if (strcmp(dirPath, "/") == 0) {
        snprintf(childPath, sizeof(childPath), "/%s", entryPath);
      } else {
        snprintf(childPath, sizeof(childPath), "%s/%s", dirPath, entryPath);
      }

      if (entry.isDirectory()) {
        if (depth < 4) {
          scanPhotoDirectoryRecursive(childPath, depth + 1);
        }
      } else if (hasPhotoExtension(childPath)) {
        addPhotoCandidate(childPath);
      }
    }
    entry.close();
  }
  dir.close();
}

static void loadSdPhotoList() {
  sdPhotoCount = 0;
  sdPhotoIndex = 0;
  sdPhotoLimitSkipped = 0;

  if (!sdMounted) {
    setPhotoFrameStatus("SD not mounted", lv_color_hex(0xEF5350));
    updatePhotoFrameNavButtons();
    return;
  }

  scanPhotoDirectoryRecursive("/", 0);
  Serial.printf("[Photo] scanned %d image files (jpg/jpeg/sjpg), skippedByLimit=%u limit=%d\n",
                sdPhotoCount, sdPhotoLimitSkipped, getPhotoScanLimit());

  if (sdPhotoCount <= 0) {
    setPhotoFrameStatus("No JPG/JPEG/SJPG found", lv_color_hex(0xFFB74D));
  } else {
    char status[72];
    if (sdPhotoLimitSkipped > 0) {
      snprintf(status, sizeof(status), "Loaded %d images (limit %d)", sdPhotoCount, getPhotoScanLimit());
    } else {
      snprintf(status, sizeof(status), "Found %d images", sdPhotoCount);
    }
    setPhotoFrameStatus(status, lv_color_hex(0x81C784));
  }

  updatePhotoFrameNavButtons();
}

static bool loadPhotoFileToMemory(const char *path, char *reason, size_t reasonSize) {
  if (reason != nullptr && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (path == nullptr || path[0] == '\0') {
    copyText(reason, reasonSize, "Invalid path");
    return false;
  }
  if (!sdMounted) {
    copyText(reason, reasonSize, "SD not mounted");
    return false;
  }
  if (!ensurePhotoDecoderReady()) {
    copyText(reason, reasonSize, "SJPG decoder disabled");
    return false;
  }

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    copyText(reason, reasonSize, "Open failed");
    return false;
  }

  const size_t maxBytes = 3 * 1024 * 1024;
  size_t fileSize = (size_t)f.size();
  if (fileSize == 0 || fileSize > maxBytes) {
    f.close();
    copyText(reason, reasonSize, "File too large/empty");
    return false;
  }

  freePhotoRawData();
  photoRawData = (uint8_t *)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (photoRawData == nullptr) {
    photoRawData = (uint8_t *)malloc(fileSize);
  }
  if (photoRawData == nullptr) {
    f.close();
    copyText(reason, reasonSize, "No memory");
    return false;
  }

  size_t offset = 0;
  while (offset < fileSize) {
    size_t chunk = fileSize - offset;
    if (chunk > 4096) {
      chunk = 4096;
    }
    size_t n = f.read(photoRawData + offset, chunk);
    if (n == 0) {
      break;
    }
    offset += n;
  }
  f.close();

  if (offset != fileSize) {
    freePhotoRawData();
    copyText(reason, reasonSize, "Read incomplete");
    return false;
  }

  photoRawDataSize = fileSize;
  memset(&photoRawDsc, 0, sizeof(photoRawDsc));
  photoRawDsc.header.always_zero = 0;
  photoRawDsc.header.w = 0;
  photoRawDsc.header.h = 0;
  photoRawDsc.header.cf = LV_IMG_CF_RAW;
  photoRawDsc.data_size = (uint32_t)photoRawDataSize;
  photoRawDsc.data = photoRawData;
  return true;
}

static void showCurrentPhotoFrame() {
  if (photoFrameImage == nullptr || photoFrameNameLabel == nullptr || photoFrameIndexLabel == nullptr) {
    return;
  }

  if (photoFrameRootLabel != nullptr) {
    lv_label_set_text_fmt(photoFrameRootLabel, "Root: %s", sdRootPreview);
  }

  if (!sdMounted) {
    freePhotoRawData();
    lv_img_set_src(photoFrameImage, nullptr);
    lv_label_set_text(photoFrameNameLabel, "No SD card");
    lv_label_set_text(photoFrameIndexLabel, "0/0");
    setPhotoFrameStatus("SD not mounted", lv_color_hex(0xEF5350));
    currentPhotoValid = false;
    copyText(currentPhotoName, sizeof(currentPhotoName), "");
    copyText(currentPhotoPath, sizeof(currentPhotoPath), "");
    copyText(currentPhotoDecoder, sizeof(currentPhotoDecoder), "-");
    updatePhotoFrameNavButtons();
    sendPhotoFrameState("no_sd");
    return;
  }

  if (sdPhotoCount <= 0) {
    freePhotoRawData();
    lv_img_set_src(photoFrameImage, nullptr);
    lv_label_set_text(photoFrameNameLabel, "No JPG/JPEG/SJPG on SD");
    lv_label_set_text(photoFrameIndexLabel, "0/0");
    setPhotoFrameStatus("Tap Reload to rescan", lv_color_hex(0xFFB74D));
    currentPhotoValid = false;
    copyText(currentPhotoName, sizeof(currentPhotoName), "");
    copyText(currentPhotoPath, sizeof(currentPhotoPath), "");
    copyText(currentPhotoDecoder, sizeof(currentPhotoDecoder), "-");
    updatePhotoFrameNavButtons();
    sendPhotoFrameState("empty");
    return;
  }

  if (sdPhotoIndex < 0) sdPhotoIndex = 0;
  if (sdPhotoIndex >= sdPhotoCount) sdPhotoIndex = sdPhotoCount - 1;
  int startIndex = sdPhotoIndex;
  int shownIndex = -1;
  lv_img_header_t shownHeader;
  memset(&shownHeader, 0, sizeof(shownHeader));
  const void *shownSrc = nullptr;
  char shownDecoder[16];
  copyText(shownDecoder, sizeof(shownDecoder), "-");
  char failReason[64];
  failReason[0] = '\0';

  for (int attempt = 0; attempt < sdPhotoCount; ++attempt) {
    int idx = (startIndex + attempt) % sdPhotoCount;
    SdPhotoFile &candidate = sdPhotoFiles[idx];

    char reason[64];
    if (!loadPhotoFileToMemory(candidate.path, reason, sizeof(reason))) {
      copyText(failReason, sizeof(failReason), reason);
      Serial.printf("[Photo] load failed: %s (%s)\n", candidate.path, reason);
      continue;
    }

    lv_img_header_t header;
    memset(&header, 0, sizeof(header));
    bool useRgb565 = false;
    if (!isSplitJpegData(photoRawData, photoRawDataSize)) {
      if (decodePhotoJpegToTrueColor(&header, reason, sizeof(reason))) {
        useRgb565 = true;
      } else {
        Serial.printf("[Photo] rgb565 decode failed: %s (%s), fallback raw decoder\n", candidate.path, reason);
      }
    }

    if (!useRgb565) {
      if (!validatePhotoRawSource(&header, reason, sizeof(reason))) {
        copyText(failReason, sizeof(failReason), reason);
        Serial.printf("[Photo] raw decoder failed: %s (%s)\n", candidate.path, reason);
        continue;
      }
      copyText(shownDecoder, sizeof(shownDecoder), "raw");
      shownSrc = (const void *)&photoRawDsc;
    } else {
      copyText(shownDecoder, sizeof(shownDecoder), "rgb565");
      shownSrc = (const void *)&photoDecodedDsc;
      freePhotoRawBytes();
    }

    shownIndex = idx;
    shownHeader = header;
    break;
  }

  if (shownIndex < 0 || shownSrc == nullptr) {
    freePhotoRawData();
    lv_img_set_src(photoFrameImage, nullptr);
    lv_label_set_text(photoFrameNameLabel, "No decodable image");
    lv_label_set_text(photoFrameIndexLabel, "0/0");
    char status[96];
    snprintf(status, sizeof(status), "Decode failed: %s", failReason[0] == '\0' ? "unsupported files" : failReason);
    setPhotoFrameStatus(status, lv_color_hex(0xEF5350));
    currentPhotoValid = false;
    copyText(currentPhotoName, sizeof(currentPhotoName), "");
    copyText(currentPhotoPath, sizeof(currentPhotoPath), "");
    copyText(currentPhotoDecoder, sizeof(currentPhotoDecoder), "-");
    updatePhotoFrameNavButtons();
    sendPhotoFrameState("decode_fail");
    return;
  }

  sdPhotoIndex = shownIndex;
  SdPhotoFile &photo = sdPhotoFiles[sdPhotoIndex];
  lv_img_set_src(photoFrameImage, shownSrc);

  int32_t viewportW = 288;
  int32_t viewportH = 202;
  if (photoFrameViewport != nullptr) {
    int32_t w = lv_obj_get_content_width(photoFrameViewport);
    int32_t h = lv_obj_get_content_height(photoFrameViewport);
    if (w > 0) viewportW = w;
    if (h > 0) viewportH = h;
  }

  int32_t zoomW = (viewportW * 256) / shownHeader.w;
  int32_t zoomH = (viewportH * 256) / shownHeader.h;
  int32_t zoom = (zoomW < zoomH) ? zoomW : zoomH;
  if (zoom > 256) zoom = 256;
  if (zoom < 16) zoom = 16;

  lv_obj_set_size(photoFrameImage, shownHeader.w, shownHeader.h);
  lv_img_set_pivot(photoFrameImage, shownHeader.w / 2, shownHeader.h / 2);
  lv_img_set_zoom(photoFrameImage, (uint16_t)zoom);
  lv_obj_center(photoFrameImage);
  Serial.printf(
    "[Photo] showing %d/%d %s (%dx%d zoom=%ld viewport=%ldx%ld decoder=%s)\n",
    sdPhotoIndex + 1,
    sdPhotoCount,
    photo.path,
    shownHeader.w,
    shownHeader.h,
    (long)zoom,
    (long)viewportW,
    (long)viewportH,
    shownDecoder
  );

  lv_label_set_text(photoFrameNameLabel, photo.name);
  lv_label_set_text_fmt(photoFrameIndexLabel, "%d/%d", sdPhotoIndex + 1, sdPhotoCount);
  char status[64];
  snprintf(status, sizeof(status), "Photo loaded (%s)", shownDecoder);
  setPhotoFrameStatus(status, lv_color_hex(0x81C784));
  currentPhotoValid = true;
  copyText(currentPhotoName, sizeof(currentPhotoName), photo.name);
  copyText(currentPhotoPath, sizeof(currentPhotoPath), photo.path);
  copyText(currentPhotoDecoder, sizeof(currentPhotoDecoder), shownDecoder);
  updatePhotoFrameNavButtons();
  sendPhotoFrameState("show");
}

static void photoFrameControlCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }

  intptr_t action = (intptr_t)lv_event_get_user_data(e);
  Serial.printf("[Photo] nav click action=%ld count=%d idx=%d mounted=%d\n", (long)action, sdPhotoCount, sdPhotoIndex, sdMounted ? 1 : 0);
  if (action == 0) { // Prev
    if (sdPhotoCount > 0) {
      setPhotoFrameStatus("Loading previous photo...", lv_color_hex(0x90CAF9));
      sdPhotoIndex = (sdPhotoIndex - 1 + sdPhotoCount) % sdPhotoCount;
      showCurrentPhotoFrame();
      lastPhotoAutoAdvanceMs = millis();
    }
  } else if (action == 1) { // Reload
    setPhotoFrameStatus("Rescanning SD...", lv_color_hex(0x90CAF9));
    detectAndScanSdCard();
    loadSdPhotoList();
    showCurrentPhotoFrame();
    lastPhotoAutoAdvanceMs = millis();
    requestPhotoFrameSettings(true);
  } else if (action == 2) { // Next
    if (sdPhotoCount > 0) {
      setPhotoFrameStatus("Loading next photo...", lv_color_hex(0x90CAF9));
      sdPhotoIndex = (sdPhotoIndex + 1) % sdPhotoCount;
      showCurrentPhotoFrame();
      lastPhotoAutoAdvanceMs = millis();
    }
  }
}

static void setVideoStatus(const char *text, lv_color_t color) {
  if (videoStatusLabel == nullptr) {
    return;
  }
  lv_label_set_text(videoStatusLabel, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(videoStatusLabel, color, LV_PART_MAIN);
}

static void setVideoButtonEnabled(lv_obj_t *btn, bool enabled) {
  if (btn == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }
}

static void updateVideoControlButtons(bool hasTracks) {
  bool canStepTrack = hasTracks && sdVideoCount > 1;
  setVideoButtonEnabled(videoPlayBtn, hasTracks);
  setVideoButtonEnabled(videoPrevBtn, canStepTrack);
  setVideoButtonEnabled(videoNextBtn, canStepTrack);
  if (videoPlayBtnLabel != nullptr) {
    lv_label_set_text(videoPlayBtnLabel, (videoPlaying && !videoPaused) ? "Pause" : "Play");
  }
}

static void addVideoCandidate(const char *path, uint32_t size) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  if (sdVideoCount >= (int)(sizeof(sdVideoFiles) / sizeof(sdVideoFiles[0]))) {
    return;
  }
  if (!hasMjpegPlaybackExtension(path)) {
    return;
  }

  SdVideoFile &target = sdVideoFiles[sdVideoCount];
  copyText(target.path, sizeof(target.path), path);
  copyText(target.name, sizeof(target.name), baseNameFromPath(path));
  target.size = size;
  sdVideoCount++;
}

static void scanVideoDirectoryRecursive(const char *dirPath, int depth) {
  if (dirPath == nullptr || depth > VIDEO_SCAN_MAX_DEPTH) {
    return;
  }
  if (sdVideoCount >= (int)(sizeof(sdVideoFiles) / sizeof(sdVideoFiles[0]))) {
    return;
  }

  File dir = SD_MMC.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  while (sdVideoCount < (int)(sizeof(sdVideoFiles) / sizeof(sdVideoFiles[0]))) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    const char *entryPath = entry.path();
    if (entryPath != nullptr && entryPath[0] != '\0') {
      char childPath[192];
      if (entryPath[0] == '/') {
        copyText(childPath, sizeof(childPath), entryPath);
      } else if (strcmp(dirPath, "/") == 0) {
        snprintf(childPath, sizeof(childPath), "/%s", entryPath);
      } else {
        snprintf(childPath, sizeof(childPath), "%s/%s", dirPath, entryPath);
      }

      if (entry.isDirectory()) {
        scanVideoDirectoryRecursive(childPath, depth + 1);
      } else {
        addVideoCandidate(childPath, (uint32_t)entry.size());
      }
    }
    entry.close();
  }
  dir.close();
}

static bool ensureVideoFrameBuffer() {
  if (videoFrameData != nullptr) {
    return true;
  }

  videoFrameData = (uint8_t *)heap_caps_malloc(VIDEO_FRAME_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (videoFrameData == nullptr) {
    videoFrameData = (uint8_t *)malloc(VIDEO_FRAME_MAX_BYTES);
  }
  if (videoFrameData == nullptr) {
    return false;
  }
  videoFrameDataSize = 0;
  return true;
}

static bool readNextMjpegFrame(File &file, uint8_t *dst, size_t dstMaxLen, size_t *outLen, char *reason, size_t reasonSize) {
  if (outLen != nullptr) {
    *outLen = 0;
  }
  if (reason != nullptr && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (!file || dst == nullptr || dstMaxLen < 4 || outLen == nullptr) {
    copyText(reason, reasonSize, "invalid frame buffer");
    return false;
  }

  bool inFrame = false;
  uint8_t prev = 0;
  size_t len = 0;

  while (file.available()) {
    int c = file.read();
    if (c < 0) {
      break;
    }
    uint8_t b = (uint8_t)c;

    if (!inFrame) {
      if (prev == 0xFF && b == 0xD8) {
        inFrame = true;
        len = 0;
        dst[len++] = 0xFF;
        dst[len++] = 0xD8;
      }
    } else {
      if (len >= dstMaxLen) {
        copyText(reason, reasonSize, "frame too large");
        return false;
      }
      dst[len++] = b;
      if (prev == 0xFF && b == 0xD9) {
        *outLen = len;
        return true;
      }
    }

    prev = b;
  }

  copyText(reason, reasonSize, inFrame ? "incomplete frame" : "no frame found");
  return false;
}

#if LV_USE_SJPG
struct VideoJpegDecodeContext {
  const uint8_t *source = nullptr;
  size_t sourceSize = 0;
  size_t sourcePos = 0;
  lv_color_t *target = nullptr;
  uint16_t targetW = 0;
  uint16_t targetH = 0;
};

static size_t videoJpegInputCallback(JDEC *jd, uint8_t *buff, size_t ndata) {
  if (jd == nullptr) {
    return 0;
  }
  VideoJpegDecodeContext *ctx = (VideoJpegDecodeContext *)jd->device;
  if (ctx == nullptr || ctx->source == nullptr || ctx->sourcePos >= ctx->sourceSize) {
    return 0;
  }

  size_t remain = ctx->sourceSize - ctx->sourcePos;
  size_t readSize = (ndata < remain) ? ndata : remain;
  if (buff != nullptr) {
    memcpy(buff, ctx->source + ctx->sourcePos, readSize);
  }
  ctx->sourcePos += readSize;
  return readSize;
}

static int videoJpegOutputCallback(JDEC *jd, void *bitmap, JRECT *rect) {
  if (jd == nullptr || bitmap == nullptr || rect == nullptr) {
    return 0;
  }
  VideoJpegDecodeContext *ctx = (VideoJpegDecodeContext *)jd->device;
  if (ctx == nullptr || ctx->target == nullptr) {
    return 0;
  }

#if JD_FORMAT != 0
  return 0;
#else
  const uint8_t *src = (const uint8_t *)bitmap;
  for (uint16_t y = rect->top; y <= rect->bottom; ++y) {
    if (y >= ctx->targetH) {
      src += (size_t)(rect->right - rect->left + 1) * 3;
      continue;
    }
    size_t dstBase = (size_t)y * ctx->targetW;
    for (uint16_t x = rect->left; x <= rect->right; ++x) {
      if (x < ctx->targetW) {
        ctx->target[dstBase + x] = lv_color_make(src[0], src[1], src[2]);
      }
      src += 3;
    }
  }
  return 1;
#endif
}
#endif

static bool decodeVideoJpegToTrueColor(const uint8_t *jpegData, size_t jpegSize, lv_img_header_t *header, char *reason, size_t reasonSize) {
  if (header == nullptr) {
    copyText(reason, reasonSize, "invalid header");
    return false;
  }

#if LV_USE_SJPG
  if (jpegData == nullptr || jpegSize == 0) {
    copyText(reason, reasonSize, "jpeg bytes missing");
    return false;
  }

#if JD_FORMAT != 0
  copyText(reason, reasonSize, "JD_FORMAT unsupported");
  return false;
#else
  static constexpr size_t kWorkBufSize = 4096;
  uint8_t *workBuf = (uint8_t *)heap_caps_malloc(kWorkBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (workBuf == nullptr) {
    workBuf = (uint8_t *)malloc(kWorkBufSize);
  }
  if (workBuf == nullptr) {
    copyText(reason, reasonSize, "jpeg workbuf OOM");
    return false;
  }

  VideoJpegDecodeContext ctx;
  ctx.source = jpegData;
  ctx.sourceSize = jpegSize;
  ctx.sourcePos = 0;

  JDEC decoder;
  JRESULT rc = jd_prepare(&decoder, videoJpegInputCallback, workBuf, kWorkBufSize, &ctx);
  if (rc != JDR_OK) {
    free(workBuf);
    char text[48];
    snprintf(text, sizeof(text), "jpeg prepare failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  uint8_t scale = choosePhotoJpegScale(decoder.width, decoder.height);
  uint16_t scaledW = (uint16_t)((decoder.width + ((1U << scale) - 1U)) >> scale);
  uint16_t scaledH = (uint16_t)((decoder.height + ((1U << scale) - 1U)) >> scale);
  if (scaledW == 0 || scaledH == 0) {
    free(workBuf);
    copyText(reason, reasonSize, "jpeg size invalid");
    return false;
  }

  size_t requiredBytes = (size_t)scaledW * scaledH * sizeof(lv_color_t);
  if (requiredBytes == 0 || requiredBytes > 900000UL) {
    free(workBuf);
    copyText(reason, reasonSize, "jpeg frame too large");
    return false;
  }

  if (videoDecodedData == nullptr || videoDecodedCapacity < requiredBytes) {
    if (videoDecodedData != nullptr) {
      free(videoDecodedData);
      videoDecodedData = nullptr;
      videoDecodedCapacity = 0;
    }
    videoDecodedData = (uint8_t *)heap_caps_malloc(requiredBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (videoDecodedData == nullptr) {
      videoDecodedData = (uint8_t *)malloc(requiredBytes);
    }
    if (videoDecodedData == nullptr) {
      free(workBuf);
      copyText(reason, reasonSize, "jpeg framebuf OOM");
      return false;
    }
    videoDecodedCapacity = requiredBytes;
  }

  ctx.sourcePos = 0;
  ctx.target = (lv_color_t *)videoDecodedData;
  ctx.targetW = scaledW;
  ctx.targetH = scaledH;

  rc = jd_prepare(&decoder, videoJpegInputCallback, workBuf, kWorkBufSize, &ctx);
  if (rc != JDR_OK) {
    free(workBuf);
    char text[48];
    snprintf(text, sizeof(text), "jpeg reopen failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  rc = jd_decomp(&decoder, videoJpegOutputCallback, scale);
  free(workBuf);
  if (rc != JDR_OK) {
    char text[48];
    snprintf(text, sizeof(text), "jpeg decomp failed (%d)", (int)rc);
    copyText(reason, reasonSize, text);
    return false;
  }

  memset(&videoDecodedDsc, 0, sizeof(videoDecodedDsc));
  videoDecodedDsc.header.always_zero = 0;
  videoDecodedDsc.header.w = scaledW;
  videoDecodedDsc.header.h = scaledH;
  videoDecodedDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  videoDecodedDsc.data_size = (uint32_t)requiredBytes;
  videoDecodedDsc.data = videoDecodedData;

  header->always_zero = 0;
  header->w = scaledW;
  header->h = scaledH;
  header->cf = LV_IMG_CF_TRUE_COLOR;
  return true;
#endif
#else
  copyText(reason, reasonSize, "sjpg disabled");
  return false;
#endif
}

static bool showBootSplashFromSd(uint32_t holdMs) {
  if (!sdMounted) {
    Serial.printf("[BootSplash] skipped: SD unavailable (%s)\n", sdMountReason);
    return false;
  }
  if (!ensurePhotoDecoderReady()) {
    Serial.println("[BootSplash] decoder unavailable");
    return false;
  }
  if (!ensureVideoFrameBuffer()) {
    Serial.println("[BootSplash] frame buffer OOM");
    return false;
  }

  char splashPath[192];
  splashPath[0] = '\0';
  bool gotPath = false;

  if (photoFrameSettings.homeWallpaperPath[0] != '\0' &&
      hasMjpegPlaybackExtension(photoFrameSettings.homeWallpaperPath) &&
      SD_MMC.exists(photoFrameSettings.homeWallpaperPath)) {
    copyText(splashPath, sizeof(splashPath), photoFrameSettings.homeWallpaperPath);
    gotPath = true;
  }

  if (!gotPath) {
    static const char *BOOT_SPLASH_CANDIDATES[] = {
      "/night7/boot.mjpeg",
      "/night7/rhythmbg.mjpeg",
      "/mjpeg/my0.mjpeg",
      "/mjpeg/my1.mjpeg",
    };
    gotPath = pickFirstExistingPath(
      BOOT_SPLASH_CANDIDATES,
      sizeof(BOOT_SPLASH_CANDIDATES) / sizeof(BOOT_SPLASH_CANDIDATES[0]),
      splashPath,
      sizeof(splashPath)
    );
  }

  if (!gotPath) {
    gotPath = findFirstMjpegInDirectory("/night7", 0, splashPath, sizeof(splashPath)) ||
              findFirstMjpegInDirectory("/mjpeg", 0, splashPath, sizeof(splashPath));
  }
  if (!gotPath) {
    Serial.println("[BootSplash] no mjpeg source");
    return false;
  }

  File splashFile = SD_MMC.open(splashPath, FILE_READ);
  if (!splashFile) {
    Serial.printf("[BootSplash] open failed: %s\n", splashPath);
    return false;
  }

  size_t frameSize = 0;
  char reason[72];
  bool gotFrame = readNextMjpegFrame(
    splashFile,
    videoFrameData,
    VIDEO_FRAME_MAX_BYTES,
    &frameSize,
    reason,
    sizeof(reason)
  );
  splashFile.close();
  if (!gotFrame || frameSize == 0) {
    Serial.printf("[BootSplash] frame read failed: %s\n", reason[0] == '\0' ? "unknown" : reason);
    return false;
  }

  lv_img_header_t frameHeader;
  if (!decodeVideoJpegToTrueColor(videoFrameData, frameSize, &frameHeader, reason, sizeof(reason))) {
    Serial.printf("[BootSplash] frame decode failed: %s\n", reason[0] == '\0' ? "unknown" : reason);
    return false;
  }

  lv_obj_t *splashImage = lv_img_create(lv_scr_act());
  lv_obj_clear_flag(splashImage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_img_set_src(splashImage, (const void *)&videoDecodedDsc);

  int32_t zoomW = ((int32_t)SCREEN_RES_HOR * 256) / frameHeader.w;
  int32_t zoomH = ((int32_t)SCREEN_RES_VER * 256) / frameHeader.h;
  int32_t zoom = (zoomW < zoomH) ? zoomW : zoomH;
  if (zoom > 256) zoom = 256;
  if (zoom < 16) zoom = 16;
  lv_obj_set_size(splashImage, frameHeader.w, frameHeader.h);
  lv_img_set_pivot(splashImage, frameHeader.w / 2, frameHeader.h / 2);
  lv_img_set_zoom(splashImage, (uint16_t)zoom);
  lv_obj_center(splashImage);

  lv_timer_handler();
  Serial.printf("[BootSplash] showing %s for %u ms\n", splashPath, (unsigned)holdMs);
  uint32_t startMs = millis();
  uint32_t nextAnimMs = startMs + 60;
  uint32_t minEndMs = startMs + holdMs;
  uint32_t hardEndMs = startMs + holdMs + 60000;
  uint32_t frameCount = 1;
  bool reachedEof = false;
  File animFile = SD_MMC.open(splashPath, FILE_READ);
  while (true) {
    uint32_t now = millis();
    if (!reachedEof && animFile && now >= nextAnimMs) {
      size_t animFrameSize = 0;
      char animReason[48];
      if (!readNextMjpegFrame(animFile, videoFrameData, VIDEO_FRAME_MAX_BYTES, &animFrameSize, animReason, sizeof(animReason))) {
        reachedEof = true;
      }
      if (animFrameSize > 0) {
        lv_img_header_t animHeader;
        if (decodeVideoJpegToTrueColor(videoFrameData, animFrameSize, &animHeader, animReason, sizeof(animReason))) {
          lv_img_set_src(splashImage, nullptr);
          lv_img_set_src(splashImage, (const void *)&videoDecodedDsc);
          frameCount++;
          if (animHeader.w != frameHeader.w || animHeader.h != frameHeader.h) {
            int32_t zW = ((int32_t)SCREEN_RES_HOR * 256) / animHeader.w;
            int32_t zH = ((int32_t)SCREEN_RES_VER * 256) / animHeader.h;
            int32_t z = (zW < zH) ? zW : zH;
            if (z > 256) z = 256;
            if (z < 16) z = 16;
            lv_obj_set_size(splashImage, animHeader.w, animHeader.h);
            lv_img_set_pivot(splashImage, animHeader.w / 2, animHeader.h / 2);
            lv_img_set_zoom(splashImage, (uint16_t)z);
            lv_obj_center(splashImage);
            frameHeader = animHeader;
          }
        }
      }
      nextAnimMs = now + 60;
    }
    lv_timer_handler();
    delay(4);
    if (reachedEof && now >= minEndMs) {
      break;
    }
    if (now >= hardEndMs) {
      Serial.printf("[BootSplash] timeout after %u ms, frames=%u\n", (unsigned)(now - startMs), (unsigned)frameCount);
      break;
    }
  }
  if (animFile) {
    animFile.close();
  }

  if (bootSplashOverlay != nullptr && bootSplashOverlay != splashImage) {
    lv_obj_del(bootSplashOverlay);
  }
  bootSplashOverlay = splashImage;
  Serial.printf("[BootSplash] done, frames=%u eof=%d\n", (unsigned)frameCount, reachedEof ? 1 : 0);
  return true;
}

static void clearBootSplashOverlay() {
  if (bootSplashOverlay != nullptr) {
    lv_obj_del(bootSplashOverlay);
    bootSplashOverlay = nullptr;
    lv_timer_handler();
  }
}

static bool renderNextVideoFrame(bool allowLoop, char *reason, size_t reasonSize) {
  if (reason != nullptr && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (!videoFile) {
    copyText(reason, reasonSize, "video file not open");
    return false;
  }
  if (!ensurePhotoDecoderReady()) {
    copyText(reason, reasonSize, "jpeg decoder not ready");
    return false;
  }
  if (!ensureVideoFrameBuffer()) {
    copyText(reason, reasonSize, "frame buffer OOM");
    return false;
  }

  size_t frameSize = 0;
  char frameReason[48];
  if (!readNextMjpegFrame(videoFile, videoFrameData, VIDEO_FRAME_MAX_BYTES, &frameSize, frameReason, sizeof(frameReason))) {
    if (allowLoop) {
      videoFile.seek(0);
      if (!readNextMjpegFrame(videoFile, videoFrameData, VIDEO_FRAME_MAX_BYTES, &frameSize, frameReason, sizeof(frameReason))) {
        copyText(reason, reasonSize, frameReason);
        return false;
      }
    } else {
      copyText(reason, reasonSize, frameReason);
      return false;
    }
  }

  lv_img_header_t frameHeader;
  if (!decodeVideoJpegToTrueColor(videoFrameData, frameSize, &frameHeader, frameReason, sizeof(frameReason))) {
    copyText(reason, reasonSize, frameReason);
    return false;
  }

  videoFrameDataSize = frameSize;
  if (videoImage != nullptr) {
    lv_img_set_src(videoImage, nullptr);
    lv_img_set_src(videoImage, (const void *)&videoDecodedDsc);

    int32_t viewportW = 288;
    int32_t viewportH = 196;
    if (videoViewport != nullptr) {
      int32_t w = lv_obj_get_content_width(videoViewport);
      int32_t h = lv_obj_get_content_height(videoViewport);
      if (w > 0) viewportW = w;
      if (h > 0) viewportH = h;
    }

    int32_t zoomW = (viewportW * 256) / frameHeader.w;
    int32_t zoomH = (viewportH * 256) / frameHeader.h;
    int32_t zoom = (zoomW < zoomH) ? zoomW : zoomH;
    if (zoom > 256) zoom = 256;
    if (zoom < 16) zoom = 16;

    lv_obj_set_size(videoImage, frameHeader.w, frameHeader.h);
    lv_img_set_pivot(videoImage, frameHeader.w / 2, frameHeader.h / 2);
    lv_img_set_zoom(videoImage, (uint16_t)zoom);
    lv_obj_center(videoImage);
  }
  return true;
}

static void stopVideoPlayback(bool keepStatus) {
  if (videoFile) {
    videoFile.close();
  }
  videoPlaying = false;
  videoPaused = false;
  videoFrameDataSize = 0;
  videoLastFrameMs = 0;
  updateVideoControlButtons(sdVideoCount > 0);

  if (!keepStatus) {
    setVideoStatus("Stopped", lv_color_hex(0xFFB74D));
  }
}

static void showCurrentVideoTrack() {
  if (videoTrackLabel == nullptr || videoIndexLabel == nullptr) {
    return;
  }

  if (!sdMounted) {
    lv_label_set_text(videoTrackLabel, "No SD card");
    lv_label_set_text(videoIndexLabel, "0/0");
    setVideoStatus("SD not mounted", lv_color_hex(0xEF5350));
    updateVideoControlButtons(false);
    return;
  }

  if (sdVideoCount <= 0) {
    lv_label_set_text(videoTrackLabel, "No MJPEG files on SD");
    lv_label_set_text(videoIndexLabel, "0/0");
    setVideoStatus("Supports .mjpeg/.mjpg", lv_color_hex(0xFFB74D));
    updateVideoControlButtons(false);
    return;
  }

  if (sdVideoIndex < 0) {
    sdVideoIndex = 0;
  }
  if (sdVideoIndex >= sdVideoCount) {
    sdVideoIndex = sdVideoCount - 1;
  }

  lv_label_set_text(videoTrackLabel, sdVideoFiles[sdVideoIndex].name);
  lv_label_set_text_fmt(videoIndexLabel, "%d/%d", sdVideoIndex + 1, sdVideoCount);
  if (videoPlaying) {
    setVideoStatus(videoPaused ? "Paused" : "Playing MJPEG", videoPaused ? lv_color_hex(0xFFB74D) : lv_color_hex(0x81C784));
  } else {
    setVideoStatus("Ready", lv_color_hex(0x90CAF9));
  }
  updateVideoControlButtons(true);
}

static void loadSdVideoList() {
  stopVideoPlayback(true);
  sdVideoCount = 0;
  sdVideoIndex = 0;

  if (!sdMounted) {
    showCurrentVideoTrack();
    return;
  }

  scanVideoDirectoryRecursive("/", 0);
  Serial.printf("[Video] scanned %d mjpeg files (.mjpeg/.mjpg)\n", sdVideoCount);
  showCurrentVideoTrack();
}

static bool startVideoPlayback(int index) {
  if (!sdMounted) {
    setVideoStatus("SD not mounted", lv_color_hex(0xEF5350));
    return false;
  }
  if (sdVideoCount <= 0) {
    setVideoStatus("No MJPEG files", lv_color_hex(0xFFB74D));
    return false;
  }
  if (index < 0 || index >= sdVideoCount) {
    return false;
  }

  stopVideoPlayback(true);
  if (!ensureVideoFrameBuffer()) {
    setVideoStatus("Frame buffer OOM", lv_color_hex(0xEF5350));
    return false;
  }

  videoFile = SD_MMC.open(sdVideoFiles[index].path, FILE_READ);
  if (!videoFile) {
    setVideoStatus("Open video failed", lv_color_hex(0xEF5350));
    return false;
  }

  sdVideoIndex = index;
  videoPlaying = true;
  videoPaused = false;
  videoLastFrameMs = 0;

  char reason[64];
  if (!renderNextVideoFrame(true, reason, sizeof(reason))) {
    stopVideoPlayback(true);
    char status[88];
    snprintf(status, sizeof(status), "Decode failed: %s", reason[0] == '\0' ? "invalid mjpeg" : reason);
    setVideoStatus(status, lv_color_hex(0xEF5350));
    return false;
  }

  showCurrentVideoTrack();
  videoLastFrameMs = millis();
  pushInboxMessage("event", "Video playback", sdVideoFiles[sdVideoIndex].name);
  return true;
}

static void processVideoPlayback() {
  if (!videoPlaying || videoPaused) {
    return;
  }

  uint32_t now = millis();
  if (videoLastFrameMs != 0 && (uint32_t)(now - videoLastFrameMs) < videoFrameIntervalMs) {
    return;
  }

  char reason[64];
  if (!renderNextVideoFrame(true, reason, sizeof(reason))) {
    stopVideoPlayback(true);
    char status[88];
    snprintf(status, sizeof(status), "Playback stopped: %s", reason[0] == '\0' ? "decode error" : reason);
    setVideoStatus(status, lv_color_hex(0xEF5350));
    return;
  }

  videoLastFrameMs = now;
}

static void processPendingVideoControl() {
  int action = pendingVideoControlAction;
  if (action == VIDEO_CONTROL_NONE) {
    return;
  }
  pendingVideoControlAction = VIDEO_CONTROL_NONE;

  if (action == VIDEO_CONTROL_RESCAN) {
    setVideoStatus("Rescanning SD...", lv_color_hex(0x90CAF9));
    detectAndScanSdCard();
    loadSdVideoList();
    return;
  }

  if (sdVideoCount <= 0) {
    showCurrentVideoTrack();
    return;
  }

  if (action == VIDEO_CONTROL_PREV) {
    int prev = (sdVideoIndex - 1 + sdVideoCount) % sdVideoCount;
    startVideoPlayback(prev);
    return;
  }

  if (action == VIDEO_CONTROL_NEXT) {
    int next = (sdVideoIndex + 1) % sdVideoCount;
    startVideoPlayback(next);
    return;
  }

  if (!videoPlaying) {
    startVideoPlayback(sdVideoIndex);
    return;
  }

  videoPaused = !videoPaused;
  if (videoPaused) {
    setVideoStatus("Paused", lv_color_hex(0xFFB74D));
  } else {
    videoLastFrameMs = 0;
    setVideoStatus("Playing MJPEG", lv_color_hex(0x81C784));
  }
  updateVideoControlButtons(true);
}

static void videoControlCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - videoLastControlMs) < VIDEO_CONTROL_COOLDOWN_MS) {
    return;
  }
  videoLastControlMs = now;

  intptr_t action = (intptr_t)lv_event_get_user_data(e);
  if (action < VIDEO_CONTROL_PREV || action > VIDEO_CONTROL_RESCAN) {
    return;
  }
  pendingVideoControlAction = (int)action;
}

static void pauseDynamicWallpapersForMs(uint32_t durationMs) {
  uint32_t target = millis() + durationMs;
  if ((int32_t)(target - dynamicWallpaperPauseUntilMs) > 0) {
    dynamicWallpaperPauseUntilMs = target;
  }
}

static void refreshDynamicWallpaperSources() {
  if (!sdMounted) {
    homeWallpaper.path[0] = '\0';
    clockWallpaper.path[0] = '\0';
    homeWallpaper.enabled = false;
    clockWallpaper.enabled = false;
    resetDynamicWallpaperPlayer(homeWallpaper);
    resetDynamicWallpaperPlayer(clockWallpaper);
    if (homeWallpaperImage != nullptr) {
      lv_img_set_src(homeWallpaperImage, nullptr);
    }
    if (clockWallpaperImage != nullptr) {
      lv_img_set_src(clockWallpaperImage, nullptr);
    }
    return;
  }

  static const char *HOME_CANDIDATES[] = {
    "/night7/boot.mjpeg",
    "/night7/rhythmbg.mjpeg",
    "/mjpeg/my0.mjpeg",
    "/mjpeg/my1.mjpeg",
    "/mjpeg/极乐净土360.mjpeg",
  };
  static const char *CLOCK_CANDIDATES[] = {
    "/clockbg/clock_elysia360.mjpeg",
    "/clockbg/clock_fuxuan360.mjpeg",
    "/clockbg/clock_genshin360.mjpeg",
  };

  char homePath[192];
  char clockPath[192];
  homePath[0] = '\0';
  clockPath[0] = '\0';
  bool gotHome = false;
  bool gotClock = false;

  if (photoFrameSettings.homeWallpaperPath[0] != '\0' &&
      hasMjpegPlaybackExtension(photoFrameSettings.homeWallpaperPath) &&
      SD_MMC.exists(photoFrameSettings.homeWallpaperPath)) {
    copyText(homePath, sizeof(homePath), photoFrameSettings.homeWallpaperPath);
    gotHome = true;
  }
  if (photoFrameSettings.clockWallpaperPath[0] != '\0' &&
      hasMjpegPlaybackExtension(photoFrameSettings.clockWallpaperPath) &&
      SD_MMC.exists(photoFrameSettings.clockWallpaperPath)) {
    copyText(clockPath, sizeof(clockPath), photoFrameSettings.clockWallpaperPath);
    gotClock = true;
  }

  if (!gotHome) {
    gotHome = pickFirstExistingPath(HOME_CANDIDATES, sizeof(HOME_CANDIDATES) / sizeof(HOME_CANDIDATES[0]), homePath, sizeof(homePath));
  }
  if (!gotClock) {
    gotClock = pickFirstExistingPath(CLOCK_CANDIDATES, sizeof(CLOCK_CANDIDATES) / sizeof(CLOCK_CANDIDATES[0]), clockPath, sizeof(clockPath));
  }

  if (!gotHome) {
    gotHome = findFirstMjpegInDirectory("/mjpeg", 0, homePath, sizeof(homePath)) ||
              findFirstMjpegInDirectory("/night7", 0, homePath, sizeof(homePath));
  }
  if (!gotClock) {
    gotClock = findFirstMjpegInDirectory("/clockbg", 0, clockPath, sizeof(clockPath));
  }
  if (!gotClock && gotHome) {
    copyText(clockPath, sizeof(clockPath), homePath);
    gotClock = true;
  }

  bool homeChanged = (!gotHome && homeWallpaper.path[0] != '\0') || (gotHome && strcmp(homeWallpaper.path, homePath) != 0);
  bool clockChanged = (!gotClock && clockWallpaper.path[0] != '\0') || (gotClock && strcmp(clockWallpaper.path, clockPath) != 0);
  if (homeChanged) {
    resetDynamicWallpaperPlayer(homeWallpaper);
  }
  if (clockChanged) {
    resetDynamicWallpaperPlayer(clockWallpaper);
  }

  if (gotHome) {
    copyText(homeWallpaper.path, sizeof(homeWallpaper.path), homePath);
    homeWallpaper.enabled = true;
  } else {
    homeWallpaper.path[0] = '\0';
    homeWallpaper.enabled = false;
    if (homeWallpaperImage != nullptr) {
      lv_img_set_src(homeWallpaperImage, nullptr);
    }
  }

  if (gotClock) {
    copyText(clockWallpaper.path, sizeof(clockWallpaper.path), clockPath);
    clockWallpaper.enabled = true;
  } else {
    clockWallpaper.path[0] = '\0';
    clockWallpaper.enabled = false;
    if (clockWallpaperImage != nullptr) {
      lv_img_set_src(clockWallpaperImage, nullptr);
    }
  }

  Serial.printf(
    "[Wallpaper] home=%s clock=%s\n",
    homeWallpaper.enabled ? homeWallpaper.path : "(disabled)",
    clockWallpaper.enabled ? clockWallpaper.path : "(disabled)"
  );
}

static void prepareDynamicWallpaperForPage(int pageIndex, bool forceFrame) {
  DynamicWallpaperPlayer *target = nullptr;
  DynamicWallpaperPlayer *other = nullptr;
  if (pageIndex == UI_PAGE_HOME) {
    target = &homeWallpaper;
    other = &clockWallpaper;
  } else if (pageIndex == UI_PAGE_CLOCK) {
    target = &clockWallpaper;
    other = &homeWallpaper;
  }

  if (target == nullptr) {
    resetDynamicWallpaperPlayer(homeWallpaper);
    resetDynamicWallpaperPlayer(clockWallpaper);
    return;
  }

  if (other != nullptr) {
    closeDynamicWallpaper(*other);
  }

  if (!target->enabled || target->path[0] == '\0' || target->imageObj == nullptr) {
    return;
  }

  if (!openDynamicWallpaper(*target)) {
    return;
  }

  if (forceFrame) {
    char reason[64];
    if (!renderNextDynamicWallpaperFrame(*target, true, reason, sizeof(reason))) {
      Serial.printf("[Wallpaper] initial frame failed (%s): %s\n", target->path, reason);
      target->failCount = 1;
    } else {
      target->lastFrameMs = millis();
      target->failCount = 0;
    }
  }
}

static void processDynamicWallpapers() {
  DynamicWallpaperPlayer *player = nullptr;
  if (currentPage == UI_PAGE_HOME) {
    player = &homeWallpaper;
  } else if (currentPage == UI_PAGE_CLOCK) {
    player = &clockWallpaper;
  } else {
    return;
  }

  if (player == nullptr || !player->enabled || player->imageObj == nullptr) {
    return;
  }

  uint32_t now = millis();
  if ((int32_t)(now - dynamicWallpaperPauseUntilMs) < 0) {
    return;
  }

  if (!openDynamicWallpaper(*player)) {
    return;
  }

  uint16_t effectiveInterval = player->intervalMs;
  if (isAudioRunning() && !audioPaused && effectiveInterval < 280) {
    effectiveInterval = 280;
  }

  if (player->lastFrameMs != 0 && (uint32_t)(now - player->lastFrameMs) < effectiveInterval) {
    return;
  }

  uint32_t startMs = millis();
  char reason[64];
  bool ok = renderNextDynamicWallpaperFrame(*player, true, reason, sizeof(reason));
  uint32_t decodeMs = millis() - startMs;
  if (!ok) {
    player->failCount++;
    if (player->failCount >= 3) {
      Serial.printf("[Wallpaper] disabled after repeated failure (%s): %s\n", player->path, reason);
      player->enabled = false;
      closeDynamicWallpaper(*player);
    }
    return;
  }

  player->failCount = 0;
  player->lastFrameMs = millis();

  bool slowFrame = decodeMs >= (uint32_t)(effectiveInterval * 8 / 10) || decodeMs > 110;
  bool fastFrame = decodeMs <= (uint32_t)(effectiveInterval / 3);
  if (slowFrame) {
    if (player->intervalMs < DYNAMIC_WALLPAPER_MIN_INTERVAL_MS) {
      uint16_t next = player->intervalMs + 24;
      player->intervalMs = (next > DYNAMIC_WALLPAPER_MIN_INTERVAL_MS) ? DYNAMIC_WALLPAPER_MIN_INTERVAL_MS : next;
    }
    if (player->slowScore < 255) player->slowScore++;
    player->fastScore = 0;
  } else if (fastFrame) {
    if (player->fastScore < 255) player->fastScore++;
    if (player->fastScore >= 20 && player->intervalMs > player->baseIntervalMs) {
      uint16_t next = (player->intervalMs > 10) ? (player->intervalMs - 10) : player->baseIntervalMs;
      player->intervalMs = (next < player->baseIntervalMs) ? player->baseIntervalMs : next;
      player->fastScore = 0;
    }
    if (player->slowScore > 0) player->slowScore--;
  } else {
    if (player->slowScore > 0) player->slowScore--;
    if (player->fastScore > 0) player->fastScore--;
  }
}

static bool isAudioRunning() {
  if (audioMp3 != nullptr && audioMp3->isRunning()) {
    return true;
  }
  if (audioWav != nullptr && audioWav->isRunning()) {
    return true;
  }
  return false;
}

static bool readFileExact(File &file, uint8_t *buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  return file.read(buf, len) == (int)len;
}

static uint16_t readLe16(const uint8_t *buf) {
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t readLe32(const uint8_t *buf) {
  return (uint32_t)buf[0]
    | ((uint32_t)buf[1] << 8)
    | ((uint32_t)buf[2] << 16)
    | ((uint32_t)buf[3] << 24);
}

static void formatAudioTimeMMSS(uint32_t sec, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  uint32_t m = sec / 60;
  uint32_t s = sec % 60;
  snprintf(out, outSize, "%02u:%02u", (unsigned)m, (unsigned)s);
}

static int mp3BitrateKbpsFromHeader(const uint8_t h[4]) {
  if (h == nullptr) {
    return 0;
  }
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) {
    return 0;
  }

  const int versionBits = (h[1] >> 3) & 0x03;
  const int layerBits = (h[1] >> 1) & 0x03;
  const int bitrateIdx = (h[2] >> 4) & 0x0F;
  const int sampleRateIdx = (h[2] >> 2) & 0x03;
  if (versionBits == 1 || layerBits == 0 || bitrateIdx == 0 || bitrateIdx == 15 || sampleRateIdx == 3) {
    return 0;
  }

  int layer = 0;
  if (layerBits == 3) {
    layer = 1;
  } else if (layerBits == 2) {
    layer = 2;
  } else {
    layer = 3;
  }

  static const int BITRATE_MPEG1_L1[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
  static const int BITRATE_MPEG1_L2[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
  static const int BITRATE_MPEG1_L3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
  static const int BITRATE_MPEG2_L1[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
  static const int BITRATE_MPEG2_L2L3[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};

  const bool isMpeg1 = versionBits == 3;
  if (isMpeg1) {
    if (layer == 1) return BITRATE_MPEG1_L1[bitrateIdx];
    if (layer == 2) return BITRATE_MPEG1_L2[bitrateIdx];
    return BITRATE_MPEG1_L3[bitrateIdx];
  }

  if (layer == 1) return BITRATE_MPEG2_L1[bitrateIdx];
  return BITRATE_MPEG2_L2L3[bitrateIdx];
}

static uint32_t estimateMp3DurationSec(const char *path, uint32_t fileSize, bool *estimated) {
  if (estimated != nullptr) {
    *estimated = true;
  }
  if (path == nullptr || path[0] == '\0' || fileSize == 0) {
    return 0;
  }

  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    return (uint32_t)((fileSize * 8ULL + 64000ULL) / 128000ULL);
  }

  uint32_t payloadSize = fileSize;
  uint8_t id3Head[10];
  if (readFileExact(file, id3Head, sizeof(id3Head))) {
    if (id3Head[0] == 'I' && id3Head[1] == 'D' && id3Head[2] == '3') {
      uint32_t tagSize = ((uint32_t)(id3Head[6] & 0x7F) << 21)
        | ((uint32_t)(id3Head[7] & 0x7F) << 14)
        | ((uint32_t)(id3Head[8] & 0x7F) << 7)
        | (uint32_t)(id3Head[9] & 0x7F);
      uint32_t skip = 10 + tagSize;
      if ((id3Head[5] & 0x10) != 0) {
        skip += 10; // footer
      }
      if (skip < fileSize) {
        payloadSize = fileSize - skip;
      }
      file.seek(skip);
    } else {
      file.seek(0);
    }
  } else {
    file.seek(0);
  }

  int bitrateKbps = 0;
  uint8_t scanBuf[768];
  int carry = 0;
  uint8_t header[4];
  while (bitrateKbps <= 0 && file.available()) {
    int n = file.read(scanBuf + carry, sizeof(scanBuf) - carry);
    if (n <= 0) {
      break;
    }
    n += carry;

    for (int i = 0; i + 3 < n; ++i) {
      header[0] = scanBuf[i];
      header[1] = scanBuf[i + 1];
      header[2] = scanBuf[i + 2];
      header[3] = scanBuf[i + 3];
      bitrateKbps = mp3BitrateKbpsFromHeader(header);
      if (bitrateKbps > 0) {
        break;
      }
    }

    carry = n >= 3 ? 3 : n;
    if (carry > 0) {
      memmove(scanBuf, scanBuf + n - carry, carry);
    }
  }
  file.close();

  if (bitrateKbps <= 0) {
    bitrateKbps = 128;
  }
  uint64_t bits = (uint64_t)payloadSize * 8ULL;
  uint64_t denom = (uint64_t)bitrateKbps * 1000ULL;
  return (uint32_t)((bits + denom / 2ULL) / denom);
}

static uint32_t wavDurationSec(const char *path, bool *estimated) {
  if (estimated != nullptr) {
    *estimated = false;
  }
  if (path == nullptr || path[0] == '\0') {
    return 0;
  }

  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    if (estimated != nullptr) {
      *estimated = true;
    }
    return 0;
  }

  uint8_t riff[12];
  if (!readFileExact(file, riff, sizeof(riff))
      || memcmp(riff, "RIFF", 4) != 0
      || memcmp(riff + 8, "WAVE", 4) != 0) {
    file.close();
    if (estimated != nullptr) {
      *estimated = true;
    }
    return 0;
  }

  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataSize = 0;

  while (file.available()) {
    uint8_t chunkHead[8];
    if (!readFileExact(file, chunkHead, sizeof(chunkHead))) {
      break;
    }
    uint32_t chunkSize = readLe32(chunkHead + 4);
    if (memcmp(chunkHead, "fmt ", 4) == 0) {
      uint8_t fmt[16];
      if (chunkSize >= 16 && readFileExact(file, fmt, sizeof(fmt))) {
        channels = readLe16(fmt + 2);
        sampleRate = readLe32(fmt + 4);
        bitsPerSample = readLe16(fmt + 14);
        if (chunkSize > 16) {
          file.seek(file.position() + (chunkSize - 16));
        }
      } else {
        break;
      }
    } else if (memcmp(chunkHead, "data", 4) == 0) {
      dataSize = chunkSize;
      break;
    } else {
      file.seek(file.position() + chunkSize);
    }

    if ((chunkSize & 1U) != 0U && file.available()) {
      file.seek(file.position() + 1);
    }
  }

  file.close();
  if (channels == 0 || bitsPerSample == 0 || sampleRate == 0 || dataSize == 0) {
    if (estimated != nullptr) {
      *estimated = true;
    }
    return 0;
  }

  uint32_t bytesPerSec = (sampleRate * channels * bitsPerSample) / 8U;
  if (bytesPerSec == 0) {
    if (estimated != nullptr) {
      *estimated = true;
    }
    return 0;
  }
  return dataSize / bytesPerSec;
}

static void ensureAudioTrackDuration(int index) {
  if (index < 0 || index >= sdAudioCount) {
    return;
  }

  SdAudioFile &item = sdAudioFiles[index];
  if (item.durationChecked) {
    return;
  }

  item.durationChecked = true;
  item.durationSec = 0;
  item.durationEstimated = true;

  const bool isWav = strstr(item.path, ".wav") != nullptr || strstr(item.path, ".WAV") != nullptr;
  bool estimated = true;
  uint32_t duration = 0;
  if (isWav) {
    duration = wavDurationSec(item.path, &estimated);
  } else {
    duration = estimateMp3DurationSec(item.path, item.size, &estimated);
  }

  item.durationSec = duration;
  item.durationEstimated = estimated;
}

static uint32_t currentAudioElapsedMs() {
  uint32_t elapsedMs = audioElapsedAccumMs;
  if (isAudioRunning() && !audioPaused && audioPlaybackResumeMs != 0) {
    elapsedMs += (uint32_t)(millis() - audioPlaybackResumeMs);
  }
  return elapsedMs;
}

static void refreshAudioTimeLabel(bool force) {
  if (audioTimeLabel == nullptr) {
    return;
  }

  if (!sdMounted || sdAudioCount <= 0 || sdAudioIndex < 0 || sdAudioIndex >= sdAudioCount) {
    lv_label_set_text(audioTimeLabel, "--:-- / --:--");
    audioShownElapsedSec = 0xFFFFFFFF;
    audioShownDurationSec = 0xFFFFFFFF;
    return;
  }

  ensureAudioTrackDuration(sdAudioIndex);
  const SdAudioFile &item = sdAudioFiles[sdAudioIndex];

  uint32_t elapsedSec = 0;
  if (isAudioRunning()) {
    elapsedSec = currentAudioElapsedMs() / 1000U;
  }
  uint32_t durationSec = item.durationSec;
  if (durationSec > 0 && elapsedSec > durationSec) {
    elapsedSec = durationSec;
  }

  if (!force && elapsedSec == audioShownElapsedSec && durationSec == audioShownDurationSec) {
    return;
  }

  char elapsedText[12];
  char durationText[12];
  char line[40];
  formatAudioTimeMMSS(elapsedSec, elapsedText, sizeof(elapsedText));
  if (durationSec > 0) {
    formatAudioTimeMMSS(durationSec, durationText, sizeof(durationText));
    if (item.durationEstimated) {
      snprintf(line, sizeof(line), "%s / ~%s", elapsedText, durationText);
    } else {
      snprintf(line, sizeof(line), "%s / %s", elapsedText, durationText);
    }
  } else {
    snprintf(line, sizeof(line), "%s / --:--", elapsedText);
  }
  lv_label_set_text(audioTimeLabel, line);
  audioShownElapsedSec = elapsedSec;
  audioShownDurationSec = durationSec;
}

static void setAudioStatus(const char *text, lv_color_t color) {
  if (audioStatusLabel == nullptr) {
    return;
  }
  lv_label_set_text(audioStatusLabel, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(audioStatusLabel, color, LV_PART_MAIN);
}

static void setAudioButtonEnabled(lv_obj_t *btn, bool enabled) {
  if (btn == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  }
}

static void updateAudioControlButtons(bool hasTracks, bool canStepTrack) {
  setAudioButtonEnabled(audioPlayBtn, hasTracks);
  setAudioButtonEnabled(audioPrevBtn, canStepTrack);
  setAudioButtonEnabled(audioNextBtn, canStepTrack);
  if (audioPlayBtnLabel != nullptr) {
    lv_label_set_text(audioPlayBtnLabel, (isAudioRunning() && !audioPaused) ? "Pause" : "Play");
  }
}

static void showCurrentAudioTrack() {
  if (audioTrackLabel == nullptr || audioIndexLabel == nullptr) {
    return;
  }

  if (!sdMounted) {
    lv_label_set_text(audioTrackLabel, "No SD card");
    lv_label_set_text(audioIndexLabel, "0/0");
    refreshAudioTimeLabel(true);
    setAudioStatus("SD not mounted", lv_color_hex(0xEF5350));
    updateAudioControlButtons(false, false);
    return;
  }

  if (sdAudioCount <= 0) {
    lv_label_set_text(audioTrackLabel, "No MP3/WAV files found");
    lv_label_set_text(audioIndexLabel, "0/0");
    refreshAudioTimeLabel(true);
    setAudioStatus("Add music and reopen page", lv_color_hex(0xFFB74D));
    updateAudioControlButtons(false, false);
    return;
  }

  if (sdAudioIndex < 0) {
    sdAudioIndex = 0;
  }
  if (sdAudioIndex >= sdAudioCount) {
    sdAudioIndex = sdAudioCount - 1;
  }

  lv_label_set_text(audioTrackLabel, sdAudioFiles[sdAudioIndex].name);
  lv_label_set_text_fmt(audioIndexLabel, "%d/%d", sdAudioIndex + 1, sdAudioCount);
  refreshAudioTimeLabel(true);
  updateAudioControlButtons(true, sdAudioCount > 1);
  if (isAudioRunning()) {
    if (audioPaused) {
      setAudioStatus("Paused", lv_color_hex(0xFFB74D));
    } else {
      setAudioStatus("Playing from SD", lv_color_hex(0x81C784));
    }
  } else {
    setAudioStatus("Ready", lv_color_hex(0x90CAF9));
  }
}

static void addAudioCandidate(const char *path, uint32_t size) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  if (sdAudioCount >= (int)(sizeof(sdAudioFiles) / sizeof(sdAudioFiles[0]))) {
    return;
  }

  copyText(sdAudioFiles[sdAudioCount].path, sizeof(sdAudioFiles[sdAudioCount].path), path);
  copyText(sdAudioFiles[sdAudioCount].name, sizeof(sdAudioFiles[sdAudioCount].name), baseNameFromPath(path));
  sdAudioFiles[sdAudioCount].size = size;
  sdAudioFiles[sdAudioCount].durationSec = 0;
  sdAudioFiles[sdAudioCount].durationChecked = false;
  sdAudioFiles[sdAudioCount].durationEstimated = true;
  sdAudioCount++;
}

static void scanAudioDirectoryRecursive(const char *dirPath, int depth) {
  if (dirPath == nullptr || depth > 4) {
    return;
  }
  if (sdAudioCount >= (int)(sizeof(sdAudioFiles) / sizeof(sdAudioFiles[0]))) {
    return;
  }

  File dir = SD_MMC.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  while (sdAudioCount < (int)(sizeof(sdAudioFiles) / sizeof(sdAudioFiles[0]))) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    const char *entryPath = entry.path();
    if (entryPath != nullptr && entryPath[0] != '\0') {
      char childPath[192];
      if (entryPath[0] == '/') {
        copyText(childPath, sizeof(childPath), entryPath);
      } else if (strcmp(dirPath, "/") == 0) {
        snprintf(childPath, sizeof(childPath), "/%s", entryPath);
      } else {
        snprintf(childPath, sizeof(childPath), "%s/%s", dirPath, entryPath);
      }

      if (entry.isDirectory()) {
        if (depth < 4) {
          scanAudioDirectoryRecursive(childPath, depth + 1);
        }
      } else if (hasAudioExtension(childPath)) {
        addAudioCandidate(childPath, (uint32_t)entry.size());
      }
    }
    entry.close();
  }
  dir.close();
}

static void stopAudioPlayback(bool keepStatus) {
  if (audioMp3 != nullptr) {
    if (audioMp3->isRunning()) {
      audioMp3->stop();
    }
    delete audioMp3;
    audioMp3 = nullptr;
  }
  if (audioWav != nullptr) {
    if (audioWav->isRunning()) {
      audioWav->stop();
    }
    delete audioWav;
    audioWav = nullptr;
  }
  if (audioFileSource != nullptr) {
    delete audioFileSource;
    audioFileSource = nullptr;
  }
  if (audioBufferedSource != nullptr) {
    delete audioBufferedSource;
    audioBufferedSource = nullptr;
  }

  if (audioOutputReady) {
    digitalWrite(AUDIO_MUTE_PIN, LOW);
  }
  audioPaused = false;
  audioElapsedAccumMs = 0;
  audioPlaybackResumeMs = 0;
  audioLastTimeLabelRefreshMs = 0;
  audioShownElapsedSec = 0xFFFFFFFF;
  audioShownDurationSec = 0xFFFFFFFF;

  if (!keepStatus) {
    setAudioStatus("Stopped", lv_color_hex(0xFFB74D));
  }
  updateAudioControlButtons(sdAudioCount > 0, sdAudioCount > 1);
  refreshAudioTimeLabel(true);
}

static bool ensureAudioOutputReady() {
  if (audioOutputReady && audioOutput != nullptr) {
    return true;
  }

  if (audioOutput != nullptr) {
    delete audioOutput;
    audioOutput = nullptr;
  }

  audioOutput = new AudioOutputI2S();
  if (audioOutput == nullptr) {
    setAudioStatus("Audio output init failed", lv_color_hex(0xEF5350));
    return false;
  }

  audioOutput->SetPinout(AUDIO_I2S_BCK_IO, AUDIO_I2S_WS_IO, AUDIO_I2S_DO_IO);
  audioOutput->SetGain(0.18f);
  pinMode(AUDIO_MUTE_PIN, OUTPUT);
  digitalWrite(AUDIO_MUTE_PIN, LOW);
  audioOutputReady = true;
  return true;
}

static bool startAudioPlayback(int index) {
  if (!sdMounted) {
    setAudioStatus("SD not mounted", lv_color_hex(0xEF5350));
    return false;
  }
  if (sdAudioCount <= 0) {
    setAudioStatus("No playable audio", lv_color_hex(0xFFB74D));
    return false;
  }
  if (index < 0 || index >= sdAudioCount) {
    return false;
  }
  if (!ensureAudioOutputReady()) {
    return false;
  }

  stopAudioPlayback(true);

  audioFileSource = new AudioFileSourceFS(SD_MMC, sdAudioFiles[index].path);
  if (audioFileSource == nullptr) {
    setAudioStatus("Open audio file failed", lv_color_hex(0xEF5350));
    return false;
  }

  audioBufferedSource = new AudioFileSourceBuffer(audioFileSource, 4096);
  AudioFileSource *decodeSource = audioBufferedSource != nullptr
    ? static_cast<AudioFileSource *>(audioBufferedSource)
    : static_cast<AudioFileSource *>(audioFileSource);

  const char *path = sdAudioFiles[index].path;
  bool ok = false;
  if (strstr(path, ".wav") != nullptr || strstr(path, ".WAV") != nullptr) {
    audioWav = new AudioGeneratorWAV();
    if (audioWav != nullptr) {
      ok = audioWav->begin(decodeSource, audioOutput);
    }
  } else {
    audioMp3 = new AudioGeneratorMP3();
    if (audioMp3 != nullptr) {
      ok = audioMp3->begin(decodeSource, audioOutput);
    }
  }

  if (!ok) {
    stopAudioPlayback(true);
    setAudioStatus("Decoder start failed", lv_color_hex(0xEF5350));
    return false;
  }

  sdAudioIndex = index;
  ensureAudioTrackDuration(sdAudioIndex);
  audioPaused = false;
  audioElapsedAccumMs = 0;
  audioPlaybackResumeMs = millis();
  audioLastTimeLabelRefreshMs = 0;
  audioShownElapsedSec = 0xFFFFFFFF;
  audioShownDurationSec = 0xFFFFFFFF;
  digitalWrite(AUDIO_MUTE_PIN, HIGH);
  if (audioTrackLabel != nullptr) {
    lv_label_set_text(audioTrackLabel, sdAudioFiles[sdAudioIndex].name);
  }
  if (audioIndexLabel != nullptr) {
    lv_label_set_text_fmt(audioIndexLabel, "%d/%d", sdAudioIndex + 1, sdAudioCount);
  }
  updateAudioControlButtons(true, sdAudioCount > 1);
  refreshAudioTimeLabel(true);
  setAudioStatus("Playing from SD", lv_color_hex(0x81C784));
  pushInboxMessage("event", "Audio playback", sdAudioFiles[sdAudioIndex].name);
  return true;
}

static void processAudioPlayback() {
  bool running = false;
  bool loopOk = true;

  if (audioMp3 != nullptr && audioMp3->isRunning()) {
    running = true;
    loopOk = audioMp3->loop();
  } else if (audioWav != nullptr && audioWav->isRunning()) {
    running = true;
    loopOk = audioWav->loop();
  }

  if (!running) {
    refreshAudioTimeLabel(false);
    return;
  }

  if (audioPaused) {
    refreshAudioTimeLabel(false);
    return;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - audioLastTimeLabelRefreshMs) >= 250) {
    audioLastTimeLabelRefreshMs = now;
    refreshAudioTimeLabel(false);
  }

  if (loopOk) {
    return;
  }

  stopAudioPlayback(true);
  if (sdAudioCount > 1) {
    int next = (sdAudioIndex + 1) % sdAudioCount;
    startAudioPlayback(next);
  } else {
    updateAudioControlButtons(true, false);
    setAudioStatus("Playback completed", lv_color_hex(0x90CAF9));
  }
}

static void loadSdAudioList() {
  stopAudioPlayback(true);
  sdAudioCount = 0;
  sdAudioIndex = 0;

  if (!sdMounted) {
    showCurrentAudioTrack();
    return;
  }

  scanAudioDirectoryRecursive("/", 0);
  Serial.printf("[Audio] scanned %d audio files (mp3/wav)\n", sdAudioCount);
  showCurrentAudioTrack();
}

static void addSdBrowserFile(const char *path, uint32_t size) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  if (sdBrowserFileCount >= (int)(sizeof(sdBrowserFiles) / sizeof(sdBrowserFiles[0]))) {
    return;
  }

  SdBrowserFile &target = sdBrowserFiles[sdBrowserFileCount];
  copyText(target.path, sizeof(target.path), path);
  copyText(target.name, sizeof(target.name), baseNameFromPath(path));
  copyText(target.type, sizeof(target.type), classifySdFileType(path));
  target.size = size;
  sdBrowserFileCount++;
}

static void scanSdBrowserFilesRecursive(const char *dirPath, int depth) {
  if (dirPath == nullptr || depth > 5) {
    return;
  }
  if (sdBrowserFileCount >= (int)(sizeof(sdBrowserFiles) / sizeof(sdBrowserFiles[0]))) {
    return;
  }

  File dir = SD_MMC.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }

  while (sdBrowserFileCount < (int)(sizeof(sdBrowserFiles) / sizeof(sdBrowserFiles[0]))) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    const char *entryPath = entry.path();
    if (entryPath != nullptr && entryPath[0] != '\0') {
      char childPath[192];
      if (entryPath[0] == '/') {
        copyText(childPath, sizeof(childPath), entryPath);
      } else if (strcmp(dirPath, "/") == 0) {
        snprintf(childPath, sizeof(childPath), "/%s", entryPath);
      } else {
        snprintf(childPath, sizeof(childPath), "%s/%s", dirPath, entryPath);
      }

      if (entry.isDirectory()) {
        scanSdBrowserFilesRecursive(childPath, depth + 1);
      } else {
        addSdBrowserFile(childPath, (uint32_t)entry.size());
      }
    }

    entry.close();
  }

  dir.close();
}

static void resetSdUploadSession(bool removeTempFile) {
  if (sdUploadSession.file) {
    sdUploadSession.file.close();
  }
  if (removeTempFile && sdUploadSession.tempPath[0] != '\0' && SD_MMC.exists(sdUploadSession.tempPath)) {
    SD_MMC.remove(sdUploadSession.tempPath);
  }

  sdUploadSession.active = false;
  sdUploadSession.waitingBinary = false;
  sdUploadSession.overwrite = false;
  sdUploadSession.uploadId[0] = '\0';
  sdUploadSession.targetPath[0] = '\0';
  sdUploadSession.tempPath[0] = '\0';
  sdUploadSession.expectedSize = 0;
  sdUploadSession.receivedSize = 0;
  sdUploadSession.expectedSeq = 0;
  sdUploadSession.pendingSeq = -1;
  sdUploadSession.pendingLen = 0;
}

static bool ensureSdParentDirectories(const char *targetPath, char *reason, size_t reasonSize) {
  if (targetPath == nullptr || targetPath[0] != '/') {
    copyText(reason, reasonSize, "invalid path");
    return false;
  }

  char pathCopy[192];
  copyText(pathCopy, sizeof(pathCopy), targetPath);
  char *lastSlash = strrchr(pathCopy, '/');
  if (lastSlash == nullptr) {
    copyText(reason, reasonSize, "invalid path");
    return false;
  }
  if (lastSlash == pathCopy) {
    return true; // file directly under root
  }
  *lastSlash = '\0';

  char current[192] = "";
  const char *segmentStart = pathCopy + 1; // skip leading slash

  while (segmentStart != nullptr && segmentStart[0] != '\0') {
    const char *slash = strchr(segmentStart, '/');
    size_t segLen = (slash == nullptr) ? strlen(segmentStart) : (size_t)(slash - segmentStart);
    if (segLen == 0) {
      if (slash == nullptr) break;
      segmentStart = slash + 1;
      continue;
    }
    if (segLen >= 64) {
      copyText(reason, reasonSize, "dir segment too long");
      return false;
    }

    char segment[64];
    memcpy(segment, segmentStart, segLen);
    segment[segLen] = '\0';

    if (current[0] == '\0') {
      snprintf(current, sizeof(current), "/%s", segment);
    } else {
      size_t currLen = strlen(current);
      if (currLen + 1 + segLen >= sizeof(current)) {
        copyText(reason, reasonSize, "dir path too long");
        return false;
      }
      snprintf(current + currLen, sizeof(current) - currLen, "/%s", segment);
    }

    if (!SD_MMC.exists(current) && !SD_MMC.mkdir(current)) {
      char detail[96];
      snprintf(detail, sizeof(detail), "mkdir failed: %s", current);
      copyText(reason, reasonSize, detail);
      return false;
    }

    if (slash == nullptr) {
      break;
    }
    segmentStart = slash + 1;
  }

  return true;
}

static void sendSdUploadBeginAck(const char *uploadId, bool success, const char *reason) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<384> doc;
  doc["type"] = "sd_upload_begin_ack";
  JsonObject data = doc.createNestedObject("data");
  data["uploadId"] = uploadId == nullptr ? "" : uploadId;
  data["deviceId"] = DEVICE_ID;
  data["success"] = success;
  data["received"] = sdUploadSession.receivedSize;
  data["timestamp"] = millis();
  if (reason != nullptr && reason[0] != '\0') {
    data["reason"] = reason;
  }

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendSdUploadChunkAck(const char *uploadId, int seq, bool success, const char *reason) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<448> doc;
  doc["type"] = "sd_upload_chunk_ack";
  JsonObject data = doc.createNestedObject("data");
  data["uploadId"] = uploadId == nullptr ? "" : uploadId;
  data["deviceId"] = DEVICE_ID;
  data["seq"] = seq;
  data["success"] = success;
  data["received"] = sdUploadSession.receivedSize;
  data["timestamp"] = millis();
  if (reason != nullptr && reason[0] != '\0') {
    data["reason"] = reason;
  }

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendSdUploadCommitAck(const char *uploadId, bool success, const char *finalPath, const char *reason) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<448> doc;
  doc["type"] = "sd_upload_commit_ack";
  JsonObject data = doc.createNestedObject("data");
  data["uploadId"] = uploadId == nullptr ? "" : uploadId;
  data["deviceId"] = DEVICE_ID;
  data["success"] = success;
  data["received"] = sdUploadSession.receivedSize;
  data["timestamp"] = millis();
  if (finalPath != nullptr && finalPath[0] != '\0') {
    data["path"] = finalPath;
  }
  if (reason != nullptr && reason[0] != '\0') {
    data["reason"] = reason;
  }

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendSdListResponse(const char *requestId, int offset, int limit) {
  if (!isConnected) {
    return;
  }

  detectAndScanSdCard();
  sdBrowserFileCount = 0;
  if (sdMounted) {
    scanSdBrowserFilesRecursive("/", 0);
  }

  int imageCount = 0;
  int audioCount = 0;
  int videoCount = 0;
  int otherCount = 0;
  for (int i = 0; i < sdBrowserFileCount; ++i) {
    const char *type = sdBrowserFiles[i].type;
    if (strcmp(type, "image") == 0) {
      imageCount++;
    } else if (strcmp(type, "audio") == 0) {
      audioCount++;
    } else if (strcmp(type, "video") == 0) {
      videoCount++;
    } else {
      otherCount++;
    }
  }

  int pageLimit = limit;
  if (pageLimit <= 0) {
    pageLimit = SD_BROWSER_RESPONSE_MAX_FILES;
  }
  if (pageLimit > SD_BROWSER_RESPONSE_MAX_FILES) {
    pageLimit = SD_BROWSER_RESPONSE_MAX_FILES;
  }

  int pageOffset = offset;
  if (pageOffset < 0) {
    pageOffset = 0;
  }
  if (pageOffset > sdBrowserFileCount) {
    pageOffset = sdBrowserFileCount;
  }

  int responseFileCount = sdBrowserFileCount - pageOffset;
  if (responseFileCount < 0) {
    responseFileCount = 0;
  }
  if (responseFileCount > pageLimit) {
    responseFileCount = pageLimit;
  }
  const bool truncated = (pageOffset + responseFileCount) < sdBrowserFileCount;

  sdListResponseDoc.clear();
  sdListResponseDoc["type"] = "sd_list_response";
  JsonObject data = sdListResponseDoc.createNestedObject("data");
  data["requestId"] = requestId == nullptr ? "" : requestId;
  data["deviceId"] = DEVICE_ID;
  data["sdMounted"] = sdMounted;
  data["root"] = "/";
  data["offset"] = pageOffset;
  data["limit"] = pageLimit;
  data["total"] = sdBrowserFileCount;
  data["returned"] = responseFileCount;
  data["truncated"] = truncated;
  data["imageCount"] = imageCount;
  data["audioCount"] = audioCount;
  data["videoCount"] = videoCount;
  data["otherCount"] = otherCount;
  data["timestamp"] = millis();
  if (!sdMounted) {
    data["reason"] = sdMountReason;
  }

  JsonArray files = data.createNestedArray("files");
  for (int i = 0; i < responseFileCount; ++i) {
    const int fileIndex = pageOffset + i;
    JsonObject item = files.createNestedObject();
    item["name"] = sdBrowserFiles[fileIndex].name;
    item["path"] = sdBrowserFiles[fileIndex].path;
    item["type"] = sdBrowserFiles[fileIndex].type;
    item["size"] = sdBrowserFiles[fileIndex].size;
  }

  String output;
  output.reserve(7000);
  const size_t bytes = serializeJson(sdListResponseDoc, output);
  Serial.printf(
    "[SD] list response: total=%d returned=%d bytes=%u\n",
    sdBrowserFileCount,
    responseFileCount,
    (unsigned)bytes
  );
  webSocket.sendTXT(output);
}

static void sendSdDeleteResponse(const char *requestId, const char *targetPath, bool success, const char *reason) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<768> doc;
  doc["type"] = "sd_delete_response";
  JsonObject data = doc.createNestedObject("data");
  data["requestId"] = requestId == nullptr ? "" : requestId;
  data["deviceId"] = DEVICE_ID;
  data["path"] = targetPath == nullptr ? "" : targetPath;
  data["success"] = success;
  data["reason"] = reason == nullptr ? "" : reason;
  data["timestamp"] = millis();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendSdPreviewResponse(const char *requestId, const char *targetPath, bool success, uint32_t len, const char *reason) {
  if (!isConnected) {
    return;
  }

  StaticJsonDocument<384> doc;
  doc["type"] = "sd_preview_response";
  JsonObject data = doc.createNestedObject("data");
  data["requestId"] = requestId == nullptr ? "" : requestId;
  data["deviceId"] = DEVICE_ID;
  data["path"] = targetPath == nullptr ? "" : targetPath;
  data["success"] = success;
  data["len"] = len;
  data["mime"] = "image/jpeg";
  data["timestamp"] = millis();
  if (reason != nullptr && reason[0] != '\0') {
    data["reason"] = reason;
  }

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void processPendingAudioControl() {
  int action = pendingAudioControlAction;
  if (action == AUDIO_CONTROL_NONE) {
    return;
  }
  pendingAudioControlAction = AUDIO_CONTROL_NONE;

  if (action == AUDIO_CONTROL_RESCAN) {
    setAudioStatus("Rescanning SD...", lv_color_hex(0x90CAF9));
    detectAndScanSdCard();
    loadSdAudioList();
    return;
  }

  if (sdAudioCount <= 0) {
    showCurrentAudioTrack();
    return;
  }

  if (action == AUDIO_CONTROL_PREV) {
    int prev = (sdAudioIndex - 1 + sdAudioCount) % sdAudioCount;
    if (isAudioRunning() && !audioPaused) {
      startAudioPlayback(prev);
    } else {
      if (isAudioRunning()) {
        stopAudioPlayback(true);
      }
      sdAudioIndex = prev;
      showCurrentAudioTrack();
    }
    return;
  }

  if (action == AUDIO_CONTROL_NEXT) {
    int next = (sdAudioIndex + 1) % sdAudioCount;
    if (isAudioRunning() && !audioPaused) {
      startAudioPlayback(next);
    } else {
      if (isAudioRunning()) {
        stopAudioPlayback(true);
      }
      sdAudioIndex = next;
      showCurrentAudioTrack();
    }
    return;
  }

  if (isAudioRunning()) {
    uint32_t now = millis();
    audioPaused = !audioPaused;
    if (audioPaused) {
      if (audioPlaybackResumeMs != 0) {
        audioElapsedAccumMs += (uint32_t)(now - audioPlaybackResumeMs);
        audioPlaybackResumeMs = 0;
      }
    } else {
      audioPlaybackResumeMs = now;
    }
    if (audioOutputReady) {
      digitalWrite(AUDIO_MUTE_PIN, audioPaused ? LOW : HIGH);
    }
    updateAudioControlButtons(true, sdAudioCount > 1);
    refreshAudioTimeLabel(true);
    showCurrentAudioTrack();
  } else {
    startAudioPlayback(sdAudioIndex);
  }
}

static void audioControlCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - audioLastControlMs) < AUDIO_CONTROL_COOLDOWN_MS) {
    return;
  }
  audioLastControlMs = now;

  intptr_t action = (intptr_t)lv_event_get_user_data(e);
  if (action < AUDIO_CONTROL_PREV || action > AUDIO_CONTROL_RESCAN) {
    return;
  }
  pendingAudioControlAction = (int)action;
}

static void voiceCommandButtonCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }

  intptr_t idxRaw = (intptr_t)lv_event_get_user_data(e);
  if (idxRaw < 0 || idxRaw >= (intptr_t)(sizeof(VOICE_PRESET_COMMANDS) / sizeof(VOICE_PRESET_COMMANDS[0]))) {
    return;
  }

  const VoicePresetCommand &preset = VOICE_PRESET_COMMANDS[idxRaw];
  sendVoiceCommand(preset.text);

  if (voiceResultLabel != nullptr) {
    lv_label_set_text_fmt(voiceResultLabel, "Command: %s", preset.text);
  }
  pushInboxMessage("event", "Voice command", preset.text);
}

static bool ensureVoiceMicReady(char *reason, size_t reasonSize) {
  if (voiceMicInitialized) {
    return true;
  }

  i2s_config_t micConfig = {};
  micConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  micConfig.sample_rate = VOICE_SAMPLE_RATE;
  micConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  micConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  micConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  micConfig.intr_alloc_flags = 0;
  micConfig.dma_buf_count = 6;
  micConfig.dma_buf_len = 256;
  micConfig.use_apll = false;
  micConfig.tx_desc_auto_clear = false;
  micConfig.fixed_mclk = 0;
#if defined(I2S_MCLK_MULTIPLE_256)
  micConfig.mclk_multiple = I2S_MCLK_MULTIPLE_256;
#endif
#if defined(I2S_BITS_PER_CHAN_32BIT)
  micConfig.bits_per_chan = I2S_BITS_PER_CHAN_32BIT;
#endif

  esp_err_t err = i2s_driver_install(VOICE_I2S_PORT, &micConfig, 0, nullptr);
  if (err != ESP_OK) {
    if (reason != nullptr && reasonSize > 0) {
      snprintf(reason, reasonSize, "mic install err=0x%x", (unsigned int)err);
    }
    return false;
  }

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = MIC_I2S_SCK;
  pinConfig.ws_io_num = MIC_I2S_WS;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
  pinConfig.data_in_num = MIC_I2S_SD;

  err = i2s_set_pin(VOICE_I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    i2s_driver_uninstall(VOICE_I2S_PORT);
    if (reason != nullptr && reasonSize > 0) {
      snprintf(reason, reasonSize, "mic pin err=0x%x", (unsigned int)err);
    }
    return false;
  }

  i2s_zero_dma_buffer(VOICE_I2S_PORT);
  i2s_start(VOICE_I2S_PORT);
  voiceMicInitialized = true;
  return true;
}

static void releaseVoiceMic() {
  if (!voiceMicInitialized) {
    return;
  }
  i2s_stop(VOICE_I2S_PORT);
  i2s_driver_uninstall(VOICE_I2S_PORT);
  voiceMicInitialized = false;
}

static void sendVoiceStreamStart() {
  if (!isConnected || voiceActiveStreamId[0] == '\0') {
    return;
  }

  StaticJsonDocument<320> doc;
  doc["type"] = "voice_stream_start";
  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["streamId"] = voiceActiveStreamId;
  data["sampleRate"] = VOICE_SAMPLE_RATE;
  data["channels"] = 1;
  data["format"] = "pcm_s16le";
  data["chunkSamples"] = (int)VOICE_SAMPLES_PER_CHUNK;
  data["source"] = "esp32_mic";
  data["timestamp"] = millis();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendVoiceStreamStop(const char *reason) {
  if (!isConnected || voiceActiveStreamId[0] == '\0') {
    return;
  }

  StaticJsonDocument<320> doc;
  doc["type"] = "voice_stream_stop";
  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["streamId"] = voiceActiveStreamId;
  data["reason"] = (reason == nullptr) ? "manual" : reason;
  data["chunksSent"] = voiceChunksSent;
  data["bytesSent"] = voiceBytesSent;
  data["timestamp"] = millis();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void sendVoiceStreamChunkMeta(size_t byteLen, uint8_t levelPercent) {
  if (!isConnected || !voiceMicStreaming || voiceActiveStreamId[0] == '\0') {
    return;
  }

  StaticJsonDocument<256> doc;
  doc["type"] = "voice_stream_chunk_meta";
  JsonObject data = doc.createNestedObject("data");
  data["streamId"] = voiceActiveStreamId;
  data["seq"] = voiceChunkSeq;
  data["len"] = (int)byteLen;
  data["level"] = levelPercent;
  data["timestamp"] = millis();

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void setVoiceMicStreaming(bool enabled, const char *reason, bool notifyServer) {
  if (enabled == voiceMicStreaming) {
    return;
  }

  if (enabled) {
    if (!isConnected) {
      if (voiceStatusLabel != nullptr) {
        lv_label_set_text(voiceStatusLabel, "WS disconnected");
        lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
      }
      return;
    }

    if (isAudioRunning()) {
      stopAudioPlayback(true);
      setAudioStatus("Audio paused by mic", lv_color_hex(0xFFB74D));
    }

    char micReason[64] = "";
    if (!ensureVoiceMicReady(micReason, sizeof(micReason))) {
      if (voiceStatusLabel != nullptr) {
        lv_label_set_text_fmt(voiceStatusLabel, "Mic init failed: %s", micReason);
        lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
      }
      return;
    }

    uint32_t randomPart = (uint32_t)esp_random();
    snprintf(voiceActiveStreamId, sizeof(voiceActiveStreamId), "vs-%lu-%08lx", millis(), (unsigned long)randomPart);
    voiceChunkSeq = 0;
    voiceChunksSent = 0;
    voiceBytesSent = 0;
    voiceLastChunkMs = millis();
    voiceLastStartSentMs = voiceLastChunkMs;
    voiceLastLevelPercent = 0;
    voiceStreamStartAcked = false;
    voiceMicStreaming = true;
    i2s_zero_dma_buffer(VOICE_I2S_PORT);
    sendVoiceStreamStart();

    if (voiceStatusLabel != nullptr) {
      lv_label_set_text(voiceStatusLabel, "Mic streaming...");
      lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x81C784), LV_PART_MAIN);
    }
    if (voiceResultLabel != nullptr) {
      lv_label_set_text(voiceResultLabel, "Level: 0% | 0 chunks");
    }
    if (voiceMicToggleLabel != nullptr) {
      lv_label_set_text(voiceMicToggleLabel, "Stop Mic");
    }
    pushInboxMessage("event", "Voice stream", "Microphone streaming started");
    return;
  }

  bool wasStreaming = voiceMicStreaming;
  voiceMicStreaming = false;
  voiceStreamStartAcked = false;
  if (notifyServer && wasStreaming) {
    sendVoiceStreamStop((reason == nullptr) ? "manual" : reason);
  }
  voiceActiveStreamId[0] = '\0';
  releaseVoiceMic();

  if (voiceMicToggleLabel != nullptr) {
    lv_label_set_text(voiceMicToggleLabel, "Start Mic");
  }
  if (voiceStatusLabel != nullptr) {
    const char *text = (reason == nullptr || reason[0] == '\0') ? "Mic stopped" : reason;
    lv_label_set_text(voiceStatusLabel, text);
    lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xB0BEC5), LV_PART_MAIN);
  }
}

static void processVoiceMicStreaming() {
  if (!voiceMicStreaming || !voiceMicInitialized || !isConnected) {
    return;
  }

  if (!voiceStreamStartAcked) {
    uint32_t now = millis();
    if ((uint32_t)(now - voiceLastStartSentMs) >= 1200) {
      sendVoiceStreamStart();
      voiceLastStartSentMs = now;
    }
    return;
  }

  size_t bytesRead = 0;
  esp_err_t err = i2s_read(VOICE_I2S_PORT, voiceRawChunk, sizeof(voiceRawChunk), &bytesRead, 0);
  if (err != ESP_OK || bytesRead == 0) {
    return;
  }

  size_t framesRead = bytesRead / (sizeof(int32_t) * 2);
  if (framesRead == 0) {
    return;
  }
  if (framesRead > VOICE_SAMPLES_PER_CHUNK) {
    framesRead = VOICE_SAMPLES_PER_CHUNK;
  }

  uint32_t peakAbsRaw = 0;
  for (size_t i = 0; i < framesRead; ++i) {
    int32_t left = voiceRawChunk[i * 2];
    int32_t right = voiceRawChunk[i * 2 + 1];

    uint32_t leftAbs = (uint32_t)((left >= 0) ? left : -left);
    uint32_t rightAbs = (uint32_t)((right >= 0) ? right : -right);
    int32_t chosen = (leftAbs >= rightAbs) ? left : right;
    voiceMonoRaw[i] = chosen;

    uint32_t chosenAbs = (leftAbs >= rightAbs) ? leftAbs : rightAbs;
    if (chosenAbs > peakAbsRaw) {
      peakAbsRaw = chosenAbs;
    }
  }

  uint8_t shift = 0;
  while (shift < 24 && (peakAbsRaw >> shift) > 24000UL) {
    shift++;
  }

  uint64_t absSum = 0;
  for (size_t i = 0; i < framesRead; ++i) {
    int32_t scaled32 = (shift > 0) ? (voiceMonoRaw[i] >> shift) : voiceMonoRaw[i];
    if (scaled32 > 32767) {
      scaled32 = 32767;
    } else if (scaled32 < -32768) {
      scaled32 = -32768;
    }

    int16_t s16 = (int16_t)scaled32;
    voicePcmChunk[i] = s16;
    int32_t absVal = (s16 >= 0) ? s16 : -s16;
    absSum += (uint32_t)absVal;
  }

  uint32_t avgAbs = (framesRead > 0) ? (uint32_t)(absSum / framesRead) : 0;
  uint32_t scaled = (avgAbs * 100UL) / 4500UL;
  if (scaled > 100UL) {
    scaled = 100UL;
  }
  uint8_t levelPercent = (uint8_t)scaled;
  voiceLastLevelPercent = levelPercent;

  size_t pcmBytes = framesRead * sizeof(int16_t);
  sendVoiceStreamChunkMeta(pcmBytes, levelPercent);
  webSocket.sendBIN((uint8_t *)voicePcmChunk, pcmBytes);

  voiceChunkSeq++;
  voiceChunksSent++;
  voiceBytesSent += (uint32_t)pcmBytes;
  voiceLastChunkMs = millis();

  if (voiceResultLabel != nullptr && (voiceChunksSent % 6) == 0) {
    lv_label_set_text_fmt(
      voiceResultLabel,
      "Level: %u%% | %lu chunks | %lu KB",
      (unsigned int)voiceLastLevelPercent,
      (unsigned long)voiceChunksSent,
      (unsigned long)(voiceBytesSent / 1024)
    );
  }
}

static void voiceMicToggleCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }
  setVoiceMicStreaming(!voiceMicStreaming, "manual", true);
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

  if (diagSdLabel != nullptr) {
    if (!sdInitAttempted) {
      lv_label_set_text(diagSdLabel, "SD: checking...");
    } else if (!sdMounted) {
      lv_label_set_text_fmt(diagSdLabel, "SD: %s", sdMountReason);
    } else {
      char totalText[16];
      char usedText[16];
      formatStorageSize(sdTotalBytes, totalText, sizeof(totalText));
      formatStorageSize(sdUsedBytes, usedText, sizeof(usedText));
      lv_label_set_text_fmt(
        diagSdLabel,
        "SD: %s %s %s/%s D%lu F%lu",
        sdMode1Bit ? "1-bit" : "4-bit",
        sdCardTypeToText(sdCardType),
        usedText,
        totalText,
        (unsigned long)sdRootDirCount,
        (unsigned long)sdRootFileCount
      );
    }
  }

  if (diagSdRootLabel != nullptr) {
    if (!sdMounted) {
      lv_label_set_text(diagSdRootLabel, "Root: --");
    } else {
      lv_label_set_text_fmt(diagSdRootLabel, "Root: %s", sdRootPreview);
    }
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
  if (shouldSuppressClick()) {
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
  lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(btn);
  lv_obj_add_event_cb(btn, settingsActionEventCallback, LV_EVENT_CLICKED, (void *)((intptr_t)action));

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

static bool shouldSuppressClick() {
  return (int32_t)(millis() - suppressClickUntilMs) < 0;
}

static void suppressClicksForMs(uint32_t durationMs) {
  suppressClickUntilMs = millis() + durationMs;
}

static bool getActiveTouchPoint(lv_point_t *point) {
  if (point == nullptr) {
    return false;
  }
  lv_indev_t *indev = lv_indev_get_act();
  if (indev == nullptr) {
    return false;
  }
  lv_indev_get_point(indev, point);
  return true;
}

static void suppressClicksAfterHome() {
  suppressClicksForMs(CLICK_SUPPRESS_MS_AFTER_HOME);
}

static void updatePageIndicator() {
  if (pageIndicatorLabel != nullptr) {
    lv_obj_add_flag(pageIndicatorLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

static void showPage(int pageIndex) {
  if (pageIndex < 0) {
    pageIndex += UI_PAGE_COUNT;
  }
  if (pageIndex >= UI_PAGE_COUNT) {
    pageIndex %= UI_PAGE_COUNT;
  }

  int previousPage = currentPage;
  if (previousPage == UI_PAGE_VOICE && pageIndex != UI_PAGE_VOICE && voiceMicStreaming) {
    setVoiceMicStreaming(false, "Mic stopped (leave page)", true);
  }
  if (previousPage == UI_PAGE_VIDEO_PLAYER && pageIndex != UI_PAGE_VIDEO_PLAYER && videoPlaying) {
    stopVideoPlayback(true);
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
  if (currentPage == UI_PAGE_HOME) {
    refreshHomeShortcutSlots();
  }
  if (currentPage == UI_PAGE_PHOTO_FRAME) {
    if (sdMounted && sdPhotoCount <= 0) {
      loadSdPhotoList();
    }
    showCurrentPhotoFrame();
    lastPhotoAutoAdvanceMs = millis();
    requestPhotoFrameSettings(true);
  }
  if (currentPage == UI_PAGE_AUDIO_PLAYER) {
    if (sdMounted && sdAudioCount <= 0) {
      loadSdAudioList();
    }
    showCurrentAudioTrack();
  }
  if (currentPage == UI_PAGE_VIDEO_PLAYER) {
    if (sdMounted && sdVideoCount <= 0) {
      loadSdVideoList();
    }
    showCurrentVideoTrack();
  }
  prepareDynamicWallpaperForPage(currentPage, true);
  updatePageIndicator();
}

static int wrapHomeShortcutIndex(int index) {
  if (HOME_SHORTCUT_COUNT <= 0) {
    return -1;
  }

  int wrapped = index % (int)HOME_SHORTCUT_COUNT;
  if (wrapped < 0) {
    wrapped += HOME_SHORTCUT_COUNT;
  }
  return wrapped;
}

static void renderHomeShortcutIcon(uint8_t slotIdx, const HomeShortcutConfig &item) {
  if (slotIdx >= HOME_VISIBLE_SLOT_COUNT) {
    return;
  }

  lv_obj_t *root = homeShortcutIcons[slotIdx];
  if (root == nullptr) {
    return;
  }

  lv_obj_clean(root);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);

  const lv_color_t fg = lv_color_hex(0xF5EEFF);

  if (item.iconKind == HOME_ICON_SYMBOL) {
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, item.icon);
    lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_center(label);
    return;
  }

  if (item.iconKind == HOME_ICON_MONITOR) {
    lv_obj_t *screen = lv_obj_create(root);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 24, 16);
    lv_obj_align(screen, LV_ALIGN_CENTER, 0, -4);
    lv_obj_set_style_radius(screen, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen, fg, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(screen, fg, LV_PART_MAIN);

    lv_obj_t *stem = lv_obj_create(root);
    lv_obj_remove_style_all(stem);
    lv_obj_set_size(stem, 4, 5);
    lv_obj_align(stem, LV_ALIGN_CENTER, 0, 7);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(stem, fg, LV_PART_MAIN);
    lv_obj_set_style_radius(stem, 1, LV_PART_MAIN);

    lv_obj_t *base = lv_obj_create(root);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, 16, 2);
    lv_obj_align(base, LV_ALIGN_CENTER, 0, 11);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(base, fg, LV_PART_MAIN);
    lv_obj_set_style_radius(base, 1, LV_PART_MAIN);
    return;
  }

  if (item.iconKind == HOME_ICON_POMODORO) {
    lv_obj_t *fruit = lv_obj_create(root);
    lv_obj_remove_style_all(fruit);
    lv_obj_set_size(fruit, 22, 22);
    lv_obj_align(fruit, LV_ALIGN_CENTER, 0, 2);
    lv_obj_set_style_radius(fruit, 11, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fruit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fruit, lv_color_hex(0xFF5252), LV_PART_MAIN);
    lv_obj_set_style_border_width(fruit, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(fruit, lv_color_hex(0xFFCDD2), LV_PART_MAIN);

    lv_obj_t *leafL = lv_obj_create(root);
    lv_obj_remove_style_all(leafL);
    lv_obj_set_size(leafL, 7, 3);
    lv_obj_align(leafL, LV_ALIGN_CENTER, -4, -9);
    lv_obj_set_style_radius(leafL, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(leafL, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(leafL, lv_color_hex(0x66BB6A), LV_PART_MAIN);

    lv_obj_t *leafR = lv_obj_create(root);
    lv_obj_remove_style_all(leafR);
    lv_obj_set_size(leafR, 7, 3);
    lv_obj_align(leafR, LV_ALIGN_CENTER, 4, -9);
    lv_obj_set_style_radius(leafR, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(leafR, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(leafR, lv_color_hex(0x66BB6A), LV_PART_MAIN);

    lv_obj_t *stem = lv_obj_create(root);
    lv_obj_remove_style_all(stem);
    lv_obj_set_size(stem, 2, 4);
    lv_obj_align(stem, LV_ALIGN_CENTER, 0, -11);
    lv_obj_set_style_radius(stem, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(stem, lv_color_hex(0xA5D6A7), LV_PART_MAIN);
    return;
  }

  if (item.iconKind == HOME_ICON_CLOCK) {
    lv_obj_t *dial = lv_obj_create(root);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 24, 24);
    lv_obj_align(dial, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(dial, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dial, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dial, fg, LV_PART_MAIN);
    lv_obj_set_style_border_width(dial, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(dial, fg, LV_PART_MAIN);

    lv_obj_t *hourHand = lv_obj_create(root);
    lv_obj_remove_style_all(hourHand);
    lv_obj_set_size(hourHand, 7, 2);
    lv_obj_align(hourHand, LV_ALIGN_CENTER, 2, 1);
    lv_obj_set_style_radius(hourHand, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hourHand, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hourHand, fg, LV_PART_MAIN);

    lv_obj_t *minuteHand = lv_obj_create(root);
    lv_obj_remove_style_all(minuteHand);
    lv_obj_set_size(minuteHand, 2, 8);
    lv_obj_align(minuteHand, LV_ALIGN_CENTER, 0, -3);
    lv_obj_set_style_radius(minuteHand, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(minuteHand, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(minuteHand, fg, LV_PART_MAIN);

    lv_obj_t *centerDot = lv_obj_create(root);
    lv_obj_remove_style_all(centerDot);
    lv_obj_set_size(centerDot, 4, 4);
    lv_obj_align(centerDot, LV_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_radius(centerDot, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(centerDot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(centerDot, fg, LV_PART_MAIN);
    return;
  }

  if (item.iconKind == HOME_ICON_WEATHER) {
    lv_obj_t *sun = lv_obj_create(root);
    lv_obj_remove_style_all(sun);
    lv_obj_set_size(sun, 11, 11);
    lv_obj_align(sun, LV_ALIGN_CENTER, 7, -8);
    lv_obj_set_style_radius(sun, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sun, lv_color_hex(0xFFD54F), LV_PART_MAIN);

    lv_obj_t *cloud1 = lv_obj_create(root);
    lv_obj_remove_style_all(cloud1);
    lv_obj_set_size(cloud1, 11, 11);
    lv_obj_align(cloud1, LV_ALIGN_CENTER, -6, 1);
    lv_obj_set_style_radius(cloud1, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cloud1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cloud1, fg, LV_PART_MAIN);

    lv_obj_t *cloud2 = lv_obj_create(root);
    lv_obj_remove_style_all(cloud2);
    lv_obj_set_size(cloud2, 13, 13);
    lv_obj_align(cloud2, LV_ALIGN_CENTER, 1, -1);
    lv_obj_set_style_radius(cloud2, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cloud2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cloud2, fg, LV_PART_MAIN);

    lv_obj_t *cloud3 = lv_obj_create(root);
    lv_obj_remove_style_all(cloud3);
    lv_obj_set_size(cloud3, 11, 11);
    lv_obj_align(cloud3, LV_ALIGN_CENTER, 9, 2);
    lv_obj_set_style_radius(cloud3, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cloud3, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cloud3, fg, LV_PART_MAIN);

    lv_obj_t *cloudBase = lv_obj_create(root);
    lv_obj_remove_style_all(cloudBase);
    lv_obj_set_size(cloudBase, 24, 8);
    lv_obj_align(cloudBase, LV_ALIGN_CENTER, 1, 5);
    lv_obj_set_style_radius(cloudBase, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cloudBase, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cloudBase, fg, LV_PART_MAIN);
    return;
  }

  if (item.iconKind == HOME_ICON_APPS) {
    static const lv_point_t line1[] = {{0, 14}, {14, 0}};
    static const lv_point_t line2[] = {{0, 18}, {18, 0}};
    static const lv_point_t line3[] = {{0, 22}, {22, 0}};

    lv_obj_t *l1 = lv_line_create(root);
    lv_line_set_points(l1, line1, 2);
    lv_obj_set_size(l1, 16, 16);
    lv_obj_align(l1, LV_ALIGN_CENTER, -4, -6);
    lv_obj_set_style_line_color(l1, fg, LV_PART_MAIN);
    lv_obj_set_style_line_width(l1, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l1, true, LV_PART_MAIN);

    lv_obj_t *l2 = lv_line_create(root);
    lv_line_set_points(l2, line2, 2);
    lv_obj_set_size(l2, 20, 20);
    lv_obj_align(l2, LV_ALIGN_CENTER, -1, -2);
    lv_obj_set_style_line_color(l2, fg, LV_PART_MAIN);
    lv_obj_set_style_line_width(l2, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l2, true, LV_PART_MAIN);

    lv_obj_t *l3 = lv_line_create(root);
    lv_line_set_points(l3, line3, 2);
    lv_obj_set_size(l3, 24, 24);
    lv_obj_align(l3, LV_ALIGN_CENTER, 2, 2);
    lv_obj_set_style_line_color(l3, fg, LV_PART_MAIN);
    lv_obj_set_style_line_width(l3, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l3, true, LV_PART_MAIN);
    return;
  }

  lv_obj_t *fallback = lv_label_create(root);
  lv_label_set_text(fallback, item.icon);
  lv_obj_set_style_text_color(fallback, fg, LV_PART_MAIN);
  lv_obj_set_style_text_font(fallback, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_center(fallback);
}

static void refreshHomeShortcutSlots() {
  uint8_t visible = HOME_VISIBLE_SLOT_COUNT;
  if (visible > HOME_SHORTCUT_COUNT) {
    visible = HOME_SHORTCUT_COUNT;
  }

  for (uint8_t i = 0; i < HOME_VISIBLE_SLOT_COUNT; ++i) {
    if (homeShortcutSlots[i] == nullptr || homeShortcutButtons[i] == nullptr || homeShortcutIcons[i] == nullptr) {
      continue;
    }

    if (i < visible) {
      int shortcutIdx = wrapHomeShortcutIndex(homeCarouselOffset + i);
      if (shortcutIdx < 0) {
        lv_obj_add_flag(homeShortcutSlots[i], LV_OBJ_FLAG_HIDDEN);
        homeSlotToShortcut[i] = -1;
        continue;
      }

      const HomeShortcutConfig &item = HOME_SHORTCUTS[shortcutIdx];
      homeSlotToShortcut[i] = (int8_t)shortcutIdx;
      lv_obj_clear_flag(homeShortcutSlots[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(homeShortcutButtons[i], lv_color_hex(item.accentColor), LV_PART_MAIN);
      lv_obj_set_style_shadow_color(homeShortcutButtons[i], lv_color_hex(item.accentColor), LV_PART_MAIN);
      renderHomeShortcutIcon(i, item);

      bool isFocused = (i == 0);
      lv_obj_set_style_border_width(homeShortcutButtons[i], isFocused ? 2 : 1, LV_PART_MAIN);
      lv_obj_set_style_border_color(homeShortcutButtons[i], isFocused ? lv_color_hex(0xF3E5FF) : lv_color_hex(0xD1C4E9), LV_PART_MAIN);
      lv_obj_set_style_shadow_width(homeShortcutButtons[i], isFocused ? 18 : 12, LV_PART_MAIN);
    } else {
      homeSlotToShortcut[i] = -1;
      lv_obj_add_flag(homeShortcutSlots[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (homeCurrentShortcutLabel != nullptr) {
    int idx = homeSlotToShortcut[0];
    if (idx >= 0 && idx < HOME_SHORTCUT_COUNT) {
      lv_label_set_text_fmt(homeCurrentShortcutLabel, "%s  (%d/%d)", HOME_SHORTCUTS[idx].label, idx + 1, HOME_SHORTCUT_COUNT);
    } else {
      lv_label_set_text(homeCurrentShortcutLabel, "--");
    }
  }
}

static void shiftHomeCarousel(int delta) {
  if (HOME_SHORTCUT_COUNT <= 1) {
    return;
  }

  homeCarouselOffset = wrapHomeShortcutIndex(homeCarouselOffset + delta);
  refreshHomeShortcutSlots();
}

static void homeShortcutEventCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  if (shouldSuppressClick()) {
    return;
  }

  intptr_t slotRaw = (intptr_t)lv_event_get_user_data(e);
  if (slotRaw < 0 || slotRaw >= HOME_VISIBLE_SLOT_COUNT) {
    return;
  }

  int shortcutIdx = homeSlotToShortcut[slotRaw];
  if (shortcutIdx < 0 || shortcutIdx >= HOME_SHORTCUT_COUNT) {
    return;
  }

  UiPage page = HOME_SHORTCUTS[shortcutIdx].page;
  if (page >= 0 && page < UI_PAGE_COUNT) {
    showPage((int)page);
  }
}

static void layoutHomeShortcuts() {
  lv_disp_t *disp = lv_disp_get_default();
  if (disp == nullptr) {
    return;
  }

  int16_t cx = lv_disp_get_hor_res(disp) / 2;
  int16_t cy = lv_disp_get_ver_res(disp) / 2 + 4;
  uint8_t visible = HOME_VISIBLE_SLOT_COUNT;
  if (visible > HOME_SHORTCUT_COUNT) {
    visible = HOME_SHORTCUT_COUNT;
  }

  for (uint8_t i = 0; i < HOME_VISIBLE_SLOT_COUNT; ++i) {
    if (homeShortcutSlots[i] == nullptr || homeShortcutButtons[i] == nullptr || homeShortcutIcons[i] == nullptr) {
      continue;
    }

    if (i >= visible) {
      homeSlotToShortcut[i] = -1;
      lv_obj_add_flag(homeShortcutSlots[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    float angleDeg = -90.0f + (360.0f * (float)i / (float)visible);
    float rad = angleDeg * HOME_DEG_TO_RAD;
    int16_t slotSize = 64;
    int16_t buttonSize = 52;
    int16_t radius = 124;
    int16_t x = cx + (int16_t)lroundf(cosf(rad) * radius) - slotSize / 2;
    int16_t y = cy + (int16_t)lroundf(sinf(rad) * radius) - slotSize / 2;

    lv_obj_clear_flag(homeShortcutSlots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(homeShortcutSlots[i], slotSize, slotSize);
    lv_obj_set_pos(homeShortcutSlots[i], x, y);
    lv_obj_set_size(homeShortcutButtons[i], buttonSize, buttonSize);
    lv_obj_set_style_radius(homeShortcutButtons[i], buttonSize / 2, LV_PART_MAIN);
    lv_obj_align(homeShortcutButtons[i], LV_ALIGN_CENTER, 0, 0);
  }

  refreshHomeShortcutSlots();
}

static void attachGestureHandlers(lv_obj_t *obj) {
  if (obj == nullptr) {
    return;
  }

  lv_obj_add_event_cb(obj, gestureEventCallback, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(obj, gestureEventCallback, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(obj, gestureEventCallback, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(obj, gestureEventCallback, LV_EVENT_PRESS_LOST, nullptr);
}

static lv_obj_t *createBasePage() {
  lv_obj_t *page = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(page);
  lv_obj_set_size(page, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
  attachGestureHandlers(page);
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
  if (shouldSuppressClick()) {
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

static const char* translateWeatherCondition(const char* condition) {
  // 将中文天气状况转换为英文
  if (strstr(condition, "晴")) return "Sunny";
  if (strstr(condition, "多云")) return "Cloudy";
  if (strstr(condition, "阴")) return "Overcast";
  if (strstr(condition, "雨")) return "Rainy";
  if (strstr(condition, "雪")) return "Snowy";
  if (strstr(condition, "雾")) return "Foggy";
  if (strstr(condition, "霾")) return "Haze";
  if (strstr(condition, "雷")) return "Thunder";
  if (strstr(condition, "风")) return "Windy";
  // 如果已经是英文或未知，直接返回
  return condition;
}

static void updateWeatherDisplay() {
  if (weatherTempLabel == nullptr) {
    return;
  }

  if (currentWeather.valid) {
    lv_label_set_text_fmt(weatherTempLabel, "%d", (int)currentWeather.temperature);
    lv_label_set_text(weatherConditionLabel, translateWeatherCondition(currentWeather.condition));
    lv_label_set_text(weatherCityLabel, currentWeather.city);
    lv_label_set_text_fmt(weatherHumidityLabel, "%d%%", currentWeather.humidity);
    lv_label_set_text_fmt(weatherFeelsLikeLabel, "%d°", (int)currentWeather.feelsLike);
  } else {
    lv_label_set_text(weatherTempLabel, "--");
    lv_label_set_text(weatherConditionLabel, "Loading...");
    lv_label_set_text(weatherCityLabel, currentWeather.city);
    lv_label_set_text(weatherHumidityLabel, "--%");
    lv_label_set_text(weatherFeelsLikeLabel, "--°");
  }
}

static void fetchWeatherData() {
  if (!isConnected) {
    Serial.println("[Weather] WebSocket not connected");
    return;
  }

  Serial.println("[Weather] Requesting weather via WebSocket...");

  // Send weather request to server
  StaticJsonDocument<256> doc;
  doc["type"] = "weather_request";

  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["cityId"] = WEATHER_CITY_ID;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);

  Serial.println("[Weather] Request sent");
}

static void weatherTimerCallback(lv_timer_t *timer) {
  (void)timer;

  // Skip if not enough time has passed since boot
  if (millis() < 10000) {
    return; // Wait at least 10 seconds after boot
  }

  // 检查是否需要更新天气
  if (!currentWeather.valid || (millis() - lastWeatherUpdateMs) >= WEATHER_UPDATE_INTERVAL_MS) {
    fetchWeatherData();
  }
}

static uint16_t clampPhotoSlideInterval(long value) {
  if (value < 3) return 3;
  if (value > 30) return 30;
  return (uint16_t)value;
}

static float clampPhotoMaxFileSize(float value) {
  if (value < 1.0f) return 1.0f;
  if (value > 5.0f) return 5.0f;
  return value;
}

static uint16_t clampPhotoMaxCount(long value) {
  if (value < 1) return 1;
  if (value > 100) return 100;
  return (uint16_t)value;
}

static void applyPhotoFrameSettings(const JsonObjectConst &data) {
  uint16_t oldInterval = photoFrameSettings.slideshowIntervalSec;
  bool oldAutoPlay = photoFrameSettings.autoPlay;
  uint16_t oldMaxPhotoCount = photoFrameSettings.maxPhotoCount;
  char oldTheme[24];
  char oldHomeWallpaperPath[192];
  char oldClockWallpaperPath[192];
  copyText(oldTheme, sizeof(oldTheme), photoFrameSettings.theme);
  copyText(oldHomeWallpaperPath, sizeof(oldHomeWallpaperPath), photoFrameSettings.homeWallpaperPath);
  copyText(oldClockWallpaperPath, sizeof(oldClockWallpaperPath), photoFrameSettings.clockWallpaperPath);

  long intervalValue = photoFrameSettings.slideshowIntervalSec;
  if (!data["slideshowInterval"].isNull()) {
    intervalValue = data["slideshowInterval"].as<long>();
  } else if (!data["slideshow_interval"].isNull()) {
    intervalValue = data["slideshow_interval"].as<long>();
  }
  photoFrameSettings.slideshowIntervalSec = clampPhotoSlideInterval(intervalValue);

  if (!data["autoPlay"].isNull()) {
    photoFrameSettings.autoPlay = data["autoPlay"].as<bool>();
  } else if (!data["auto_play"].isNull()) {
    photoFrameSettings.autoPlay = data["auto_play"].as<bool>();
  }

  const char *theme = photoFrameSettings.theme;
  if (!data["theme"].isNull()) {
    theme = data["theme"].as<const char *>();
  }
  copyText(photoFrameSettings.theme, sizeof(photoFrameSettings.theme), theme);

  float maxFileSizeValue = photoFrameSettings.maxFileSizeMb;
  if (!data["maxFileSize"].isNull()) {
    maxFileSizeValue = data["maxFileSize"].as<float>();
  } else if (!data["max_file_size"].isNull()) {
    maxFileSizeValue = data["max_file_size"].as<float>();
  }
  photoFrameSettings.maxFileSizeMb = clampPhotoMaxFileSize(maxFileSizeValue);

  if (!data["autoCompress"].isNull()) {
    photoFrameSettings.autoCompress = data["autoCompress"].as<bool>();
  } else if (!data["auto_compress"].isNull()) {
    photoFrameSettings.autoCompress = data["auto_compress"].as<bool>();
  }

  long maxPhotoCountValue = photoFrameSettings.maxPhotoCount;
  if (!data["maxPhotoCount"].isNull()) {
    maxPhotoCountValue = data["maxPhotoCount"].as<long>();
  } else if (!data["max_photo_count"].isNull()) {
    maxPhotoCountValue = data["max_photo_count"].as<long>();
  }
  photoFrameSettings.maxPhotoCount = clampPhotoMaxCount(maxPhotoCountValue);

  if (!data["homeWallpaperPath"].isNull() || !data["home_wallpaper_path"].isNull()) {
    const char *homePath = !data["homeWallpaperPath"].isNull()
      ? data["homeWallpaperPath"].as<const char *>()
      : data["home_wallpaper_path"].as<const char *>();
    if (homePath == nullptr || homePath[0] == '\0') {
      photoFrameSettings.homeWallpaperPath[0] = '\0';
    } else if (homePath[0] == '/' && hasMjpegPlaybackExtension(homePath)) {
      copyText(photoFrameSettings.homeWallpaperPath, sizeof(photoFrameSettings.homeWallpaperPath), homePath);
    }
  }

  if (!data["clockWallpaperPath"].isNull() || !data["clock_wallpaper_path"].isNull()) {
    const char *clockPath = !data["clockWallpaperPath"].isNull()
      ? data["clockWallpaperPath"].as<const char *>()
      : data["clock_wallpaper_path"].as<const char *>();
    if (clockPath == nullptr || clockPath[0] == '\0') {
      photoFrameSettings.clockWallpaperPath[0] = '\0';
    } else if (clockPath[0] == '/' && hasMjpegPlaybackExtension(clockPath)) {
      copyText(photoFrameSettings.clockWallpaperPath, sizeof(photoFrameSettings.clockWallpaperPath), clockPath);
    }
  }

  photoFrameSettings.valid = true;
  lastPhotoSettingsApplyMs = millis();
  lastPhotoAutoAdvanceMs = millis();

  bool changed =
    oldInterval != photoFrameSettings.slideshowIntervalSec ||
    oldAutoPlay != photoFrameSettings.autoPlay ||
    strcmp(oldTheme, photoFrameSettings.theme) != 0 ||
    strcmp(oldHomeWallpaperPath, photoFrameSettings.homeWallpaperPath) != 0 ||
    strcmp(oldClockWallpaperPath, photoFrameSettings.clockWallpaperPath) != 0;

  Serial.printf(
    "[PhotoSettings] synced interval=%us autoPlay=%s theme=%s maxSize=%.1fMB autoCompress=%s maxCount=%u home=%s clock=%s\n",
    photoFrameSettings.slideshowIntervalSec,
    photoFrameSettings.autoPlay ? "true" : "false",
    photoFrameSettings.theme,
    (double)photoFrameSettings.maxFileSizeMb,
    photoFrameSettings.autoCompress ? "true" : "false",
    photoFrameSettings.maxPhotoCount,
    photoFrameSettings.homeWallpaperPath[0] != '\0' ? photoFrameSettings.homeWallpaperPath : "auto",
    photoFrameSettings.clockWallpaperPath[0] != '\0' ? photoFrameSettings.clockWallpaperPath : "auto"
  );

  if (currentPage == UI_PAGE_PHOTO_FRAME) {
    char status[96];
    snprintf(
      status,
      sizeof(status),
      "Synced %us | %s",
      photoFrameSettings.slideshowIntervalSec,
      photoFrameSettings.autoPlay ? "auto on" : "auto off"
    );
    setPhotoFrameStatus(status, lv_color_hex(0x81C784));
  }

  if (changed) {
    char body[112];
    snprintf(
      body,
      sizeof(body),
      "Interval %us, %s, %s",
      photoFrameSettings.slideshowIntervalSec,
      photoFrameSettings.autoPlay ? "auto on" : "auto off",
      photoFrameSettings.theme
    );
    pushInboxMessage("event", "Photo settings synced", body);
  }

  if (sdMounted && oldMaxPhotoCount != photoFrameSettings.maxPhotoCount) {
    loadSdPhotoList();
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      showCurrentPhotoFrame();
    }
  }

  if (sdMounted &&
      (strcmp(oldHomeWallpaperPath, photoFrameSettings.homeWallpaperPath) != 0 ||
       strcmp(oldClockWallpaperPath, photoFrameSettings.clockWallpaperPath) != 0)) {
    refreshDynamicWallpaperSources();
    if (pages[UI_PAGE_HOME] != nullptr) {
      prepareDynamicWallpaperForPage(currentPage, true);
    }
  }

  sendPhotoFrameState("settings_sync", true);
}

static void requestPhotoFrameSettings(bool force) {
  if (!isConnected) {
    return;
  }

  uint32_t now = millis();
  if (!force && (now - lastPhotoSettingsRequestMs) < PHOTO_SETTINGS_POLL_INTERVAL_MS) {
    return;
  }
  lastPhotoSettingsRequestMs = now;

  StaticJsonDocument<256> doc;
  doc["type"] = "photo_settings_request";
  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["page"] = "photo_frame";
  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
  Serial.println("[PhotoSettings] request sent");
}

static void processPhotoFrameAutoPlay() {
  if (currentPage != UI_PAGE_PHOTO_FRAME) {
    return;
  }
  if (!photoFrameSettings.autoPlay || sdPhotoCount <= 1) {
    return;
  }

  uint32_t intervalMs = (uint32_t)photoFrameSettings.slideshowIntervalSec * 1000UL;
  if (intervalMs < 3000UL) {
    intervalMs = 3000UL;
  }

  uint32_t now = millis();
  if ((now - lastPhotoAutoAdvanceMs) < intervalMs) {
    return;
  }

  sdPhotoIndex = (sdPhotoIndex + 1) % sdPhotoCount;
  showCurrentPhotoFrame();
  lastPhotoAutoAdvanceMs = now;
}

static void sendPhotoFrameState(const char *reason, bool force) {
  if (!isConnected) {
    return;
  }

  uint32_t now = millis();
  if (!force && (now - lastPhotoStateEventMs) < PHOTO_STATE_EVENT_MIN_GAP_MS) {
    return;
  }
  if (force) {
    lastPhotoStateReportMs = now;
  }
  lastPhotoStateEventMs = now;

  StaticJsonDocument<512> doc;
  doc["type"] = "photo_state";
  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["reason"] = (reason != nullptr) ? reason : "update";
  data["pageActive"] = (currentPage == UI_PAGE_PHOTO_FRAME);
  data["sdMounted"] = sdMounted;
  data["total"] = sdPhotoCount;
  data["index"] = (sdPhotoCount > 0) ? (sdPhotoIndex + 1) : 0;
  data["autoPlay"] = photoFrameSettings.autoPlay;
  data["slideshowInterval"] = photoFrameSettings.slideshowIntervalSec;
  data["theme"] = photoFrameSettings.theme;
  data["settingsSynced"] = photoFrameSettings.valid;
  data["currentPhoto"] = currentPhotoName;
  data["decoder"] = currentPhotoDecoder;
  data["valid"] = currentPhotoValid;
  data["maxPhotoCount"] = photoFrameSettings.maxPhotoCount;
  data["skippedByLimit"] = sdPhotoLimitSkipped;
  data["uptime"] = now / 1000;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static void handlePhotoControlCommand(const JsonObjectConst &data) {
  const char *action = data["action"] | "";
  if (action[0] == '\0') {
    return;
  }

  bool handled = false;
  if (strcmp(action, "prev") == 0) {
    if (sdPhotoCount > 0) {
      sdPhotoIndex = (sdPhotoIndex - 1 + sdPhotoCount) % sdPhotoCount;
      showCurrentPhotoFrame();
      lastPhotoAutoAdvanceMs = millis();
      handled = true;
      if (currentPage == UI_PAGE_PHOTO_FRAME) {
        setPhotoFrameStatus("Remote: previous", lv_color_hex(0x90CAF9));
      }
    }
  } else if (strcmp(action, "next") == 0) {
    if (sdPhotoCount > 0) {
      sdPhotoIndex = (sdPhotoIndex + 1) % sdPhotoCount;
      showCurrentPhotoFrame();
      lastPhotoAutoAdvanceMs = millis();
      handled = true;
      if (currentPage == UI_PAGE_PHOTO_FRAME) {
        setPhotoFrameStatus("Remote: next", lv_color_hex(0x90CAF9));
      }
    }
  } else if (strcmp(action, "reload") == 0) {
    detectAndScanSdCard();
    loadSdPhotoList();
    showCurrentPhotoFrame();
    lastPhotoAutoAdvanceMs = millis();
    handled = true;
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      setPhotoFrameStatus("Remote: reload", lv_color_hex(0x90CAF9));
    }
  } else if (strcmp(action, "play") == 0) {
    photoFrameSettings.autoPlay = true;
    photoFrameSettings.valid = true;
    lastPhotoAutoAdvanceMs = millis();
    handled = true;
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      char status[64];
      snprintf(status, sizeof(status), "Remote: auto on (%us)", photoFrameSettings.slideshowIntervalSec);
      setPhotoFrameStatus(status, lv_color_hex(0x81C784));
    }
  } else if (strcmp(action, "pause") == 0) {
    photoFrameSettings.autoPlay = false;
    photoFrameSettings.valid = true;
    handled = true;
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      setPhotoFrameStatus("Remote: auto off", lv_color_hex(0xFFB74D));
    }
  } else if (strcmp(action, "set_interval") == 0) {
    long interval = photoFrameSettings.slideshowIntervalSec;
    if (!data["intervalSec"].isNull()) {
      interval = data["intervalSec"].as<long>();
    } else if (!data["interval"].isNull()) {
      interval = data["interval"].as<long>();
    } else if (!data["slideshowInterval"].isNull()) {
      interval = data["slideshowInterval"].as<long>();
    }
    photoFrameSettings.slideshowIntervalSec = clampPhotoSlideInterval(interval);
    photoFrameSettings.valid = true;
    lastPhotoAutoAdvanceMs = millis();
    handled = true;
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      char status[64];
      snprintf(status, sizeof(status), "Remote: interval %us", photoFrameSettings.slideshowIntervalSec);
      setPhotoFrameStatus(status, lv_color_hex(0x81C784));
    }
  }

  if (!handled) {
    if (currentPage == UI_PAGE_PHOTO_FRAME) {
      setPhotoFrameStatus("Remote command ignored", lv_color_hex(0xEF5350));
    }
    sendPhotoFrameState("remote_ignored", true);
    return;
  }

  char body[112];
  snprintf(body, sizeof(body), "Action=%s auto=%s interval=%us", action,
           photoFrameSettings.autoPlay ? "on" : "off",
           photoFrameSettings.slideshowIntervalSec);
  pushInboxMessage("event", "Photo remote control", body);
  sendPhotoFrameState("remote_control", true);
}

static uint32_t getColorFromString(const char* str) {
  uint32_t hash = 0;
  for (int i = 0; str[i] != '\0'; i++) {
    hash = str[i] + ((hash << 5) - hash);
  }

  const uint32_t colors[] = {
    0xFF6B6B, 0x4ECDC4, 0x45B7D1, 0xFFA07A,
    0x98D8C8, 0xF7DC6F, 0xBB8FCE, 0x85C1E2,
    0xF8B739, 0x52B788, 0xE76F51, 0x2A9D8F
  };

  return colors[hash % 12];
}

static void appLauncherStatusTimerCallback(lv_timer_t *timer) {
  (void)timer;
  appLauncherStatusTimer = nullptr;
  if (appLauncherStatusLabel != nullptr) {
    lv_obj_add_flag(appLauncherStatusLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

static void setAppLauncherStatus(const char *text, lv_color_t color, bool autoHide = false, uint32_t hideMs = 2200) {
  if (appLauncherStatusLabel == nullptr) {
    return;
  }

  if (text == nullptr) {
    text = "";
  }

  lv_label_set_text(appLauncherStatusLabel, text);
  lv_obj_set_style_text_color(appLauncherStatusLabel, color, LV_PART_MAIN);
  lv_obj_clear_flag(appLauncherStatusLabel, LV_OBJ_FLAG_HIDDEN);

  if (appLauncherStatusTimer != nullptr) {
    lv_timer_del(appLauncherStatusTimer);
    appLauncherStatusTimer = nullptr;
  }

  if (autoHide) {
    appLauncherStatusTimer = lv_timer_create(appLauncherStatusTimerCallback, hideMs, nullptr);
    lv_timer_set_repeat_count(appLauncherStatusTimer, 1);
  }
}

static void updateAppLauncherNavButtons() {
  int totalPages = (appCount > 0) ? ((appCount + APPS_PER_PAGE - 1) / APPS_PER_PAGE) : 1;
  bool canPrev = appPage > 0;
  bool canNext = appPage < (totalPages - 1);

  if (appLauncherPrevBtn != nullptr) {
    if (canPrev) {
      lv_obj_add_flag(appLauncherPrevBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(appLauncherPrevBtn, LV_OPA_COVER, LV_PART_MAIN);
    } else {
      lv_obj_clear_flag(appLauncherPrevBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(appLauncherPrevBtn, LV_OPA_50, LV_PART_MAIN);
    }
  }

  if (appLauncherNextBtn != nullptr) {
    if (canNext) {
      lv_obj_add_flag(appLauncherNextBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(appLauncherNextBtn, LV_OPA_COVER, LV_PART_MAIN);
    } else {
      lv_obj_clear_flag(appLauncherNextBtn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(appLauncherNextBtn, LV_OPA_50, LV_PART_MAIN);
    }
  }
}

static void requestAppList() {
  if (!isConnected) {
    Serial.println("[AppLauncher] WebSocket not connected");
    if (currentPage == UI_PAGE_APP_LAUNCHER) {
      setAppLauncherStatus("WS disconnected", lv_color_hex(0xEF5350), true, 2800);
    }
    return;
  }

  Serial.println("[AppLauncher] Requesting app list...");

  StaticJsonDocument<256> doc;
  doc["type"] = "app_list_request";

  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
}

static bool launchApp(const char *appPath, const char *appName, char *reason, size_t reasonSize) {
  if (reason != nullptr && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (!isConnected) {
    Serial.println("[AppLauncher] WebSocket not connected");
    copyText(reason, reasonSize, "WS disconnected");
    return false;
  }

  if (appPath == nullptr || appPath[0] == '\0') {
    copyText(reason, reasonSize, "Invalid app path");
    return false;
  }

  Serial.printf("[AppLauncher] Launching app: %s (%s)\n", appName == nullptr ? "App" : appName, appPath);

  StaticJsonDocument<256> doc;
  doc["type"] = "launch_app";

  JsonObject data = doc.createNestedObject("data");
  data["deviceId"] = DEVICE_ID;
  data["appPath"] = appPath;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);
  return true;
}

static void updateAppLauncherDisplay() {
  if (appLauncherList == nullptr) {
    return;
  }

  // Clear existing items
  lv_obj_clean(appLauncherList);

  if (appCount <= 0) {
    lv_obj_t *empty = lv_label_create(appLauncherList);
    lv_label_set_text(empty, "No apps loaded.\nReconnect WS or retry.");
    lv_obj_set_width(empty, 260);
    lv_obj_set_style_text_color(empty, lv_color_hex(0xBDBDBD), LV_PART_MAIN);
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    if (appLauncherPageLabel != nullptr) {
      lv_label_set_text(appLauncherPageLabel, "0/0");
    }
    updateAppLauncherNavButtons();
    return;
  }

  int totalPages = (appCount + APPS_PER_PAGE - 1) / APPS_PER_PAGE;
  if (appPage >= totalPages) {
    appPage = totalPages - 1;
  }
  if (appPage < 0) {
    appPage = 0;
  }

  int startIdx = appPage * APPS_PER_PAGE;
  int endIdx = min(startIdx + APPS_PER_PAGE, appCount);

  for (int i = startIdx; i < endIdx; i++) {
    MacApp &app = appList[i];

    // Create app item container
    lv_obj_t *item = lv_obj_create(appLauncherList);
    lv_obj_set_size(item, 280, 44);
    lv_obj_set_style_radius(item, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x171717), LV_PART_MAIN);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x252525), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(item, lv_color_hex(0x2B2B2B), LV_PART_MAIN);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_all(item, 4, LV_PART_MAIN);
    attachGestureHandlers(item);

    // Store app index in user data
    lv_obj_set_user_data(item, (void*)(intptr_t)i);

    // Add click event
    lv_obj_add_event_cb(item, [](lv_event_t *e) {
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      if (shouldSuppressClick()) return;

      lv_obj_t *target = lv_event_get_target(e);
      int appIdx = (int)(intptr_t)lv_obj_get_user_data(target);

      if (appIdx >= 0 && appIdx < appCount) {
        char reason[96];
        if (launchApp(appList[appIdx].path, appList[appIdx].name, reason, sizeof(reason))) {
          char statusText[96];
          snprintf(statusText, sizeof(statusText), "Launching %s...", appList[appIdx].name);
          setAppLauncherStatus(statusText, lv_color_hex(0x81C784), true, 2600);
          pushInboxMessage("app", "Launching", appList[appIdx].name);
        } else {
          char statusText[96];
          snprintf(statusText, sizeof(statusText), "Launch blocked: %s", reason);
          setAppLauncherStatus(statusText, lv_color_hex(0xEF5350), true, 3600);
          pushInboxMessage("alert", "App launch blocked", reason);
        }
      }
    }, LV_EVENT_CLICKED, nullptr);

    // Icon circle
    lv_obj_t *icon = lv_obj_create(item);
    lv_obj_set_size(icon, 30, 30);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(icon, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, lv_color_hex(app.color), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    // Letter label
    lv_obj_t *letter = lv_label_create(icon);
    char letterStr[2] = {app.letter, '\0'};
    lv_label_set_text(letter, letterStr);
    lv_obj_set_style_text_font(letter, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(letter, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(letter);

    // App name
    lv_obj_t *name = lv_label_create(item);
    lv_label_set_text(name, app.name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, lv_color_hex(0xF5F5F5), LV_PART_MAIN);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 220);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);
  }

  // Update page indicator
  if (appLauncherPageLabel != nullptr) {
    lv_label_set_text_fmt(appLauncherPageLabel, "%d/%d", appPage + 1, totalPages);
  }
  updateAppLauncherNavButtons();
}

static void appLauncherPageCallback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (shouldSuppressClick()) return;

  intptr_t direction = (intptr_t)lv_event_get_user_data(e);
  int totalPages = (appCount > 0) ? ((appCount + APPS_PER_PAGE - 1) / APPS_PER_PAGE) : 1;

  if (direction == 0) { // Previous
    if (appPage > 0) {
      appPage--;
      updateAppLauncherDisplay();
    }
  } else { // Next
    if (appPage < totalPages - 1) {
      appPage++;
      updateAppLauncherDisplay();
    }
  }
}

static void updateClockDisplay() {
  if (ntpConfigured) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      ntpSynced = true;
      if (clockLabel != nullptr) {
        lv_label_set_text_fmt(clockLabel, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
      if (homeClockLabel != nullptr) {
        lv_label_set_text_fmt(homeClockLabel, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
      if (homeDateLabel != nullptr) {
        lv_label_set_text_fmt(homeDateLabel, "%02d/%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
      }

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
  if (clockLabel != nullptr) {
    lv_label_set_text_fmt(clockLabel, "%02lu:%02lu", h, m);
  }
  if (homeClockLabel != nullptr) {
    lv_label_set_text_fmt(homeClockLabel, "%02lu:%02lu", h, m);
  }
  if (homeDateLabel != nullptr) {
    lv_label_set_text(homeDateLabel, "syncing...");
  }

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
    pauseDynamicWallpapersForMs(DYNAMIC_WALLPAPER_TOUCH_PAUSE_MS);
    gestureState.pressed = true;
    gestureState.longPressHandled = false;
    gestureState.startMs = millis();
    lv_point_t start = {0, 0};
    if (getActiveTouchPoint(&start)) {
      gestureState.startPoint = start;
    } else {
      gestureState.startPoint = lv_point_t{0, 0};
    }
    return;
  }

  if (code == LV_EVENT_PRESSING) {
    pauseDynamicWallpapersForMs(DYNAMIC_WALLPAPER_TOUCH_PAUSE_MS);
    if (!gestureState.pressed || gestureState.longPressHandled) {
      return;
    }

    if ((millis() - gestureState.startMs) < LONG_PRESS_MS) {
      return;
    }

    gestureState.longPressHandled = true;
    if (currentPage != UI_PAGE_HOME) {
      showPage(UI_PAGE_HOME);
      suppressClicksAfterHome();
    }
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    pauseDynamicWallpapersForMs(DYNAMIC_WALLPAPER_TOUCH_PAUSE_MS);
    if (!gestureState.pressed) {
      return;
    }

    if (!gestureState.longPressHandled) {
      lv_point_t endPoint = gestureState.startPoint;
      if (getActiveTouchPoint(&endPoint)) {
        int32_t dx = (int32_t)endPoint.x - (int32_t)gestureState.startPoint.x;
        int32_t dy = (int32_t)endPoint.y - (int32_t)gestureState.startPoint.y;
        int32_t adx = (dx >= 0) ? dx : -dx;
        int32_t ady = (dy >= 0) ? dy : -dy;

        if (currentPage == UI_PAGE_HOME) {
          bool swipeHorizontal = (adx >= HOME_CAROUSEL_SWIPE_THRESHOLD) &&
                                 (adx > (ady + HOME_CAROUSEL_DIRECTION_MARGIN));
          if (swipeHorizontal) {
            shiftHomeCarousel((dx < 0) ? 1 : -1);
            suppressClicksForMs(CLICK_SUPPRESS_MS_AFTER_CAROUSEL);
          }
        } else {
          bool swipeUp = (dy <= -SWIPE_HOME_THRESHOLD) &&
                         (ady > (adx + SWIPE_HOME_DIRECTION_MARGIN)) &&
                         (adx <= SWIPE_HOME_MAX_X_DRIFT);
          if (swipeUp) {
            showPage(UI_PAGE_HOME);
            suppressClicksAfterHome();
          }
        }
      }
    }

    gestureState = TouchGestureState();
    return;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    pauseDynamicWallpapersForMs(DYNAMIC_WALLPAPER_TOUCH_PAUSE_MS);
    gestureState = TouchGestureState();
  }
}

static void createUi() {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_text_color(lv_scr_act(), lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  // Page 1: Home Hub
  pages[UI_PAGE_HOME] = createBasePage();

  homeWallpaperImage = lv_img_create(pages[UI_PAGE_HOME]);
  lv_img_set_src(homeWallpaperImage, nullptr);
  lv_obj_set_size(homeWallpaperImage, 360, 360);
  lv_obj_center(homeWallpaperImage);
  lv_obj_clear_flag(homeWallpaperImage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  homeWallpaper.imageObj = homeWallpaperImage;

  homeWallpaperShade = lv_obj_create(pages[UI_PAGE_HOME]);
  lv_obj_remove_style_all(homeWallpaperShade);
  lv_obj_set_size(homeWallpaperShade, 360, 360);
  lv_obj_center(homeWallpaperShade);
  lv_obj_set_style_bg_opa(homeWallpaperShade, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeWallpaperShade, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_clear_flag(homeWallpaperShade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *homeOuter = lv_obj_create(pages[UI_PAGE_HOME]);
  lv_obj_set_size(homeOuter, 344, 344);
  lv_obj_align(homeOuter, LV_ALIGN_CENTER, 0, 4);
  lv_obj_set_style_radius(homeOuter, 172, LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeOuter, lv_color_hex(0x0B0F20), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(homeOuter, lv_color_hex(0x121A3A), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(homeOuter, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(homeOuter, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_border_width(homeOuter, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(homeOuter, lv_color_hex(0x1C2448), LV_PART_MAIN);
  lv_obj_clear_flag(homeOuter, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(homeOuter, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *homeInner = lv_obj_create(pages[UI_PAGE_HOME]);
  lv_obj_set_size(homeInner, 304, 304);
  lv_obj_align(homeInner, LV_ALIGN_CENTER, 0, 4);
  lv_obj_set_style_radius(homeInner, 152, LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeInner, lv_color_hex(0x111735), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(homeInner, lv_color_hex(0x191F43), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(homeInner, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(homeInner, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_border_width(homeInner, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(homeInner, lv_color_hex(0x222B58), LV_PART_MAIN);
  lv_obj_clear_flag(homeInner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(homeInner, LV_OBJ_FLAG_CLICKABLE);

  for (uint8_t i = 0; i < HOME_VISIBLE_SLOT_COUNT; ++i) {
    const HomeShortcutConfig &item = HOME_SHORTCUTS[i % HOME_SHORTCUT_COUNT];

    homeShortcutSlots[i] = lv_obj_create(pages[UI_PAGE_HOME]);
    lv_obj_remove_style_all(homeShortcutSlots[i]);
    lv_obj_set_size(homeShortcutSlots[i], 64, 64);
    lv_obj_set_style_bg_opa(homeShortcutSlots[i], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(homeShortcutSlots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(homeShortcutSlots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(homeShortcutSlots[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    attachGestureHandlers(homeShortcutSlots[i]);

    homeShortcutButtons[i] = lv_btn_create(homeShortcutSlots[i]);
    lv_obj_set_size(homeShortcutButtons[i], 52, 52);
    lv_obj_align(homeShortcutButtons[i], LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(homeShortcutButtons[i], 26, LV_PART_MAIN);
    lv_obj_set_style_bg_color(homeShortcutButtons[i], lv_color_hex(item.accentColor), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(homeShortcutButtons[i], lv_color_hex(0x7E57C2), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(homeShortcutButtons[i], LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(homeShortcutButtons[i], lv_color_hex(0x4527A0), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(homeShortcutButtons[i], 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(homeShortcutButtons[i], lv_color_hex(item.accentColor), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(homeShortcutButtons[i], LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(homeShortcutButtons[i], 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(homeShortcutButtons[i], lv_color_hex(0xD1C4E9), LV_PART_MAIN);
    lv_obj_add_flag(homeShortcutButtons[i], LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
    attachGestureHandlers(homeShortcutButtons[i]);
    lv_obj_add_event_cb(homeShortcutButtons[i], homeShortcutEventCallback, LV_EVENT_CLICKED, (void *)((intptr_t)i));

    homeShortcutIcons[i] = lv_obj_create(homeShortcutButtons[i]);
    lv_obj_remove_style_all(homeShortcutIcons[i]);
    lv_obj_set_size(homeShortcutIcons[i], 36, 36);
    lv_obj_center(homeShortcutIcons[i]);
    lv_obj_set_style_bg_opa(homeShortcutIcons[i], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(homeShortcutIcons[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(homeShortcutIcons[i], LV_OBJ_FLAG_CLICKABLE);
    renderHomeShortcutIcon(i, item);
  }

  lv_obj_t *homeCenter = lv_obj_create(pages[UI_PAGE_HOME]);
  lv_obj_set_size(homeCenter, 128, 128);
  lv_obj_align(homeCenter, LV_ALIGN_CENTER, 0, 4);
  lv_obj_set_style_radius(homeCenter, 64, LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeCenter, lv_color_hex(0x18305C), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(homeCenter, lv_color_hex(0x102242), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(homeCenter, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_width(homeCenter, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(homeCenter, lv_color_hex(0x2D5AA0), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(homeCenter, 20, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(homeCenter, lv_color_hex(0x1A3B74), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(homeCenter, LV_OPA_50, LV_PART_MAIN);
  lv_obj_clear_flag(homeCenter, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(homeCenter, LV_OBJ_FLAG_CLICKABLE);

  homeClockLabel = lv_label_create(homeCenter);
  lv_label_set_text(homeClockLabel, "--:--");
  lv_obj_set_style_text_font(homeClockLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_set_style_text_color(homeClockLabel, lv_color_hex(0xF2F7FF), LV_PART_MAIN);
  lv_obj_align(homeClockLabel, LV_ALIGN_CENTER, 0, -14);

  homeDateLabel = lv_label_create(homeCenter);
  lv_label_set_text(homeDateLabel, "--/--");
  lv_obj_set_style_text_font(homeDateLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(homeDateLabel, lv_color_hex(0xB7D1FF), LV_PART_MAIN);
  lv_obj_align(homeDateLabel, LV_ALIGN_CENTER, 0, 26);

  homeCurrentShortcutLabel = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeCurrentShortcutLabel, "--");
  lv_obj_set_style_text_color(homeCurrentShortcutLabel, lv_color_hex(0xD5DBFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(homeCurrentShortcutLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_align(homeCurrentShortcutLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(homeCurrentShortcutLabel, LV_ALIGN_BOTTOM_MID, 0, -30);

  homeSwipeHintLabel = lv_label_create(pages[UI_PAGE_HOME]);
  lv_label_set_text(homeSwipeHintLabel, "Swipe <- / ->   Tap to open");
  lv_obj_set_style_text_color(homeSwipeHintLabel, lv_color_hex(0x8A93C8), LV_PART_MAIN);
  lv_obj_set_style_text_font(homeSwipeHintLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_align(homeSwipeHintLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(homeSwipeHintLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

  layoutHomeShortcuts();

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

  clockWallpaperImage = lv_img_create(pages[UI_PAGE_CLOCK]);
  lv_img_set_src(clockWallpaperImage, nullptr);
  lv_obj_set_size(clockWallpaperImage, 360, 360);
  lv_obj_center(clockWallpaperImage);
  lv_obj_clear_flag(clockWallpaperImage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  clockWallpaper.imageObj = clockWallpaperImage;

  clockWallpaperShade = lv_obj_create(pages[UI_PAGE_CLOCK]);
  lv_obj_remove_style_all(clockWallpaperShade);
  lv_obj_set_size(clockWallpaperShade, 360, 360);
  lv_obj_center(clockWallpaperShade);
  lv_obj_set_style_bg_opa(clockWallpaperShade, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(clockWallpaperShade, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_clear_flag(clockWallpaperShade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

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
  lv_label_set_text(clockHint, "Long-press anywhere to return Home");
  lv_obj_align(clockHint, LV_ALIGN_BOTTOM_MID, 0, -30);

  // Page 4: Settings & Diagnostics
  pages[UI_PAGE_SETTINGS] = createBasePage();
  lv_obj_t *settingsTitle = lv_label_create(pages[UI_PAGE_SETTINGS]);
  lv_label_set_text(settingsTitle, "Settings & Diagnostics");
  lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *diagPanel = lv_obj_create(pages[UI_PAGE_SETTINGS]);
  lv_obj_set_size(diagPanel, 300, 124);
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

  diagSdLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagSdLabel, "SD: checking...");
  lv_obj_align(diagSdLabel, LV_ALIGN_TOP_LEFT, 10, 90);

  diagSdRootLabel = lv_label_create(diagPanel);
  lv_label_set_text(diagSdRootLabel, "Root: --");
  lv_obj_set_width(diagSdRootLabel, 280);
  lv_label_set_long_mode(diagSdRootLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(diagSdRootLabel, lv_color_hex(0xB0B0B0), LV_PART_MAIN);
  lv_obj_align(diagSdRootLabel, LV_ALIGN_TOP_LEFT, 10, 106);

  createSettingsButton(pages[UI_PAGE_SETTINGS], "WiFi Reconnect", 34, 166, SETTINGS_ACTION_WIFI_RECONNECT);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "WS Reconnect", 190, 166, SETTINGS_ACTION_WS_RECONNECT);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "NTP Sync", 34, 206, SETTINGS_ACTION_NTP_SYNC);
  createSettingsButton(pages[UI_PAGE_SETTINGS], "Reboot", 190, 206, SETTINGS_ACTION_REBOOT);

  lv_obj_t *brightnessPanel = lv_obj_create(pages[UI_PAGE_SETTINGS]);
  lv_obj_set_size(brightnessPanel, 300, 54);
  lv_obj_align(brightnessPanel, LV_ALIGN_TOP_MID, 0, 246);
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
  lv_obj_add_flag(brightnessSlider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(brightnessSlider);
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
  lv_obj_align(diagUptimeLabel, LV_ALIGN_TOP_MID, 0, 306);

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
  lv_label_set_text(inboxActionLabel, "Use buttons | long-press for Home");
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
  lv_obj_add_flag(startBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(startBtn);
  lv_obj_add_event_cb(startBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)0);
  lv_obj_t *startLabel = lv_label_create(startBtn);
  lv_label_set_text(startLabel, "Start");
  lv_obj_center(startLabel);

  lv_obj_t *resetBtn = lv_btn_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(resetBtn, 90, 36);
  lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 0, -36);
  lv_obj_set_style_radius(resetBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_add_flag(resetBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(resetBtn);
  lv_obj_add_event_cb(resetBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)1);
  lv_obj_t *resetLabel = lv_label_create(resetBtn);
  lv_label_set_text(resetLabel, "Reset");
  lv_obj_center(resetLabel);

  lv_obj_t *skipBtn = lv_btn_create(pages[UI_PAGE_POMODORO]);
  lv_obj_set_size(skipBtn, 90, 36);
  lv_obj_align(skipBtn, LV_ALIGN_BOTTOM_RIGHT, -30, -36);
  lv_obj_set_style_radius(skipBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(skipBtn, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_add_flag(skipBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(skipBtn);
  lv_obj_add_event_cb(skipBtn, pomodoroControlCallback, LV_EVENT_CLICKED, (void*)2);
  lv_obj_t *skipLabel = lv_label_create(skipBtn);
  lv_label_set_text(skipLabel, "Skip");
  lv_obj_center(skipLabel);

  // Page 7: Weather
  pages[UI_PAGE_WEATHER] = createBasePage();

  weatherCityLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherCityLabel, currentWeather.city);
  lv_obj_set_style_text_color(weatherCityLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_set_style_text_font(weatherCityLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(weatherCityLabel, LV_ALIGN_TOP_MID, 0, 30);

  weatherTempLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherTempLabel, "--");
  lv_obj_set_style_text_font(weatherTempLabel, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(weatherTempLabel, LV_ALIGN_CENTER, 0, -20);

  lv_obj_t *degreeLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(degreeLabel, "°C");
  lv_obj_set_style_text_font(degreeLabel, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_align(degreeLabel, LV_ALIGN_CENTER, 40, -30);

  weatherConditionLabel = lv_label_create(pages[UI_PAGE_WEATHER]);
  lv_label_set_text(weatherConditionLabel, "Loading...");
  lv_obj_set_style_text_color(weatherConditionLabel, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
  lv_obj_align(weatherConditionLabel, LV_ALIGN_CENTER, 0, 30);

  // Bottom info panel
  lv_obj_t *weatherInfoPanel = lv_obj_create(pages[UI_PAGE_WEATHER]);
  lv_obj_set_size(weatherInfoPanel, 280, 80);
  lv_obj_align(weatherInfoPanel, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_radius(weatherInfoPanel, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(weatherInfoPanel, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
  lv_obj_set_style_border_width(weatherInfoPanel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(weatherInfoPanel, LV_OBJ_FLAG_SCROLLABLE);

  // Feels Like (left)
  lv_obj_t *feelsLikeTitle = lv_label_create(weatherInfoPanel);
  lv_label_set_text(feelsLikeTitle, "FEELS LIKE");
  lv_obj_set_style_text_color(feelsLikeTitle, lv_color_hex(0x808080), LV_PART_MAIN);
  lv_obj_align(feelsLikeTitle, LV_ALIGN_TOP_LEFT, 20, 12);

  weatherFeelsLikeLabel = lv_label_create(weatherInfoPanel);
  lv_label_set_text(weatherFeelsLikeLabel, "--°");
  lv_obj_set_style_text_font(weatherFeelsLikeLabel, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_align(weatherFeelsLikeLabel, LV_ALIGN_TOP_LEFT, 20, 35);

  // Humidity (right)
  lv_obj_t *humidityTitle = lv_label_create(weatherInfoPanel);
  lv_label_set_text(humidityTitle, "HUMIDITY");
  lv_obj_set_style_text_color(humidityTitle, lv_color_hex(0x808080), LV_PART_MAIN);
  lv_obj_align(humidityTitle, LV_ALIGN_TOP_RIGHT, -20, 12);

  weatherHumidityLabel = lv_label_create(weatherInfoPanel);
  lv_label_set_text(weatherHumidityLabel, "--%");
  lv_obj_set_style_text_font(weatherHumidityLabel, &lv_font_montserrat_22, LV_PART_MAIN);
  lv_obj_align(weatherHumidityLabel, LV_ALIGN_TOP_RIGHT, -20, 35);

  // Page 8: App Launcher
  pages[UI_PAGE_APP_LAUNCHER] = createBasePage();

  appLauncherTitle = lv_label_create(pages[UI_PAGE_APP_LAUNCHER]);
  lv_label_set_text(appLauncherTitle, "App Launcher");
  lv_obj_set_style_text_color(appLauncherTitle, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(appLauncherTitle, LV_ALIGN_TOP_MID, 0, 14);

  // App list container
  appLauncherList = lv_obj_create(pages[UI_PAGE_APP_LAUNCHER]);
  lv_obj_set_size(appLauncherList, 300, 216);
  lv_obj_align(appLauncherList, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_style_bg_color(appLauncherList, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_border_width(appLauncherList, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(appLauncherList, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_set_style_radius(appLauncherList, 12, LV_PART_MAIN);
  lv_obj_clear_flag(appLauncherList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(appLauncherList, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_flex_flow(appLauncherList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(appLauncherList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(appLauncherList, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(appLauncherList, 10, LV_PART_MAIN);
  attachGestureHandlers(appLauncherList);

  // Status label for launch feedback
  appLauncherStatusLabel = lv_label_create(pages[UI_PAGE_APP_LAUNCHER]);
  lv_label_set_text(appLauncherStatusLabel, "Ready");
  lv_obj_set_style_text_color(appLauncherStatusLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_set_style_text_font(appLauncherStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(appLauncherStatusLabel, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_add_flag(appLauncherStatusLabel, LV_OBJ_FLAG_HIDDEN);

  // Navigation bar (moved inward for easier tapping on circular screen)
  lv_obj_t *appNavBar = lv_obj_create(pages[UI_PAGE_APP_LAUNCHER]);
  lv_obj_set_size(appNavBar, 280, 46);
  lv_obj_align(appNavBar, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_radius(appNavBar, 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(appNavBar, lv_color_hex(0x141414), LV_PART_MAIN);
  lv_obj_set_style_border_color(appNavBar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(appNavBar, 1, LV_PART_MAIN);
  lv_obj_clear_flag(appNavBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(appNavBar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(appNavBar);

  appLauncherPrevBtn = lv_btn_create(appNavBar);
  lv_obj_set_size(appLauncherPrevBtn, 94, 34);
  lv_obj_align(appLauncherPrevBtn, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_set_style_radius(appLauncherPrevBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(appLauncherPrevBtn, lv_color_hex(0x222222), LV_PART_MAIN);
  lv_obj_add_flag(appLauncherPrevBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(appLauncherPrevBtn);
  lv_obj_add_event_cb(appLauncherPrevBtn, appLauncherPageCallback, LV_EVENT_CLICKED, (void*)0);
  lv_obj_t *prevLabel = lv_label_create(appLauncherPrevBtn);
  lv_label_set_text(prevLabel, "Prev");
  lv_obj_center(prevLabel);

  appLauncherPageLabel = lv_label_create(appNavBar);
  lv_label_set_text(appLauncherPageLabel, "1/1");
  lv_obj_set_style_text_color(appLauncherPageLabel, lv_color_hex(0xC2C2C2), LV_PART_MAIN);
  lv_obj_align(appLauncherPageLabel, LV_ALIGN_CENTER, 0, 0);

  appLauncherNextBtn = lv_btn_create(appNavBar);
  lv_obj_set_size(appLauncherNextBtn, 94, 34);
  lv_obj_align(appLauncherNextBtn, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_radius(appLauncherNextBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(appLauncherNextBtn, lv_color_hex(0x222222), LV_PART_MAIN);
  lv_obj_add_flag(appLauncherNextBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(appLauncherNextBtn);
  lv_obj_add_event_cb(appLauncherNextBtn, appLauncherPageCallback, LV_EVENT_CLICKED, (void*)1);
  lv_obj_t *nextLabel = lv_label_create(appLauncherNextBtn);
  lv_label_set_text(nextLabel, "Next");
  lv_obj_center(nextLabel);

  // Page 9: Photo Frame (SD)
  pages[UI_PAGE_PHOTO_FRAME] = createBasePage();

  lv_obj_t *photoTitle = lv_label_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_label_set_text(photoTitle, "Photo Frame (SD)");
  lv_obj_set_style_text_color(photoTitle, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(photoTitle, LV_ALIGN_TOP_MID, 0, 14);

  photoFrameRootLabel = lv_label_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_label_set_text(photoFrameRootLabel, "Root: --");
  lv_obj_set_width(photoFrameRootLabel, 300);
  lv_label_set_long_mode(photoFrameRootLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(photoFrameRootLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_align(photoFrameRootLabel, LV_ALIGN_TOP_MID, 0, 36);

  lv_obj_t *photoCard = lv_obj_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_obj_set_size(photoCard, 300, 208);
  lv_obj_align(photoCard, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_radius(photoCard, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(photoCard, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_border_color(photoCard, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(photoCard, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(photoCard, 6, LV_PART_MAIN);
  lv_obj_clear_flag(photoCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(photoCard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(photoCard);

  photoFrameViewport = lv_obj_create(photoCard);
  lv_obj_set_size(photoFrameViewport, 286, 194);
  lv_obj_center(photoFrameViewport);
  lv_obj_set_style_radius(photoFrameViewport, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(photoFrameViewport, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(photoFrameViewport, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(photoFrameViewport, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(photoFrameViewport, true, LV_PART_MAIN);
  lv_obj_clear_flag(photoFrameViewport, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(photoFrameViewport, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(photoFrameViewport);

  photoFrameImage = lv_img_create(photoFrameViewport);
  lv_img_set_src(photoFrameImage, nullptr);
  lv_obj_center(photoFrameImage);

  photoFrameStatusLabel = lv_label_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_label_set_text(photoFrameStatusLabel, "Waiting SD scan...");
  lv_obj_set_style_text_color(photoFrameStatusLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_align(photoFrameStatusLabel, LV_ALIGN_TOP_MID, 0, 266);

  photoFrameNameLabel = lv_label_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_label_set_text(photoFrameNameLabel, "--");
  lv_obj_set_width(photoFrameNameLabel, 280);
  lv_label_set_long_mode(photoFrameNameLabel, LV_LABEL_LONG_DOT);
  lv_obj_align(photoFrameNameLabel, LV_ALIGN_TOP_MID, 0, 284);

  lv_obj_t *photoNavBar = lv_obj_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_obj_set_size(photoNavBar, 304, 50);
  lv_obj_align(photoNavBar, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_set_style_radius(photoNavBar, 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(photoNavBar, lv_color_hex(0x151515), LV_PART_MAIN);
  lv_obj_set_style_border_color(photoNavBar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(photoNavBar, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(photoNavBar, 5, LV_PART_MAIN);
  lv_obj_clear_flag(photoNavBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(photoNavBar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(photoNavBar);

  photoFramePrevBtn = lv_btn_create(photoNavBar);
  lv_obj_set_size(photoFramePrevBtn, 94, 38);
  lv_obj_align(photoFramePrevBtn, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_set_style_radius(photoFramePrevBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(photoFramePrevBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(photoFramePrevBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(photoFramePrevBtn);
  lv_obj_add_event_cb(photoFramePrevBtn, photoFrameControlCallback, LV_EVENT_CLICKED, (void *)0);
  lv_obj_t *photoPrevLabel = lv_label_create(photoFramePrevBtn);
  lv_label_set_text(photoPrevLabel, "Prev");
  lv_obj_center(photoPrevLabel);

  photoFrameReloadBtn = lv_btn_create(photoNavBar);
  lv_obj_set_size(photoFrameReloadBtn, 94, 38);
  lv_obj_align(photoFrameReloadBtn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(photoFrameReloadBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(photoFrameReloadBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(photoFrameReloadBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(photoFrameReloadBtn);
  lv_obj_add_event_cb(photoFrameReloadBtn, photoFrameControlCallback, LV_EVENT_CLICKED, (void *)1);
  lv_obj_t *photoReloadLabel = lv_label_create(photoFrameReloadBtn);
  lv_label_set_text(photoReloadLabel, "Reload");
  lv_obj_center(photoReloadLabel);

  photoFrameNextBtn = lv_btn_create(photoNavBar);
  lv_obj_set_size(photoFrameNextBtn, 94, 38);
  lv_obj_align(photoFrameNextBtn, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_radius(photoFrameNextBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(photoFrameNextBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(photoFrameNextBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(photoFrameNextBtn);
  lv_obj_add_event_cb(photoFrameNextBtn, photoFrameControlCallback, LV_EVENT_CLICKED, (void *)2);
  lv_obj_t *photoNextLabel = lv_label_create(photoFrameNextBtn);
  lv_label_set_text(photoNextLabel, "Next");
  lv_obj_center(photoNextLabel);

  photoFrameIndexLabel = lv_label_create(pages[UI_PAGE_PHOTO_FRAME]);
  lv_label_set_text(photoFrameIndexLabel, "0/0");
  lv_obj_set_style_text_color(photoFrameIndexLabel, lv_color_hex(0xBFBFBF), LV_PART_MAIN);
  lv_obj_align(photoFrameIndexLabel, LV_ALIGN_BOTTOM_MID, 0, -12);

  // Page 10: Audio Player (SD)
  pages[UI_PAGE_AUDIO_PLAYER] = createBasePage();

  lv_obj_t *audioTitle = lv_label_create(pages[UI_PAGE_AUDIO_PLAYER]);
  lv_label_set_text(audioTitle, "Music Player (SD)");
  lv_obj_set_style_text_color(audioTitle, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(audioTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *audioCard = lv_obj_create(pages[UI_PAGE_AUDIO_PLAYER]);
  lv_obj_set_size(audioCard, 300, 186);
  lv_obj_align(audioCard, LV_ALIGN_TOP_MID, 0, 54);
  lv_obj_set_style_radius(audioCard, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioCard, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(audioCard, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(audioCard, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(audioCard, 8, LV_PART_MAIN);
  lv_obj_clear_flag(audioCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(audioCard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(audioCard);

  lv_obj_t *audioIcon = lv_label_create(audioCard);
  lv_label_set_text(audioIcon, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_font(audioIcon, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_set_style_text_color(audioIcon, lv_color_hex(0xB39DDB), LV_PART_MAIN);
  lv_obj_align(audioIcon, LV_ALIGN_TOP_MID, 0, 2);

  audioTrackLabel = lv_label_create(audioCard);
  lv_label_set_text(audioTrackLabel, "Scanning SD...");
  lv_obj_set_width(audioTrackLabel, 260);
  lv_label_set_long_mode(audioTrackLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(audioTrackLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_align(audioTrackLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(audioTrackLabel, LV_ALIGN_TOP_MID, 0, 56);

  audioTimeLabel = lv_label_create(audioCard);
  lv_label_set_text(audioTimeLabel, "00:00 / --:--");
  lv_obj_set_style_text_font(audioTimeLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(audioTimeLabel, lv_color_hex(0xC5CAE9), LV_PART_MAIN);
  lv_obj_set_style_text_align(audioTimeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(audioTimeLabel, LV_ALIGN_TOP_MID, 0, 86);

  audioIndexLabel = lv_label_create(audioCard);
  lv_label_set_text(audioIndexLabel, "0/0");
  lv_obj_set_style_text_color(audioIndexLabel, lv_color_hex(0xBDBDBD), LV_PART_MAIN);
  lv_obj_align(audioIndexLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

  lv_obj_t *audioRescanBtn = lv_btn_create(audioCard);
  lv_obj_set_size(audioRescanBtn, 86, 32);
  lv_obj_align(audioRescanBtn, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  lv_obj_set_style_radius(audioRescanBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioRescanBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(audioRescanBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(audioRescanBtn);
  lv_obj_add_event_cb(audioRescanBtn, audioControlCallback, LV_EVENT_CLICKED, (void *)3);
  lv_obj_t *audioRescanLabel = lv_label_create(audioRescanBtn);
  lv_label_set_text(audioRescanLabel, "Rescan");
  lv_obj_center(audioRescanLabel);

  audioStatusLabel = lv_label_create(pages[UI_PAGE_AUDIO_PLAYER]);
  lv_label_set_text(audioStatusLabel, "Ready");
  lv_obj_set_style_text_color(audioStatusLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(audioStatusLabel, LV_ALIGN_TOP_MID, 0, 248);

  lv_obj_t *audioNavBar = lv_obj_create(pages[UI_PAGE_AUDIO_PLAYER]);
  lv_obj_set_size(audioNavBar, 304, 50);
  lv_obj_align(audioNavBar, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_set_style_radius(audioNavBar, 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioNavBar, lv_color_hex(0x151515), LV_PART_MAIN);
  lv_obj_set_style_border_color(audioNavBar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(audioNavBar, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(audioNavBar, 5, LV_PART_MAIN);
  lv_obj_clear_flag(audioNavBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(audioNavBar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(audioNavBar);

  audioPrevBtn = lv_btn_create(audioNavBar);
  lv_obj_set_size(audioPrevBtn, 94, 38);
  lv_obj_align(audioPrevBtn, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_set_style_radius(audioPrevBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioPrevBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(audioPrevBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(audioPrevBtn);
  lv_obj_add_event_cb(audioPrevBtn, audioControlCallback, LV_EVENT_CLICKED, (void *)0);
  lv_obj_t *audioPrevLabel = lv_label_create(audioPrevBtn);
  lv_label_set_text(audioPrevLabel, "Prev");
  lv_obj_center(audioPrevLabel);

  audioPlayBtn = lv_btn_create(audioNavBar);
  lv_obj_set_size(audioPlayBtn, 94, 38);
  lv_obj_align(audioPlayBtn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(audioPlayBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioPlayBtn, lv_color_hex(0x2E7D32), LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioPlayBtn, lv_color_hex(0x1B5E20), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_flag(audioPlayBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(audioPlayBtn);
  lv_obj_add_event_cb(audioPlayBtn, audioControlCallback, LV_EVENT_CLICKED, (void *)1);
  audioPlayBtnLabel = lv_label_create(audioPlayBtn);
  lv_label_set_text(audioPlayBtnLabel, "Play");
  lv_obj_center(audioPlayBtnLabel);

  audioNextBtn = lv_btn_create(audioNavBar);
  lv_obj_set_size(audioNextBtn, 94, 38);
  lv_obj_align(audioNextBtn, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_radius(audioNextBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(audioNextBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(audioNextBtn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(audioNextBtn);
  lv_obj_add_event_cb(audioNextBtn, audioControlCallback, LV_EVENT_CLICKED, (void *)2);
  lv_obj_t *audioNextLabel = lv_label_create(audioNextBtn);
  lv_label_set_text(audioNextLabel, "Next");
  lv_obj_center(audioNextLabel);

  updateAudioControlButtons(false, false);

  // Page 11: Video Player (SD, MJPEG MVP)
  pages[UI_PAGE_VIDEO_PLAYER] = createBasePage();

  lv_obj_t *videoTitle = lv_label_create(pages[UI_PAGE_VIDEO_PLAYER]);
  lv_label_set_text(videoTitle, "Video Player (SD)");
  lv_obj_set_style_text_color(videoTitle, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(videoTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *videoCard = lv_obj_create(pages[UI_PAGE_VIDEO_PLAYER]);
  lv_obj_set_size(videoCard, 300, 196);
  lv_obj_align(videoCard, LV_ALIGN_TOP_MID, 0, 54);
  lv_obj_set_style_radius(videoCard, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoCard, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_border_color(videoCard, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(videoCard, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(videoCard, 6, LV_PART_MAIN);
  lv_obj_clear_flag(videoCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(videoCard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(videoCard);

  videoViewport = lv_obj_create(videoCard);
  lv_obj_set_size(videoViewport, 286, 148);
  lv_obj_align(videoViewport, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_radius(videoViewport, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoViewport, lv_color_hex(0x050505), LV_PART_MAIN);
  lv_obj_set_style_border_color(videoViewport, lv_color_hex(0x202020), LV_PART_MAIN);
  lv_obj_set_style_border_width(videoViewport, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(videoViewport, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(videoViewport, true, LV_PART_MAIN);
  lv_obj_clear_flag(videoViewport, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(videoViewport, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(videoViewport);

  videoImage = lv_img_create(videoViewport);
  lv_img_set_src(videoImage, nullptr);
  lv_obj_center(videoImage);

  videoTrackLabel = lv_label_create(videoCard);
  lv_label_set_text(videoTrackLabel, "Scanning SD...");
  lv_obj_set_width(videoTrackLabel, 276);
  lv_label_set_long_mode(videoTrackLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(videoTrackLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_align(videoTrackLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(videoTrackLabel, LV_ALIGN_TOP_MID, 0, 154);

  videoHintLabel = lv_label_create(videoCard);
  lv_label_set_text(videoHintLabel, "MJPEG only (.mjpeg/.mjpg)");
  lv_obj_set_style_text_color(videoHintLabel, lv_color_hex(0xAFAFAF), LV_PART_MAIN);
  lv_obj_set_style_text_font(videoHintLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_width(videoHintLabel, 276);
  lv_obj_set_style_text_align(videoHintLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(videoHintLabel, LV_ALIGN_TOP_MID, 0, 172);

  lv_obj_t *videoRescanBtn = lv_btn_create(videoCard);
  lv_obj_set_size(videoRescanBtn, 74, 28);
  lv_obj_align(videoRescanBtn, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_set_style_radius(videoRescanBtn, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoRescanBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(videoRescanBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(videoRescanBtn);
  lv_obj_add_event_cb(videoRescanBtn, videoControlCallback, LV_EVENT_CLICKED, (void *)VIDEO_CONTROL_RESCAN);
  lv_obj_t *videoRescanLabel = lv_label_create(videoRescanBtn);
  lv_label_set_text(videoRescanLabel, "Rescan");
  lv_obj_set_style_text_font(videoRescanLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(videoRescanLabel);

  videoStatusLabel = lv_label_create(pages[UI_PAGE_VIDEO_PLAYER]);
  lv_label_set_text(videoStatusLabel, "Ready");
  lv_obj_set_style_text_color(videoStatusLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  lv_obj_align(videoStatusLabel, LV_ALIGN_TOP_MID, 0, 252);

  lv_obj_t *videoNavBar = lv_obj_create(pages[UI_PAGE_VIDEO_PLAYER]);
  lv_obj_set_size(videoNavBar, 304, 50);
  lv_obj_align(videoNavBar, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_set_style_radius(videoNavBar, 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoNavBar, lv_color_hex(0x151515), LV_PART_MAIN);
  lv_obj_set_style_border_color(videoNavBar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(videoNavBar, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(videoNavBar, 5, LV_PART_MAIN);
  lv_obj_clear_flag(videoNavBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(videoNavBar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(videoNavBar);

  videoPrevBtn = lv_btn_create(videoNavBar);
  lv_obj_set_size(videoPrevBtn, 94, 38);
  lv_obj_align(videoPrevBtn, LV_ALIGN_LEFT_MID, 6, 0);
  lv_obj_set_style_radius(videoPrevBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoPrevBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(videoPrevBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(videoPrevBtn);
  lv_obj_add_event_cb(videoPrevBtn, videoControlCallback, LV_EVENT_CLICKED, (void *)VIDEO_CONTROL_PREV);
  lv_obj_t *videoPrevLabel = lv_label_create(videoPrevBtn);
  lv_label_set_text(videoPrevLabel, "Prev");
  lv_obj_center(videoPrevLabel);

  videoPlayBtn = lv_btn_create(videoNavBar);
  lv_obj_set_size(videoPlayBtn, 94, 38);
  lv_obj_align(videoPlayBtn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(videoPlayBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoPlayBtn, lv_color_hex(0x2E7D32), LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoPlayBtn, lv_color_hex(0x1B5E20), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_flag(videoPlayBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(videoPlayBtn);
  lv_obj_add_event_cb(videoPlayBtn, videoControlCallback, LV_EVENT_CLICKED, (void *)VIDEO_CONTROL_TOGGLE);
  videoPlayBtnLabel = lv_label_create(videoPlayBtn);
  lv_label_set_text(videoPlayBtnLabel, "Play");
  lv_obj_center(videoPlayBtnLabel);

  videoNextBtn = lv_btn_create(videoNavBar);
  lv_obj_set_size(videoNextBtn, 94, 38);
  lv_obj_align(videoNextBtn, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_radius(videoNextBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(videoNextBtn, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_add_flag(videoNextBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(videoNextBtn);
  lv_obj_add_event_cb(videoNextBtn, videoControlCallback, LV_EVENT_CLICKED, (void *)VIDEO_CONTROL_NEXT);
  lv_obj_t *videoNextLabel = lv_label_create(videoNextBtn);
  lv_label_set_text(videoNextLabel, "Next");
  lv_obj_center(videoNextLabel);

  videoIndexLabel = lv_label_create(pages[UI_PAGE_VIDEO_PLAYER]);
  lv_label_set_text(videoIndexLabel, "0/0");
  lv_obj_set_style_text_color(videoIndexLabel, lv_color_hex(0xBFBFBF), LV_PART_MAIN);
  lv_obj_align(videoIndexLabel, LV_ALIGN_BOTTOM_MID, 0, -12);

  updateVideoControlButtons(false);

  // Page 12: Voice Commands (phase-1 bridge)
  pages[UI_PAGE_VOICE] = createBasePage();

  lv_obj_t *voiceTitle = lv_label_create(pages[UI_PAGE_VOICE]);
  lv_label_set_text(voiceTitle, "Voice Commands");
  lv_obj_set_style_text_color(voiceTitle, lv_color_hex(0x80CBC4), LV_PART_MAIN);
  lv_obj_align(voiceTitle, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *voiceHint = lv_label_create(pages[UI_PAGE_VOICE]);
  lv_label_set_text(voiceHint, "Preset commands + live mic stream");
  lv_obj_set_style_text_color(voiceHint, lv_color_hex(0xB0BEC5), LV_PART_MAIN);
  lv_obj_set_style_text_font(voiceHint, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(voiceHint, LV_ALIGN_TOP_MID, 0, 38);

  lv_obj_t *voiceCard = lv_obj_create(pages[UI_PAGE_VOICE]);
  lv_obj_set_size(voiceCard, 312, 236);
  lv_obj_align(voiceCard, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_style_radius(voiceCard, 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(voiceCard, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_border_color(voiceCard, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
  lv_obj_set_style_border_width(voiceCard, 1, LV_PART_MAIN);
  lv_obj_clear_flag(voiceCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(voiceCard, LV_OBJ_FLAG_GESTURE_BUBBLE);
  attachGestureHandlers(voiceCard);

  const int buttonW = 140;
  const int buttonH = 42;
  const int startX = 10;
  const int startY = 12;
  const int gapX = 12;
  const int gapY = 12;
  for (int i = 0; i < (int)(sizeof(VOICE_PRESET_COMMANDS) / sizeof(VOICE_PRESET_COMMANDS[0])); ++i) {
    int row = i / 2;
    int col = i % 2;
    lv_obj_t *btn = lv_btn_create(voiceCard);
    lv_obj_set_size(btn, buttonW, buttonH);
    lv_obj_set_pos(btn, startX + col * (buttonW + gapX), startY + row * (buttonH + gapY));
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1F2A44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x263859), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
    attachGestureHandlers(btn);
    lv_obj_add_event_cb(btn, voiceCommandButtonCallback, LV_EVENT_CLICKED, (void *)((intptr_t)i));

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, VOICE_PRESET_COMMANDS[i].label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE3F2FD), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(label);
  }

  voiceMicToggleBtn = lv_btn_create(voiceCard);
  lv_obj_set_size(voiceMicToggleBtn, 108, 38);
  lv_obj_align(voiceMicToggleBtn, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
  lv_obj_set_style_radius(voiceMicToggleBtn, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(voiceMicToggleBtn, lv_color_hex(0x00695C), LV_PART_MAIN);
  lv_obj_set_style_bg_color(voiceMicToggleBtn, lv_color_hex(0x00796B), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_flag(voiceMicToggleBtn, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_PRESS_LOCK);
  attachGestureHandlers(voiceMicToggleBtn);
  lv_obj_add_event_cb(voiceMicToggleBtn, voiceMicToggleCallback, LV_EVENT_CLICKED, nullptr);

  voiceMicToggleLabel = lv_label_create(voiceMicToggleBtn);
  lv_label_set_text(voiceMicToggleLabel, "Start Mic");
  lv_obj_set_style_text_font(voiceMicToggleLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_center(voiceMicToggleLabel);

  voiceStatusLabel = lv_label_create(voiceCard);
  lv_label_set_text(voiceStatusLabel, "Ready");
  lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x80CBC4), LV_PART_MAIN);
  lv_obj_set_style_text_font(voiceStatusLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(voiceStatusLabel, LV_ALIGN_BOTTOM_LEFT, 10, -56);

  voiceResultLabel = lv_label_create(voiceCard);
  lv_label_set_text(voiceResultLabel, "Result: --");
  lv_obj_set_width(voiceResultLabel, 176);
  lv_label_set_long_mode(voiceResultLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(voiceResultLabel, lv_color_hex(0xCFD8DC), LV_PART_MAIN);
  lv_obj_set_style_text_font(voiceResultLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(voiceResultLabel, LV_ALIGN_BOTTOM_LEFT, 10, -30);

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

static void sendVoiceCommand(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  if (!isConnected) {
    if (voiceStatusLabel != nullptr) {
      lv_label_set_text(voiceStatusLabel, "WS disconnected");
      lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
    }
    pushInboxMessage("alert", "Voice command", "WS disconnected");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["type"] = "voice_command";
  JsonObject data = doc.createNestedObject("data");
  data["text"] = text;
  data["source"] = "esp32_ui";

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(output);

  if (voiceStatusLabel != nullptr) {
    lv_label_set_text_fmt(voiceStatusLabel, "Sending: %s", text);
    lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x90CAF9), LV_PART_MAIN);
  }
}

static int parseUiPageFromVoiceName(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return -1;
  }

  if (strcmp(name, "home") == 0) return UI_PAGE_HOME;
  if (strcmp(name, "monitor") == 0) return UI_PAGE_MONITOR;
  if (strcmp(name, "clock") == 0) return UI_PAGE_CLOCK;
  if (strcmp(name, "settings") == 0) return UI_PAGE_SETTINGS;
  if (strcmp(name, "inbox") == 0) return UI_PAGE_INBOX;
  if (strcmp(name, "pomodoro") == 0 || strcmp(name, "timer") == 0) return UI_PAGE_POMODORO;
  if (strcmp(name, "weather") == 0) return UI_PAGE_WEATHER;
  if (strcmp(name, "apps") == 0 || strcmp(name, "app_launcher") == 0 || strcmp(name, "launcher") == 0) return UI_PAGE_APP_LAUNCHER;
  if (strcmp(name, "photo") == 0 || strcmp(name, "photos") == 0) return UI_PAGE_PHOTO_FRAME;
  if (strcmp(name, "music") == 0 || strcmp(name, "audio") == 0) return UI_PAGE_AUDIO_PLAYER;
  if (strcmp(name, "video") == 0 || strcmp(name, "videos") == 0) return UI_PAGE_VIDEO_PLAYER;
  if (strcmp(name, "voice") == 0) return UI_PAGE_VOICE;
  return -1;
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
      resetSdUploadSession(true);
      setWsStatus("WS: disconnected");
      if (voiceMicStreaming) {
        setVoiceMicStreaming(false, "WS disconnected", false);
      } else if (voiceStatusLabel != nullptr) {
        lv_label_set_text(voiceStatusLabel, "WS disconnected");
        lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
      }
      pushInboxMessage("alert", "WebSocket", "Connection lost");
      setAppLauncherStatus("WS disconnected", lv_color_hex(0xEF5350), true, 2800);
      break;

    case WStype_CONNECTED:
      Serial.println("[WebSocket] connected");
      isConnected = true;
      setWsStatus("WS: connected");
      if (voiceStatusLabel != nullptr) {
        lv_label_set_text(voiceStatusLabel, "WS connected");
        lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x80CBC4), LV_PART_MAIN);
      }
      pushInboxMessage("event", "WebSocket", "Connected to server");
      sendHandshake();

      // Request weather data immediately after connection
      fetchWeatherData();

      // Request app list
      requestAppList();

      // Request photo settings
      requestPhotoFrameSettings(true);
      sendPhotoFrameState("ws_connected", true);
      break;

    case WStype_BIN: {
      if (!sdUploadSession.active || !sdUploadSession.waitingBinary) {
        Serial.printf("[SD upload] unexpected binary frame len=%u\n", (unsigned)length);
        break;
      }

      const char *uploadId = sdUploadSession.uploadId;
      const int seq = sdUploadSession.pendingSeq;
      bool success = true;
      const char *reason = "";

      if ((int)length != sdUploadSession.pendingLen) {
        success = false;
        reason = "binary length mismatch";
      } else if (sdUploadSession.receivedSize + (uint32_t)length > sdUploadSession.expectedSize) {
        success = false;
        reason = "size overflow";
      } else {
        size_t written = sdUploadSession.file.write(payload, length);
        if (written != length) {
          success = false;
          reason = "sd write failed";
        } else {
          sdUploadSession.receivedSize += (uint32_t)written;
          sdUploadSession.expectedSeq++;
          sdUploadSession.waitingBinary = false;
          sdUploadSession.pendingLen = 0;
          sdUploadSession.pendingSeq = -1;
          if ((sdUploadSession.expectedSeq % 8) == 0) {
            sdUploadSession.file.flush();
          }
        }
      }

      sendSdUploadChunkAck(uploadId, seq, success, reason);
      if (!success) {
        Serial.printf("[SD upload] chunk failed: %s\n", reason);
        resetSdUploadSession(true);
      }
      break;
    }

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
      } else if (strcmp(messageType, "launch_app_response") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool success = data["success"] | false;
        const char *message = data["message"] | "";
        const char *reason = data["reason"] | "";
        const char *appPath = data["appPath"] | "";
        const char *appNameRaw = data["appName"] | "";

        char appName[48];
        if (appNameRaw[0] != '\0') {
          copyText(appName, sizeof(appName), appNameRaw);
        } else if (appPath[0] != '\0') {
          const char *base = strrchr(appPath, '/');
          base = (base == nullptr) ? appPath : (base + 1);
          copyText(appName, sizeof(appName), base);
          size_t len = strlen(appName);
          if (len > 4 && strcmp(appName + len - 4, ".app") == 0) {
            appName[len - 4] = '\0';
          }
        } else {
          copyText(appName, sizeof(appName), "App");
        }

        char detail[120];
        if (success) {
          snprintf(detail, sizeof(detail), "Opened: %s", appName);
          setAppLauncherStatus(detail, lv_color_hex(0x81C784), true, 2400);
          pushInboxMessage("event", "App launch OK", detail);
        } else {
          const char *errorText = (reason[0] != '\0') ? reason : ((message[0] != '\0') ? message : "Unknown error");
          snprintf(detail, sizeof(detail), "Launch failed: %s", errorText);
          setAppLauncherStatus(detail, lv_color_hex(0xEF5350), true, 4800);
          pushInboxMessage("alert", "App launch failed", detail);
        }
      } else if (strcmp(messageType, "voice_stream_ack") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *streamId = data["streamId"] | "";
        bool success = data["success"] | false;
        const char *status = data["status"] | "";
        const char *reason = data["reason"] | "";
        const char *text = data["text"] | "";
        bool isFinal = data["isFinal"] | false;

        if (streamId[0] != '\0' && voiceActiveStreamId[0] != '\0' && strcmp(streamId, voiceActiveStreamId) != 0) {
          break;
        }

        if (success && strcmp(status, "ready") == 0) {
          voiceStreamStartAcked = true;
        } else if (strcmp(status, "accepted") == 0) {
          voiceStreamStartAcked = false;
        }

        if (!success) {
          const char *errorText = (reason[0] != '\0') ? reason : "voice stream error";
          if (voiceStatusLabel != nullptr) {
            lv_label_set_text_fmt(voiceStatusLabel, "Mic error: %s", errorText);
            lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
          }
          if (voiceMicStreaming) {
            setVoiceMicStreaming(false, errorText, false);
          }
        } else if (voiceStatusLabel != nullptr) {
          if (strcmp(status, "accepted") == 0) {
            lv_label_set_text(voiceStatusLabel, "Mic stream accepted");
          } else if (strcmp(status, "ready") == 0) {
            lv_label_set_text(voiceStatusLabel, "Mic stream ready");
          } else if (strcmp(status, "stopped") == 0) {
            lv_label_set_text(voiceStatusLabel, "Mic stopped");
          }
          lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x81C784), LV_PART_MAIN);
        }

        if (text[0] != '\0' && voiceResultLabel != nullptr) {
          lv_label_set_text_fmt(voiceResultLabel, "ASR: %s", text);
          if (isFinal) {
            pushInboxMessage("event", "Voice transcript", text);
          }
        }
      } else if (strcmp(messageType, "voice_stream_chunk_ack") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool success = data["success"] | false;
        int seq = data["seq"] | -1;
        const char *reason = data["reason"] | "";

        if (!success) {
          const char *errorText = (reason[0] != '\0') ? reason : "chunk upload failed";
          if (voiceStatusLabel != nullptr) {
            lv_label_set_text_fmt(voiceStatusLabel, "Mic chunk err: %s", errorText);
            lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0xEF5350), LV_PART_MAIN);
          }
          if (voiceMicStreaming) {
            setVoiceMicStreaming(false, errorText, false);
          }
        } else if (voiceStatusLabel != nullptr && seq >= 0 && (seq % 24) == 0) {
          lv_label_set_text_fmt(voiceStatusLabel, "Mic streaming (%d)", seq);
          lv_obj_set_style_text_color(voiceStatusLabel, lv_color_hex(0x81C784), LV_PART_MAIN);
        }
      } else if (strcmp(messageType, "voice_command_result") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool success = data["success"] | false;
        const char *action = data["action"] | "unknown";
        const char *page = data["page"] | "";
        const char *message = data["message"] | "";
        const char *reason = data["reason"] | "";
        const char *command = data["command"] | "";

        char statusLine[120];
        if (success) {
          snprintf(statusLine, sizeof(statusLine), "Voice OK: %s", action);
        } else {
          const char *why = (reason[0] != '\0') ? reason : "command failed";
          snprintf(statusLine, sizeof(statusLine), "Voice failed: %s", why);
        }

        if (voiceStatusLabel != nullptr) {
          lv_label_set_text(voiceStatusLabel, statusLine);
          lv_obj_set_style_text_color(
            voiceStatusLabel,
            success ? lv_color_hex(0x81C784) : lv_color_hex(0xEF5350),
            LV_PART_MAIN
          );
        }

        if (voiceResultLabel != nullptr) {
          const char *resultText = (message[0] != '\0') ? message : statusLine;
          lv_label_set_text_fmt(voiceResultLabel, "Result: %s", resultText);
        }

        if (success && strcmp(action, "navigate") == 0 && page[0] != '\0') {
          int targetPage = parseUiPageFromVoiceName(page);
          if (targetPage >= 0 && targetPage < UI_PAGE_COUNT) {
            showPage(targetPage);
          }
        }

        const char *inboxTitle = success ? "Voice command OK" : "Voice command failed";
        const char *inboxBody = (message[0] != '\0') ? message : ((reason[0] != '\0') ? reason : command);
        pushInboxMessage(success ? "event" : "alert", inboxTitle, inboxBody);
      } else if (
        strcmp(messageType, "app_launched") == 0 ||
        strcmp(messageType, "command_result") == 0
      ) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        bool success = data["success"] | false;
        const char *title = success ? "Command success" : "Command failed";
        const char *body = data["message"] | data["appName"] | data["reason"] | messageType;
        pushInboxMessage(success ? "event" : "alert", title, body);
      } else if (strcmp(messageType, "weather_data") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        currentWeather.temperature = data["temperature"] | 0.0f;
        currentWeather.feelsLike = data["feelsLike"] | 0.0f;
        currentWeather.humidity = data["humidity"] | 0;
        const char *condition = data["condition"] | "Unknown";
        const char *city = data["city"] | "Beijing";
        snprintf(currentWeather.condition, sizeof(currentWeather.condition), "%s", condition);
        snprintf(currentWeather.city, sizeof(currentWeather.city), "%s", city);
        currentWeather.valid = true;
        lastWeatherUpdateMs = millis();
        Serial.printf("[Weather] Received: %.1f°C, %s, %d%%\n",
                     currentWeather.temperature,
                     currentWeather.condition,
                     currentWeather.humidity);
        updateWeatherDisplay();
      } else if (strcmp(messageType, "app_list") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        JsonArrayConst apps = data["apps"].as<JsonArrayConst>();

        appCount = 0;
        for (JsonObjectConst app : apps) {
          if (appCount >= 12) break; // Max 12 apps

          const char *name = app["name"] | "Unknown";
          const char *path = app["path"] | "";

          strncpy(appList[appCount].name, name, sizeof(appList[appCount].name) - 1);
          appList[appCount].name[sizeof(appList[appCount].name) - 1] = '\0';
          strncpy(appList[appCount].path, path, sizeof(appList[appCount].path) - 1);
          appList[appCount].path[sizeof(appList[appCount].path) - 1] = '\0';
          char letter = appList[appCount].name[0];
          if (letter >= 'a' && letter <= 'z') {
            letter = letter - 'a' + 'A';
          } else if (!((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9'))) {
            letter = '#';
          }
          appList[appCount].letter = letter;
          appList[appCount].color = getColorFromString(name);

          appCount++;
        }

        Serial.printf("[AppLauncher] Received %d apps\n", appCount);
        appPage = 0;
        setAppLauncherStatus("App list updated", lv_color_hex(0x9CCC65), true, 1600);
        updateAppLauncherDisplay();
      } else if (strcmp(messageType, "photo_settings") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        applyPhotoFrameSettings(data);
      } else if (strcmp(messageType, "photo_control") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        handlePhotoControlCommand(data);
      } else if (strcmp(messageType, "sd_list_request") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *requestId = data["requestId"] | "";
        int offset = data["offset"] | 0;
        int limit = data["limit"] | SD_BROWSER_RESPONSE_MAX_FILES;
        Serial.printf("[SD] list request: requestId=%s offset=%d limit=%d\n", requestId, offset, limit);
        sendSdListResponse(requestId, offset, limit);
      } else if (strcmp(messageType, "sd_preview_request") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *requestId = data["requestId"] | "";
        const char *targetPath = data["path"] | "";
        if (requestId == nullptr || requestId[0] == '\0' || targetPath == nullptr || targetPath[0] != '/') {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "invalid request/path");
          break;
        }
        if (!hasMjpegPlaybackExtension(targetPath)) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "only mjpeg/mjpg supported");
          break;
        }

        detectAndScanSdCard();
        if (!sdMounted) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "sd not mounted");
          break;
        }
        if (!SD_MMC.exists(targetPath)) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "file not found");
          break;
        }
        if (!ensureVideoFrameBuffer()) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "preview buffer OOM");
          break;
        }

        File previewFile = SD_MMC.open(targetPath, FILE_READ);
        if (!previewFile) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, "open failed");
          break;
        }

        size_t frameSize = 0;
        char reason[72];
        bool ok = readNextMjpegFrame(previewFile, videoFrameData, VIDEO_FRAME_MAX_BYTES, &frameSize, reason, sizeof(reason));
        previewFile.close();
        if (!ok || frameSize == 0) {
          sendSdPreviewResponse(requestId, targetPath, false, 0, reason[0] == '\0' ? "preview decode failed" : reason);
          break;
        }

        sendSdPreviewResponse(requestId, targetPath, true, (uint32_t)frameSize, "");
        webSocket.sendBIN(videoFrameData, frameSize);
        Serial.printf("[SD] preview sent requestId=%s path=%s bytes=%u\n", requestId, targetPath, (unsigned)frameSize);
      } else if (strcmp(messageType, "sd_delete_request") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *requestId = data["requestId"] | "";
        const char *targetPath = data["path"] | "";
        if (targetPath == nullptr || targetPath[0] != '/') {
          sendSdDeleteResponse(requestId, targetPath, false, "invalid path");
        } else {
          detectAndScanSdCard();
          if (!sdMounted) {
            sendSdDeleteResponse(requestId, targetPath, false, "sd not mounted");
          } else if (!SD_MMC.exists(targetPath)) {
            sendSdDeleteResponse(requestId, targetPath, false, "file not found");
          } else if (!SD_MMC.remove(targetPath)) {
            sendSdDeleteResponse(requestId, targetPath, false, "delete failed");
          } else {
            loadSdPhotoList();
            showCurrentPhotoFrame();
            loadSdAudioList();
            sendSdDeleteResponse(requestId, targetPath, true, "");
          }
        }
      } else if (strcmp(messageType, "sd_upload_begin") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *uploadId = data["uploadId"] | "";
        const char *targetPath = data["path"] | "";
        uint32_t expectedSize = data["size"] | 0;
        int chunkSize = data["chunkSize"] | 2048;
        bool overwrite = data["overwrite"] | false;

        if (uploadId[0] == '\0' || targetPath[0] != '/') {
          sendSdUploadBeginAck(uploadId, false, "invalid uploadId/path");
          break;
        }
        if (sdUploadSession.active) {
          sendSdUploadBeginAck(uploadId, false, "upload busy");
          break;
        }

        detectAndScanSdCard();
        if (!sdMounted) {
          sendSdUploadBeginAck(uploadId, false, "sd not mounted");
          break;
        }
        if (chunkSize <= 0 || chunkSize > 4096) {
          sendSdUploadBeginAck(uploadId, false, "invalid chunk size");
          break;
        }
        if (expectedSize == 0 || expectedSize > 50UL * 1024UL * 1024UL) {
          sendSdUploadBeginAck(uploadId, false, "invalid file size");
          break;
        }

        char reason[96];
        if (!ensureSdParentDirectories(targetPath, reason, sizeof(reason))) {
          sendSdUploadBeginAck(uploadId, false, reason);
          break;
        }

        char tempPath[208];
        if (snprintf(tempPath, sizeof(tempPath), "%s.uploadtmp", targetPath) >= (int)sizeof(tempPath)) {
          sendSdUploadBeginAck(uploadId, false, "temp path too long");
          break;
        }
        if (SD_MMC.exists(tempPath)) {
          SD_MMC.remove(tempPath);
        }

        if (SD_MMC.exists(targetPath)) {
          if (!overwrite) {
            sendSdUploadBeginAck(uploadId, false, "target exists");
            break;
          }
          if (!SD_MMC.remove(targetPath)) {
            sendSdUploadBeginAck(uploadId, false, "remove target failed");
            break;
          }
        }

        sdUploadSession.file = SD_MMC.open(tempPath, FILE_WRITE);
        if (!sdUploadSession.file) {
          sendSdUploadBeginAck(uploadId, false, "open temp failed");
          break;
        }

        sdUploadSession.active = true;
        sdUploadSession.waitingBinary = false;
        sdUploadSession.overwrite = overwrite;
        copyText(sdUploadSession.uploadId, sizeof(sdUploadSession.uploadId), uploadId);
        copyText(sdUploadSession.targetPath, sizeof(sdUploadSession.targetPath), targetPath);
        copyText(sdUploadSession.tempPath, sizeof(sdUploadSession.tempPath), tempPath);
        sdUploadSession.expectedSize = expectedSize;
        sdUploadSession.receivedSize = 0;
        sdUploadSession.expectedSeq = 0;
        sdUploadSession.pendingSeq = -1;
        sdUploadSession.pendingLen = 0;

        Serial.printf(
          "[SD upload] begin id=%s target=%s size=%u chunk=%d\n",
          sdUploadSession.uploadId,
          sdUploadSession.targetPath,
          (unsigned)sdUploadSession.expectedSize,
          chunkSize
        );
        sendSdUploadBeginAck(uploadId, true, "");
      } else if (strcmp(messageType, "sd_upload_chunk_meta") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *uploadId = data["uploadId"] | "";
        int seq = data["seq"] | -1;
        int len = data["len"] | 0;

        if (!sdUploadSession.active || strcmp(uploadId, sdUploadSession.uploadId) != 0) {
          sendSdUploadChunkAck(uploadId, seq, false, "upload not active");
          break;
        }
        if (sdUploadSession.waitingBinary) {
          sendSdUploadChunkAck(uploadId, seq, false, "binary pending");
          resetSdUploadSession(true);
          break;
        }
        if (seq != sdUploadSession.expectedSeq) {
          sendSdUploadChunkAck(uploadId, seq, false, "seq mismatch");
          resetSdUploadSession(true);
          break;
        }
        if (len <= 0 || len > 4096) {
          sendSdUploadChunkAck(uploadId, seq, false, "invalid chunk len");
          resetSdUploadSession(true);
          break;
        }
        if (sdUploadSession.receivedSize + (uint32_t)len > sdUploadSession.expectedSize) {
          sendSdUploadChunkAck(uploadId, seq, false, "chunk exceeds size");
          resetSdUploadSession(true);
          break;
        }

        sdUploadSession.waitingBinary = true;
        sdUploadSession.pendingSeq = seq;
        sdUploadSession.pendingLen = len;
      } else if (strcmp(messageType, "sd_upload_commit") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *uploadId = data["uploadId"] | "";
        uint32_t expectedSize = data["expectedSize"] | sdUploadSession.expectedSize;

        if (!sdUploadSession.active || strcmp(uploadId, sdUploadSession.uploadId) != 0) {
          sendSdUploadCommitAck(uploadId, false, "", "upload not active");
          break;
        }
        if (sdUploadSession.waitingBinary) {
          sendSdUploadCommitAck(uploadId, false, "", "chunk pending");
          resetSdUploadSession(true);
          break;
        }
        if (expectedSize != sdUploadSession.expectedSize || sdUploadSession.receivedSize != sdUploadSession.expectedSize) {
          sendSdUploadCommitAck(uploadId, false, "", "size mismatch");
          resetSdUploadSession(true);
          break;
        }

        sdUploadSession.file.flush();
        sdUploadSession.file.close();

        bool renamed = SD_MMC.rename(sdUploadSession.tempPath, sdUploadSession.targetPath);
        if (!renamed) {
          sendSdUploadCommitAck(uploadId, false, "", "rename failed");
          resetSdUploadSession(true);
          break;
        }

        sendSdUploadCommitAck(uploadId, true, sdUploadSession.targetPath, "");
        Serial.printf(
          "[SD upload] commit ok id=%s path=%s size=%u\n",
          sdUploadSession.uploadId,
          sdUploadSession.targetPath,
          (unsigned)sdUploadSession.receivedSize
        );
        resetSdUploadSession(false);
        loadSdPhotoList();
        showCurrentPhotoFrame();
        loadSdAudioList();
      } else if (strcmp(messageType, "sd_upload_abort") == 0) {
        JsonObjectConst data = doc["data"].as<JsonObjectConst>();
        const char *uploadId = data["uploadId"] | "";
        if (sdUploadSession.active && strcmp(uploadId, sdUploadSession.uploadId) == 0) {
          Serial.printf("[SD upload] abort id=%s\n", uploadId);
          resetSdUploadSession(true);
        }
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
  resetSdUploadSession(false);
  detectAndScanSdCard();
  // Some cards need a short settle period right after power-on.
  if (!sdMounted) {
    delay(120);
    detectAndScanSdCard();
  }
  bool bootSplashShown = showBootSplashFromSd(1800);
  createUi();
  clearBootSplashOverlay();
  if (!bootSplashShown) {
    if (!sdMounted) {
      detectAndScanSdCard();
    }
    if (sdMounted) {
      Serial.println("[BootSplash] retry after UI init");
      bootSplashShown = showBootSplashFromSd(1200);
      clearBootSplashOverlay();
    }
  }
  refreshDynamicWallpaperSources();
  prepareDynamicWallpaperForPage(currentPage, true);
  if (sdMounted) {
    char body[128];
    snprintf(
      body,
      sizeof(body),
      "%s, D%lu/F%lu, %s",
      sdCardTypeToText(sdCardType),
      (unsigned long)sdRootDirCount,
      (unsigned long)sdRootFileCount,
      sdRootPreview
    );
    pushInboxMessage("event", "SD mounted", body);
  } else {
    char body[96];
    snprintf(body, sizeof(body), "status: %s", sdMountReason);
    pushInboxMessage("alert", "SD not mounted", body);
  }
  loadSdPhotoList();
  showCurrentPhotoFrame();
  loadSdAudioList();
  loadSdVideoList();

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

  // Schedule weather fetch (don't block startup)
  Serial.println("Weather fetch scheduled...");
}

void loop() {
  webSocket.loop();
  processPendingAction();
  processVoiceMicStreaming();

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

  if (currentPage == UI_PAGE_PHOTO_FRAME) {
    requestPhotoFrameSettings(false);
    processPhotoFrameAutoPlay();
  }

  if (isConnected && (millis() - lastPhotoStateReportMs) >= PHOTO_STATE_REPORT_INTERVAL_MS) {
    sendPhotoFrameState("periodic", true);
  }

  lv_timer_handler();
  processDynamicWallpapers();
  processPendingVideoControl();
  processVideoPlayback();
  processPendingAudioControl();
  processAudioPlayback();
  bool mediaBusy = (isAudioRunning() && !audioPaused) || (videoPlaying && !videoPaused);
  delay(mediaBusy ? 1 : 5);
}
