#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <heltec-eink-modules.h>
#include "mbedtls/sha256.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/rtc_io.h"
#include "qrcode.h"
#include "src/quotes.h"
#include "src/timezone_support.h"
#include "src/config.h"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>

#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_SECRETS 1
#else
#define HAS_SECRETS 0
#endif

#ifndef ENABLE_TEST_HISTORY
#define ENABLE_TEST_HISTORY 0
#endif

#ifndef FORCE_TEST_HISTORY_ON_EVERY_BOOT
#define FORCE_TEST_HISTORY_ON_EVERY_BOOT 0
#endif

#ifndef USE_SECRETS_BOOTSTRAP
#define USE_SECRETS_BOOTSTRAP 0
#endif

#ifndef FREEDOM_WIFI_SSID
#define FREEDOM_WIFI_SSID ""
#endif

#ifndef FREEDOM_WIFI_PASS
#define FREEDOM_WIFI_PASS ""
#endif

#ifndef FREEDOM_MQTT_SERVER
#define FREEDOM_MQTT_SERVER DEFAULT_MQTT_SERVER
#endif

#ifndef FREEDOM_MQTT_PORT
#define FREEDOM_MQTT_PORT DEFAULT_MQTT_PORT
#endif

#ifndef FREEDOM_MQTT_USER
#define FREEDOM_MQTT_USER DEFAULT_MQTT_USER
#endif

#ifndef FREEDOM_MQTT_PASS
#define FREEDOM_MQTT_PASS DEFAULT_MQTT_PASS
#endif

#ifndef ENABLE_DEVELOPER_STATS
#define ENABLE_DEVELOPER_STATS 0
#endif

// ============================================================
// RTC persisted data (survives deep sleep)
// ============================================================
RTC_DATA_ATTR char lastPriceValue[VALUE_BUFFER_SIZE] = "--";
RTC_DATA_ATTR char lastBalanceBtc[VALUE_BUFFER_SIZE] = "--";
RTC_DATA_ATTR time_t lastKnownUnixTime = 0;
RTC_DATA_ATTR time_t lastBtcCacheUnixTime = 0;

static constexpr uint16_t WEALTH_HISTORY_DAYS = 366;
static constexpr uint8_t BATTERY_LOG_SAMPLES = 64;
static constexpr int32_t WEALTH_HISTORY_EMPTY = INT32_MIN;
static constexpr int32_t PRICE_HISTORY_EMPTY = INT32_MIN;
static constexpr int64_t BALANCE_HISTORY_EMPTY = INT64_MIN;
RTC_DATA_ATTR uint32_t setupPinFailedAttempts = 0;
RTC_DATA_ATTR time_t setupPinLockedUntil = 0;
RTC_DATA_ATTR bool rtcFactoryResetPending = false;
RTC_DATA_ATTR bool rtcPortalSaveRestartPending = false;

// ============================================================
// Globals
// ============================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
FreedomClockDisplay display;
WebServer portalServer(CONFIG_PORTAL_PORT);
DNSServer portalDnsServer;
Preferences preferences;

class TrustedWiFiClientSecure : public WiFiClientSecure {
public:
  bool useDefaultCertificateBundle() {
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    attach_ssl_certificate_bundle(sslclient.get(), true);
    _use_ca_bundle = true;
    _use_insecure = false;
    return true;
#else
    return false;
#endif
  }
};

static volatile bool gotPrice = false;
static volatile bool gotBalance = false;

static char priceValueBuf[VALUE_BUFFER_SIZE] = "";
static char balanceBtcBuf[VALUE_BUFFER_SIZE] = "";
static char deviceId[DEVICE_ID_SIZE] = "";
static char portalApSsid[AP_SSID_SIZE] = "";
static char portalApPassword[AP_PASSWORD_SIZE] = "";
static char portalApStartFailureDetail[96] = "";
static bool portalSaveRequested = false;
enum PortalExitAction {
  PORTAL_EXIT_ACTION_NONE = 0,
  PORTAL_EXIT_ACTION_SAVE_CONFIG = 1,
  PORTAL_EXIT_ACTION_FIRMWARE_UPDATE = 2,
  PORTAL_EXIT_ACTION_FACTORY_RESET = 3,
  PORTAL_EXIT_ACTION_APP_SCREEN = 4
};
static PortalExitAction portalExitAction = PORTAL_EXIT_ACTION_NONE;
static char portalSessionMessage[120] = "";
static bool portalSessionMessageIsError = false;
static bool portalUnlocked = false;
static time_t portalBootUnixBase = 0;
static uint32_t portalUnlockedAtMs = 0;
static bool portalFirmwareUploadAuthorized = false;
static bool portalFirmwareUploadStarted = false;
static bool portalFirmwareUploadFailed = false;
static bool portalFirmwareUploadSucceeded = false;
static char portalFirmwareUploadMessage[PORTAL_FIRMWARE_MESSAGE_SIZE] = "";
static bool mqttValidationMode = false;
static char mqttValidationPriceTopic[MQTT_TOPIC_SIZE] = "";
static char mqttValidationBalanceTopic[MQTT_TOPIC_SIZE] = "";
static bool hardwareFlashEncryptionActive = false;
static bool hardwareFlashEncryptionReleaseMode = false;
static bool hardwareSecureBootActive = false;
static bool hardwareNvsKeysPartitionPresent = false;
static bool secureNvsActive = false;

struct LifeStats {
  int yearsLeft;           // when pastExpectancy: extra years beyond expectancy
  int monthsLeft;          // when pastExpectancy: extra months (remainder)
  int weeksLeftRemainder;  // when pastExpectancy: extra weeks (remainder)
  int totalWeeks;
  int remainingWeeks;      // when pastExpectancy: total extra weeks beyond expectancy
  int remainingPercent;
  bool pastExpectancy;     // true when current date is past the expected death date
};

struct DisplayThemeColors {
  uint16_t foreground;
  uint16_t background;
};

struct DeviceConfig {
  bool configured;
  char ownerName[OWNER_NAME_SIZE];
  int birthYear;
  int lifeExpectancyYears;
  float monthlyExpenseValue;
  float inflationAnnual;
  float wealthGrowthAnnual;
  float defaultWealthValue;
  float manualBtcAmount;
  char manualBtcAmountText[MANUAL_BTC_TEXT_SIZE];
  float borrowFeeAnnual;
  uint8_t assetMode;
  uint8_t portfolioUseMode;
  uint8_t currencyCode;
  uint8_t displayThemeMode;
  uint16_t refreshIntervalMinutes;
  bool setupPinEnabled;
  char setupPinHash[SETUP_PIN_HASH_HEX_SIZE];
  char wifiSsid[WIFI_SSID_SIZE];
  char wifiPass[WIFI_PASS_SIZE];
  char mqttServer[MQTT_SERVER_SIZE];
  uint16_t mqttPort;
  char mqttUser[MQTT_USER_SIZE];
  char mqttPass[MQTT_PASS_SIZE];
  char topicPriceValue[MQTT_TOPIC_SIZE];
  char topicBalanceBtc[MQTT_TOPIC_SIZE];
  bool autoUpdateEnabled;
  uint8_t dailyWakeHour;    // 0-23; 255 = no scheduled wake, use interval
  uint8_t dailyWakeMinute;  // 0-59
  uint8_t dailyWakeTimeZone;
  bool quoteOfDayEnabled;
  uint8_t timeDisplayFormat; // 0=standard (YMW), 1=compact (weeks for small values)
  bool showBatteryPercent;   // false=icon only at far right, true=icon+number
  bool showWealthChangeScreen;
  bool showSettingsScreen;
};

#include "src/screen_model.h"

struct WealthHistory {
  uint32_t latestDay;
  int32_t dailyWealthValue[WEALTH_HISTORY_DAYS];
  int32_t dailyPriceValue[WEALTH_HISTORY_DAYS];
  int64_t dailyBalanceSats[WEALTH_HISTORY_DAYS];
};

struct BatteryLog {
  uint32_t version;
  uint32_t nextSampleId;
  uint8_t count;
  uint8_t nextIndex;
  uint32_t sampleId[BATTERY_LOG_SAMPLES];
  uint32_t unixTime[BATTERY_LOG_SAMPLES];
  uint16_t millivolts[BATTERY_LOG_SAMPLES];
  uint8_t percent[BATTERY_LOG_SAMPLES];
};

struct GitHubReleaseInfo {
  String tagName;
  String name;
  String body;
  String htmlUrl;
  String openBinUrl;
  String secureBinUrl;
};

static DeviceConfig deviceConfig = {};
static WealthHistory wealthHistory = {};
static BatteryLog batteryLog = {};
static AppScreenId portalExitScreen = APP_SCREEN_MAIN;
static char lastConfigLoadStatus[CONFIG_LOAD_STATUS_SIZE] = "not loaded";
static uint32_t lastConfigStoredVersion = 0;
static bool lastConfigStoredOkFlag = false;

enum SetupBootAction {
  SETUP_BOOT_ACTION_NONE = 0,
  SETUP_BOOT_ACTION_PORTAL = 1,
  SETUP_BOOT_ACTION_FACTORY_RESET = 2
};

#include "src/diagnostics.h"

static bool connectWiFi(const DeviceConfig& cfg, uint16_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS, bool keepPortalAp = false);
static bool connectMQTT(const DeviceConfig& cfg, uint16_t timeout_ms = MQTT_CONNECT_TIMEOUT_MS);
static String buildPortalUnlockPage(const char* statusMessage, bool isError);
#include "src/setup_portal.h"

// ============================================================
#include "src/btc_data.h"

#include "src/ota.h"

#include "src/sleep_scheduler.h"

// ============================================================
// Setup
// ============================================================

static void setManualFinalRefreshWindow(AppScreenId selectedScreen, bool showNoWifiIcon) {
  (void)selectedScreen;
  (void)showNoWifiIcon;

  // Fastmode is the "partial refresh" path. Use the full display as the
  // refresh window to avoid controller-rounded window edges leaving stripes.
  setScreenRenderWindow(0, 0, DEVICE_DISPLAY_WIDTH, DEVICE_DISPLAY_HEIGHT);
}

static void drawSelectedAppScreen(
  AppScreenId selectedScreen,
  int selectedQuoteIndex,
  const DeviceConfig& cfg,
  time_t now,
  float wealthValue,
  float balanceBtc,
  float priceValue,
  int deviceBatteryPct,
  const WealthHistory& history,
  int freedomYears,
  int freedomMonths,
  int freedomWeeks,
  const LifeStats& lifeStats,
  DisplayThemeMode displayThemeMode,
  bool freedomHitCap,
  bool coveredInfinite,
  int coveredPercent,
  bool showFreedomDataWarning,
  bool freedomDataAvailable,
  bool showNoWifiIcon,
  bool externalDataPending = false
) {
  if (selectedScreen == APP_SCREEN_FREEDOM_CHANGE) {
    drawFreedomCheckinScreen(
      cfg,
      now,
      wealthValue,
      deviceBatteryPct,
      history,
      cfg.timeDisplayFormat,
      freedomDataAvailable,
      showNoWifiIcon,
      externalDataPending
    );
  } else if (selectedScreen == APP_SCREEN_WEALTH_CHANGE) {
    drawWealthStatsScreen(
      cfg,
      wealthValue,
      balanceBtc,
      priceValue,
      deviceBatteryPct,
      history,
      freedomDataAvailable,
      showNoWifiIcon,
      externalDataPending
    );
  } else if (selectedScreen == APP_SCREEN_SETTINGS) {
    drawInfoScreen(
      cfg,
      wealthValue,
      balanceBtc,
      priceValue,
      deviceBatteryPct,
      now,
      freedomDataAvailable,
      showNoWifiIcon,
      externalDataPending
    );
  } else if (selectedScreen == APP_SCREEN_QUOTE) {
    drawQuoteScreen(
      selectedQuoteIndex,
      freedomYears,
      freedomMonths,
      freedomWeeks,
      lifeStats,
      displayThemeMode,
      deviceBatteryPct,
      coveredPercent,
      cfg.timeDisplayFormat,
      freedomHitCap,
      cfg.showBatteryPercent,
      showFreedomDataWarning,
      freedomDataAvailable,
      showNoWifiIcon,
      externalDataPending
    );
  } else {
    drawFreedomClock(
      cfg.ownerName,
      displayThemeMode,
      freedomHitCap,
      freedomYears,
      freedomMonths,
      freedomWeeks,
      lifeStats,
      coveredInfinite,
      coveredPercent,
      deviceBatteryPct,
      cfg.timeDisplayFormat,
      cfg.showBatteryPercent,
      showFreedomDataWarning,
      freedomDataAvailable,
      showNoWifiIcon,
      externalDataPending
    );
  }
}

void setup() {
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  esp_reset_reason_t resetReason = esp_reset_reason();
  const uint64_t ext1WakeStatus = esp_sleep_get_ext1_wakeup_status();

  Serial.begin(115200);
  delay(30);
  Serial.println();
  Serial.println("[FreedomClock] Boot start");
  Serial.printf(
    "[FreedomClock] wake=%d (%s), reset=%d (%s), ext1=0x%llX\n",
    (int)wakeCause,
    wakeCauseText(wakeCause),
    (int)resetReason,
    resetReasonText(resetReason),
    (unsigned long long)ext1WakeStatus
  );

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  rtc_gpio_deinit((gpio_num_t)PIN_FUNCTION_BUTTON);
  rtc_gpio_deinit((gpio_num_t)PIN_SETUP_BUTTON);
  pinMode(PIN_FUNCTION_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);

  SetupBootAction bootAction = detectSetupBootAction(wakeCause);
  const bool functionButtonWake = wasFunctionButtonWake(wakeCause);
  const bool setupButtonWake = wasSetupButtonWake(wakeCause);
  const uint8_t wakeExtraPressCount = functionButtonWake
    ? detectAdditionalWakePresses(2, 900)
    : 0;
  saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "boot-start");

  pinMode(PIN_EINK_POWER, OUTPUT);
  digitalWrite(PIN_EINK_POWER, HIGH);
  delay(EINK_POWER_UP_DELAY_MS);

  display.begin();
  buildDeviceId(deviceId, sizeof(deviceId));
  initializeEncryptedNvsIfAvailable();
  portalSessionMessage[0] = '\0';
  portalSessionMessageIsError = false;

  float vbat = readBatteryVoltage();
  int pct = batteryPercentFromVoltage(vbat);
#if ENABLE_DEVELOPER_STATS
  loadBatteryLog(batteryLog);
  if (appendBatteryLogSample(batteryLog, lastKnownUnixTime, vbat, pct)) {
    saveBatteryLog(batteryLog);
  }
#else
  loadBatteryLog(batteryLog);
#endif

  if (bootAction == SETUP_BOOT_ACTION_FACTORY_RESET) {
    saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "factory-reset");
    factoryResetAndShowWelcome();
  }

  bool configLoaded = loadDeviceConfig(deviceConfig);
  loadCachedBtcData(deviceConfig);
  loadWealthHistory(wealthHistory);
  const bool postSaveRestartPending = takePostSaveRestartPending();
  if (postSaveRestartPending) {
    savePortalDiagnostics(
      "post-save-restart-boot",
      configLoaded ? "forcing normal app screen after settings save" : "post-save flag set but config did not load",
      configLoaded
    );
    if (configLoaded) {
      bootAction = SETUP_BOOT_ACTION_NONE;
      saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "post-save-app-screen", configLoaded, true);
    } else {
      snprintf(
        portalSessionMessage,
        sizeof(portalSessionMessage),
        "Settings were saved, but could not be loaded after restart: %s. Please review and save again.",
        lastConfigLoadStatus
      );
      portalSessionMessageIsError = true;
      markWelcomeShown();
      saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "post-save-config-load-failed", false, true);
    }
  }
  if (!configLoaded) {
#ifdef SKIP_WELCOME_SCREEN
    const bool welcomeShown = true;
#else
    const bool welcomeShown = hasWelcomeBeenShown();
#endif
    if (welcomeShown) {
      if (rtcFactoryResetPending) {
        rtcFactoryResetPending = false;
      }
      saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "setup-portal-unconfigured", false, welcomeShown);
      runConfigurationPortal();
    } else {
      saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "welcome-screen", false, welcomeShown);
      markWelcomeShown();
      drawWelcomeScreen();
      goToWelcomeSleep();
    }
  }
  if (bootAction == SETUP_BOOT_ACTION_PORTAL) {
    saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "setup-portal-button", configLoaded, true);
    runConfigurationPortal();
  }
  saveBootPathDiagnostics(wakeCause, resetReason, ext1WakeStatus, bootAction, "app-screen-flow", configLoaded, true);

  const bool homeButtonReset = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED
    && configLoaded
    && bootAction == SETUP_BOOT_ACTION_NONE
    && resetReason != ESP_RST_SW;
  const bool portalAppScreenExit = portalExitAction == PORTAL_EXIT_ACTION_APP_SCREEN;
  const AppScreenId selectedScreen = portalAppScreenExit
    ? portalExitScreen
    : selectWakeButtonScreen(functionButtonWake, wakeExtraPressCount, deviceConfig);
  const AssetMode assetMode = sanitizeAssetMode(deviceConfig.assetMode);
  const PortfolioUseMode portfolioUseMode = sanitizePortfolioUseMode(deviceConfig.portfolioUseMode);
  const DisplayThemeMode displayThemeMode = sanitizeThemeMode(deviceConfig.displayThemeMode);
  const bool selectedQuoteScreen = selectedScreen == APP_SCREEN_QUOTE;
  const int selectedQuoteIndex = selectedQuoteScreen ? selectNextQuote() : -1;
  const bool manualScreenRefresh = functionButtonWake || homeButtonReset || portalAppScreenExit;
  const bool manualExternalDataRefresh = manualScreenRefresh && isAnyBtcAssetMode(assetMode);

  gotPrice = false;
  gotBalance = false;
  priceValueBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';
  bool manualFastModeActive = false;
  bool pendingFrameDrawn = false;
  bool pendingFrameFastMode = false;
  bool finalFrameFastMode = false;
  bool finalFrameWindowed = false;
  int finalFrameWindowX = 0;
  int finalFrameWindowY = 0;
  int finalFrameWindowW = DEVICE_DISPLAY_WIDTH;
  int finalFrameWindowH = DEVICE_DISPLAY_HEIGHT;

  if (manualExternalDataRefresh) {
    LifeStats pendingLifeStats = {};
    const time_t pendingNow = estimateCurrentTime(false, wakeCause, deviceConfig.refreshIntervalMinutes);
    computeLifeStats(deviceConfig.birthYear, deviceConfig.lifeExpectancyYears, pendingNow, pendingLifeStats);
    clearScreenRenderWindow();
    drawSelectedAppScreen(
      selectedScreen,
      selectedQuoteIndex,
      deviceConfig,
      pendingNow,
      0.0f,
      0.0f,
      0.0f,
      pct,
      wealthHistory,
      0,
      0,
      0,
      pendingLifeStats,
      displayThemeMode,
      false,
      false,
      0,
      false,
      false,
      false,
      true
    );
    pendingFrameDrawn = true;
  }

  const bool shouldRefreshExternalData = true;
  bool wifiOK = shouldRefreshExternalData ? connectWiFi(deviceConfig) : false;
  bool ntpSynced = false;
  if (wifiOK) {
    ntpSynced = syncClockFromNtp();
  }

  // Auto-update: only on timer wake, not button press, not in portal mode
  if (wifiOK && ntpSynced && deviceConfig.autoUpdateEnabled
      && !wasButtonWake(wakeCause)
      && !homeButtonReset
      && bootAction == SETUP_BOOT_ACTION_NONE) {
    GitHubReleaseInfo autoReleaseInfo;
    char autoUpdateError[128];
    if (fetchGitHubInTask(autoReleaseInfo, autoUpdateError, sizeof(autoUpdateError))) {
      if (isReleaseNewerThanCurrent(autoReleaseInfo)) {
        String fwUrl;
        String fwLabel;
        if (selectReleaseFirmwareUrl(autoReleaseInfo, hardwareSecureBootActive, fwUrl, fwLabel)) {
          drawAutoUpdateScreen();
          char installError[128];
          if (installFirmwareInTask(fwUrl, installError, sizeof(installError))) {
            drawSetupPortalFirmwareUpdatedScreen();
            delay(2000);
            esp_restart();
          }
          // Install failed — continue with normal operation silently
        }
      }
    }
  }

  bool mqttOK = false;
  if (wifiOK && isMqttBtcAssetMode(assetMode)) {
    mqttOK = connectMQTT(deviceConfig);
  } else if (wifiOK && isManualBtcAssetMode(assetMode)) {
    char fetchError[96];
    float fetchedPrice = 0.0f;
    fetchCoinGeckoInTask(deviceConfig, fetchedPrice, fetchError, sizeof(fetchError));
  }

  if (mqttOK) {
    uint32_t start = millis();
    while (millis() - start < MQTT_WAIT_FOR_MESSAGES_MS) {
      mqttClient.loop();
      delay(MQTT_LOOP_DELAY_MS);
      if (gotPrice && gotBalance) break;
    }
  }

  if (isManualBtcAssetMode(assetMode)) {
    snprintf(balanceBtcBuf, sizeof(balanceBtcBuf), "%.8f", (double)deviceConfig.manualBtcAmount);
    gotBalance = true;
  }

  const bool allowCachedBtcInputs = isAnyBtcAssetMode(assetMode);
  const char* priceStr = gotPrice ? priceValueBuf : (allowCachedBtcInputs ? lastPriceValue : "--");
  const char* balanceStr = isManualBtcAssetMode(assetMode)
    ? balanceBtcBuf
    : (gotBalance ? balanceBtcBuf : (allowCachedBtcInputs ? lastBalanceBtc : "--"));

  float priceValue = parseFloatSafe(priceStr);
  float balanceBtc = parseFloatSafe(balanceStr);
  float parsedPrice = 0.0f;
  float parsedBalance = 0.0f;
  const bool hasValidPrice = parseNonNegativeFloatStrict(priceStr, parsedPrice, false);
  const bool hasValidBalance = parseNonNegativeFloatStrict(balanceStr, parsedBalance, true);
  const bool isBtcMode = isAnyBtcAssetMode(assetMode);
  const bool hasValidBtcInputs = isBtcMode && hasValidPrice && hasValidBalance;
  const bool hasFreshBtcInputs = isManualBtcAssetMode(assetMode)
    ? (gotPrice && hasValidBtcInputs)
    : (mqttOK && gotPrice && gotBalance && hasValidBtcInputs);
  const bool showFreedomDataWarning = isBtcMode && !hasFreshBtcInputs;
  const bool freedomDataAvailable = !isBtcMode || hasValidBtcInputs;
  const bool showNoWifiIcon = shouldRefreshExternalData && !wifiOK;

  float wealthValue = (assetMode == ASSET_MODE_WEALTH)
    ? deviceConfig.defaultWealthValue
    : (balanceBtc * priceValue);

  bool freedomHitCap = false;
  int years = 0;
  int months = 0;
  int weeks = 0;
  float coveredWeeks = 0.0f;
  computeLongevityWithInflation(
    wealthValue,
    deviceConfig.monthlyExpenseValue,
    deviceConfig.inflationAnnual,
    deviceConfig.wealthGrowthAnnual,
    portfolioUseMode,
    deviceConfig.borrowFeeAnnual,
    freedomHitCap,
    years,
    months,
    weeks,
    coveredWeeks
  );

  LifeStats lifeStats = {};
  const time_t now = estimateCurrentTime(ntpSynced, wakeCause, deviceConfig.refreshIntervalMinutes);
  computeLifeStats(deviceConfig.birthYear, deviceConfig.lifeExpectancyYears, now, lifeStats);

  const bool hasBtcBreakdown = isAnyBtcAssetMode(assetMode) && hasValidBtcInputs;
  if (freedomDataAvailable) {
    maybeSeedTestWealthHistory(wealthHistory, now, deviceConfig, wealthValue, hasBtcBreakdown, priceValue, balanceBtc);
    if (updateWealthHistory(wealthHistory, now, wealthValue, hasBtcBreakdown, priceValue, balanceBtc)) {
      saveWealthHistory(wealthHistory);
    }
  }

  bool coveredInfinite = freedomHitCap;
  int coveredPercent = 100;
  if (lifeStats.pastExpectancy) {
    // Past expectancy: compare fund against the originally planned total lifespan.
    // This answers "what % of my planned life can my wealth fund?" and can exceed 100%.
    if (lifeStats.totalWeeks > 0) {
      float coverageRatio = coveredWeeks / (float)lifeStats.totalWeeks;
      coveredPercent = clampInt((int)(coverageRatio * 100.0f + 0.5f), 0, 999);
    }
  } else if (lifeStats.remainingWeeks > 0) {
    float coverageRatio = coveredWeeks / (float)lifeStats.remainingWeeks;
    coveredPercent = clampInt((int)(coverageRatio * 100.0f + 0.5f), 0, 999);
  }

  if (manualScreenRefresh && !manualFastModeActive) {
    display.fastmodeOn(false);
    manualFastModeActive = true;
  }
  if (manualExternalDataRefresh && pendingFrameDrawn) {
    setManualFinalRefreshWindow(selectedScreen, showNoWifiIcon);
    finalFrameWindowed = true;
    finalFrameWindowX = screenRenderWindowX;
    finalFrameWindowY = screenRenderWindowY;
    finalFrameWindowW = screenRenderWindowW;
    finalFrameWindowH = screenRenderWindowH;
  } else {
    clearScreenRenderWindow();
  }
  finalFrameFastMode = manualScreenRefresh && manualFastModeActive;
  drawSelectedAppScreen(
    selectedScreen,
    selectedQuoteIndex,
    deviceConfig,
    now,
    wealthValue,
    balanceBtc,
    priceValue,
    pct,
    wealthHistory,
    years,
    months,
    weeks,
    lifeStats,
    displayThemeMode,
    freedomHitCap,
    coveredInfinite,
    coveredPercent,
    showFreedomDataWarning,
    freedomDataAvailable,
    showNoWifiIcon
  );
  clearScreenRenderWindow();
  if (manualFastModeActive) {
    display.fastmodeOff();
  }

  if (now >= 1700000000) {
    lastKnownUnixTime = now;
  }

  bool cachePriceUpdated = false;
  bool cacheBalanceUpdated = false;
  if (gotPrice && parseNonNegativeFloatStrict(priceValueBuf, parsedPrice, false)) {
    safeCopy(lastPriceValue, sizeof(lastPriceValue), priceValueBuf, strlen(priceValueBuf));
    cachePriceUpdated = true;
  }
  if (gotBalance && parseNonNegativeFloatStrict(balanceBtcBuf, parsedBalance, true)) {
    safeCopy(lastBalanceBtc, sizeof(lastBalanceBtc), balanceBtcBuf, strlen(balanceBtcBuf));
    cacheBalanceUpdated = true;
  }
  if (hasFreshBtcInputs) {
    saveCachedBtcData(cachePriceUpdated, cacheBalanceUpdated, now);
  }

  saveLastAppBootDiagnostics(
    wakeCause,
    resetReason,
    ext1WakeStatus,
    bootAction,
    configLoaded,
    functionButtonWake,
    setupButtonWake,
    homeButtonReset,
    portalAppScreenExit,
    selectedScreen,
    wakeExtraPressCount,
    assetMode,
    manualScreenRefresh,
    manualExternalDataRefresh,
    pendingFrameDrawn,
    pendingFrameFastMode,
    finalFrameFastMode,
    finalFrameWindowed,
    finalFrameWindowX,
    finalFrameWindowY,
    finalFrameWindowW,
    finalFrameWindowH,
    wifiOK,
    ntpSynced,
    mqttOK,
    gotPrice,
    gotBalance,
    hasValidBtcInputs,
    hasFreshBtcInputs,
    freedomDataAvailable
  );

  if (mqttClient.connected()) mqttClient.disconnect();
  WiFi.disconnect(true);

  goToSleep(
    deviceConfig.refreshIntervalMinutes,
    deviceConfig.dailyWakeHour,
    deviceConfig.dailyWakeMinute,
    deviceConfig.dailyWakeTimeZone,
    now
  );
}

void loop() {
  // not used
}
