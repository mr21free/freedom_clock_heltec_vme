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
#include "esp_partition.h"
#include "esp_flash_encrypt.h"
#include "esp_secure_boot.h"
#include "nvs_flash.h"
#include "driver/rtc_io.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>

#if __has_include("secrets.h")
#include "secrets.h"
#define FREEDOM_CLOCK_HAS_SECRETS 1
#else
#define FREEDOM_CLOCK_HAS_SECRETS 0
#endif

#ifndef FREEDOM_CLOCK_ENABLE_TEST_HISTORY
#define FREEDOM_CLOCK_ENABLE_TEST_HISTORY 0
#endif

#ifndef FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED
#define FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED 0
#endif

#ifndef FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP
#define FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP 0
#endif

// ============================================================
// Freedom Clock - Default Config
// ============================================================

static constexpr float DEFAULT_MONTHLY_EXP_USD = 10000.0f;
static constexpr float DEFAULT_INFLATION_ANNUAL = 0.02f;
static constexpr float DEFAULT_WEALTH_GROWTH_ANNUAL = 0.10f;
static constexpr float DEFAULT_WEALTH_USD = 1000000.0f;
static constexpr float DEFAULT_BORROW_FEE_ANNUAL = 0.08f;
static constexpr float DEFAULT_MANUAL_BTC_AMOUNT = 0.1f;

enum AssetMode {
  ASSET_MODE_BTC = 0,
  ASSET_MODE_WEALTH = 1,
  ASSET_MODE_BTC_MANUAL = 2
};

enum PortfolioUseMode {
  PORTFOLIO_USE_MODE_SELL = 0,
  PORTFOLIO_USE_MODE_BORROW = 1
};

enum DisplayThemeMode {
  DISPLAY_THEME_LIGHT = 0,
  DISPLAY_THEME_DARK = 1
};

enum RefreshIntervalUnit {
  REFRESH_INTERVAL_UNIT_MINUTES = 0,
  REFRESH_INTERVAL_UNIT_HOURS = 1,
  REFRESH_INTERVAL_UNIT_DAYS = 2
};

static constexpr AssetMode DEFAULT_ASSET_MODE = ASSET_MODE_BTC_MANUAL;
static constexpr PortfolioUseMode DEFAULT_PORTFOLIO_USE_MODE = PORTFOLIO_USE_MODE_SELL;
static constexpr DisplayThemeMode DEFAULT_DISPLAY_THEME_MODE = DISPLAY_THEME_DARK;

static constexpr char DEFAULT_OWNER_NAME[] = "OWNER";
static constexpr int DEFAULT_OWNER_BIRTH_YEAR = 1990;
static constexpr int DEFAULT_OWNER_LIFE_EXPECTANCY_YEARS = 85;

static constexpr char DEFAULT_MQTT_SERVER[] = "mqtt.local";
static constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
static constexpr char DEFAULT_MQTT_USER[] = "";
static constexpr char DEFAULT_MQTT_PASS[] = "";
static constexpr char DEFAULT_TOPIC_PRICE_USD[] = "home/bitcoin/price/usd";
static constexpr char DEFAULT_TOPIC_BALANCE_BTC[] = "home/bitcoin/wallets/total_btc";

static constexpr uint16_t DEFAULT_REFRESH_INTERVAL_MINUTES = 1440;
static constexpr uint16_t MIN_REFRESH_INTERVAL_MINUTES = 15;
static constexpr uint16_t MAX_REFRESH_INTERVAL_MINUTES = 10080;
static constexpr uint64_t MICROSECONDS_PER_MINUTE = 60ULL * 1000000ULL;

// ============================================================
// Hardware pins (Vision Master E213)
// ============================================================
static constexpr int PIN_EINK_POWER = 45;  // E-ink VCC enable
static constexpr int PIN_BAT_ADC = 7;      // VBAT_Read
static constexpr int PIN_ADC_CTRL = 46;    // ADC_Ctrl gate
static constexpr int PIN_USER_BUTTON = 21; // Custom side button

// Battery ADC constants
static constexpr float ADC_MAX = 4095.0f;
static constexpr float ADC_REF_V = 3.3f;
static constexpr float VBAT_SCALE = 4.9f;

// ============================================================
// General constants
// ============================================================
static constexpr size_t VALUE_BUFFER_SIZE = 16;
static constexpr size_t OWNER_NAME_SIZE = 24;
static constexpr size_t WIFI_SSID_SIZE = 33;
static constexpr size_t WIFI_PASS_SIZE = 65;
static constexpr size_t MQTT_SERVER_SIZE = 64;
static constexpr size_t MQTT_USER_SIZE = 64;
static constexpr size_t MQTT_PASS_SIZE = 64;
static constexpr size_t MQTT_TOPIC_SIZE = 96;
static constexpr size_t DEVICE_ID_SIZE = 7;
static constexpr size_t AP_SSID_SIZE = 32;
static constexpr size_t AP_PASSWORD_SIZE = 20;
static constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS = 5000;
static constexpr uint16_t MQTT_WAIT_FOR_MESSAGES_MS = 4000;
static constexpr uint16_t MQTT_LOOP_DELAY_MS = 10;
static constexpr uint16_t BATTERY_ADC_SETTLE_DELAY_MS = 5;
static constexpr uint16_t EINK_POWER_UP_DELAY_MS = 100;
static constexpr uint16_t WIFI_POLL_DELAY_MS = 250;
static constexpr uint16_t MQTT_RETRY_DELAY_MS = 500;
static constexpr uint16_t NTP_SYNC_TIMEOUT_MS = 10000;
static constexpr uint16_t PRICE_HTTP_TIMEOUT_MS = 8000;
static constexpr uint16_t BUTTON_POLL_DELAY_MS = 20;
static constexpr uint32_t CONFIG_BUTTON_HOLD_MS = 2500;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
static constexpr uint32_t SECOND_PRESS_WINDOW_MS = 700;
static constexpr uint32_t CONFIG_PORTAL_DELAY_MS = 10;
static constexpr uint32_t CONFIG_PORTAL_UNLOCK_TIMEOUT_MS = 120000;
static constexpr uint16_t CONFIG_PORTAL_PORT = 80;
static constexpr uint16_t CONFIG_DNS_PORT = 53;
static constexpr size_t PORTAL_FIRMWARE_MESSAGE_SIZE = 160;
static constexpr char CONFIG_NAMESPACE[] = "freedomclk";
static constexpr char HISTORY_NAMESPACE[] = "wealthhist";
static constexpr uint32_t CONFIG_VERSION = 1;
static constexpr uint32_t HISTORY_VERSION = 2;
static constexpr char FIRMWARE_VERSION[] = "2026.05.05.6";
static constexpr char GITHUB_RELEASES_URL[] = "https://github.com/mr21free/freedom_clock_heltec_vme213/releases";
static constexpr char MQTT_CLIENT_ID_PREFIX[] = "FreedomClock";
static constexpr char AP_SSID_PREFIX[] = "Freedom_Clock_";
static constexpr char AP_PASSWORD_PREFIX[] = "setup-";
static constexpr uint8_t SETUP_PIN_LENGTH = 6;
static constexpr size_t SETUP_PIN_HASH_HEX_SIZE = 65;
static constexpr uint16_t SETUP_PIN_HASH_ROUNDS = 2048;
static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";
static constexpr char COINGECKO_SIMPLE_PRICE_URL[] = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&precision=2";
static constexpr long NTP_GMT_OFFSET_SECONDS = 0;
static constexpr int NTP_DAYLIGHT_OFFSET_SECONDS = 0;
static const IPAddress CONFIG_AP_IP(192, 168, 4, 1);
static const IPAddress CONFIG_AP_GATEWAY(192, 168, 4, 1);
static const IPAddress CONFIG_AP_SUBNET(255, 255, 255, 0);

// ============================================================
// RTC persisted data (survives deep sleep)
// ============================================================
RTC_DATA_ATTR char lastPriceUsd[VALUE_BUFFER_SIZE] = "--";
RTC_DATA_ATTR char lastBalanceBtc[VALUE_BUFFER_SIZE] = "--";
RTC_DATA_ATTR time_t lastKnownUnixTime = 0;

static constexpr uint16_t WEALTH_HISTORY_DAYS = 366;
static constexpr int32_t WEALTH_HISTORY_EMPTY = INT32_MIN;
static constexpr int32_t PRICE_HISTORY_EMPTY = INT32_MIN;
static constexpr int64_t BALANCE_HISTORY_EMPTY = INT64_MIN;
RTC_DATA_ATTR uint32_t setupPinFailedAttempts = 0;
RTC_DATA_ATTR time_t setupPinLockedUntil = 0;

// ============================================================
// Globals
// ============================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
EInkDisplay_VisionMasterE213 display;
WebServer portalServer(CONFIG_PORTAL_PORT);
DNSServer portalDnsServer;
Preferences preferences;

static volatile bool gotPrice = false;
static volatile bool gotBalance = false;

static char priceUsdBuf[VALUE_BUFFER_SIZE] = "";
static char balanceBtcBuf[VALUE_BUFFER_SIZE] = "";
static char deviceId[DEVICE_ID_SIZE] = "";
static char portalApSsid[AP_SSID_SIZE] = "";
static char portalApPassword[AP_PASSWORD_SIZE] = "";
static bool portalSaveRequested = false;
enum PortalExitAction {
  PORTAL_EXIT_ACTION_NONE = 0,
  PORTAL_EXIT_ACTION_SAVE_CONFIG = 1,
  PORTAL_EXIT_ACTION_FIRMWARE_UPDATE = 2
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
  int yearsLeft;
  int monthsLeft;
  int weeksLeftRemainder;
  int totalWeeks;
  int remainingWeeks;
  int remainingPercent;
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
  float monthlyExpUsd;
  float inflationAnnual;
  float wealthGrowthAnnual;
  float defaultWealthUsd;
  float manualBtcAmount;
  float borrowFeeAnnual;
  uint8_t assetMode;
  uint8_t portfolioUseMode;
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
  char topicPriceUsd[MQTT_TOPIC_SIZE];
  char topicBalanceBtc[MQTT_TOPIC_SIZE];
};

struct WealthHistory {
  uint32_t latestDay;
  int32_t dailyWealthUsd[WEALTH_HISTORY_DAYS];
  int32_t dailyPriceUsd[WEALTH_HISTORY_DAYS];
  int64_t dailyBalanceSats[WEALTH_HISTORY_DAYS];
};

static DeviceConfig deviceConfig = {};
static WealthHistory wealthHistory = {};

enum SetupBootAction {
  SETUP_BOOT_ACTION_NONE = 0,
  SETUP_BOOT_ACTION_PORTAL = 1,
  SETUP_BOOT_ACTION_FACTORY_RESET = 2
};

static bool connectWiFi(const DeviceConfig& cfg, uint16_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS, bool keepPortalAp = false);
static bool connectMQTT(const DeviceConfig& cfg, uint16_t timeout_ms = MQTT_CONNECT_TIMEOUT_MS);
static String buildPortalUnlockPage(const char* statusMessage, bool isError);
static void portalSendHtml(const String& html);
static void refreshHardwareSecurityStatus();
static bool initializeEncryptedNvsIfAvailable();
static bool isPortalUnlockExpired();
static void refreshPortalUnlockSession();
static bool hasSetupPinConfigured(const DeviceConfig& cfg);
static void handlePortalFirmwareUpload();
static void handlePortalFirmwareUploadComplete();

// ============================================================
// Utilities
// ============================================================

static float clampNonNegative(float v) {
  return (v < 0.0f) ? 0.0f : v;
}

static uint32_t setupPinDelaySecondsForAttempt(uint32_t failedAttempts) {
  if (failedAttempts >= 7) return 300;
  if (failedAttempts >= 4) return 30;
  return 3;
}

static int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  if (!dst || dstSize == 0) return;
  size_t n = (srcLen < (dstSize - 1)) ? srcLen : (dstSize - 1);
  if (src && srcLen > 0) memcpy(dst, src, n);
  dst[n] = '\0';
}

static void safeCopyCString(char* dst, size_t dstSize, const char* src) {
  if (!src) {
    safeCopy(dst, dstSize, "", 0);
    return;
  }
  safeCopy(dst, dstSize, src, strlen(src));
}

static void safeCopyString(char* dst, size_t dstSize, const String& src) {
  safeCopy(dst, dstSize, src.c_str(), src.length());
}

static bool checkboxArgIsTrue(const String& rawValue) {
  return rawValue == "1" || rawValue == "true" || rawValue == "on" || rawValue == "yes";
}

static bool hasText(const char* s) {
  return s && s[0] != '\0';
}

static void uppercaseAsciiInPlace(char* s) {
  if (!s) return;
  for (size_t i = 0; s[i] != '\0'; i++) {
    s[i] = (char)toupper((unsigned char)s[i]);
  }
}

static bool isSixDigitPin(const char* pin) {
  if (!pin) return false;
  for (uint8_t i = 0; i < SETUP_PIN_LENGTH; i++) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }
  return pin[SETUP_PIN_LENGTH] == '\0';
}

static void bytesToHex(const uint8_t* src, size_t srcSize, char* dst, size_t dstSize) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  if (!dst || dstSize == 0) return;

  size_t maxBytes = (dstSize - 1) / 2;
  if (maxBytes > srcSize) maxBytes = srcSize;
  for (size_t i = 0; i < maxBytes; i++) {
    dst[i * 2] = HEX_DIGITS[(src[i] >> 4) & 0x0F];
    dst[i * 2 + 1] = HEX_DIGITS[src[i] & 0x0F];
  }
  dst[maxBytes * 2] = '\0';
}

static void computeSetupPinHash(const char* pin, char* outHashHex, size_t outHashHexSize) {
  if (!outHashHex || outHashHexSize == 0) return;
  outHashHex[0] = '\0';
  if (!isSixDigitPin(pin)) return;

  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);

  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)"FreedomClockPIN|", 17);
  mbedtls_sha256_update(&ctx, (const unsigned char*)deviceId, strlen(deviceId));
  mbedtls_sha256_update(&ctx, (const unsigned char*)"|", 1);
  mbedtls_sha256_update(&ctx, (const unsigned char*)pin, strlen(pin));
  mbedtls_sha256_finish(&ctx, digest);

  for (uint16_t round = 0; round < SETUP_PIN_HASH_ROUNDS; round++) {
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, digest, sizeof(digest));
    mbedtls_sha256_update(&ctx, (const unsigned char*)pin, strlen(pin));
    mbedtls_sha256_update(&ctx, (const unsigned char*)deviceId, strlen(deviceId));
    mbedtls_sha256_finish(&ctx, digest);
  }

  mbedtls_sha256_free(&ctx);
  bytesToHex(digest, sizeof(digest), outHashHex, outHashHexSize);
}

static float parseFloatSafe(const char* s) {
  if (!s || !*s) return 0.0f;
  char* end = nullptr;
  float v = strtof(s, &end);
  if (end == s) return 0.0f;
  return clampNonNegative(v);
}

static bool parseNonNegativeFloatStrict(const char* s, float& outValue, bool allowZero = true) {
  if (!s) return false;

  while (*s && isspace((unsigned char)*s)) s++;
  if (*s == '\0') return false;

  char* end = nullptr;
  float parsed = strtof(s, &end);
  if (end == s) return false;

  while (*end && isspace((unsigned char)*end)) end++;
  if (*end != '\0') return false;
  if (!(parsed >= 0.0f)) return false;
  if (!allowZero && !(parsed > 0.0f)) return false;

  outValue = parsed;
  return true;
}

static int estimateTextWidthSize1(const char* text) {
  if (!text) return 0;
  return (int)strlen(text) * 6;
}

static void formatCompactUsd(float usdValue, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;

  if (usdValue >= 1000000.0f) {
    snprintf(dst, dstSize, "%.2f mil", usdValue / 1000000.0f);
  } else if (usdValue >= 1000.0f) {
    snprintf(dst, dstSize, "%.2fk", usdValue / 1000.0f);
  } else {
    snprintf(dst, dstSize, "%.2f", usdValue);
  }
}

static void formatSignedCompactUsd(int32_t deltaUsd, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  char compact[16];
  const float absValue = (deltaUsd < 0) ? (float)(-(int64_t)deltaUsd) : (float)deltaUsd;
  formatCompactUsd(absValue, compact, sizeof(compact));
  snprintf(dst, dstSize, "%c%s", (deltaUsd < 0) ? '-' : '+', compact);
}

static void formatSignedCompactPrice(int32_t deltaUsd, char* dst, size_t dstSize) {
  formatSignedCompactUsd(deltaUsd, dst, dstSize);
}

static void formatSignedBtcDelta(int64_t deltaSats, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  const double btc = (double)deltaSats / 100000000.0;
  snprintf(dst, dstSize, "%+.4f", btc);
}

static void formatTrimmedBtcAmount(float btcValue, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  if (!(btcValue >= 0.0f)) {
    safeCopyCString(dst, dstSize, "0");
    return;
  }
  snprintf(dst, dstSize, "%.8f", (double)btcValue);
  size_t len = strlen(dst);
  while (len > 0 && dst[len - 1] == '0') {
    dst[len - 1] = '\0';
    len--;
  }
  if (len > 0 && dst[len - 1] == '.') {
    dst[len - 1] = '\0';
  }
}

static void clearWealthHistoryInMemory(WealthHistory& history) {
  history.latestDay = 0;
  for (uint16_t i = 0; i < WEALTH_HISTORY_DAYS; i++) {
    history.dailyWealthUsd[i] = WEALTH_HISTORY_EMPTY;
    history.dailyPriceUsd[i] = PRICE_HISTORY_EMPTY;
    history.dailyBalanceSats[i] = BALANCE_HISTORY_EMPTY;
  }
}

static bool loadWealthHistory(WealthHistory& history) {
  clearWealthHistoryInMemory(history);

  if (!preferences.begin(HISTORY_NAMESPACE, true)) {
    return false;
  }

  const uint32_t storedVersion = preferences.getUInt("ver", 0);
  if (storedVersion != HISTORY_VERSION) {
    preferences.end();
    return false;
  }

  history.latestDay = preferences.getUInt("day", 0);
  size_t wealthBytesRead = preferences.getBytes("wealth", history.dailyWealthUsd, sizeof(history.dailyWealthUsd));
  size_t priceBytesRead = preferences.getBytes("price", history.dailyPriceUsd, sizeof(history.dailyPriceUsd));
  size_t balanceBytesRead = preferences.getBytes("bal", history.dailyBalanceSats, sizeof(history.dailyBalanceSats));
  preferences.end();

  if (
    wealthBytesRead != sizeof(history.dailyWealthUsd) ||
    priceBytesRead != sizeof(history.dailyPriceUsd) ||
    balanceBytesRead != sizeof(history.dailyBalanceSats)
  ) {
    clearWealthHistoryInMemory(history);
    return false;
  }

  return history.latestDay > 0;
}

static bool saveWealthHistory(const WealthHistory& history) {
  if (!preferences.begin(HISTORY_NAMESPACE, false)) {
    return false;
  }

  preferences.putUInt("ver", HISTORY_VERSION);
  preferences.putUInt("day", history.latestDay);
  size_t wealthBytesWritten = preferences.putBytes("wealth", history.dailyWealthUsd, sizeof(history.dailyWealthUsd));
  size_t priceBytesWritten = preferences.putBytes("price", history.dailyPriceUsd, sizeof(history.dailyPriceUsd));
  size_t balanceBytesWritten = preferences.putBytes("bal", history.dailyBalanceSats, sizeof(history.dailyBalanceSats));
  preferences.end();
  return (
    wealthBytesWritten == sizeof(history.dailyWealthUsd) &&
    priceBytesWritten == sizeof(history.dailyPriceUsd) &&
    balanceBytesWritten == sizeof(history.dailyBalanceSats)
  );
}

static bool clearWealthHistory() {
  if (!preferences.begin(HISTORY_NAMESPACE, false)) {
    return false;
  }
  bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}

static bool updateWealthHistory(
  WealthHistory& history,
  time_t now,
  float wealthUsd,
  bool hasBtcBreakdown,
  float priceUsd,
  float balanceBtc
) {
  if (now <= 0 || !(wealthUsd >= 0.0f)) return false;

  const uint32_t currentDay = (uint32_t)((uint64_t)now / 86400ULL);
  const int32_t wealthRounded = (wealthUsd >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(wealthUsd + 0.5f);
  const int32_t priceRounded = (hasBtcBreakdown && priceUsd < (float)INT32_MAX)
    ? (int32_t)(priceUsd + 0.5f)
    : PRICE_HISTORY_EMPTY;
  const int64_t balanceSats = hasBtcBreakdown
    ? (int64_t)(balanceBtc * 100000000.0 + 0.5)
    : BALANCE_HISTORY_EMPTY;

  if (history.latestDay == 0) {
    clearWealthHistoryInMemory(history);
    history.latestDay = currentDay;
    history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
    history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1] = priceRounded;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
    return true;
  }

  if (currentDay <= history.latestDay) {
    int32_t& latest = history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1];
    int32_t& latestPrice = history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1];
    int64_t& latestBalance = history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1];
    if (latest == wealthRounded && latestPrice == priceRounded && latestBalance == balanceSats) return false;
    latest = wealthRounded;
    latestPrice = priceRounded;
    latestBalance = balanceSats;
    return true;
  }

  const uint32_t dayDelta = currentDay - history.latestDay;
  const int32_t previousLatest = history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1];
  const int32_t previousLatestPrice = history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1];
  const int64_t previousLatestBalance = history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1];

  if (dayDelta >= WEALTH_HISTORY_DAYS) {
    clearWealthHistoryInMemory(history);
    history.latestDay = currentDay;
    history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
    history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1] = priceRounded;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
    return true;
  }

  memmove(
    history.dailyWealthUsd,
    history.dailyWealthUsd + dayDelta,
    (WEALTH_HISTORY_DAYS - dayDelta) * sizeof(history.dailyWealthUsd[0])
  );
  memmove(
    history.dailyPriceUsd,
    history.dailyPriceUsd + dayDelta,
    (WEALTH_HISTORY_DAYS - dayDelta) * sizeof(history.dailyPriceUsd[0])
  );
  memmove(
    history.dailyBalanceSats,
    history.dailyBalanceSats + dayDelta,
    (WEALTH_HISTORY_DAYS - dayDelta) * sizeof(history.dailyBalanceSats[0])
  );

  const int32_t fillValue = (previousLatest == WEALTH_HISTORY_EMPTY) ? wealthRounded : previousLatest;
  const int32_t fillPriceValue = (previousLatestPrice == PRICE_HISTORY_EMPTY) ? priceRounded : previousLatestPrice;
  const int64_t fillBalanceValue = (previousLatestBalance == BALANCE_HISTORY_EMPTY) ? balanceSats : previousLatestBalance;
  for (uint32_t i = WEALTH_HISTORY_DAYS - dayDelta; i < (WEALTH_HISTORY_DAYS - 1); i++) {
    history.dailyWealthUsd[i] = fillValue;
    history.dailyPriceUsd[i] = (hasBtcBreakdown ? fillPriceValue : PRICE_HISTORY_EMPTY);
    history.dailyBalanceSats[i] = (hasBtcBreakdown ? fillBalanceValue : BALANCE_HISTORY_EMPTY);
  }
  history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
  history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1] = priceRounded;
  history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
  history.latestDay = currentDay;
  return true;
}

static bool getHistoricalWealthUsd(const WealthHistory& history, uint16_t daysAgo, int32_t& outWealthUsd) {
  if (history.latestDay == 0 || daysAgo >= WEALTH_HISTORY_DAYS) return false;

  const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
  if (index < 0) return false;

  const int32_t stored = history.dailyWealthUsd[index];
  if (stored == WEALTH_HISTORY_EMPTY) return false;

  outWealthUsd = stored;
  return true;
}

static bool getHistoricalPriceUsd(const WealthHistory& history, uint16_t daysAgo, int32_t& outPriceUsd) {
  if (history.latestDay == 0 || daysAgo >= WEALTH_HISTORY_DAYS) return false;

  const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
  if (index < 0) return false;

  const int32_t stored = history.dailyPriceUsd[index];
  if (stored == PRICE_HISTORY_EMPTY) return false;

  outPriceUsd = stored;
  return true;
}

static bool getHistoricalBalanceSats(const WealthHistory& history, uint16_t daysAgo, int64_t& outBalanceSats) {
  if (history.latestDay == 0 || daysAgo >= WEALTH_HISTORY_DAYS) return false;

  const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
  if (index < 0) return false;

  const int64_t stored = history.dailyBalanceSats[index];
  if (stored == BALANCE_HISTORY_EMPTY) return false;

  outBalanceSats = stored;
  return true;
}

static bool maybeSeedTestWealthHistory(
  WealthHistory& history,
  time_t now,
  const DeviceConfig& cfg,
  float currentWealthUsd,
  bool hasBtcBreakdown,
  float currentPriceUsd,
  float currentBalanceBtc
) {
#if FREEDOM_CLOCK_ENABLE_TEST_HISTORY
  if (now <= 0) return false;
  if (!FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED && history.latestDay != 0) return false;

  clearWealthHistoryInMemory(history);
  history.latestDay = (uint32_t)((uint64_t)now / 86400ULL);

  const AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  const bool seedBtcBreakdown = isAnyBtcAssetMode(assetMode);
  const float endingPriceUsd = (hasBtcBreakdown && currentPriceUsd > 0.0f) ? currentPriceUsd : 65000.0f;
  const float endingBalanceBtc = (hasBtcBreakdown && currentBalanceBtc > 0.0f) ? currentBalanceBtc : 1.2500f;
  const float endingWealthUsd = (currentWealthUsd > 0.0f)
    ? currentWealthUsd
    : (seedBtcBreakdown
        ? (endingPriceUsd * endingBalanceBtc)
        : ((cfg.defaultWealthUsd > 0.0f) ? cfg.defaultWealthUsd : DEFAULT_WEALTH_USD));

  for (uint16_t i = 0; i < WEALTH_HISTORY_DAYS; i++) {
    const float progress = (WEALTH_HISTORY_DAYS <= 1)
      ? 1.0f
      : ((float)i / (float)(WEALTH_HISTORY_DAYS - 1));

    if (seedBtcBreakdown) {
      const float priceWave = (float)((int)(i % 21) - 10) / 10.0f;
      const float balanceWave = (float)((int)(i % 35) - 17) / 17.0f;
      float seededPriceUsd = endingPriceUsd * (0.72f + (0.28f * progress));
      float seededBalanceBtc = endingBalanceBtc * (0.88f + (0.12f * progress));
      seededPriceUsd += endingPriceUsd * 0.03f * priceWave;
      seededBalanceBtc += endingBalanceBtc * 0.01f * balanceWave;
      seededBalanceBtc += endingBalanceBtc * 0.0025f * (float)(i / 45);
      if (!(seededPriceUsd > 0.0f)) seededPriceUsd = endingPriceUsd;
      if (!(seededBalanceBtc > 0.0f)) seededBalanceBtc = endingBalanceBtc;
      const float seededWealthUsd = seededPriceUsd * seededBalanceBtc;
      history.dailyPriceUsd[i] = (int32_t)(seededPriceUsd + 0.5f);
      history.dailyBalanceSats[i] = (int64_t)(seededBalanceBtc * 100000000.0 + 0.5);
      history.dailyWealthUsd[i] = (seededWealthUsd >= (float)INT32_MAX)
        ? INT32_MAX
        : (int32_t)(seededWealthUsd + 0.5f);
    } else {
      const float wealthWave = (float)((int)(i % 31) - 15) / 15.0f;
      float seededWealthUsd = endingWealthUsd * (0.76f + (0.24f * progress));
      seededWealthUsd += endingWealthUsd * 0.015f * wealthWave;
      if (!(seededWealthUsd >= 0.0f)) seededWealthUsd = endingWealthUsd;
      history.dailyWealthUsd[i] = (seededWealthUsd >= (float)INT32_MAX)
        ? INT32_MAX
        : (int32_t)(seededWealthUsd + 0.5f);
      history.dailyPriceUsd[i] = PRICE_HISTORY_EMPTY;
      history.dailyBalanceSats[i] = BALANCE_HISTORY_EMPTY;
    }
  }

  const uint16_t weeklyStartIndex = (WEALTH_HISTORY_DAYS > 8) ? (WEALTH_HISTORY_DAYS - 8) : 0;
  const uint16_t weeklySteps = (uint16_t)(WEALTH_HISTORY_DAYS - weeklyStartIndex - 1);
  if (weeklySteps > 0) {
    for (uint16_t i = weeklyStartIndex; i < WEALTH_HISTORY_DAYS; i++) {
      const float weeklyProgress = (float)(i - weeklyStartIndex) / (float)weeklySteps;
      if (seedBtcBreakdown) {
        const float weeklyPriceUsd = endingPriceUsd * (0.95f + (0.05f * weeklyProgress));
        const float weeklyBalanceBtc = endingBalanceBtc * (0.992f + (0.008f * weeklyProgress));
        const float weeklyWealthUsd = weeklyPriceUsd * weeklyBalanceBtc;
        history.dailyPriceUsd[i] = (int32_t)(weeklyPriceUsd + 0.5f);
        history.dailyBalanceSats[i] = (int64_t)(weeklyBalanceBtc * 100000000.0 + 0.5);
        history.dailyWealthUsd[i] = (weeklyWealthUsd >= (float)INT32_MAX)
          ? INT32_MAX
          : (int32_t)(weeklyWealthUsd + 0.5f);
      } else {
        const float weeklyWealthUsd = endingWealthUsd * (0.94f + (0.06f * weeklyProgress));
        history.dailyWealthUsd[i] = (weeklyWealthUsd >= (float)INT32_MAX)
          ? INT32_MAX
          : (int32_t)(weeklyWealthUsd + 0.5f);
      }
    }
  }

  if (seedBtcBreakdown) {
    history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1] = (int32_t)(endingPriceUsd + 0.5f);
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = (int64_t)(endingBalanceBtc * 100000000.0 + 0.5);
    const float exactEndingWealthUsd = endingPriceUsd * endingBalanceBtc;
    history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1] = (exactEndingWealthUsd >= (float)INT32_MAX)
      ? INT32_MAX
      : (int32_t)(exactEndingWealthUsd + 0.5f);
  } else {
    history.dailyWealthUsd[WEALTH_HISTORY_DAYS - 1] = (endingWealthUsd >= (float)INT32_MAX)
      ? INT32_MAX
      : (int32_t)(endingWealthUsd + 0.5f);
    history.dailyPriceUsd[WEALTH_HISTORY_DAYS - 1] = PRICE_HISTORY_EMPTY;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = BALANCE_HISTORY_EMPTY;
  }

  return saveWealthHistory(history);
#else
  (void)history;
  (void)now;
  (void)cfg;
  (void)currentWealthUsd;
  (void)hasBtcBreakdown;
  (void)currentPriceUsd;
  (void)currentBalanceBtc;
  return false;
#endif
}

static bool didHistorySourceChange(const DeviceConfig& oldCfg, const DeviceConfig& newCfg) {
  if (!oldCfg.configured) return false;

  const AssetMode oldMode = sanitizeAssetMode(oldCfg.assetMode);
  const AssetMode newMode = sanitizeAssetMode(newCfg.assetMode);
  if (oldMode != newMode) return true;

  if (isMqttBtcAssetMode(newMode)) {
    if (oldCfg.mqttPort != newCfg.mqttPort) return true;
    if (strcmp(oldCfg.mqttServer, newCfg.mqttServer) != 0) return true;
    if (strcmp(oldCfg.topicPriceUsd, newCfg.topicPriceUsd) != 0) return true;
    if (strcmp(oldCfg.topicBalanceBtc, newCfg.topicBalanceBtc) != 0) return true;
  }

  return false;
}

static DisplayThemeColors getDisplayThemeColors(DisplayThemeMode themeMode) {
  if (themeMode == DISPLAY_THEME_DARK) {
    return { WHITE, BLACK };
  }
  return { BLACK, WHITE };
}

static void prepareScreen(DisplayThemeMode themeMode) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  display.clear();
  display.setRotation(1);
  display.fillScreen(theme.background);
  display.setTextColor(theme.foreground, theme.background);
}

static DisplayThemeMode sanitizeThemeMode(uint8_t rawValue) {
  return (rawValue == DISPLAY_THEME_LIGHT) ? DISPLAY_THEME_LIGHT : DISPLAY_THEME_DARK;
}

static AssetMode sanitizeAssetMode(uint8_t rawValue) {
  if (rawValue == ASSET_MODE_WEALTH) return ASSET_MODE_WEALTH;
  if (rawValue == ASSET_MODE_BTC_MANUAL) return ASSET_MODE_BTC_MANUAL;
  return ASSET_MODE_BTC;
}

static bool isMqttBtcAssetMode(AssetMode assetMode) {
  return assetMode == ASSET_MODE_BTC;
}

static bool isManualBtcAssetMode(AssetMode assetMode) {
  return assetMode == ASSET_MODE_BTC_MANUAL;
}

static bool isAnyBtcAssetMode(AssetMode assetMode) {
  return isMqttBtcAssetMode(assetMode) || isManualBtcAssetMode(assetMode);
}

static PortfolioUseMode sanitizePortfolioUseMode(uint8_t rawValue) {
  return (rawValue == PORTFOLIO_USE_MODE_BORROW) ? PORTFOLIO_USE_MODE_BORROW : PORTFOLIO_USE_MODE_SELL;
}

static RefreshIntervalUnit preferredRefreshIntervalUnit(uint16_t minutes) {
  if (minutes >= 1440 && (minutes % 1440) == 0) return REFRESH_INTERVAL_UNIT_DAYS;
  if (minutes >= 60 && (minutes % 60) == 0) return REFRESH_INTERVAL_UNIT_HOURS;
  return REFRESH_INTERVAL_UNIT_MINUTES;
}

static uint16_t refreshIntervalUnitMultiplier(RefreshIntervalUnit unit) {
  switch (unit) {
    case REFRESH_INTERVAL_UNIT_DAYS:
      return 1440;
    case REFRESH_INTERVAL_UNIT_HOURS:
      return 60;
    case REFRESH_INTERVAL_UNIT_MINUTES:
    default:
      return 1;
  }
}

static uint16_t refreshIntervalDisplayValue(uint16_t minutes, RefreshIntervalUnit unit) {
  const uint16_t multiplier = refreshIntervalUnitMultiplier(unit);
  if (multiplier == 0) return minutes;
  const uint16_t value = (uint16_t)(minutes / multiplier);
  return value > 0 ? value : 1;
}

static RefreshIntervalUnit parseRefreshIntervalUnit(const String& rawUnit) {
  const int unitValue = rawUnit.toInt();
  if (unitValue == (int)REFRESH_INTERVAL_UNIT_DAYS) return REFRESH_INTERVAL_UNIT_DAYS;
  if (unitValue == (int)REFRESH_INTERVAL_UNIT_HOURS) return REFRESH_INTERVAL_UNIT_HOURS;
  return REFRESH_INTERVAL_UNIT_MINUTES;
}

static uint16_t refreshIntervalMinutesFromForm(uint16_t rawValue, RefreshIntervalUnit unit) {
  const uint32_t totalMinutes = (uint32_t)rawValue * (uint32_t)refreshIntervalUnitMultiplier(unit);
  if (totalMinutes > 65535U) return 65535U;
  return (uint16_t)totalMinutes;
}

static const char* themeModeLabel(DisplayThemeMode themeMode) {
  return (themeMode == DISPLAY_THEME_DARK) ? "DARK" : "LIGHT";
}

static const char* portfolioUseModeLabel(PortfolioUseMode useMode) {
  return (useMode == PORTFOLIO_USE_MODE_BORROW) ? "BORROW YEARLY" : "SELL MONTHLY";
}

static void buildDeviceId(char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  uint64_t chipId = ESP.getEfuseMac();
  unsigned long long shortId = (unsigned long long)(chipId & 0xFFFFFFULL);
  snprintf(dst, dstSize, "%06llX", shortId);
}

static void buildPortalCredentials() {
  buildDeviceId(deviceId, sizeof(deviceId));
  snprintf(portalApSsid, sizeof(portalApSsid), "%s%s", AP_SSID_PREFIX, deviceId);
  snprintf(portalApPassword, sizeof(portalApPassword), "%s%s", AP_PASSWORD_PREFIX, deviceId);
}

static void buildMqttClientId(char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  if (deviceId[0] == '\0') {
    buildDeviceId(deviceId, sizeof(deviceId));
  }
  snprintf(dst, dstSize, "%s-%s", MQTT_CLIENT_ID_PREFIX, deviceId);
}

static int batteryPercentFromVoltage(float v) {
  static constexpr int NUM_POINTS = 8;
  static constexpr float voltTable[NUM_POINTS] = { 3.20f, 3.30f, 3.60f, 3.75f, 3.85f, 3.95f, 4.05f, 4.15f };
  static constexpr int socTable[NUM_POINTS] = { 0, 2, 25, 50, 70, 90, 97, 100 };

  if (v <= voltTable[0]) return socTable[0];
  if (v >= voltTable[NUM_POINTS - 1]) return socTable[NUM_POINTS - 1];

  for (int i = 0; i < NUM_POINTS - 1; i++) {
    float v1 = voltTable[i];
    float v2 = voltTable[i + 1];
    if (v >= v1 && v <= v2) {
      int soc1 = socTable[i];
      int soc2 = socTable[i + 1];
      float t = (v - v1) / (v2 - v1);
      float soc = soc1 + t * (soc2 - soc1);
      if (soc < 0) soc = 0;
      if (soc > 100) soc = 100;
      return (int)(soc + 0.5f);
    }
  }
  return 0;
}

static float readBatteryVoltage() {
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, HIGH);
  delay(BATTERY_ADC_SETTLE_DELAY_MS);

  pinMode(PIN_BAT_ADC, INPUT);
  int raw = analogRead(PIN_BAT_ADC);

  digitalWrite(PIN_ADC_CTRL, LOW);

  float v_adc = (raw / ADC_MAX) * ADC_REF_V;
  float v_batt = v_adc * VBAT_SCALE;
  return v_batt;
}

static bool syncClockFromNtp(uint16_t timeout_ms = NTP_SYNC_TIMEOUT_MS) {
  configTime(NTP_GMT_OFFSET_SECONDS, NTP_DAYLIGHT_OFFSET_SECONDS, NTP_SERVER_1, NTP_SERVER_2);

  uint32_t start = millis();
  time_t now = time(nullptr);
  while (now < 1700000000 && (millis() - start) < timeout_ms) {
    delay(WIFI_POLL_DELAY_MS);
    now = time(nullptr);
  }
  return now >= 1700000000;
}

static time_t makeUtcDate(int year, int month, int day) {
  struct tm tmValue = {};
  tmValue.tm_year = year - 1900;
  tmValue.tm_mon = month - 1;
  tmValue.tm_mday = day;
  tmValue.tm_hour = 12;
  tmValue.tm_isdst = 0;
  return mktime(&tmValue);
}

static int monthFromBuildString(const char* monthStr) {
  if (!monthStr) return 1;
  if (strncmp(monthStr, "Jan", 3) == 0) return 1;
  if (strncmp(monthStr, "Feb", 3) == 0) return 2;
  if (strncmp(monthStr, "Mar", 3) == 0) return 3;
  if (strncmp(monthStr, "Apr", 3) == 0) return 4;
  if (strncmp(monthStr, "May", 3) == 0) return 5;
  if (strncmp(monthStr, "Jun", 3) == 0) return 6;
  if (strncmp(monthStr, "Jul", 3) == 0) return 7;
  if (strncmp(monthStr, "Aug", 3) == 0) return 8;
  if (strncmp(monthStr, "Sep", 3) == 0) return 9;
  if (strncmp(monthStr, "Oct", 3) == 0) return 10;
  if (strncmp(monthStr, "Nov", 3) == 0) return 11;
  if (strncmp(monthStr, "Dec", 3) == 0) return 12;
  return 1;
}

static time_t buildTimestamp() {
  const char* dateStr = __DATE__;
  int month = monthFromBuildString(dateStr);
  int day = atoi(dateStr + 4);
  int year = atoi(dateStr + 7);

  struct tm tmValue = {};
  tmValue.tm_year = year - 1900;
  tmValue.tm_mon = month - 1;
  tmValue.tm_mday = day;
  tmValue.tm_hour = atoi(__TIME__);
  tmValue.tm_min = atoi(__TIME__ + 3);
  tmValue.tm_sec = atoi(__TIME__ + 6);
  tmValue.tm_isdst = 0;
  return mktime(&tmValue);
}

static int calendarYearFromUnixTime(time_t when) {
  if (when <= 0) return 1970;
  struct tm tmValue = {};
  if (!gmtime_r(&when, &tmValue)) return 1970;
  return tmValue.tm_year + 1900;
}

static int currentAgeFromBirthYear(int birthYear, time_t now) {
  const int currentYear = calendarYearFromUnixTime(now);
  return clampInt(currentYear - birthYear, 0, 130);
}

static void refreshHardwareSecurityStatus() {
  hardwareFlashEncryptionActive = esp_flash_encryption_enabled();
  hardwareFlashEncryptionReleaseMode = hardwareFlashEncryptionActive && esp_flash_encryption_cfg_verify_release_mode();
  hardwareSecureBootActive = esp_secure_boot_enabled();
  hardwareNvsKeysPartitionPresent = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
    nullptr
  ) != nullptr;
}

static bool hardwareSecurityIsFull() {
  return hardwareFlashEncryptionReleaseMode && secureNvsActive && hardwareSecureBootActive;
}

static const char* hardwareSecurityLevelLabel() {
  if (hardwareSecurityIsFull()) return "FULL";
  if (hardwareFlashEncryptionActive || secureNvsActive || hardwareSecureBootActive) return "PARTIAL";
  return "OPEN";
}

static String hardwareSecurityMessage() {
  if (hardwareSecurityIsFull()) {
    return "Hardware security is fully enabled: flash encryption, encrypted NVS, and secure boot are active.";
  }
  if (hardwareFlashEncryptionActive && !hardwareFlashEncryptionReleaseMode) {
    return "Flash encryption is active in development mode. This is safer than open flash, but it is not production-grade yet.";
  }
  if (hardwareFlashEncryptionReleaseMode && secureNvsActive && !hardwareSecureBootActive) {
    return "Stored settings are protected at rest, but secure boot is still off. A physical attacker could still replace the firmware.";
  }
  if (hardwareSecureBootActive && !hardwareFlashEncryptionActive) {
    return "Secure boot is active, but flash storage is still readable. Stored settings are not protected at rest yet.";
  }
  if (hardwareFlashEncryptionActive && !secureNvsActive) {
    if (hardwareNvsKeysPartitionPresent) {
      return "Flash encryption is active, but encrypted NVS is not running. Stored settings are not fully hardened yet.";
    }
    return "Flash encryption is active, but the nvs_keys partition is missing, so encrypted NVS cannot start.";
  }
  return "Setup PIN slows casual access, but this board is not hardware-hardened yet. A physical attacker may still extract saved secrets.";
}

static esp_err_t reinitializePlainNvsStorage() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err == ESP_OK) {
      err = nvs_flash_init();
    }
  }
  return err;
}

static bool initializeEncryptedNvsIfAvailable() {
  refreshHardwareSecurityStatus();
  secureNvsActive = false;

  if (!hardwareFlashEncryptionActive || !hardwareNvsKeysPartitionPresent) {
    return false;
  }

  const esp_partition_t* nvsKeysPart = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
    nullptr
  );
  if (!nvsKeysPart) {
    refreshHardwareSecurityStatus();
    return false;
  }

  esp_err_t err = ESP_OK;
  nvs_sec_cfg_t nvsSecCfg = {};
  err = nvs_flash_read_security_cfg(nvsKeysPart, &nvsSecCfg);
  if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
    err = nvs_flash_generate_keys(nvsKeysPart, &nvsSecCfg);
  }

  if (err == ESP_OK) {
    nvs_flash_deinit();
    err = nvs_flash_secure_init(&nvsSecCfg);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      esp_err_t eraseErr = nvs_flash_erase();
      if (eraseErr == ESP_OK) {
        err = nvs_flash_secure_init(&nvsSecCfg);
      } else {
        err = eraseErr;
      }
    }
  }

  if (err == ESP_OK) {
    secureNvsActive = true;
    refreshHardwareSecurityStatus();
    return true;
  }

  nvs_flash_deinit();
  reinitializePlainNvsStorage();
  refreshHardwareSecurityStatus();
  return false;
}

static time_t estimateCurrentTime(bool ntpSynced, esp_sleep_wakeup_cause_t wakeCause, uint16_t refreshIntervalMinutes) {
  if (ntpSynced) {
    time_t now = time(nullptr);
    if (now >= 1700000000) return now;
  }

  if (lastKnownUnixTime >= 1700000000) {
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
      return lastKnownUnixTime + (time_t)((uint64_t)refreshIntervalMinutes * 60ULL);
    }
    return lastKnownUnixTime;
  }

  return buildTimestamp();
}

static time_t portalCurrentUnixTime() {
  time_t base = portalBootUnixBase;
  if (base < 1700000000) {
    base = (lastKnownUnixTime >= 1700000000) ? lastKnownUnixTime : buildTimestamp();
  }
  return base + (time_t)(millis() / 1000UL);
}

static void checkpointPortalUnixTime() {
  time_t now = portalCurrentUnixTime();
  if (now > lastKnownUnixTime) {
    lastKnownUnixTime = now;
  }
}

static bool isPortalUnlockExpired() {
  if (!portalUnlocked) return true;
  return (millis() - portalUnlockedAtMs) > CONFIG_PORTAL_UNLOCK_TIMEOUT_MS;
}

static void refreshPortalUnlockSession() {
  if (portalUnlocked) {
    portalUnlockedAtMs = millis();
  }
}

static bool requirePortalUnlock(const char* message = nullptr) {
  if (!hasSetupPinConfigured(deviceConfig)) {
    portalUnlocked = true;
    portalUnlockedAtMs = millis();
    return false;
  }

  if (isPortalUnlockExpired()) {
    portalUnlocked = false;
    portalUnlockedAtMs = 0;
  }

  if (!portalUnlocked) {
    portalSendHtml(buildPortalUnlockPage(message, message != nullptr));
    return true;
  }

  refreshPortalUnlockSession();
  return false;
}

static bool hasSetupPinConfigured(const DeviceConfig& cfg) {
  return cfg.setupPinEnabled && hasText(cfg.setupPinHash);
}

static uint32_t setupPinLockRemainingSeconds() {
  if (setupPinLockedUntil <= 0) return 0;
  const time_t now = portalCurrentUnixTime();
  if (setupPinLockedUntil <= now) return 0;
  return (uint32_t)(setupPinLockedUntil - now);
}

static void normalizeDeviceConfig(DeviceConfig& cfg) {
  if (!hasText(cfg.ownerName)) {
    safeCopyCString(cfg.ownerName, sizeof(cfg.ownerName), DEFAULT_OWNER_NAME);
  }
  uppercaseAsciiInPlace(cfg.ownerName);

  cfg.birthYear = clampInt(cfg.birthYear, 1900, 2100);
  cfg.lifeExpectancyYears = clampInt(cfg.lifeExpectancyYears, 1, 130);

  if (!(cfg.monthlyExpUsd >= 0.0f)) cfg.monthlyExpUsd = DEFAULT_MONTHLY_EXP_USD;
  if (!(cfg.inflationAnnual >= 0.0f)) cfg.inflationAnnual = DEFAULT_INFLATION_ANNUAL;
  if (!(cfg.wealthGrowthAnnual > -1.0f)) cfg.wealthGrowthAnnual = DEFAULT_WEALTH_GROWTH_ANNUAL;
  if (!(cfg.defaultWealthUsd >= 0.0f)) cfg.defaultWealthUsd = DEFAULT_WEALTH_USD;
  if (!(cfg.manualBtcAmount >= 0.0f)) cfg.manualBtcAmount = DEFAULT_MANUAL_BTC_AMOUNT;
  if (!(cfg.borrowFeeAnnual > -1.0f)) cfg.borrowFeeAnnual = DEFAULT_BORROW_FEE_ANNUAL;

  cfg.monthlyExpUsd = clampNonNegative(cfg.monthlyExpUsd);
  cfg.inflationAnnual = clampNonNegative(cfg.inflationAnnual);
  cfg.defaultWealthUsd = clampNonNegative(cfg.defaultWealthUsd);
  cfg.manualBtcAmount = clampNonNegative(cfg.manualBtcAmount);
  if (cfg.wealthGrowthAnnual < -0.99f) cfg.wealthGrowthAnnual = -0.99f;
  if (cfg.borrowFeeAnnual < -0.99f) cfg.borrowFeeAnnual = -0.99f;

  cfg.assetMode = (uint8_t)sanitizeAssetMode(cfg.assetMode);
  cfg.portfolioUseMode = (uint8_t)sanitizePortfolioUseMode(cfg.portfolioUseMode);
  cfg.displayThemeMode = (uint8_t)sanitizeThemeMode(cfg.displayThemeMode);
  cfg.refreshIntervalMinutes = (uint16_t)clampInt((int)cfg.refreshIntervalMinutes, MIN_REFRESH_INTERVAL_MINUTES, MAX_REFRESH_INTERVAL_MINUTES);

  if (cfg.mqttPort == 0) cfg.mqttPort = DEFAULT_MQTT_PORT;
}

static void applyDefaultDeviceConfig(DeviceConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.configured = false;
  safeCopyCString(cfg.ownerName, sizeof(cfg.ownerName), DEFAULT_OWNER_NAME);
  cfg.birthYear = DEFAULT_OWNER_BIRTH_YEAR;
  cfg.lifeExpectancyYears = DEFAULT_OWNER_LIFE_EXPECTANCY_YEARS;
  cfg.monthlyExpUsd = DEFAULT_MONTHLY_EXP_USD;
  cfg.inflationAnnual = DEFAULT_INFLATION_ANNUAL;
  cfg.wealthGrowthAnnual = DEFAULT_WEALTH_GROWTH_ANNUAL;
  cfg.defaultWealthUsd = DEFAULT_WEALTH_USD;
  cfg.manualBtcAmount = DEFAULT_MANUAL_BTC_AMOUNT;
  cfg.borrowFeeAnnual = DEFAULT_BORROW_FEE_ANNUAL;
  cfg.assetMode = (uint8_t)DEFAULT_ASSET_MODE;
  cfg.portfolioUseMode = (uint8_t)DEFAULT_PORTFOLIO_USE_MODE;
  cfg.displayThemeMode = (uint8_t)DEFAULT_DISPLAY_THEME_MODE;
  cfg.refreshIntervalMinutes = DEFAULT_REFRESH_INTERVAL_MINUTES;
  cfg.setupPinEnabled = false;
  cfg.mqttPort = DEFAULT_MQTT_PORT;
  safeCopyCString(cfg.mqttServer, sizeof(cfg.mqttServer), DEFAULT_MQTT_SERVER);
  safeCopyCString(cfg.mqttUser, sizeof(cfg.mqttUser), DEFAULT_MQTT_USER);
  safeCopyCString(cfg.mqttPass, sizeof(cfg.mqttPass), DEFAULT_MQTT_PASS);
  safeCopyCString(cfg.topicPriceUsd, sizeof(cfg.topicPriceUsd), DEFAULT_TOPIC_PRICE_USD);
  safeCopyCString(cfg.topicBalanceBtc, sizeof(cfg.topicBalanceBtc), DEFAULT_TOPIC_BALANCE_BTC);

#if FREEDOM_CLOCK_HAS_SECRETS && FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP
  safeCopyCString(cfg.wifiSsid, sizeof(cfg.wifiSsid), WIFI_SSID);
  safeCopyCString(cfg.wifiPass, sizeof(cfg.wifiPass), WIFI_PASS);
  safeCopyCString(cfg.mqttServer, sizeof(cfg.mqttServer), MQTT_SERVER);
  cfg.mqttPort = MQTT_PORT;
  safeCopyCString(cfg.mqttUser, sizeof(cfg.mqttUser), MQTT_USER);
  safeCopyCString(cfg.mqttPass, sizeof(cfg.mqttPass), MQTT_PASS);
#endif

  normalizeDeviceConfig(cfg);
}

static bool isDeviceConfigComplete(const DeviceConfig& cfg) {
  if (!cfg.configured) return false;
  if (!hasText(cfg.ownerName)) return false;
  if (cfg.birthYear < 1900 || cfg.birthYear > 2100) return false;
  if (cfg.lifeExpectancyYears <= 0) return false;
  if (cfg.monthlyExpUsd <= 0.0f) return false;

  AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  if (isMqttBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) return false;
    if (!hasText(cfg.mqttServer)) return false;
    if (cfg.mqttPort == 0) return false;
    if (!hasText(cfg.topicPriceUsd)) return false;
    if (!hasText(cfg.topicBalanceBtc)) return false;
  } else if (isManualBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) return false;
    if (!(cfg.manualBtcAmount > 0.0f)) return false;
  }
  return true;
}

static bool loadDeviceConfig(DeviceConfig& cfg) {
  applyDefaultDeviceConfig(cfg);

  if (!preferences.begin(CONFIG_NAMESPACE, true)) {
    return false;
  }

  uint32_t storedVersion = preferences.getUInt("cfg_ver", 0);
  if (storedVersion != CONFIG_VERSION) {
    preferences.end();
    return false;
  }

  cfg.configured = preferences.getBool("cfg_ok", false);
  preferences.getString("owner", cfg.ownerName, sizeof(cfg.ownerName));
  cfg.birthYear = (int)preferences.getUInt("birth", (uint32_t)cfg.birthYear);
  cfg.lifeExpectancyYears = (int)preferences.getUInt("lifeexp", (uint32_t)cfg.lifeExpectancyYears);
  cfg.monthlyExpUsd = preferences.getFloat("monthusd", cfg.monthlyExpUsd);
  cfg.inflationAnnual = preferences.getFloat("infl", cfg.inflationAnnual);
  cfg.wealthGrowthAnnual = preferences.getFloat("growth", cfg.wealthGrowthAnnual);
  cfg.defaultWealthUsd = preferences.getFloat("wealth", cfg.defaultWealthUsd);
  cfg.manualBtcAmount = preferences.getFloat("manbtc", cfg.manualBtcAmount);
  cfg.borrowFeeAnnual = preferences.getFloat("borrowfee", cfg.borrowFeeAnnual);
  cfg.assetMode = (uint8_t)preferences.getUInt("asset", cfg.assetMode);
  cfg.portfolioUseMode = (uint8_t)preferences.getUInt("usemode", cfg.portfolioUseMode);
  cfg.displayThemeMode = (uint8_t)preferences.getUInt("theme", cfg.displayThemeMode);
  cfg.refreshIntervalMinutes = (uint16_t)preferences.getUInt("sleepmin", cfg.refreshIntervalMinutes);
  cfg.setupPinEnabled = preferences.getBool("pin_on", cfg.setupPinEnabled);
  preferences.getString("pin_hash", cfg.setupPinHash, sizeof(cfg.setupPinHash));
  preferences.getString("wifissid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  preferences.getString("wifipass", cfg.wifiPass, sizeof(cfg.wifiPass));
  preferences.getString("mqttsrv", cfg.mqttServer, sizeof(cfg.mqttServer));
  cfg.mqttPort = (uint16_t)preferences.getUInt("mqttport", cfg.mqttPort);
  preferences.getString("mqttuser", cfg.mqttUser, sizeof(cfg.mqttUser));
  preferences.getString("mqttpass", cfg.mqttPass, sizeof(cfg.mqttPass));
  preferences.getString("topicprice", cfg.topicPriceUsd, sizeof(cfg.topicPriceUsd));
  preferences.getString("topicbal", cfg.topicBalanceBtc, sizeof(cfg.topicBalanceBtc));
  setupPinFailedAttempts = preferences.getUInt("pin_fail", setupPinFailedAttempts);
  setupPinLockedUntil = (time_t)preferences.getUInt("pin_lock", (uint32_t)setupPinLockedUntil);
  preferences.end();

  normalizeDeviceConfig(cfg);
  return isDeviceConfigComplete(cfg);
}

static bool saveDeviceConfig(const DeviceConfig& cfg) {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    return false;
  }

  preferences.clear();
  preferences.putUInt("cfg_ver", CONFIG_VERSION);
  preferences.putBool("cfg_ok", true);
  preferences.putString("owner", cfg.ownerName);
  preferences.putUInt("birth", (uint32_t)cfg.birthYear);
  preferences.putUInt("lifeexp", (uint32_t)cfg.lifeExpectancyYears);
  preferences.putFloat("monthusd", cfg.monthlyExpUsd);
  preferences.putFloat("infl", cfg.inflationAnnual);
  preferences.putFloat("growth", cfg.wealthGrowthAnnual);
  preferences.putFloat("wealth", cfg.defaultWealthUsd);
  preferences.putFloat("manbtc", cfg.manualBtcAmount);
  preferences.putFloat("borrowfee", cfg.borrowFeeAnnual);
  preferences.putUInt("asset", (uint32_t)cfg.assetMode);
  preferences.putUInt("usemode", (uint32_t)cfg.portfolioUseMode);
  preferences.putUInt("theme", (uint32_t)cfg.displayThemeMode);
  preferences.putUInt("sleepmin", (uint32_t)cfg.refreshIntervalMinutes);
  preferences.putBool("pin_on", cfg.setupPinEnabled);
  preferences.putString("pin_hash", cfg.setupPinHash);
  preferences.putUInt("pin_fail", 0);
  preferences.putUInt("pin_lock", 0);
  preferences.putString("wifissid", cfg.wifiSsid);
  preferences.putString("wifipass", cfg.wifiPass);
  preferences.putString("mqttsrv", cfg.mqttServer);
  preferences.putUInt("mqttport", (uint32_t)cfg.mqttPort);
  preferences.putString("mqttuser", cfg.mqttUser);
  preferences.putString("mqttpass", cfg.mqttPass);
  preferences.putString("topicprice", cfg.topicPriceUsd);
  preferences.putString("topicbal", cfg.topicBalanceBtc);
  preferences.end();
  return true;
}

static bool saveSetupPinThrottleState() {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    return false;
  }
  preferences.putUInt("pin_fail", setupPinFailedAttempts);
  preferences.putUInt("pin_lock", (uint32_t)setupPinLockedUntil);
  preferences.end();
  return true;
}

static bool clearSavedDeviceConfig() {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    return false;
  }
  bool cleared = preferences.clear();
  preferences.end();
  setupPinFailedAttempts = 0;
  setupPinLockedUntil = 0;
  return cleared;
}

static bool validateDeviceConfig(const DeviceConfig& cfg, char* errorBuf, size_t errorBufSize) {
  if (!hasText(cfg.ownerName)) {
    snprintf(errorBuf, errorBufSize, "Owner name is required.");
    return false;
  }
  if (cfg.birthYear < 1900 || cfg.birthYear > 2100) {
    snprintf(errorBuf, errorBufSize, "Birth year must be between 1900 and 2100.");
    return false;
  }
  if (cfg.lifeExpectancyYears <= 0 || cfg.lifeExpectancyYears > 130) {
    snprintf(errorBuf, errorBufSize, "Life expectancy must be between 1 and 130 years.");
    return false;
  }
  if (cfg.monthlyExpUsd <= 0.0f) {
    snprintf(errorBuf, errorBufSize, "Monthly expenses must be greater than zero.");
    return false;
  }
  if (cfg.refreshIntervalMinutes < MIN_REFRESH_INTERVAL_MINUTES || cfg.refreshIntervalMinutes > MAX_REFRESH_INTERVAL_MINUTES) {
    snprintf(errorBuf, errorBufSize, "Refresh interval must be between 15 minutes and 7 days.");
    return false;
  }

  AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  if (isMqttBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) {
      snprintf(errorBuf, errorBufSize, "Wi-Fi SSID is required in BTC via MQTT mode.");
      return false;
    }
    if (!hasText(cfg.mqttServer)) {
      snprintf(errorBuf, errorBufSize, "MQTT server is required in BTC via MQTT mode.");
      return false;
    }
    if (cfg.mqttPort == 0) {
      snprintf(errorBuf, errorBufSize, "MQTT port must be greater than zero.");
      return false;
    }
    if (!hasText(cfg.topicPriceUsd) || !hasText(cfg.topicBalanceBtc)) {
      snprintf(errorBuf, errorBufSize, "BTC via MQTT mode requires both MQTT topics.");
      return false;
    }
  } else if (isManualBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) {
      snprintf(errorBuf, errorBufSize, "Wi-Fi SSID is required in static BTC mode.");
      return false;
    }
    if (!(cfg.manualBtcAmount > 0.0f)) {
      snprintf(errorBuf, errorBufSize, "Static BTC amount must be greater than zero.");
      return false;
    }
  }

  errorBuf[0] = '\0';
  return true;
}

static bool validatePortalPinSettings(
  const DeviceConfig& currentCfg,
  bool requestedPinEnabled,
  const String& requestedPin,
  const String& requestedPinConfirm,
  char* errorBuf,
  size_t errorBufSize
) {
  if (!requestedPinEnabled) {
    errorBuf[0] = '\0';
    return true;
  }

  const bool hasExistingPin = hasSetupPinConfigured(currentCfg);
  const bool pinEntered = requestedPin.length() > 0 || requestedPinConfirm.length() > 0;

  if (!hasExistingPin && !pinEntered) {
    snprintf(errorBuf, errorBufSize, "Enter and confirm a new 6-digit setup PIN.");
    return false;
  }

  if (!pinEntered) {
    errorBuf[0] = '\0';
    return true;
  }

  if (requestedPin != requestedPinConfirm) {
    snprintf(errorBuf, errorBufSize, "Setup PIN and confirmation must match.");
    return false;
  }

  char pinBuf[SETUP_PIN_LENGTH + 1];
  safeCopyString(pinBuf, sizeof(pinBuf), requestedPin);
  if (!isSixDigitPin(pinBuf)) {
    snprintf(errorBuf, errorBufSize, "Setup PIN must be exactly 6 digits.");
    return false;
  }

  errorBuf[0] = '\0';
  return true;
}

static void applyPortalPinSettings(
  DeviceConfig& submitted,
  const DeviceConfig& currentCfg,
  bool requestedPinEnabled,
  const String& requestedPin
) {
  if (!requestedPinEnabled) {
    submitted.setupPinEnabled = false;
    submitted.setupPinHash[0] = '\0';
    return;
  }

  submitted.setupPinEnabled = true;
  if (requestedPin.length() > 0) {
    char pinBuf[SETUP_PIN_LENGTH + 1];
    safeCopyString(pinBuf, sizeof(pinBuf), requestedPin);
    computeSetupPinHash(pinBuf, submitted.setupPinHash, sizeof(submitted.setupPinHash));
    return;
  }

  safeCopyCString(submitted.setupPinHash, sizeof(submitted.setupPinHash), currentCfg.setupPinHash);
}

static void resetPortalFirmwareUploadState() {
  portalFirmwareUploadAuthorized = false;
  portalFirmwareUploadStarted = false;
  portalFirmwareUploadFailed = false;
  portalFirmwareUploadSucceeded = false;
  portalFirmwareUploadMessage[0] = '\0';
}

static void setPortalFirmwareUploadFailure(const char* message) {
  portalFirmwareUploadFailed = true;
  portalFirmwareUploadSucceeded = false;
  safeCopyCString(portalFirmwareUploadMessage, sizeof(portalFirmwareUploadMessage), message ? message : "Firmware upload failed.");
}

static void formatFirmwareUpdateError(char* buffer, size_t bufferSize) {
  snprintf(
    buffer,
    bufferSize,
    "Firmware upload failed (Update error %u).",
    (unsigned int)Update.getError()
  );
}

static bool portalUploadSessionAllowed() {
  if (!hasSetupPinConfigured(deviceConfig)) {
    portalUnlocked = true;
    portalUnlockedAtMs = millis();
    return true;
  }

  if (isPortalUnlockExpired()) {
    portalUnlocked = false;
    portalUnlockedAtMs = 0;
  }

  if (!portalUnlocked) {
    return false;
  }

  refreshPortalUnlockSession();
  return true;
}

static String htmlEscape(const char* src) {
  if (!src) return String();

  String escaped;
  escaped.reserve(strlen(src) + 16);
  for (size_t i = 0; src[i] != '\0'; i++) {
    switch (src[i]) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped += src[i]; break;
    }
  }
  return escaped;
}

static String jsonEscape(const char* src) {
  if (!src) return String();

  String escaped;
  escaped.reserve(strlen(src) + 16);
  for (size_t i = 0; src[i] != '\0'; i++) {
    switch (src[i]) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += src[i]; break;
    }
  }
  return escaped;
}

static String selectedAttr(bool selected) {
  return selected ? " selected" : "";
}

static String formatFloatForInput(float value, uint8_t decimals = 2) {
  return String((double)value, (unsigned int)decimals);
}

static String buildPortalConfirmationPage(const char* title, const char* message) {
  String html;
  html.reserve(1600);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Freedom Clock Setup</title>";
  html += "<style>";
  html += "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".panel{width:min(520px,100%);background:#fff;border-radius:22px;padding:28px;box-shadow:0 18px 40px rgba(18,18,18,.12);text-align:center;}";
  html += "h1{margin:0 0 12px;font-size:30px;}";
  html += "p{margin:0;font-size:16px;line-height:1.55;color:#4e463d;}";
  html += "</style></head><body><main class=\"panel\">";
  html += "<h1>";
  html += htmlEscape(title ? title : "Saved");
  html += "</h1><p>";
  html += htmlEscape(message ? message : "The device is restarting now.");
  html += "</p>";
  html += "</main></body></html>";
  return html;
}

static String buildPortalUnlockPage(const char* statusMessage = nullptr, bool isError = false) {
  const uint32_t remainingSeconds = setupPinLockRemainingSeconds();
  const String securityMessage = hardwareSecurityMessage();
  String html;
  html.reserve(6200);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Freedom Clock Setup Locked</title>";
  html += "<style>";
  html += "body{margin:0;min-height:100vh;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:560px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".hero h1{margin:0 0 8px;font-size:30px;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".meta{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px;}";
  html += ".pill{background:#f7931a;padding:8px 12px;border-radius:999px;font-size:13px;color:#2b1700;}";
  html += ".card{margin-top:22px;background:#fff;border-radius:18px;padding:20px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input{width:100%;box-sizing:border-box;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:18px;background:#fdfbf6;color:#171717;letter-spacing:.18em;text-align:center;}";
  html += ".hint{font-size:12px;color:#6b6258;margin-top:8px;line-height:1.45;}";
  html += ".message{padding:14px 16px;border-radius:14px;font-size:14px;line-height:1.45;white-space:pre-line;margin-top:18px;}";
  html += ".ok{background:#e8f7ea;color:#1d5d2d;}";
  html += ".err{background:#fdeaea;color:#8d2020;}";
  html += ".info{background:#eee7db;color:#5c5349;}";
  html += ".actions{display:flex;gap:12px;align-items:center;margin-top:16px;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:15px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;}";
  html += "button[disabled],input[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "@media (max-width:640px){.hero h1{font-size:25px;}}";
  html += "</style></head><body><div class=\"wrap\">";
  html += "<section class=\"hero\"><h1>Freedom Clock Setup</h1>";
  html += "<p>Setup access is protected by a 6-digit PIN. Enter it to view or change saved settings. The normal clock screens stay unlocked.</p>";
  html += "<div class=\"meta\">";
  html += String("<div class=\"pill\">Device ID: ") + htmlEscape(deviceId) + "</div>";
  html += String("<div class=\"pill\">Setup Wi-Fi: ") + htmlEscape(portalApSsid) + "</div>";
  html += String("<div class=\"pill\">Security: ") + hardwareSecurityLevelLabel() + "</div>";
  html += "<div class=\"pill\">Portal: http://192.168.4.1</div>";
  html += "</div></section>";
  html += "<div class=\"message ";
  html += hardwareSecurityIsFull() ? "ok" : "info";
  html += "\">";
  html += htmlEscape(securityMessage.c_str());
  html += "</div>";

  if (remainingSeconds > 0) {
    html += "<div class=\"message err\">";
    html += "Wrong PIN. Try again in <span id=\"pin_countdown\">";
    html += String(remainingSeconds);
    html += "</span> seconds.";
    html += "</div>";
  } else if (statusMessage && statusMessage[0] != '\0') {
    html += "<div class=\"message ";
    html += isError ? "err" : "ok";
    html += "\">";
    html += htmlEscape(statusMessage);
    html += "</div>";
  }

  html += "<section class=\"card\"><form method=\"post\" action=\"/unlock\">";
  html += "<label for=\"setup_pin_unlock\">Setup PIN</label>";
  html += "<input id=\"setup_pin_unlock\" name=\"setup_pin_unlock\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" autocomplete=\"one-time-code\"";
  if (remainingSeconds > 0) html += " disabled";
  html += ">";
  html += "<div class=\"hint\">After wrong attempts, the wait grows. A 10-second factory reset still clears the PIN and all saved settings.</div>";
  html += "<div class=\"actions\"><button type=\"submit\"";
  if (remainingSeconds > 0) html += " disabled";
  html += ">Unlock setup</button></div>";
  html += "</form></section>";
  if (remainingSeconds > 0) {
    html += "<script>";
    html += "(()=>{let remaining=";
    html += String(remainingSeconds);
    html += ";const countdown=document.getElementById('pin_countdown');";
    html += "const tick=()=>{if(countdown)countdown.textContent=String(Math.max(remaining,0));";
    html += "if(remaining<=0){window.location.replace('/');return;}remaining-=1;setTimeout(tick,1000);};tick();})();";
    html += "</script>";
  }
  html += "</div></body></html>";
  return html;
}

static String buildPortalPage(const DeviceConfig& cfg, const char* statusMessage = nullptr, bool isError = false) {
  String html;
  const RefreshIntervalUnit refreshUnit = preferredRefreshIntervalUnit(cfg.refreshIntervalMinutes);
  const uint16_t refreshValue = refreshIntervalDisplayValue(cfg.refreshIntervalMinutes, refreshUnit);
  const bool hasSavedWifiPassword = hasText(cfg.wifiPass);
  const bool hasSavedMqttPassword = hasText(cfg.mqttPass);
  const bool hasExistingSetupPin = hasSetupPinConfigured(cfg);
  const String securityMessage = hardwareSecurityMessage();
  html.reserve(28200);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Freedom Clock Setup</title>";
  html += "<style>";
  html += "body{margin:0;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:860px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".hero h1{margin:0 0 8px;font-size:30px;letter-spacing:.02em;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".meta{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px;}";
  html += ".pill{background:#f7931a;padding:8px 12px;border-radius:999px;font-size:13px;color:#2b1700;}";
  html += "form{margin-top:22px;display:grid;gap:18px;}";
  html += ".card{background:#fff;border-radius:18px;padding:18px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += ".card h2{margin:0 0 14px;font-size:18px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;}";
  html += ".hidden{display:none;}";
  html += ".stack{display:grid;gap:10px;}";
  html += ".inline{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}";
  html += ".dual{display:grid;grid-template-columns:minmax(120px,1fr) 150px;gap:10px;align-items:end;}";
  html += ".check{display:flex;align-items:center;gap:8px;font-size:13px;color:#3a342d;margin-top:8px;}";
  html += ".check input{width:auto;margin:0;}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input,select{width:100%;box-sizing:border-box;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:15px;background:#fdfbf6;color:#171717;}";
  html += "input[type=password]{letter-spacing:.08em;}";
  html += ".hint{font-size:12px;color:#6b6258;margin-top:6px;line-height:1.45;}";
  html += ".message{padding:14px 16px;border-radius:14px;font-size:14px;line-height:1.45;white-space:pre-line;}";
  html += ".ok{background:#e8f7ea;color:#1d5d2d;}";
  html += ".err{background:#fdeaea;color:#8d2020;}";
  html += ".info{background:#eee7db;color:#5c5349;}";
  html += ".actions{display:flex;flex-wrap:wrap;gap:12px;align-items:center;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:15px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;}";
  html += "button.secondary{background:#ded4c2;color:#211d19;}";
  html += "button[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "a.extlink{color:#8b4f00;font-weight:700;text-decoration:none;}";
  html += "a.extlink:hover,a.extlink:focus{text-decoration:underline;}";
  html += ".subtle{font-size:13px;color:#645c53;}";
  html += "@media (max-width:640px){.hero h1{font-size:25px;}}";
  html += "</style></head><body><div class=\"wrap\">";
  html += "<section class=\"hero\"><h1>Freedom Clock Setup</h1>";
  html += "<p>Configure this device once, save locally, and it will boot straight into clock mode on future wakeups. Hold the side button while waking the device. Release after about 3 seconds to reopen setup, or keep holding for about 10 seconds to clear saved settings and start fresh.</p>";
  html += "<div class=\"meta\">";
  html += String("<div class=\"pill\">Device ID: ") + htmlEscape(deviceId) + "</div>";
  html += String("<div class=\"pill\">Setup Wi-Fi: ") + htmlEscape(portalApSsid) + "</div>";
  html += String("<div class=\"pill\">Firmware: ") + FIRMWARE_VERSION + "</div>";
  html += String("<div class=\"pill\">Security: ") + hardwareSecurityLevelLabel() + "</div>";
  html += "<div class=\"pill\">Portal: http://192.168.4.1</div>";
  html += "</div></section>";
  html += "<div class=\"message ";
  html += hardwareSecurityIsFull() ? "ok" : "info";
  html += "\" style=\"margin-top:18px;\">";
  html += htmlEscape(securityMessage.c_str());
  html += "</div>";

  if (statusMessage && statusMessage[0] != '\0') {
    html += "<div class=\"message ";
    html += isError ? "err" : "ok";
    html += "\" style=\"margin-top:12px;\">";
    html += htmlEscape(statusMessage);
    html += "</div>";
  }

  html += "<form id=\"config_form\" method=\"post\" action=\"/save\">";

  html += "<section class=\"card\"><h2>Owner</h2><div class=\"grid\">";
  html += String("<div><label for=\"owner_name\">Owner name</label><input id=\"owner_name\" name=\"owner_name\" maxlength=\"23\" autocapitalize=\"characters\" autocorrect=\"off\" spellcheck=\"false\" value=\"") + htmlEscape(cfg.ownerName) + "\"><div class=\"hint\">Used on the e-ink title line. Saved in uppercase.</div></div>";
  html += String("<div><label for=\"birth_year\">Birth year</label><input id=\"birth_year\" name=\"birth_year\" type=\"number\" min=\"1900\" max=\"2100\" value=\"") + String(cfg.birthYear) + "\"></div>";
  html += String("<div><label for=\"life_expectancy_years\">Life expectancy (years)</label><input id=\"life_expectancy_years\" name=\"life_expectancy_years\" type=\"number\" min=\"1\" max=\"130\" value=\"") + String(cfg.lifeExpectancyYears) + "\"></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Portfolio + Model</h2><div class=\"grid\">";
  html += "<div><label for=\"asset_mode\">Asset mode</label><select id=\"asset_mode\" name=\"asset_mode\">";
  html += String("<option value=\"2\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_BTC_MANUAL) + ">Static BTC + price online</option>";
  html += String("<option value=\"0\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_BTC) + ">BTC via MQTT</option>";
  html += String("<option value=\"1\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_WEALTH) + ">Static net worth</option>";
  html += "</select></div>";
  html += String("<div id=\"manual_btc_field_wrap\"><label for=\"manual_btc_amount\">Static BTC amount</label><input id=\"manual_btc_amount\" name=\"manual_btc_amount\" type=\"number\" min=\"0.00000001\" step=\"0.00000001\" value=\"") + formatFloatForInput(cfg.manualBtcAmount, 8) + "\"><div class=\"hint\">Used only in static BTC mode. BTC/USD price is fetched from CoinGecko over the internet.</div></div>";
  html += "<div><label for=\"portfolio_use_mode\">Withdrawal mode</label><select id=\"portfolio_use_mode\" name=\"portfolio_use_mode\">";
  html += String("<option value=\"0\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_SELL) + ">Sell monthly</option>";
  html += String("<option value=\"1\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_BORROW) + ">Borrow yearly</option>";
  html += "</select></div>";
  html += String("<div id=\"wealth_field_wrap\"><label for=\"default_wealth_usd\">Static wealth USD</label><input id=\"default_wealth_usd\" name=\"default_wealth_usd\" type=\"number\" min=\"0\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.defaultWealthUsd, 2) + "\"><div class=\"hint\">Used only in static net worth mode.</div></div>";
  html += String("<div><label for=\"monthly_exp_usd\">Monthly expenses USD</label><input id=\"monthly_exp_usd\" name=\"monthly_exp_usd\" type=\"number\" min=\"0\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.monthlyExpUsd, 2) + "\"></div>";
  html += String("<div><label for=\"inflation_annual_pct\">Inflation % / year</label><input id=\"inflation_annual_pct\" name=\"inflation_annual_pct\" type=\"number\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.inflationAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div><label for=\"wealth_growth_annual_pct\">Portfolio growth % / year</label><input id=\"wealth_growth_annual_pct\" name=\"wealth_growth_annual_pct\" type=\"number\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.wealthGrowthAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div id=\"borrow_fee_wrap\"><label for=\"borrow_fee_annual_pct\">Borrow fee % / year</label><input id=\"borrow_fee_annual_pct\" name=\"borrow_fee_annual_pct\" type=\"number\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.borrowFeeAnnual * 100.0f, 2) + "\"></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Display</h2><div class=\"grid\">";
  html += "<div><label for=\"display_theme_mode\">Display theme</label><select id=\"display_theme_mode\" name=\"display_theme_mode\">";
  html += String("<option value=\"1\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_DARK) + ">Dark</option>";
  html += String("<option value=\"0\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_LIGHT) + ">Light</option>";
  html += "</select></div>";
  html += String("<div><label for=\"refresh_interval_value\">Refresh every</label><div class=\"dual\"><input id=\"refresh_interval_value\" name=\"refresh_interval_value\" type=\"number\" min=\"1\" step=\"1\" value=\"") + String(refreshValue) + "\"><select id=\"refresh_interval_unit\" name=\"refresh_interval_unit\"><option value=\"0\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_MINUTES) + ">Minutes</option><option value=\"1\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_HOURS) + ">Hours</option><option value=\"2\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_DAYS) + ">Days</option></select></div><div id=\"refresh_interval_hint\" class=\"hint\">Default is 1 day. Shorter intervals use more battery.</div></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Setup Security</h2><div class=\"grid\">";
  html += "<div><label for=\"setup_pin_enabled\">Protect setup with PIN</label><select id=\"setup_pin_enabled\" name=\"setup_pin_enabled\">";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.setupPinEnabled) + ">Disabled</option>";
  html += String("<option value=\"1\"") + selectedAttr(cfg.setupPinEnabled) + ">Enabled</option>";
  html += "</select><div class=\"hint\">The clock screens stay unlocked. The PIN is only required before showing the setup page.</div></div>";
  html += "<div id=\"setup_pin_wrap\"><label for=\"setup_pin\">New setup PIN</label><input id=\"setup_pin\" name=\"setup_pin\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" placeholder=\"6 digits\"><div class=\"hint\">";
  html += hasExistingSetupPin
    ? "Leave blank to keep the current PIN. Enter a new 6-digit PIN to replace it."
    : "Enter a new 6-digit PIN if setup protection is enabled.";
  html += "</div></div>";
  html += "<div id=\"setup_pin_confirm_wrap\"><label for=\"setup_pin_confirm\">Confirm setup PIN</label><input id=\"setup_pin_confirm\" name=\"setup_pin_confirm\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" placeholder=\"Repeat the same 6 digits\"></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Wi-Fi</h2>";
  html += "<div class=\"stack\">";
  html += "<div class=\"stack\"><div><label for=\"wifi_ssid_select\">Available Wi-Fi</label><div class=\"inline\"><select id=\"wifi_ssid_select\"><option value=\"\">Loading nearby networks...</option></select><button id=\"scan_wifi_button\" class=\"secondary\" type=\"button\">Refresh Wi-Fi List</button></div></div><div class=\"hint\">Choose a scanned Wi-Fi name here, or type one manually below for hidden networks. In static net worth mode, Wi-Fi is optional, but recommended because without time sync the lifetime and coverage calculations can become less accurate. Static BTC mode uses Wi-Fi to fetch the BTC/USD price from CoinGecko.</div></div>";
  html += "<div class=\"grid\">";
  html += String("<div><label for=\"wifi_ssid\">Wi-Fi SSID</label><input id=\"wifi_ssid\" name=\"wifi_ssid\" maxlength=\"32\" value=\"") + htmlEscape(cfg.wifiSsid) + "\"></div>";
  html += "<div><label for=\"wifi_pass\">Wi-Fi password</label><input id=\"wifi_pass\" name=\"wifi_pass\" type=\"password\" maxlength=\"64\"";
  if (hasSavedWifiPassword) html += " placeholder=\"Saved password kept unless replaced\"";
  html += "><div class=\"hint\">";
  html += hasSavedWifiPassword
    ? "Leave blank to keep the saved password. Enter a new value to replace it."
    : "Leave empty only if your home Wi-Fi is open.";
  html += "</div>";
  if (hasSavedWifiPassword) {
    html += "<label class=\"check\"><input id=\"clear_wifi_pass\" name=\"clear_wifi_pass\" type=\"checkbox\" value=\"1\">Clear saved Wi-Fi password</label>";
  }
  html += "</div>";
  html += "</div></div></section>";

  html += "<section id=\"mqtt_card\" class=\"card\"><h2>MQTT For BTC Mode</h2><div class=\"grid\">";
  html += String("<div><label for=\"mqtt_server\">MQTT server</label><input id=\"mqtt_server\" name=\"mqtt_server\" maxlength=\"63\" value=\"") + htmlEscape(cfg.mqttServer) + "\"></div>";
  html += String("<div><label for=\"mqtt_port\">MQTT port</label><input id=\"mqtt_port\" name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"") + String(cfg.mqttPort) + "\"></div>";
  html += String("<div><label for=\"mqtt_user\">MQTT username</label><input id=\"mqtt_user\" name=\"mqtt_user\" maxlength=\"63\" value=\"") + htmlEscape(cfg.mqttUser) + "\"></div>";
  html += "<div><label for=\"mqtt_pass\">MQTT password</label><input id=\"mqtt_pass\" name=\"mqtt_pass\" type=\"password\" maxlength=\"63\"";
  if (hasSavedMqttPassword) html += " placeholder=\"Saved password kept unless replaced\"";
  html += "><div class=\"hint\">";
  html += hasSavedMqttPassword
    ? "Leave blank to keep the saved password. Enter a new value to replace it."
    : "Leave empty only if your MQTT broker does not use a password.";
  html += "</div>";
  if (hasSavedMqttPassword) {
    html += "<label class=\"check\"><input id=\"clear_mqtt_pass\" name=\"clear_mqtt_pass\" type=\"checkbox\" value=\"1\">Clear saved MQTT password</label>";
  }
  html += "</div>";
  html += String("<div><label for=\"topic_price_usd\">BTC price topic</label><input id=\"topic_price_usd\" name=\"topic_price_usd\" maxlength=\"95\" value=\"") + htmlEscape(cfg.topicPriceUsd) + "\"></div>";
  html += String("<div><label for=\"topic_balance_btc\">BTC balance topic</label><input id=\"topic_balance_btc\" name=\"topic_balance_btc\" maxlength=\"95\" value=\"") + htmlEscape(cfg.topicBalanceBtc) + "\"></div>";
  html += "</div><div class=\"hint\">These MQTT values are only required when the asset mode is set to BTC via MQTT. Everything stays local to the device and your local network. Changing the BTC broker or BTC topics resets historical stats on save.</div></section>";

  html += "<section class=\"card\"><h2>Validate Before Save</h2>";
  html += "<div class=\"actions\"><button id=\"validate_button\" type=\"button\">Test Connection</button><button id=\"save_button\" type=\"submit\" disabled>Save</button></div>";
  html += "<div id=\"validation_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "<div class=\"hint\">Save stays locked until the current form passes validation. In static net worth mode, Wi-Fi is optional. In BTC via MQTT mode, validation checks Wi-Fi, MQTT login, both topics, and whether both payloads are valid numbers. In static BTC mode, validation checks Wi-Fi, your static BTC amount, and a live BTC/USD price fetch from CoinGecko. Switching between asset modes resets historical stats on save.</div>";
  html += "</section>";
  html += "</form>";
  html += "<section class=\"card\" style=\"margin-top:16px;\"><h2>Firmware Update</h2>";
  html += "<form id=\"firmware_form\" method=\"post\" action=\"/firmware\" enctype=\"multipart/form-data\">";
  html += "<div class=\"grid\">";
  html += "<div><label for=\"firmware_file\">Firmware .bin file</label><input id=\"firmware_file\" name=\"firmware_file\" type=\"file\" accept=\".bin,application/octet-stream\"></div>";
  html += "</div>";
  html += String("<div class=\"hint\">Current firmware: ") + FIRMWARE_VERSION + ". Upload a Freedom Clock application .bin file here to update the device locally. Saved settings stay on the device. ";
  html += hardwareSecureBootActive
    ? "Use signed Freedom Clock firmware files for production-hardened devices."
    : "This works well for manual local updates during development and for future public release packages.";
  html += " ";
  html += hardwareSecureBootActive
    ? "For this device, choose the secure package."
    : "For this device, choose the open package.";
  html += " Need the newest .bin file? Open <a class=\"extlink\" href=\"";
  html += GITHUB_RELEASES_URL;
  html += "\" target=\"_blank\" rel=\"noopener noreferrer\">GitHub Releases</a>.</div>";
  html += "<div class=\"actions\" style=\"margin-top:14px;\"><button id=\"firmware_upload_button\" type=\"submit\">Upload Firmware</button></div>";
  html += "<div id=\"firmware_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "</form></section>";
  html += "<script>";
  html += "(function(){";
  html += "const form=document.getElementById('config_form');";
  html += "const ownerInput=document.getElementById('owner_name');";
  html += "const asset=document.getElementById('asset_mode');";
  html += "const mode=document.getElementById('portfolio_use_mode');";
  html += "const wealthWrap=document.getElementById('wealth_field_wrap');";
  html += "const manualBtcWrap=document.getElementById('manual_btc_field_wrap');";
  html += "const borrowWrap=document.getElementById('borrow_fee_wrap');";
  html += "const borrowInput=document.getElementById('borrow_fee_annual_pct');";
  html += "const setupPinEnabled=document.getElementById('setup_pin_enabled');";
  html += "const setupPinWrap=document.getElementById('setup_pin_wrap');";
  html += "const setupPinConfirmWrap=document.getElementById('setup_pin_confirm_wrap');";
  html += "const mqttCard=document.getElementById('mqtt_card');";
  html += "const wifiSelect=document.getElementById('wifi_ssid_select');";
  html += "const wifiSsidInput=document.getElementById('wifi_ssid');";
  html += "const wifiPassInput=document.getElementById('wifi_pass');";
  html += "const clearWifiPass=document.getElementById('clear_wifi_pass');";
  html += "const refreshValueInput=document.getElementById('refresh_interval_value');";
  html += "const refreshUnitSelect=document.getElementById('refresh_interval_unit');";
  html += "const refreshHint=document.getElementById('refresh_interval_hint');";
  html += "const scanButton=document.getElementById('scan_wifi_button');";
  html += "const validateButton=document.getElementById('validate_button');";
  html += "const saveButton=document.getElementById('save_button');";
  html += "const statusBox=document.getElementById('validation_status');";
  html += "const firmwareForm=document.getElementById('firmware_form');";
  html += "const firmwareFileInput=document.getElementById('firmware_file');";
  html += "const firmwareUploadButton=document.getElementById('firmware_upload_button');";
  html += "const firmwareStatus=document.getElementById('firmware_status');";
  html += "const mqttPassInput=document.getElementById('mqtt_pass');";
  html += "const clearMqttPass=document.getElementById('clear_mqtt_pass');";
  html += "let validatedSignature='';";
  html += "function signature(){return form?new URLSearchParams(new FormData(form)).toString():'';}";
  html += "function setStatus(text,kind){if(!statusBox)return;if(!text){statusBox.style.display='none';statusBox.textContent='';statusBox.className='message info';return;}statusBox.textContent=text;statusBox.className='message '+(kind||'info');statusBox.style.display='block';}";
  html += "function setFirmwareStatus(text,kind){if(!firmwareStatus)return;if(!text){firmwareStatus.style.display='none';firmwareStatus.textContent='';firmwareStatus.className='message info';return;}firmwareStatus.textContent=text;firmwareStatus.className='message '+(kind||'info');firmwareStatus.style.display='block';}";
  html += "function invalidate(msg){validatedSignature='';if(saveButton)saveButton.disabled=true;if(msg)setStatus(msg,'info');}";
  html += "function syncOwnerUppercase(){if(ownerInput)ownerInput.value=(ownerInput.value||'').toUpperCase();}";
  html += "function refreshSettingsForUnit(){if(!refreshUnitSelect)return{min:15,max:10080,hint:'Default is 1 day. Shorter intervals use more battery.'};if(refreshUnitSelect.value==='2')return{min:1,max:7,hint:'Choose 1 to 7 days. Default is 1 day. Shorter intervals use more battery.'};if(refreshUnitSelect.value==='1')return{min:1,max:168,hint:'Choose 1 to 168 hours. Default is 24 hours. Shorter intervals use more battery.'};return{min:15,max:10080,hint:'Choose 15 to 10080 minutes. Default is 1440 minutes, which is 1 day. Shorter intervals use more battery.'};}";
  html += "function updateRefreshControls(){if(!refreshValueInput)return;const settings=refreshSettingsForUnit();refreshValueInput.min=String(settings.min);refreshValueInput.max=String(settings.max);refreshValueInput.step='1';let value=parseInt(refreshValueInput.value||'',10);if(!Number.isFinite(value))value=settings.min;if(value<settings.min)value=settings.min;if(value>settings.max)value=settings.max;refreshValueInput.value=String(value);if(refreshHint)refreshHint.textContent=settings.hint;}";
  html += "function update(){";
  html += "const isMqttBtc=asset&&asset.value==='0';";
  html += "const isWealth=asset&&asset.value==='1';";
  html += "const isManualBtc=asset&&asset.value==='2';";
  html += "const isBorrow=mode&&mode.value==='1';";
  html += "if(wealthWrap) wealthWrap.classList.toggle('hidden', !isWealth);";
  html += "if(manualBtcWrap) manualBtcWrap.classList.toggle('hidden', !isManualBtc);";
  html += "if(mqttCard) mqttCard.classList.toggle('hidden', !isMqttBtc);";
  html += "if(borrowWrap) borrowWrap.classList.toggle('hidden', !isBorrow);";
  html += "const pinProtected=setupPinEnabled&&setupPinEnabled.value==='1';";
  html += "if(setupPinWrap) setupPinWrap.classList.toggle('hidden', !pinProtected);";
  html += "if(setupPinConfirmWrap) setupPinConfirmWrap.classList.toggle('hidden', !pinProtected);";
  html += "if(borrowInput) borrowInput.disabled=!isBorrow;";
  html += "updateRefreshControls();";
  html += "}";
  html += "async function refreshWifiList(){";
  html += "if(scanButton)scanButton.disabled=true;";
  html += "if(wifiSelect){wifiSelect.disabled=true;wifiSelect.innerHTML='<option value=\"\">Scanning nearby networks...</option>';}";
  html += "try{";
  html += "const response=await fetch('/wifi-list',{cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(!data.ok)throw new Error(data.message||'Wi-Fi scan failed.');";
  html += "const networks=Array.isArray(data.networks)?data.networks:[];";
  html += "if(wifiSelect){wifiSelect.innerHTML='';const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent=networks.length?'Choose a nearby network':'No networks found';wifiSelect.appendChild(placeholder);const currentSsid=wifiSsidInput?wifiSsidInput.value:'';networks.forEach(function(net){const option=document.createElement('option');option.value=net.ssid||'';option.textContent=(net.ssid||'');if(currentSsid&&option.value===currentSsid)option.selected=true;wifiSelect.appendChild(option);});wifiSelect.disabled=false;}";
  html += "}catch(err){if(wifiSelect){wifiSelect.innerHTML='<option value=\"\">Type your Wi-Fi name manually</option>';wifiSelect.disabled=false;}setStatus(err&&err.message?err.message:'Wi-Fi scan failed. Type the SSID manually.','err');}";
  html += "if(scanButton)scanButton.disabled=false;";
  html += "}";
  html += "async function validateCurrentSettings(){";
  html += "const currentSignature=signature();";
  html += "validatedSignature='';";
  html += "if(saveButton)saveButton.disabled=true;";
  html += "if(validateButton)validateButton.disabled=true;";
  html += "if(scanButton)scanButton.disabled=true;";
  html += "setStatus('Testing current settings. This can take a few seconds...','info');";
  html += "try{";
  html += "const response=await fetch('/validate',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:new URLSearchParams(new FormData(form)).toString(),cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(!data.ok){setStatus(data.message||'Validation failed.','err');return;}";
  html += "if(currentSignature!==signature()){setStatus('Validation finished, but the form changed meanwhile. Run the test again.','info');return;}";
  html += "validatedSignature=currentSignature;";
  html += "if(saveButton)saveButton.disabled=false;";
  html += "setStatus(data.message||'Validation successful.','ok');";
  html += "}catch(err){setStatus('Validation request failed. Keep this phone connected to the device Wi-Fi and try again.','err');}";
  html += "finally{if(validateButton)validateButton.disabled=false;if(scanButton)scanButton.disabled=false;}";
  html += "}";
  html += "function onFormEdited(event){if(event&&event.target===wifiSelect)return;invalidate('Test the current settings before saving.');}";
  html += "if(asset) asset.addEventListener('change', update);";
  html += "if(mode) mode.addEventListener('change', update);";
  html += "if(setupPinEnabled) setupPinEnabled.addEventListener('change', update);";
  html += "if(refreshUnitSelect) refreshUnitSelect.addEventListener('change', updateRefreshControls);";
  html += "if(ownerInput) ownerInput.addEventListener('input', syncOwnerUppercase);";
  html += "if(wifiPassInput) wifiPassInput.addEventListener('input', function(){if(clearWifiPass&&wifiPassInput.value)clearWifiPass.checked=false;});";
  html += "if(mqttPassInput) mqttPassInput.addEventListener('input', function(){if(clearMqttPass&&mqttPassInput.value)clearMqttPass.checked=false;});";
  html += "if(wifiSelect) wifiSelect.addEventListener('change', function(){if(wifiSsidInput)wifiSsidInput.value=wifiSelect.value||'';invalidate('Test the current settings before saving.');});";
  html += "if(form){form.addEventListener('input', onFormEdited);form.addEventListener('change', onFormEdited);form.addEventListener('submit', function(event){if(!validatedSignature||(saveButton&&saveButton.disabled)){event.preventDefault();setStatus('Please test the current settings before saving.','err');}});}";
  html += "if(firmwareFileInput) firmwareFileInput.addEventListener('change', function(){if(firmwareUploadButton)firmwareUploadButton.disabled=false;setFirmwareStatus('', 'info');});";
  html += "if(firmwareForm){firmwareForm.addEventListener('submit', function(event){if(!firmwareFileInput||!firmwareFileInput.files||firmwareFileInput.files.length===0){event.preventDefault();setFirmwareStatus('Choose a firmware .bin file first.','err');return;}const fileName=String((firmwareFileInput.files[0]&&firmwareFileInput.files[0].name)||'').toLowerCase();if(!fileName.endsWith('.bin')){event.preventDefault();setFirmwareStatus('Firmware file must end with .bin.','err');return;}if(firmwareUploadButton)firmwareUploadButton.disabled=true;setFirmwareStatus('Uploading firmware. Keep this phone connected until the device restarts.','info');});}";
  html += "if(scanButton) scanButton.addEventListener('click', refreshWifiList);";
  html += "if(validateButton) validateButton.addEventListener('click', validateCurrentSettings);";
  html += "syncOwnerUppercase();";
  html += "update();";
  html += "invalidate('Test the current settings before saving.');";
  html += "refreshWifiList();";
  html += "})();";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

static void drawBatteryIcon(
  int x,
  int y,
  int pct,
  int bodyW,
  int bodyH,
  int tipW,
  int tipH,
  DisplayThemeMode themeMode
) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  const int pad = (bodyW >= 28) ? 3 : 2;

  display.drawRoundRect(x, y, bodyW, bodyH, 3, theme.foreground);
  display.fillRect(x + bodyW, y + (bodyH - tipH) / 2, tipW, tipH, theme.foreground);

  const int innerX = x + pad;
  const int innerY = y + pad;
  const int innerW = bodyW - (pad * 2);
  const int innerH = bodyH - (pad * 2);

  display.fillRect(innerX, innerY, innerW, innerH, theme.background);

  int fillW = (innerW * pct) / 100;
  if (fillW > 0) {
    display.fillRect(innerX, innerY, fillW, innerH, theme.foreground);
  }
}

static void drawPortalScreen(
  const char* title,
  const char* line1,
  const char* line2,
  const char* line3,
  const char* line4,
  bool showPortalUrl = true
) {
  prepareScreen(DISPLAY_THEME_LIGHT);
  display.setTextSize(2);
  display.setCursor(8, 8);
  display.print(title ? title : "SETUP");

  display.setTextSize(1);
  display.setCursor(8, 34);
  display.print(line1 ? line1 : "");
  display.setCursor(8, 48);
  display.print(line2 ? line2 : "");
  display.setCursor(8, 68);
  display.print(line3 ? line3 : "");
  display.setCursor(8, 82);
  display.print(line4 ? line4 : "");
  if (showPortalUrl) {
    display.setCursor(8, 106);
    display.print("Open http://192.168.4.1 in a browser.");
  }
  display.update();
}

static void drawSetupPortalReadyScreen() {
  static constexpr int LABEL_X = 8;
  static constexpr int SSID_ROW_Y = 48;
  static constexpr int PASS_ROW_Y = 68;

  prepareScreen(DISPLAY_THEME_LIGHT);
  display.setTextSize(2);
  display.setCursor(8, 8);
  display.print("FREEDOM CLOCK SETUP");

  display.setTextSize(1);
  display.setCursor(LABEL_X, SSID_ROW_Y);
  display.print("Join Wi-Fi:");
  const int valueX = display.getCursorX() + 6;
  display.setCursor(valueX, SSID_ROW_Y);
  display.print(portalApSsid);

  display.setCursor(LABEL_X, PASS_ROW_Y);
  display.print("Password:");
  display.setCursor(valueX, PASS_ROW_Y);
  display.print(portalApPassword);

  display.setCursor(8, 106);
  display.print("Open http://192.168.4.1 in a browser.");
  display.update();
}

static void drawSetupPortalSavedScreen() {
  prepareScreen(DISPLAY_THEME_LIGHT);
  display.setTextSize(2);
  display.setCursor(8, 48);
  display.print("SETTINGS SAVED");
  display.setTextSize(1);
  display.setCursor(8, 72);
  display.print("Rebooting ...");
  display.update();
}

static void drawSetupPortalFirmwareUpdatedScreen() {
  prepareScreen(DISPLAY_THEME_LIGHT);
  display.setTextSize(2);
  display.setCursor(8, 48);
  display.print("FIRMWARE UPDATED");
  display.setTextSize(1);
  display.setCursor(8, 72);
  display.print("Rebooting ...");
  display.update();
}

static void drawSetupPortalErrorScreen(const char* message) {
  drawPortalScreen("SETUP ERROR", "Could not start config portal", message, "Restart the device and try again", "");
}

static void drawSetupPortalResetScreen() {
  prepareScreen(DISPLAY_THEME_LIGHT);
  display.setTextSize(2);
  display.setCursor(8, 48);
  display.print("FACTORY RESET");
  display.setTextSize(1);
  display.setCursor(8, 72);
  display.print("Rebooting ...");
  display.update();
}

static void drawFreedomClock(
  const char* ownerName,
  DisplayThemeMode themeMode,
  bool freedomHitCap,
  int freedomYears,
  int freedomMonths,
  int freedomWeeks,
  const LifeStats& lifeStats,
  bool coveredInfinite,
  int coveredPercent,
  int deviceBatteryPct
) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  char freedomTitle[40];
  char lifeTitle[32];
  char percentText[8];
  char freedomTitleLong[48];
  char freedomTitleMedium[40];
  char freedomTitleShort[32];
  const char* safeOwnerName = hasText(ownerName) ? ownerName : DEFAULT_OWNER_NAME;
  const size_t ownerNameLen = strlen(safeOwnerName);
  const int LEFT_X = 4;
  const int NUMBER_LEFT_X = LEFT_X + 10;
  const int NUMBER_STEP_X = 62;
  const int MONTH_X = NUMBER_LEFT_X + NUMBER_STEP_X;
  const int WEEK_X = NUMBER_LEFT_X + (NUMBER_STEP_X * 2);
  const int DIVIDER_X = 181;
  const int RIGHT_X = 188;
  const int BATTERY_X = 200;
  const int TOP_NUMBER_Y = 23;
  const int TOP_SUFFIX_Y = 28;
  const int MID_LINE_Y = 59;
  const int LIFE_TITLE_Y = 66;
  const int BOTTOM_NUMBER_Y = 85;
  const int BOTTOM_SUFFIX_Y = 90;
  const int RIGHT_TITLE_Y = 36;
  const int RIGHT_VALUE_Y = 66;
  const int RIGHT_TITLE_X = RIGHT_X + 4;
  const int RIGHT_VALUE_X = RIGHT_X + 8;
  const int titleMaxWidthWide = BATTERY_X - LEFT_X - 8;
  const int titleMaxWidthNarrow = DIVIDER_X - LEFT_X - 4;
  const int titleFitSlack = 18;

  snprintf(freedomTitleLong, sizeof(freedomTitleLong), "%s'S EXPECTED FREEDOM TIME", safeOwnerName);
  snprintf(freedomTitleMedium, sizeof(freedomTitleMedium), "%s'S FREEDOM TIME", safeOwnerName);
  snprintf(freedomTitleShort, sizeof(freedomTitleShort), "%s'S CLOCK", safeOwnerName);

  if (ownerNameLen <= 5 && estimateTextWidthSize1(freedomTitleLong) <= (titleMaxWidthWide + titleFitSlack)) {
    safeCopy(freedomTitle, sizeof(freedomTitle), freedomTitleLong, strlen(freedomTitleLong));
  } else if (estimateTextWidthSize1(freedomTitleLong) <= titleMaxWidthNarrow) {
    safeCopy(freedomTitle, sizeof(freedomTitle), freedomTitleLong, strlen(freedomTitleLong));
  } else if (estimateTextWidthSize1(freedomTitleMedium) <= (titleMaxWidthNarrow + 8)) {
    safeCopy(freedomTitle, sizeof(freedomTitle), freedomTitleMedium, strlen(freedomTitleMedium));
  } else if (estimateTextWidthSize1(freedomTitleShort) <= (titleMaxWidthNarrow + titleFitSlack)) {
    safeCopy(freedomTitle, sizeof(freedomTitle), freedomTitleShort, strlen(freedomTitleShort));
  } else {
    safeCopyCString(freedomTitle, sizeof(freedomTitle), "FREEDOM CLOCK");
  }

  snprintf(lifeTitle, sizeof(lifeTitle), "EXPECTED LIFETIME LEFT");
  if (coveredInfinite) {
    safeCopyCString(percentText, sizeof(percentText), "INF");
  } else {
    snprintf(percentText, sizeof(percentText), "%d%%", clampInt(coveredPercent, 0, 999));
  }

  prepareScreen(themeMode);

  display.setTextSize(1);
  display.setCursor(LEFT_X, 8);
  display.println(freedomTitle);

  drawBatteryIcon(BATTERY_X, 5, deviceBatteryPct, 22, 10, 2, 5, themeMode);
  display.setCursor(226, 7);
  display.setTextSize(1);
  display.print(deviceBatteryPct);
  display.print("%");

  display.drawLine(DIVIDER_X, 12, DIVIDER_X, 116, theme.foreground);

  int x = 0;
  if (freedomHitCap) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print("FOREVER");
  } else if (freedomYears > 99) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print(freedomYears);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("Y");
  } else {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    if (freedomYears < 10) display.print('0');
    display.print(freedomYears);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("Y");

    display.setCursor(MONTH_X, TOP_NUMBER_Y);
    display.setTextSize(3);
    if (freedomMonths < 10) display.print('0');
    display.print(freedomMonths);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("M");

    display.setCursor(WEEK_X, TOP_NUMBER_Y);
    display.setTextSize(3);
    display.print(freedomWeeks);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("W");
  }

  display.drawLine(LEFT_X, MID_LINE_Y, DIVIDER_X - 8, MID_LINE_Y, theme.foreground);

  display.setTextSize(1);
  display.setCursor(LEFT_X, LIFE_TITLE_Y);
  display.print(lifeTitle);

  display.setTextSize(3);
  display.setCursor(NUMBER_LEFT_X, BOTTOM_NUMBER_Y);
  if (lifeStats.yearsLeft < 10) display.print('0');
  display.print(lifeStats.yearsLeft);
  display.setTextSize(2);
  x = display.getCursorX();
  display.setCursor(x, BOTTOM_SUFFIX_Y);
  display.print("Y");

  display.setCursor(MONTH_X, BOTTOM_NUMBER_Y);
  display.setTextSize(3);
  if (lifeStats.monthsLeft < 10) display.print('0');
  display.print(lifeStats.monthsLeft);
  display.setTextSize(2);
  x = display.getCursorX();
  display.setCursor(x, BOTTOM_SUFFIX_Y);
  display.print("M");

  display.setCursor(WEEK_X, BOTTOM_NUMBER_Y);
  display.setTextSize(3);
  display.print(lifeStats.weeksLeftRemainder);
  display.setTextSize(2);
  x = display.getCursorX();
  display.setCursor(x, BOTTOM_SUFFIX_Y);
  display.print("W");

  display.setTextSize(1);
  display.setCursor(RIGHT_TITLE_X, RIGHT_TITLE_Y);
  display.print("FREEDOM");
  display.setCursor(RIGHT_TITLE_X, RIGHT_TITLE_Y + 10);
  display.print("COVERAGE");

  display.setTextSize(2);
  display.setCursor(RIGHT_VALUE_X, RIGHT_VALUE_Y);
  display.print(percentText);

  display.update();
}

static void drawInfoScreen(
  const DeviceConfig& cfg,
  float usdWealth,
  float balanceBtc,
  float priceUsd,
  int deviceBatteryPct,
  time_t now
) {
  char value[24];
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);
  const AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  const PortfolioUseMode portfolioUseMode = sanitizePortfolioUseMode(cfg.portfolioUseMode);
  static constexpr int LABEL_X = 10;
  static constexpr int VALUE_X = 142;
  static constexpr int ROW_Y0 = 10;
  static constexpr int ROW_STEP = 11;

  prepareScreen(themeMode);

  display.setTextSize(1);
  drawBatteryIcon(200, 5, deviceBatteryPct, 22, 10, 2, 5, themeMode);
  display.setCursor(226, 7);
  display.print(deviceBatteryPct);
  display.print("%");

  display.setTextSize(1);
  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 0));
  if (assetMode == ASSET_MODE_WEALTH) {
    display.print("ASSET TYPE:");
    display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 0));
    display.print("WEALTH");

    display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 1));
    display.print("NET WORTH USD:");
    display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 1));
    formatCompactUsd(usdWealth, value, sizeof(value));
    display.print(value);
  } else {
    display.print("BTC AMOUNT:");
    display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 0));
    snprintf(value, sizeof(value), "~%.4f", balanceBtc);
    display.print(value);

    display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 1));
    display.print("BTC PRICE USD:");
    display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 1));
    snprintf(value, sizeof(value), "%.0f", priceUsd);
    display.print(value);
  }

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 2));
  display.print("PORTFOLIO GROWTH:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 2));
  snprintf(value, sizeof(value), "%.1f%%", cfg.wealthGrowthAnnual * 100.0f);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 3));
  display.print("INFLATION:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 3));
  snprintf(value, sizeof(value), "%.1f%%", cfg.inflationAnnual * 100.0f);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 4));
  display.print("MONTHLY EXPENSES:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 4));
  formatCompactUsd(cfg.monthlyExpUsd, value, sizeof(value));
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 5));
  display.print("AGE NOW:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 5));
  snprintf(value, sizeof(value), "%d Y", currentAgeFromBirthYear(cfg.birthYear, now));
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 6));
  display.print("LIFE EXPECTANCY:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 6));
  snprintf(value, sizeof(value), "%d Y", cfg.lifeExpectancyYears);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 7));
  display.print("WITHDRAWAL MODE:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 7));
  if (portfolioUseMode == PORTFOLIO_USE_MODE_BORROW) {
    snprintf(value, sizeof(value), "BORROW YEARLY (%.1f%%/Y)", cfg.borrowFeeAnnual * 100.0f);
  } else {
    snprintf(value, sizeof(value), "%s", portfolioUseModeLabel(portfolioUseMode));
  }
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 8));
  display.print("FIRMWARE:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 8));
  display.print(FIRMWARE_VERSION);

  display.update();
}

static void drawWealthStatsScreen(
  const DeviceConfig& cfg,
  float usdWealth,
  float balanceBtc,
  float priceUsd,
  int deviceBatteryPct,
  const WealthHistory& history
) {
  struct WealthPeriod {
    const char* shortLabel;
    uint16_t daysAgo;
  };

  static const WealthPeriod periods[] = {
    { "7D", 7 },
    { "1M", 30 },
    { "3M", 90 },
    { "6M", 180 },
    { "12M", 365 }
  };

  char currentWealth[20];
  char wealthDeltaText[20];
  char balanceDeltaText[20];
  char priceDeltaText[20];
  int32_t historicalWealth = 0;
  int32_t historicalPrice = 0;
  int64_t historicalBalance = 0;
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);
  const AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  const bool showBtcBreakdown = isAnyBtcAssetMode(assetMode) && (priceUsd > 0.0f) && (balanceBtc >= 0.0f);
  const int32_t currentWealthRounded = (usdWealth >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(usdWealth + 0.5f);
  const int32_t currentPriceRounded = (priceUsd >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(priceUsd + 0.5f);
  const int64_t currentBalanceSats = (int64_t)(balanceBtc * 100000000.0 + 0.5);
  static constexpr int TITLE_X = 8;
  static constexpr int ROW_LABEL_X = 10;
  static constexpr int TITLE_Y = 14;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 44;
  static constexpr int ROW_STEP = 14;

  prepareScreen(themeMode);

  drawBatteryIcon(200, 5, deviceBatteryPct, 22, 10, 2, 5, themeMode);
  display.setCursor(226, 7);
  display.setTextSize(1);
  display.print(deviceBatteryPct);
  display.print("%");

  display.setTextSize(1);
  formatCompactUsd(usdWealth, currentWealth, sizeof(currentWealth));
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("CURRENT WEALTH USD: ");
  display.print(currentWealth);

  const int rowBtcX = 44;
  const int rowPriceX = 100;
  const int rowWealthX = showBtcBreakdown ? 162 : 68;
  display.setCursor(ROW_LABEL_X, HEADER_Y);
  display.print("LAST");
  if (showBtcBreakdown) {
    display.setCursor(rowBtcX, HEADER_Y);
    display.print("BTC");
    display.setCursor(rowPriceX, HEADER_Y);
    display.print("PRICE");
  }
  display.setCursor(rowWealthX, HEADER_Y);
  display.print("WEALTH");

  for (uint8_t i = 0; i < (sizeof(periods) / sizeof(periods[0])); i++) {
    const int rowY = ROW_Y0 + (ROW_STEP * i);
    display.setCursor(ROW_LABEL_X, rowY);
    display.print(periods[i].shortLabel);

    if (!showBtcBreakdown) continue;

    display.setCursor(rowBtcX, rowY);
    if (!getHistoricalBalanceSats(history, periods[i].daysAgo, historicalBalance)) {
      display.print("N/A");
    } else {
      formatSignedBtcDelta(currentBalanceSats - historicalBalance, balanceDeltaText, sizeof(balanceDeltaText));
      display.print(balanceDeltaText);
    }

    display.setCursor(rowPriceX, rowY);
    if (!getHistoricalPriceUsd(history, periods[i].daysAgo, historicalPrice)) {
      display.print("N/A");
    } else {
      formatSignedCompactPrice(currentPriceRounded - historicalPrice, priceDeltaText, sizeof(priceDeltaText));
      display.print(priceDeltaText);
    }

    display.setCursor(rowWealthX, rowY);
    if (!getHistoricalWealthUsd(history, periods[i].daysAgo, historicalWealth)) {
      display.print("N/A");
    } else {
      formatSignedCompactUsd(currentWealthRounded - historicalWealth, wealthDeltaText, sizeof(wealthDeltaText));
      display.print(wealthDeltaText);
    }
  }

  if (!showBtcBreakdown) {
    for (uint8_t i = 0; i < (sizeof(periods) / sizeof(periods[0])); i++) {
      const int rowY = ROW_Y0 + (ROW_STEP * i);
      display.setCursor(rowWealthX, rowY);
      if (!getHistoricalWealthUsd(history, periods[i].daysAgo, historicalWealth)) {
        display.print("N/A");
      } else {
        formatSignedCompactUsd(currentWealthRounded - historicalWealth, wealthDeltaText, sizeof(wealthDeltaText));
        display.print(wealthDeltaText);
      }
    }
  }

  display.update();
}

static void computeLifeStats(
  int birthYear,
  int lifeExpectancyYears,
  time_t now,
  LifeStats& outStats
) {
  outStats = {0, 0, 0, 0, 0, 0};

  if (lifeExpectancyYears <= 0) return;

  const time_t birthTime = makeUtcDate(birthYear, 1, 1);
  const time_t endTime = makeUtcDate(birthYear + lifeExpectancyYears, 1, 1);
  if (birthTime <= 0 || endTime <= birthTime) return;

  const double totalDays = difftime(endTime, birthTime) / 86400.0;
  const time_t clampedNow = (now < birthTime) ? birthTime : ((now > endTime) ? endTime : now);
  const double remainingDays = difftime(endTime, clampedNow) / 86400.0;

  outStats.totalWeeks = (int)(totalDays / 7.0 + 0.5);
  outStats.remainingWeeks = (remainingDays <= 0.0) ? 0 : (int)((remainingDays + 6.999) / 7.0);

  const double remainingRatio = (totalDays > 0.0) ? (remainingDays / totalDays) : 0.0;
  outStats.remainingPercent = clampInt((int)(remainingRatio * 100.0 + 0.5), 0, 100);

  int roundedRemainingDays = (remainingDays <= 0.0) ? 0 : (int)(remainingDays + 0.5);
  outStats.yearsLeft = roundedRemainingDays / 365;
  roundedRemainingDays %= 365;
  outStats.monthsLeft = roundedRemainingDays / 30;
  roundedRemainingDays %= 30;
  outStats.weeksLeftRemainder = clampInt(roundedRemainingDays / 7, 0, 4);
}

static void computeLongevityWithInflation(
  float usdWealth,
  float monthlyExpenseToday,
  float inflationAnnual,
  float assetGrowthAnnual,
  PortfolioUseMode portfolioUseMode,
  float borrowFeeAnnual,
  bool& outHitCap,
  int& outYears,
  int& outMonths,
  int& outWeeks,
  float& outCoveredWeeks
) {
  outHitCap = false;
  outYears = 0;
  outMonths = 0;
  outWeeks = 0;
  outCoveredWeeks = 0.0f;

  if (usdWealth <= 0.0f || monthlyExpenseToday <= 0.0f) return;
  if (inflationAnnual < 0.0f) inflationAnnual = 0.0f;
  if (assetGrowthAnnual < -0.99f) assetGrowthAnnual = -0.99f;
  if (borrowFeeAnnual < -0.99f) borrowFeeAnnual = -0.99f;

  static constexpr int MAX_YEARS = 200;
  static constexpr int MAX_MONTHS = 12 * MAX_YEARS;

  if (portfolioUseMode == PORTFOLIO_USE_MODE_BORROW) {
    const float annualExpenseMul = 1.0f + inflationAnnual;
    const float annualAssetMul = 1.0f + assetGrowthAnnual;
    const float annualDebtMul = 1.0f + borrowFeeAnnual;

    float annualExpense = monthlyExpenseToday * 12.0f;
    float collateralValue = usdWealth;
    float debt = 0.0f;
    int years = 0;

    while (years < MAX_YEARS) {
      collateralValue *= annualAssetMul;
      debt *= annualDebtMul;
      if ((debt + annualExpense) > collateralValue) break;
      debt += annualExpense;
      annualExpense *= annualExpenseMul;
      years++;
    }

    outHitCap = (years >= MAX_YEARS);

    float partialYear = 0.0f;
    if (!outHitCap && annualExpense > 0.0f && collateralValue > debt) {
      partialYear = (collateralValue - debt) / annualExpense;
      if (partialYear > 0.999f) partialYear = 0.999f;
      if (partialYear < 0.0f) partialYear = 0.0f;
    }

    const float coveredMonthsFloat = ((float)years + partialYear) * 12.0f;
    const int coveredFullMonths = (int)coveredMonthsFloat;
    const float partialMonth = coveredMonthsFloat - (float)coveredFullMonths;

    outYears = coveredFullMonths / 12;
    outMonths = coveredFullMonths % 12;
    outWeeks = clampInt((int)(partialMonth * 4.345f + 0.5f), 0, 4);
    outCoveredWeeks = coveredMonthsFloat * 4.345f;
    return;
  }

  const float monthlyExpenseMul = powf(1.0f + inflationAnnual, 1.0f / 12.0f);
  const float monthlyAssetMul = powf(1.0f + assetGrowthAnnual, 1.0f / 12.0f);

  float monthlyExpense = monthlyExpenseToday;
  float remaining = usdWealth;
  int months = 0;

  while (months < MAX_MONTHS) {
    remaining *= monthlyAssetMul;
    if (remaining < monthlyExpense) break;
    remaining -= monthlyExpense;
    monthlyExpense *= monthlyExpenseMul;
    months++;
  }

  outHitCap = (months >= MAX_MONTHS);
  outYears = months / 12;
  outMonths = months % 12;

  float partialMonth = 0.0f;
  if (monthlyExpense > 0.0f && remaining > 0.0f) {
    partialMonth = remaining / monthlyExpense;
    if (partialMonth > 0.999f) partialMonth = 0.999f;
    if (partialMonth < 0.0f) partialMonth = 0.0f;
  }

  outWeeks = clampInt((int)(partialMonth * 4.345f + 0.5f), 0, 4);
  outCoveredWeeks = months * 4.345f + (partialMonth * 4.345f);
}

static void formatSignedWeeksDelta(float deltaWeeks, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  snprintf(dst, dstSize, "%+.1fW", (double)deltaWeeks);
}

static void drawFreedomCheckinScreen(
  const DeviceConfig& cfg,
  time_t now,
  float usdWealth,
  int deviceBatteryPct,
  const WealthHistory& history
) {
  struct FreedomPeriod {
    const char* shortLabel;
    uint16_t daysAgo;
  };

  static const FreedomPeriod periods[] = {
    { "7D", 7 },
    { "1M", 30 },
    { "3M", 90 },
    { "6M", 180 },
    { "12M", 365 }
  };

  int32_t historicalWealthUsd = 0;
  char freedomDeltaText[16];
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);

  prepareScreen(themeMode);

  drawBatteryIcon(200, 5, deviceBatteryPct, 22, 10, 2, 5, themeMode);
  display.setCursor(226, 7);
  display.setTextSize(1);
  display.print(deviceBatteryPct);
  display.print("%");

  display.setTextSize(1);
  display.setCursor(8, 12);
  display.print("FREEDOM CHANGE");

  static constexpr int LAST_X = 10;
  static constexpr int FREEDOM_X = 92;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 46;
  static constexpr int ROW_STEP = 14;

  display.setCursor(LAST_X, HEADER_Y);
  display.print("LAST");
  display.setCursor(FREEDOM_X, HEADER_Y);
  display.print("FREEDOM");

  bool currentFreedomHitCap = false;
  int currentYears = 0;
  int currentMonths = 0;
  int currentWeeks = 0;
  float currentCoveredWeeks = 0.0f;
  computeLongevityWithInflation(
    usdWealth,
    cfg.monthlyExpUsd,
    cfg.inflationAnnual,
    cfg.wealthGrowthAnnual,
    sanitizePortfolioUseMode(cfg.portfolioUseMode),
    cfg.borrowFeeAnnual,
    currentFreedomHitCap,
    currentYears,
    currentMonths,
    currentWeeks,
    currentCoveredWeeks
  );

  for (size_t i = 0; i < (sizeof(periods) / sizeof(periods[0])); i++) {
    display.setCursor(LAST_X, ROW_Y0 + (ROW_STEP * (int)i));
    display.print(periods[i].shortLabel);

    display.setCursor(FREEDOM_X, ROW_Y0 + (ROW_STEP * (int)i));
    if (!getHistoricalWealthUsd(history, periods[i].daysAgo, historicalWealthUsd)) {
      display.print("N/A");
      continue;
    }

    bool previousFreedomHitCap = false;
    int previousYears = 0;
    int previousMonths = 0;
    int previousWeeks = 0;
    float previousCoveredWeeks = 0.0f;
    computeLongevityWithInflation(
      (float)historicalWealthUsd,
      cfg.monthlyExpUsd,
      cfg.inflationAnnual,
      cfg.wealthGrowthAnnual,
      sanitizePortfolioUseMode(cfg.portfolioUseMode),
      cfg.borrowFeeAnnual,
      previousFreedomHitCap,
      previousYears,
      previousMonths,
      previousWeeks,
      previousCoveredWeeks
    );

    const float freedomDeltaWeeks = currentCoveredWeeks - previousCoveredWeeks;
    formatSignedWeeksDelta(freedomDeltaWeeks, freedomDeltaText, sizeof(freedomDeltaText));
    display.print(freedomDeltaText);
  }

  display.update();
}

// ============================================================
// Config Portal
// ============================================================

static void portalSendHtml(const String& html) {
  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(200, "text/html; charset=utf-8", html);
}

static void portalSendJson(bool ok, const char* message) {
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"message\":\"";
  json += jsonEscape(message ? message : "");
  json += "\"}";

  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(200, "application/json; charset=utf-8", json);
}

static void loadSubmittedPortalConfig(DeviceConfig& submitted) {
  safeCopyString(submitted.ownerName, sizeof(submitted.ownerName), portalServer.arg("owner_name"));
  submitted.birthYear = portalServer.arg("birth_year").toInt();
  submitted.lifeExpectancyYears = portalServer.arg("life_expectancy_years").toInt();
  if (portalServer.hasArg("refresh_interval_value")) {
    const uint16_t refreshValue = (uint16_t)portalServer.arg("refresh_interval_value").toInt();
    const RefreshIntervalUnit refreshUnit = parseRefreshIntervalUnit(portalServer.arg("refresh_interval_unit"));
    submitted.refreshIntervalMinutes = refreshIntervalMinutesFromForm(refreshValue, refreshUnit);
  } else {
    submitted.refreshIntervalMinutes = (uint16_t)portalServer.arg("refresh_interval_minutes").toInt();
  }
  submitted.monthlyExpUsd = portalServer.arg("monthly_exp_usd").toFloat();
  submitted.inflationAnnual = portalServer.arg("inflation_annual_pct").toFloat() / 100.0f;
  submitted.wealthGrowthAnnual = portalServer.arg("wealth_growth_annual_pct").toFloat() / 100.0f;
  submitted.defaultWealthUsd = portalServer.arg("default_wealth_usd").toFloat();
  submitted.manualBtcAmount = portalServer.arg("manual_btc_amount").toFloat();
  submitted.borrowFeeAnnual = portalServer.arg("borrow_fee_annual_pct").toFloat() / 100.0f;
  submitted.assetMode = (uint8_t)portalServer.arg("asset_mode").toInt();
  submitted.portfolioUseMode = (uint8_t)portalServer.arg("portfolio_use_mode").toInt();
  submitted.displayThemeMode = (uint8_t)portalServer.arg("display_theme_mode").toInt();
  safeCopyString(submitted.wifiSsid, sizeof(submitted.wifiSsid), portalServer.arg("wifi_ssid"));
  if (portalServer.hasArg("clear_wifi_pass") && checkboxArgIsTrue(portalServer.arg("clear_wifi_pass"))) {
    submitted.wifiPass[0] = '\0';
  } else if (portalServer.hasArg("wifi_pass") && portalServer.arg("wifi_pass").length() > 0) {
    safeCopyString(submitted.wifiPass, sizeof(submitted.wifiPass), portalServer.arg("wifi_pass"));
  }
  safeCopyString(submitted.mqttServer, sizeof(submitted.mqttServer), portalServer.arg("mqtt_server"));
  submitted.mqttPort = (uint16_t)portalServer.arg("mqtt_port").toInt();
  safeCopyString(submitted.mqttUser, sizeof(submitted.mqttUser), portalServer.arg("mqtt_user"));
  if (portalServer.hasArg("clear_mqtt_pass") && checkboxArgIsTrue(portalServer.arg("clear_mqtt_pass"))) {
    submitted.mqttPass[0] = '\0';
  } else if (portalServer.hasArg("mqtt_pass") && portalServer.arg("mqtt_pass").length() > 0) {
    safeCopyString(submitted.mqttPass, sizeof(submitted.mqttPass), portalServer.arg("mqtt_pass"));
  }
  safeCopyString(submitted.topicPriceUsd, sizeof(submitted.topicPriceUsd), portalServer.arg("topic_price_usd"));
  safeCopyString(submitted.topicBalanceBtc, sizeof(submitted.topicBalanceBtc), portalServer.arg("topic_balance_btc"));
  submitted.configured = true;
  normalizeDeviceConfig(submitted);
}

static void handlePortalRoot() {
  if (requirePortalUnlock()) {
    return;
  }
  if (hasText(portalSessionMessage)) {
    portalSendHtml(buildPortalPage(deviceConfig, portalSessionMessage, portalSessionMessageIsError));
    return;
  }
  portalSendHtml(buildPortalPage(deviceConfig));
}

static void handlePortalUnlock() {
  if (!hasSetupPinConfigured(deviceConfig)) {
    portalUnlocked = true;
    portalServer.sendHeader("Location", "/", true);
    portalServer.send(302, "text/plain", "");
    return;
  }

  const uint32_t remainingSeconds = setupPinLockRemainingSeconds();
  if (remainingSeconds > 0) {
    char message[96];
    snprintf(message, sizeof(message), "Too many wrong PIN attempts. Try again in %lu seconds.", (unsigned long)remainingSeconds);
    portalSendHtml(buildPortalUnlockPage(message, true));
    return;
  }

  char enteredPin[SETUP_PIN_LENGTH + 1];
  safeCopyString(enteredPin, sizeof(enteredPin), portalServer.arg("setup_pin_unlock"));
  if (!isSixDigitPin(enteredPin)) {
    portalSendHtml(buildPortalUnlockPage("Enter the 6-digit setup PIN.", true));
    return;
  }

  char computedHash[SETUP_PIN_HASH_HEX_SIZE];
  computeSetupPinHash(enteredPin, computedHash, sizeof(computedHash));
  if (strcmp(computedHash, deviceConfig.setupPinHash) != 0) {
    checkpointPortalUnixTime();
    setupPinFailedAttempts++;
    setupPinLockedUntil = portalCurrentUnixTime() + (time_t)setupPinDelaySecondsForAttempt(setupPinFailedAttempts);
    saveSetupPinThrottleState();

    char message[128];
    snprintf(
      message,
      sizeof(message),
      "Wrong PIN. Try again in %lu seconds.",
      (unsigned long)setupPinLockRemainingSeconds()
    );
    portalSendHtml(buildPortalUnlockPage(message, true));
    return;
  }

  setupPinFailedAttempts = 0;
  setupPinLockedUntil = 0;
  saveSetupPinThrottleState();
  portalUnlocked = true;
  portalUnlockedAtMs = millis();
  portalServer.sendHeader("Location", "/", true);
  portalServer.send(302, "text/plain", "");
}

static void handlePortalSave() {
  if (requirePortalUnlock("Enter the setup PIN to change values.")) {
    return;
  }

  DeviceConfig submitted = deviceConfig;
  char errorMessage[128];
  const bool requestedPinEnabled = portalServer.arg("setup_pin_enabled").toInt() == 1;
  const String requestedPin = portalServer.arg("setup_pin");
  const String requestedPinConfirm = portalServer.arg("setup_pin_confirm");

  loadSubmittedPortalConfig(submitted);
  const bool sourceChanged = didHistorySourceChange(deviceConfig, submitted);

  if (!validateDeviceConfig(submitted, errorMessage, sizeof(errorMessage))) {
    portalSendHtml(buildPortalPage(submitted, errorMessage, true));
    return;
  }

  if (!validatePortalPinSettings(deviceConfig, requestedPinEnabled, requestedPin, requestedPinConfirm, errorMessage, sizeof(errorMessage))) {
    portalSendHtml(buildPortalPage(submitted, errorMessage, true));
    return;
  }

  applyPortalPinSettings(submitted, deviceConfig, requestedPinEnabled, requestedPin);

  if (!saveDeviceConfig(submitted)) {
    portalSendHtml(buildPortalPage(submitted, "Failed to save settings to device storage.", true));
    return;
  }

  deviceConfig = submitted;
  setupPinFailedAttempts = 0;
  setupPinLockedUntil = 0;
  if (sourceChanged) {
    clearWealthHistory();
    clearWealthHistoryInMemory(wealthHistory);
  }
  portalExitAction = PORTAL_EXIT_ACTION_SAVE_CONFIG;
  portalSaveRequested = true;
  portalSendHtml(buildPortalConfirmationPage(
    "SETTINGS SAVED",
    "Rebooting ..."
  ));
}

static void handlePortalFirmwareUpload() {
  HTTPUpload& upload = portalServer.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetPortalFirmwareUploadState();
      portalFirmwareUploadStarted = true;

      if (!portalUploadSessionAllowed()) {
        setPortalFirmwareUploadFailure("Unlock setup first.");
        return;
      }

      String filename = upload.filename;
      filename.toLowerCase();
      if (!filename.endsWith(".bin")) {
        setPortalFirmwareUploadFailure("Firmware file must end with .bin.");
        return;
      }

      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        char updateError[PORTAL_FIRMWARE_MESSAGE_SIZE];
        formatFirmwareUpdateError(updateError, sizeof(updateError));
        setPortalFirmwareUploadFailure(updateError);
        return;
      }

      portalFirmwareUploadAuthorized = true;
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (!portalFirmwareUploadAuthorized || portalFirmwareUploadFailed) {
        return;
      }

      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        char updateError[PORTAL_FIRMWARE_MESSAGE_SIZE];
        formatFirmwareUpdateError(updateError, sizeof(updateError));
        setPortalFirmwareUploadFailure(updateError);
        Update.abort();
        portalFirmwareUploadAuthorized = false;
        return;
      }

      refreshPortalUnlockSession();
      break;

    case UPLOAD_FILE_END:
      if (!portalFirmwareUploadAuthorized || portalFirmwareUploadFailed) {
        return;
      }

      if (!Update.end(true)) {
        char updateError[PORTAL_FIRMWARE_MESSAGE_SIZE];
        formatFirmwareUpdateError(updateError, sizeof(updateError));
        setPortalFirmwareUploadFailure(updateError);
        portalFirmwareUploadAuthorized = false;
        return;
      }

      portalFirmwareUploadSucceeded = true;
      safeCopyCString(
        portalFirmwareUploadMessage,
        sizeof(portalFirmwareUploadMessage),
        "Firmware uploaded successfully."
      );
      portalFirmwareUploadAuthorized = false;
      break;

    case UPLOAD_FILE_ABORTED:
      if (portalFirmwareUploadAuthorized) {
        Update.abort();
      }
      setPortalFirmwareUploadFailure("Firmware upload was cancelled.");
      portalFirmwareUploadAuthorized = false;
      break;

    default:
      break;
  }
}

static void handlePortalFirmwareUploadComplete() {
  if (requirePortalUnlock("Enter the setup PIN to update firmware.")) {
    resetPortalFirmwareUploadState();
    return;
  }

  if (!portalFirmwareUploadStarted) {
    portalSendHtml(buildPortalPage(deviceConfig, "Choose a firmware .bin file first.", true));
    resetPortalFirmwareUploadState();
    return;
  }

  if (portalFirmwareUploadFailed || !portalFirmwareUploadSucceeded) {
    const char* message = portalFirmwareUploadMessage[0] != '\0'
      ? portalFirmwareUploadMessage
      : "Firmware upload failed.";
    portalSendHtml(buildPortalPage(deviceConfig, message, true));
    resetPortalFirmwareUploadState();
    return;
  }

  checkpointPortalUnixTime();
  portalExitAction = PORTAL_EXIT_ACTION_FIRMWARE_UPDATE;
  portalSaveRequested = true;
  portalSendHtml(buildPortalConfirmationPage(
    "FIRMWARE UPDATED",
    "Rebooting ..."
  ));
}

static void handlePortalWifiList() {
  if (hasSetupPinConfigured(deviceConfig)) {
    if (isPortalUnlockExpired()) {
      portalUnlocked = false;
      portalUnlockedAtMs = 0;
    }
    if (!portalUnlocked) {
      portalSendJson(false, "Unlock setup first.");
      return;
    }
    refreshPortalUnlockSession();
  }

  String json = "{\"ok\":true,\"networks\":[";
  bool first = true;

  WiFi.mode(WIFI_AP_STA);
  delay(50);
  int networkCount = WiFi.scanNetworks();
  if (networkCount < 0) {
    portalSendJson(false, "Wi-Fi scan failed. You can still type the SSID manually.");
    return;
  }

  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;

    bool duplicate = false;
    for (int j = 0; j < i; j++) {
      if (ssid == WiFi.SSID(j)) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    if (!first) json += ",";
    json += "{\"ssid\":\"";
    json += jsonEscape(ssid.c_str());
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += "}";
    first = false;
  }
  WiFi.scanDelete();
  json += "]}";

  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(200, "application/json; charset=utf-8", json);
}

static void handlePortalValidate() {
  if (hasSetupPinConfigured(deviceConfig)) {
    if (isPortalUnlockExpired()) {
      portalUnlocked = false;
      portalUnlockedAtMs = 0;
    }
    if (!portalUnlocked) {
      portalSendJson(false, "Unlock setup first.");
      return;
    }
    refreshPortalUnlockSession();
  }

  DeviceConfig submitted = deviceConfig;
  char errorMessage[128];
  const bool requestedPinEnabled = portalServer.arg("setup_pin_enabled").toInt() == 1;
  const String requestedPin = portalServer.arg("setup_pin");
  const String requestedPinConfirm = portalServer.arg("setup_pin_confirm");
  loadSubmittedPortalConfig(submitted);
  const bool sourceChanged = didHistorySourceChange(deviceConfig, submitted);
  const char* sourceChangedNote = sourceChanged
    ? "\nNote: saving this portfolio source change will reset historical stats."
    : "";

  if (!validateDeviceConfig(submitted, errorMessage, sizeof(errorMessage))) {
    portalSendJson(false, errorMessage);
    return;
  }

  if (!validatePortalPinSettings(deviceConfig, requestedPinEnabled, requestedPin, requestedPinConfirm, errorMessage, sizeof(errorMessage))) {
    portalSendJson(false, errorMessage);
    return;
  }

  const AssetMode assetMode = sanitizeAssetMode(submitted.assetMode);
  const bool hasWifiConfig = hasText(submitted.wifiSsid);

  if (assetMode == ASSET_MODE_WEALTH && !hasWifiConfig) {
    String successMessage = String("Wi-Fi skipped\n")
      + "Static net worth mode does not require Wi-Fi.\n"
      + "Without Wi-Fi time sync, lifetime and coverage calculations can become less accurate.\n"
      + "Save unlocked."
      + sourceChangedNote;
    portalSendJson(true, successMessage.c_str());
    return;
  }

  if (!connectWiFi(submitted, WIFI_CONNECT_TIMEOUT_MS, true)) {
    portalSendJson(false, "Wi-Fi failed\nCheck the SSID and password, then try again.");
    return;
  }

  if (assetMode == ASSET_MODE_WEALTH) {
    String successMessage = String("Wi-Fi OK\n")
      + "Save unlocked."
      + sourceChangedNote;
    portalSendJson(true, successMessage.c_str());
    return;
  }

  if (isManualBtcAssetMode(assetMode)) {
    char fetchError[96];
    float validatedPrice = 0.0f;
    if (!fetchCoinGeckoBtcPriceUsd(validatedPrice, fetchError, sizeof(fetchError))) {
      String error = String("Wi-Fi OK\nPRICE API failed\n") + fetchError;
      portalSendJson(false, error.c_str());
      return;
    }

    char amountValue[24];
    char successMessage[256];
    formatTrimmedBtcAmount(submitted.manualBtcAmount, amountValue, sizeof(amountValue));
    snprintf(
      successMessage,
      sizeof(successMessage),
      "Wi-Fi OK\nPRICE API OK\n- Price: %.2f USD\nSave unlocked.%s",
      validatedPrice,
      sourceChangedNote
    );
    portalSendJson(true, successMessage);
    return;
  }

  gotPrice = false;
  gotBalance = false;
  priceUsdBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';
  mqttValidationMode = true;
  safeCopyCString(mqttValidationPriceTopic, sizeof(mqttValidationPriceTopic), submitted.topicPriceUsd);
  safeCopyCString(mqttValidationBalanceTopic, sizeof(mqttValidationBalanceTopic), submitted.topicBalanceBtc);

  if (!connectMQTT(submitted, MQTT_CONNECT_TIMEOUT_MS)) {
    mqttValidationMode = false;
    if (mqttClient.connected()) mqttClient.disconnect();
    portalSendJson(false, "Wi-Fi OK\nMQTT failed\nCheck the broker host, port, and credentials.");
    return;
  }

  uint32_t start = millis();
  while (millis() - start < MQTT_WAIT_FOR_MESSAGES_MS) {
    mqttClient.loop();
    delay(MQTT_LOOP_DELAY_MS);
    if (gotPrice && gotBalance) break;
  }

  mqttValidationMode = false;
  if (mqttClient.connected()) mqttClient.disconnect();

  float validatedPrice = 0.0f;
  float validatedBalance = 0.0f;
  const bool priceValid = gotPrice && parseNonNegativeFloatStrict(priceUsdBuf, validatedPrice, false);
  const bool balanceValid = gotBalance && parseNonNegativeFloatStrict(balanceBtcBuf, validatedBalance, true);

  char priceLine[48];
  char amountValue[24];
  char amountLine[48];
  if (priceValid) {
    snprintf(priceLine, sizeof(priceLine), "- Price: %.2f USD", validatedPrice);
  } else {
    snprintf(priceLine, sizeof(priceLine), "- Price: %s", gotPrice ? "invalid number" : "no value returned");
  }
  if (balanceValid) {
    formatTrimmedBtcAmount(validatedBalance, amountValue, sizeof(amountValue));
    snprintf(amountLine, sizeof(amountLine), "- Amount: %s BTC", amountValue);
  } else {
    snprintf(amountLine, sizeof(amountLine), "- Amount: %s", gotBalance ? "invalid number" : "no value returned");
  }

  if (!priceValid || !balanceValid) {
    String error = String("Wi-Fi OK\nMQTT OK\n")
      + priceLine + "\n"
      + amountLine + "\n"
      + "Use retained numeric messages on both topics.";
    portalSendJson(false, error.c_str());
    return;
  }

  String successMessage = String("Wi-Fi OK\nMQTT OK\n")
    + priceLine + "\n"
    + amountLine + "\n"
    + "Save unlocked."
    + sourceChangedNote;
  portalSendJson(true, successMessage.c_str());
}

static void handlePortalRedirect() {
  portalServer.sendHeader("Location", String("http://") + CONFIG_AP_IP.toString() + "/", true);
  portalServer.send(302, "text/plain", "");
}

static void setupPortalRoutes() {
  portalServer.on("/", HTTP_GET, handlePortalRoot);
  portalServer.on("/unlock", HTTP_POST, handlePortalUnlock);
  portalServer.on("/save", HTTP_POST, handlePortalSave);
  portalServer.on("/firmware", HTTP_POST, handlePortalFirmwareUploadComplete, handlePortalFirmwareUpload);
  portalServer.on("/validate", HTTP_POST, handlePortalValidate);
  portalServer.on("/wifi-list", HTTP_GET, handlePortalWifiList);
  portalServer.on("/generate_204", HTTP_GET, handlePortalRedirect);
  portalServer.on("/redirect", HTTP_GET, handlePortalRedirect);
  portalServer.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  portalServer.on("/connecttest.txt", HTTP_GET, handlePortalRoot);
  portalServer.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
  portalServer.onNotFound(handlePortalRedirect);
}

static bool startConfigurationPortal() {
  buildPortalCredentials();
  portalSaveRequested = false;
  portalExitAction = PORTAL_EXIT_ACTION_NONE;
  resetPortalFirmwareUploadState();
  portalUnlocked = !hasSetupPinConfigured(deviceConfig);
  portalBootUnixBase = (lastKnownUnixTime >= 1700000000) ? lastKnownUnixTime : buildTimestamp();
  portalUnlockedAtMs = portalUnlocked ? millis() : 0;

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_GATEWAY, CONFIG_AP_SUBNET);
  if (!WiFi.softAP(portalApSsid, portalApPassword)) {
    return false;
  }

  portalDnsServer.start(CONFIG_DNS_PORT, "*", CONFIG_AP_IP);
  setupPortalRoutes();
  portalServer.begin();
  drawSetupPortalReadyScreen();
  return true;
}

static void stopConfigurationPortal() {
  portalServer.stop();
  portalDnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void runConfigurationPortal() {
  if (!startConfigurationPortal()) {
    drawSetupPortalErrorScreen("AP start failed");
    delay(10000);
    ESP.restart();
  }

  while (!portalSaveRequested) {
    portalServer.handleClient();
    delay(CONFIG_PORTAL_DELAY_MS);
  }

  stopConfigurationPortal();
  checkpointPortalUnixTime();
  if (portalExitAction == PORTAL_EXIT_ACTION_FIRMWARE_UPDATE) {
    drawSetupPortalFirmwareUpdatedScreen();
  } else {
    drawSetupPortalSavedScreen();
  }
  delay(1200);
  ESP.restart();
}

// ============================================================
// MQTT callback
// ============================================================

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!topic || !payload || length == 0) return;

  const char* expectedPriceTopic = mqttValidationMode ? mqttValidationPriceTopic : deviceConfig.topicPriceUsd;
  const char* expectedBalanceTopic = mqttValidationMode ? mqttValidationBalanceTopic : deviceConfig.topicBalanceBtc;

  if (hasText(expectedPriceTopic) && strcmp(topic, expectedPriceTopic) == 0) {
    safeCopy(priceUsdBuf, sizeof(priceUsdBuf), (const char*)payload, length);
    gotPrice = true;
    return;
  }

  if (hasText(expectedBalanceTopic) && strcmp(topic, expectedBalanceTopic) == 0) {
    safeCopy(balanceBtcBuf, sizeof(balanceBtcBuf), (const char*)payload, length);
    gotBalance = true;
    return;
  }
}

// ============================================================
// WiFi + MQTT
// ============================================================

static bool connectWiFi(const DeviceConfig& cfg, uint16_t timeout_ms, bool keepPortalAp) {
  if (!hasText(cfg.wifiSsid)) return false;

  WiFi.mode(keepPortalAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(WIFI_POLL_DELAY_MS);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool parseCoinGeckoUsdPrice(const String& payload, float& outPriceUsd) {
  const int usdKeyIndex = payload.indexOf("\"usd\"");
  if (usdKeyIndex < 0) return false;

  int colonIndex = payload.indexOf(':', usdKeyIndex);
  if (colonIndex < 0) return false;
  colonIndex++;

  while (colonIndex < payload.length() && isspace((unsigned char)payload[colonIndex])) {
    colonIndex++;
  }
  if (colonIndex >= payload.length()) return false;

  int endIndex = colonIndex;
  while (endIndex < payload.length()) {
    const char c = payload[endIndex];
    if (
      (c >= '0' && c <= '9') ||
      c == '.' || c == '+' || c == '-' || c == 'e' || c == 'E'
    ) {
      endIndex++;
      continue;
    }
    break;
  }

  if (endIndex <= colonIndex) return false;

  char numberBuf[32];
  safeCopy(numberBuf, sizeof(numberBuf), payload.c_str() + colonIndex, (size_t)(endIndex - colonIndex));
  float parsedValue = 0.0f;
  if (!parseNonNegativeFloatStrict(numberBuf, parsedValue, false)) return false;
  outPriceUsd = parsedValue;
  return true;
}

static bool fetchCoinGeckoBtcPriceUsd(float& outPriceUsd, char* errorBuf, size_t errorBufSize) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(errorBuf, errorBufSize, "Wi-Fi is not connected.");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  http.setTimeout(PRICE_HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, COINGECKO_SIMPLE_PRICE_URL)) {
    snprintf(errorBuf, errorBufSize, "Could not start BTC price request.");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "FreedomClock/2026");
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    snprintf(errorBuf, errorBufSize, "CoinGecko request failed (%d).", httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (!parseCoinGeckoUsdPrice(payload, outPriceUsd)) {
    snprintf(errorBuf, errorBufSize, "CoinGecko price response could not be parsed.");
    return false;
  }

  snprintf(priceUsdBuf, sizeof(priceUsdBuf), "%.2f", outPriceUsd);
  gotPrice = true;
  errorBuf[0] = '\0';
  return true;
}

static bool connectMQTT(const DeviceConfig& cfg, uint16_t timeout_ms) {
  if (!hasText(cfg.mqttServer) || cfg.mqttPort == 0) return false;

  mqttClient.setServer(cfg.mqttServer, cfg.mqttPort);
  mqttClient.setCallback(mqttCallback);
  char mqttClientId[32];
  buildMqttClientId(mqttClientId, sizeof(mqttClientId));

  uint32_t start = millis();
  while (!mqttClient.connected() && (millis() - start) < timeout_ms) {
    bool connected = false;
    if (hasText(cfg.mqttUser)) {
      connected = mqttClient.connect(mqttClientId, cfg.mqttUser, cfg.mqttPass);
    } else {
      connected = mqttClient.connect(mqttClientId);
    }
    if (connected) break;
    delay(MQTT_RETRY_DELAY_MS);
  }

  if (!mqttClient.connected()) return false;

  if (hasText(cfg.topicPriceUsd)) mqttClient.subscribe(cfg.topicPriceUsd);
  if (hasText(cfg.topicBalanceBtc)) mqttClient.subscribe(cfg.topicBalanceBtc);
  return true;
}

// ============================================================
// Setup Mode Trigger
// ============================================================

static bool isSetupButtonPressed() {
  return digitalRead(PIN_USER_BUTTON) == LOW;
}

static bool detectQuickSecondPress(uint32_t timeoutMs = SECOND_PRESS_WINDOW_MS) {
  const uint32_t start = millis();

  while ((millis() - start) < timeoutMs) {
    if (isSetupButtonPressed()) {
      delay(BUTTON_POLL_DELAY_MS);
      while (isSetupButtonPressed() && (millis() - start) < timeoutMs) {
        delay(BUTTON_POLL_DELAY_MS);
      }
      return true;
    }
    delay(BUTTON_POLL_DELAY_MS);
  }

  return false;
}

static uint8_t detectAdditionalWakePresses(uint8_t maxAdditionalPresses = 2, uint32_t interPressTimeoutMs = SECOND_PRESS_WINDOW_MS) {
  uint8_t pressCount = 0;

  uint32_t releaseStart = millis();
  while (isSetupButtonPressed() && (millis() - releaseStart) < interPressTimeoutMs) {
    delay(BUTTON_POLL_DELAY_MS);
  }

  while (pressCount < maxAdditionalPresses) {
    const uint32_t waitStart = millis();
    bool detected = false;

    while ((millis() - waitStart) < interPressTimeoutMs) {
      if (isSetupButtonPressed()) {
        delay(BUTTON_POLL_DELAY_MS);
        if (!isSetupButtonPressed()) {
          continue;
        }
        detected = true;
        pressCount++;
        while (isSetupButtonPressed() && (millis() - waitStart) < interPressTimeoutMs) {
          delay(BUTTON_POLL_DELAY_MS);
        }
        break;
      }
      delay(BUTTON_POLL_DELAY_MS);
    }

    if (!detected) {
      break;
    }
  }

  return pressCount;
}

static SetupBootAction detectSetupBootAction() {
  if (!isSetupButtonPressed()) {
    return SETUP_BOOT_ACTION_NONE;
  }

  uint32_t start = millis();
  while (isSetupButtonPressed()) {
    uint32_t heldMs = millis() - start;
    if (heldMs >= FACTORY_RESET_HOLD_MS) {
      return SETUP_BOOT_ACTION_FACTORY_RESET;
    }
    delay(BUTTON_POLL_DELAY_MS);
  }

  uint32_t heldMs = millis() - start;
  if (heldMs >= FACTORY_RESET_HOLD_MS) {
    return SETUP_BOOT_ACTION_FACTORY_RESET;
  }
  if (heldMs >= CONFIG_BUTTON_HOLD_MS) {
    return SETUP_BOOT_ACTION_PORTAL;
  }
  return SETUP_BOOT_ACTION_NONE;
}

// ============================================================
// Deep sleep
// ============================================================

static void goToSleep(uint16_t refreshIntervalMinutes) {
  digitalWrite(PIN_EINK_POWER, LOW);
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_USER_BUTTON);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BUTTON, 0);
  uint64_t sleep_us = (uint64_t)refreshIntervalMinutes * MICROSECONDS_PER_MINUTE;
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_deep_sleep_start();
}

// ============================================================
// Setup
// ============================================================

void setup() {
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  pinMode(PIN_EINK_POWER, OUTPUT);
  digitalWrite(PIN_EINK_POWER, HIGH);
  delay(EINK_POWER_UP_DELAY_MS);

  rtc_gpio_deinit((gpio_num_t)PIN_USER_BUTTON);
  pinMode(PIN_USER_BUTTON, INPUT_PULLUP);

  display.begin();
  buildPortalCredentials();
  initializeEncryptedNvsIfAvailable();
  portalSessionMessage[0] = '\0';
  portalSessionMessageIsError = false;

  SetupBootAction bootAction = detectSetupBootAction();
  if (bootAction == SETUP_BOOT_ACTION_FACTORY_RESET) {
    clearSavedDeviceConfig();
    clearWealthHistory();
    clearWealthHistoryInMemory(wealthHistory);
    applyDefaultDeviceConfig(deviceConfig);
    safeCopyCString(portalSessionMessage, sizeof(portalSessionMessage), "Factory reset complete. Showing default values below.");
    drawSetupPortalResetScreen();
    delay(900);
    runConfigurationPortal();
  }

  bool configLoaded = loadDeviceConfig(deviceConfig);
  bool shouldRunPortal = !configLoaded || (bootAction == SETUP_BOOT_ACTION_PORTAL);
  if (shouldRunPortal) {
    runConfigurationPortal();
  }

  loadWealthHistory(wealthHistory);
  const uint8_t wakeExtraPressCount = (wakeCause == ESP_SLEEP_WAKEUP_EXT0)
    ? detectAdditionalWakePresses(2)
    : 0;
  const bool showPerformanceScreen = wakeExtraPressCount >= 1;
  const bool showFreedomCheckinScreen = wakeExtraPressCount >= 2;

  float vbat = readBatteryVoltage();
  int pct = batteryPercentFromVoltage(vbat);

  gotPrice = false;
  gotBalance = false;
  priceUsdBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';

  bool wifiOK = connectWiFi(deviceConfig);
  bool ntpSynced = false;
  if (wifiOK) {
    ntpSynced = syncClockFromNtp();
  }

  bool mqttOK = false;
  const AssetMode assetMode = sanitizeAssetMode(deviceConfig.assetMode);
  const PortfolioUseMode portfolioUseMode = sanitizePortfolioUseMode(deviceConfig.portfolioUseMode);
  const DisplayThemeMode displayThemeMode = sanitizeThemeMode(deviceConfig.displayThemeMode);

  if (wifiOK && isMqttBtcAssetMode(assetMode)) {
    mqttOK = connectMQTT(deviceConfig);
  } else if (wifiOK && isManualBtcAssetMode(assetMode)) {
    char fetchError[96];
    float fetchedPrice = 0.0f;
    fetchCoinGeckoBtcPriceUsd(fetchedPrice, fetchError, sizeof(fetchError));
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

  const char* priceStr = gotPrice ? priceUsdBuf : lastPriceUsd;
  const char* balanceStr = isManualBtcAssetMode(assetMode)
    ? balanceBtcBuf
    : (gotBalance ? balanceBtcBuf : lastBalanceBtc);

  float priceUsd = parseFloatSafe(priceStr);
  float balanceBtc = parseFloatSafe(balanceStr);
  float parsedPrice = 0.0f;
  float parsedBalance = 0.0f;
  const bool hasValidPrice = parseNonNegativeFloatStrict(priceStr, parsedPrice, false);
  const bool hasValidBalance = parseNonNegativeFloatStrict(balanceStr, parsedBalance, true);

  float usdWealth = (assetMode == ASSET_MODE_WEALTH)
    ? deviceConfig.defaultWealthUsd
    : (balanceBtc * priceUsd);

  bool freedomHitCap = false;
  int years = 0;
  int months = 0;
  int weeks = 0;
  float coveredWeeks = 0.0f;
  computeLongevityWithInflation(
    usdWealth,
    deviceConfig.monthlyExpUsd,
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

  const bool hasBtcBreakdown = isAnyBtcAssetMode(assetMode) && hasValidPrice && hasValidBalance;
  maybeSeedTestWealthHistory(wealthHistory, now, deviceConfig, usdWealth, hasBtcBreakdown, priceUsd, balanceBtc);
  const uint32_t previousHistoryDay = wealthHistory.latestDay;
  if (updateWealthHistory(wealthHistory, now, usdWealth, hasBtcBreakdown, priceUsd, balanceBtc)
      && (previousHistoryDay == 0 || wealthHistory.latestDay != previousHistoryDay)) {
    saveWealthHistory(wealthHistory);
  }

  bool coveredInfinite = freedomHitCap;
  int coveredPercent = 100;
  if (lifeStats.remainingWeeks > 0) {
    float coverageRatio = coveredWeeks / (float)lifeStats.remainingWeeks;
    coveredPercent = clampInt((int)(coverageRatio * 100.0f + 0.5f), 0, 999);
  }

  bool showInfoScreen = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) && !showPerformanceScreen && !showFreedomCheckinScreen;
  if (showFreedomCheckinScreen) {
    drawFreedomCheckinScreen(deviceConfig, now, usdWealth, pct, wealthHistory);
  } else if (showPerformanceScreen) {
    drawWealthStatsScreen(deviceConfig, usdWealth, balanceBtc, priceUsd, pct, wealthHistory);
  } else if (showInfoScreen) {
    drawInfoScreen(deviceConfig, usdWealth, balanceBtc, priceUsd, pct, now);
  } else {
    drawFreedomClock(
      deviceConfig.ownerName,
      displayThemeMode,
      freedomHitCap,
      years,
      months,
      weeks,
      lifeStats,
      coveredInfinite,
      coveredPercent,
      pct
    );
  }

  if (now >= 1700000000) {
    lastKnownUnixTime = now;
  }

  if (gotPrice) {
    safeCopy(lastPriceUsd, sizeof(lastPriceUsd), priceUsdBuf, strlen(priceUsdBuf));
  }
  if (gotBalance) {
    safeCopy(lastBalanceBtc, sizeof(lastBalanceBtc), balanceBtcBuf, strlen(balanceBtcBuf));
  }

  if (mqttClient.connected()) mqttClient.disconnect();
  WiFi.disconnect(true);

  goToSleep(deviceConfig.refreshIntervalMinutes);
}

void loop() {
  // not used
}
