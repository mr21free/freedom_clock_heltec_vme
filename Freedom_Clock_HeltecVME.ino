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
#include "nvs.h"
#include "driver/rtc_io.h"
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

#ifndef LOG_BATTERY_STATS
#define LOG_BATTERY_STATS 1
#endif

// ============================================================
// Board profile
// ============================================================
// Heltec Vision Master boards do not expose a reliable runtime model ID.
// The Arduino board profile gives us compile-time macros instead, so one
// firmware source can select the right display driver for each target.
#if defined(HELTEC_VISION_MASTER_E290) || defined(ARDUINO_HELTEC_VISION_MASTER_E290) || defined(Vision_Master_E290)
#define DEVICE_PROFILE_E290 1
#else
#define DEVICE_PROFILE_E290 0
#endif

#if defined(HELTEC_VISION_MASTER_E_213) || defined(ARDUINO_HELTEC_VISION_MASTER_E_213) || defined(Vision_Master_E213)
#define DEVICE_PROFILE_E213 1
#else
#define DEVICE_PROFILE_E213 0
#endif

#if DEVICE_PROFILE_E290
using FreedomClockDisplay = EInkDisplay_VisionMasterE290;
static constexpr char DEVICE_MODEL_NAME[] = "E290";
static constexpr int DEVICE_DISPLAY_WIDTH = 296;
static constexpr int DEVICE_DISPLAY_HEIGHT = 128;
#elif DEVICE_PROFILE_E213
using FreedomClockDisplay = EInkDisplay_VisionMasterE213;
static constexpr char DEVICE_MODEL_NAME[] = "E213";
static constexpr int DEVICE_DISPLAY_WIDTH = 250;
static constexpr int DEVICE_DISPLAY_HEIGHT = 122;
#else
// Keep local development safe if a non-Heltec board profile is selected.
using FreedomClockDisplay = EInkDisplay_VisionMasterE213;
static constexpr char DEVICE_MODEL_NAME[] = "E213";
static constexpr int DEVICE_DISPLAY_WIDTH = 250;
static constexpr int DEVICE_DISPLAY_HEIGHT = 122;
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
static constexpr char DEFAULT_MANUAL_BTC_AMOUNT_TEXT[] = "0.1";

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
// Hardware pins (Vision Master E-series)
// ============================================================
static constexpr int PIN_EINK_POWER = 45;  // E-ink VCC enable
static constexpr int PIN_BAT_ADC = 7;      // VBAT_Read
static constexpr int PIN_ADC_CTRL = 46;    // ADC_Ctrl gate
static constexpr int PIN_USER_BUTTON = 21; // Custom GPIO21 / 21 button

// Battery ADC constants
static constexpr float ADC_MAX = 4095.0f;
static constexpr float ADC_REF_V = 3.3f;
static constexpr float VBAT_SCALE = 4.9f;

// ============================================================
// General constants
// ============================================================
static constexpr size_t VALUE_BUFFER_SIZE = 16;
static constexpr size_t OWNER_NAME_SIZE = 24;
static constexpr size_t OWNER_NAME_MAX_DISPLAY_CHARS = 19;
static constexpr size_t MANUAL_BTC_TEXT_SIZE = 24;
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
static constexpr uint16_t FIRMWARE_HTTP_TIMEOUT_MS = 30000;
static constexpr uint32_t FIRMWARE_DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
static constexpr uint16_t BUTTON_POLL_DELAY_MS = 20;
static constexpr uint32_t CONFIG_BUTTON_HOLD_MS = 5000;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
static constexpr uint32_t SECOND_PRESS_WINDOW_MS = 700;
static constexpr uint32_t CONFIG_PORTAL_DELAY_MS = 10;
static constexpr uint32_t CONFIG_PORTAL_UNLOCK_TIMEOUT_MS = 600000;
static constexpr uint16_t CONFIG_PORTAL_PORT = 80;
static constexpr uint16_t CONFIG_DNS_PORT = 53;
static constexpr size_t PORTAL_FIRMWARE_MESSAGE_SIZE = 160;
static constexpr char CONFIG_NAMESPACE[] = "freedomclk";
static constexpr char HISTORY_NAMESPACE[] = "wealthhist";
static constexpr char BATTERY_LOG_NAMESPACE[] = "batlog";
static constexpr uint32_t CONFIG_VERSION = 1;
static constexpr uint32_t HISTORY_VERSION = 2;
static constexpr uint32_t BATTERY_LOG_VERSION = 1;
static constexpr char FIRMWARE_VERSION[] = "2026.05.07.2"
static constexpr char GITHUB_REPO_SLUG[] = "mr21free/freedom_clock_heltec_vme";
static constexpr char GITHUB_RELEASES_URL[] = "https://github.com/mr21free/freedom_clock_heltec_vme/releases";
static constexpr char GITHUB_LATEST_RELEASE_API_URL[] = "https://api.github.com/repos/mr21free/freedom_clock_heltec_vme/releases/latest";
static constexpr char GITHUB_API_VERSION[] = "2022-11-28";
static constexpr char MQTT_CLIENT_ID_PREFIX[] = "FreedomClock";
static constexpr char AP_SSID_PREFIX[] = "Freedom_Clock_";
static constexpr char AP_PASSWORD_PREFIX[] = "setup-";
static constexpr uint8_t SETUP_PIN_LENGTH = 6;
static constexpr size_t SETUP_PIN_HASH_HEX_SIZE = 65;
static constexpr uint16_t SETUP_PIN_HASH_ROUNDS = 2048;
static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";
static constexpr char COINGECKO_SIMPLE_PRICE_URL[] = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&precision=2";
static constexpr size_t GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS = 8192;
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
static constexpr uint8_t BATTERY_LOG_SAMPLES = 64;
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
  char manualBtcAmountText[MANUAL_BTC_TEXT_SIZE];
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
static bool fetchLatestGitHubReleaseInfo(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize);
static bool selectReleaseFirmwareUrl(const GitHubReleaseInfo& releaseInfo, bool securePackage, String& outUrl, String& outPackageLabel);

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

static void truncateCString(char* s, size_t maxChars) {
  if (!s) return;
  if (strlen(s) > maxChars) {
    s[maxChars] = '\0';
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

static String formatDecimalInputValue(float value, uint8_t maxDecimals) {
  if (!(value >= 0.0f)) value = 0.0f;
  if (maxDecimals > 8) maxDecimals = 8;

  uint8_t decimalsToUse = maxDecimals;
  double factor = 1.0;
  for (uint8_t decimals = 0; decimals <= maxDecimals; decimals++) {
    const double rounded = round((double)value * factor) / factor;
    if (fabs((double)value - rounded) <= 0.0000005) {
      decimalsToUse = decimals;
      break;
    }
    factor *= 10.0;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", decimalsToUse, (double)value);
  if (strchr(buf, '.')) {
    char* end = buf + strlen(buf) - 1;
    while (end > buf && *end == '0') {
      *end = '\0';
      end--;
    }
    if (end > buf && *end == '.') {
      *end = '\0';
    }
  }
  return String(buf);
}

static void normalizeDecimalInputText(
  const String& rawValue,
  char* dst,
  size_t dstSize,
  uint8_t maxDecimals,
  const char* fallback
) {
  if (!dst || dstSize == 0) return;
  if (maxDecimals > 8) maxDecimals = 8;

  String cleaned;
  cleaned.reserve(dstSize);
  bool seenDot = false;
  uint8_t decimals = 0;

  for (size_t i = 0; i < rawValue.length() && cleaned.length() < dstSize - 1; i++) {
    char c = rawValue.charAt(i);
    if (c == ',') c = '.';
    if (c >= '0' && c <= '9') {
      if (seenDot) {
        if (decimals >= maxDecimals) continue;
        decimals++;
      }
      cleaned += c;
    } else if (c == '.' && !seenDot) {
      seenDot = true;
      if (cleaned.length() == 0) cleaned += '0';
      cleaned += '.';
    }
  }

  while (cleaned.length() > 1 && cleaned.endsWith("0") && cleaned.indexOf('.') >= 0) {
    cleaned.remove(cleaned.length() - 1);
  }
  if (cleaned.endsWith(".")) {
    cleaned.remove(cleaned.length() - 1);
  }
  while (cleaned.length() > 1 && cleaned.charAt(0) == '0' && cleaned.charAt(1) != '.') {
    cleaned.remove(0, 1);
  }
  if (cleaned.length() == 0) {
    cleaned = fallback && fallback[0] ? fallback : "0";
  }

  safeCopyString(dst, dstSize, cleaned);
}

static void clearBatteryLogInMemory(BatteryLog& log) {
  memset(&log, 0, sizeof(log));
  log.version = BATTERY_LOG_VERSION;
}

static bool loadBatteryLog(BatteryLog& log) {
  clearBatteryLogInMemory(log);

  if (!preferences.begin(BATTERY_LOG_NAMESPACE, true)) {
    return false;
  }

  const size_t storedSize = preferences.getBytesLength("log");
  if (storedSize == sizeof(BatteryLog)) {
    preferences.getBytes("log", &log, sizeof(BatteryLog));
  }
  preferences.end();

  if (log.version != BATTERY_LOG_VERSION || log.count > BATTERY_LOG_SAMPLES || log.nextIndex >= BATTERY_LOG_SAMPLES) {
    clearBatteryLogInMemory(log);
    return false;
  }
  return true;
}

static bool saveBatteryLog(const BatteryLog& log) {
  if (!preferences.begin(BATTERY_LOG_NAMESPACE, false)) {
    return false;
  }
  const size_t written = preferences.putBytes("log", &log, sizeof(BatteryLog));
  preferences.end();
  return written == sizeof(BatteryLog);
}

static bool clearBatteryLog() {
  if (!preferences.begin(BATTERY_LOG_NAMESPACE, false)) {
    return false;
  }
  const bool ok = preferences.clear();
  preferences.end();
  return ok;
}

static bool appendBatteryLogSample(BatteryLog& log, time_t now, float voltage, int percent) {
  if (!(voltage > 0.0f)) return false;
  if (log.version != BATTERY_LOG_VERSION || log.count > BATTERY_LOG_SAMPLES || log.nextIndex >= BATTERY_LOG_SAMPLES) {
    clearBatteryLogInMemory(log);
  }

  const uint8_t index = log.nextIndex;
  log.nextSampleId++;
  if (log.nextSampleId == 0) log.nextSampleId = 1;
  log.sampleId[index] = log.nextSampleId;
  log.unixTime[index] = (now >= 1700000000) ? (uint32_t)now : 0;
  log.millivolts[index] = (uint16_t)clampInt((int)(voltage * 1000.0f + 0.5f), 0, 65535);
  log.percent[index] = (uint8_t)clampInt(percent, 0, 100);

  log.nextIndex = (uint8_t)((index + 1) % BATTERY_LOG_SAMPLES);
  if (log.count < BATTERY_LOG_SAMPLES) log.count++;
  return true;
}

static String buildBatteryLogText(const BatteryLog& log) {
  String out;
  out.reserve(2200);
  out += "Battery calibration log\n";
  out += "Format: sample, unix_time, voltage, percent\n";
  out += "Newest sample is last.\n\n";

  if (log.version != BATTERY_LOG_VERSION || log.count == 0 || log.count > BATTERY_LOG_SAMPLES) {
    out += "No battery samples recorded yet.";
    return out;
  }

  const uint8_t firstIndex = (log.count == BATTERY_LOG_SAMPLES) ? log.nextIndex : 0;
  for (uint8_t i = 0; i < log.count; i++) {
    const uint8_t index = (uint8_t)((firstIndex + i) % BATTERY_LOG_SAMPLES);
    char line[80];
    snprintf(
      line,
      sizeof(line),
      "#%lu, t=%lu, v=%.2fV, p=%u%%\n",
      (unsigned long)log.sampleId[index],
      (unsigned long)log.unixTime[index],
      (double)log.millivolts[index] / 1000.0,
      (unsigned int)log.percent[index]
    );
    out += line;
  }
  return out;
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
#if ENABLE_TEST_HISTORY
  if (now <= 0) return false;
  if (!FORCE_TEST_HISTORY_ON_EVERY_BOOT && history.latestDay != 0) return false;

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
  static constexpr float voltTable[NUM_POINTS] = { 3.20f, 3.30f, 3.60f, 3.75f, 3.85f, 3.95f, 4.02f, 4.06f };
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
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_0db);

  static constexpr uint8_t BATTERY_SAMPLE_COUNT = 12;
  uint32_t millivoltsSum = 0;
  for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
    millivoltsSum += (uint32_t)analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }

  digitalWrite(PIN_ADC_CTRL, LOW);

  const float adcVolts = ((float)millivoltsSum / (float)BATTERY_SAMPLE_COUNT) / 1000.0f;
  return adcVolts * VBAT_SCALE;
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

static bool ensureClockReadyForTls(char* errorBuf, size_t errorBufSize) {
  if (time(nullptr) >= 1700000000) {
    if (errorBuf && errorBufSize > 0) errorBuf[0] = '\0';
    return true;
  }

  if (syncClockFromNtp()) {
    if (errorBuf && errorBufSize > 0) errorBuf[0] = '\0';
    return true;
  }

  if (errorBuf && errorBufSize > 0) {
    snprintf(errorBuf, errorBufSize, "Could not sync time for HTTPS certificate validation.");
  }
  return false;
}

static bool configureTrustedTlsClient(TrustedWiFiClientSecure& secureClient, char* errorBuf, size_t errorBufSize) {
  if (secureClient.useDefaultCertificateBundle()) {
    if (errorBuf && errorBufSize > 0) errorBuf[0] = '\0';
    return true;
  }

  if (errorBuf && errorBufSize > 0) {
    snprintf(errorBuf, errorBufSize, "TLS certificate bundle is not available in this board package.");
  }
  return false;
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
  truncateCString(cfg.ownerName, OWNER_NAME_MAX_DISPLAY_CHARS);

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
  if (!hasText(cfg.manualBtcAmountText)) {
    safeCopyString(cfg.manualBtcAmountText, sizeof(cfg.manualBtcAmountText), formatDecimalInputValue(cfg.manualBtcAmount, 8));
  } else {
    normalizeDecimalInputText(
      String(cfg.manualBtcAmountText),
      cfg.manualBtcAmountText,
      sizeof(cfg.manualBtcAmountText),
      8,
      DEFAULT_MANUAL_BTC_AMOUNT_TEXT
    );
    cfg.manualBtcAmount = clampNonNegative(strtof(cfg.manualBtcAmountText, nullptr));
  }
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
  safeCopyCString(cfg.manualBtcAmountText, sizeof(cfg.manualBtcAmountText), DEFAULT_MANUAL_BTC_AMOUNT_TEXT);
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

#if HAS_SECRETS && USE_SECRETS_BOOTSTRAP
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
  if (preferences.getString("manbtctxt", cfg.manualBtcAmountText, sizeof(cfg.manualBtcAmountText)) == 0) {
    cfg.manualBtcAmountText[0] = '\0';
  }
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
  preferences.putString("manbtctxt", cfg.manualBtcAmountText);
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
  html += "html{-webkit-text-size-adjust:100%;text-size-adjust:100%;}";
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
  String html;
  html.reserve(4800);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Freedom Clock Setup Locked</title>";
  html += "<style>";
  html += "html{-webkit-text-size-adjust:100%;text-size-adjust:100%;}";
  html += "body{margin:0;min-height:100vh;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:560px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".hero h1{margin:0 0 8px;font-size:30px;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".card{margin-top:22px;background:#fff;border-radius:18px;padding:20px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input{width:100%;box-sizing:border-box;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:18px;background:#fdfbf6;color:#171717;letter-spacing:.18em;text-align:center;}";
  html += ".hint{font-size:12px;color:#6b6258;margin-top:8px;line-height:1.45;}";
  html += ".message{padding:14px 16px;border-radius:14px;font-size:14px;line-height:1.45;white-space:pre-line;margin-top:18px;}";
  html += ".ok{background:#e8f7ea;color:#1d5d2d;}";
  html += ".err{background:#fdeaea;color:#8d2020;}";
  html += ".info{background:#eee7db;color:#5c5349;}";
  html += ".actions{display:flex;gap:12px;align-items:center;margin-top:16px;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:16px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;touch-action:manipulation;}";
  html += "button[disabled],input[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "@media (max-width:640px){.hero h1{font-size:25px;}}";
  html += "</style></head><body><div class=\"wrap\">";
  html += "<section class=\"hero\"><h1>Freedom Clock Setup</h1>";
  html += "<p>Setup access is protected by a 6-digit PIN. Enter it to view or change saved settings. The normal clock screens stay unlocked.</p>";
  html += "</section>";

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
  html += "<label for=\"setup_pin_unlock\">Enter your PIN</label>";
  html += "<input id=\"setup_pin_unlock\" name=\"setup_pin_unlock\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" autocomplete=\"one-time-code\"";
  if (remainingSeconds > 0) html += " disabled";
  html += ">";
  html += "<div class=\"hint\">After wrong attempts, the wait grows. A 10-second factory reset still clears the PIN and all saved settings.</div>";
  html += "<div class=\"actions\"><button type=\"submit\"";
  if (remainingSeconds > 0) html += " disabled";
  html += ">Unlock</button></div>";
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
  const char* previewPriceText = (gotPrice && hasText(priceUsdBuf)) ? priceUsdBuf : lastPriceUsd;
  const char* previewBalanceText = (gotBalance && hasText(balanceBtcBuf)) ? balanceBtcBuf : lastBalanceBtc;
  float previewPriceUsd = 0.0f;
  float previewBalanceBtc = 0.0f;
  const bool previewHasPriceUsd = parseNonNegativeFloatStrict(previewPriceText, previewPriceUsd, false);
  const bool previewHasBalanceBtc = parseNonNegativeFloatStrict(previewBalanceText, previewBalanceBtc, true);
  const String batteryLogText = buildBatteryLogText(batteryLog);
  html.reserve(40200);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>Freedom Clock Setup</title>";
  html += "<style>";
  html += "html{-webkit-text-size-adjust:100%;text-size-adjust:100%;}";
  html += "body{margin:0;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:860px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".hero h1{margin:0 0 8px;font-size:30px;letter-spacing:.02em;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".meta{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px;}";
  html += ".pill{background:transparent;padding:0;border-radius:0;font-size:13px;color:#2b1700;}";
  html += "form{margin-top:22px;display:grid;gap:18px;}";
  html += ".card{background:#fff;border-radius:18px;padding:18px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += ".card h2{margin:0 0 14px;font-size:18px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;}";
  html += ".hidden{display:none!important;}";
  html += ".stack{display:grid;gap:10px;}";
  html += ".inline{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}";
  html += ".dual{display:grid;grid-template-columns:minmax(120px,1fr) 150px;gap:10px;align-items:end;}";
  html += ".check{display:flex;align-items:center;gap:8px;font-size:13px;color:#3a342d;margin-top:8px;}";
  html += ".check input{width:auto;margin:0;}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input,select{width:100%;box-sizing:border-box;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:16px;background:#fdfbf6;color:#171717;}";
  html += "input[type=password]{letter-spacing:.08em;}";
  html += ".hint{font-size:12px;color:#6b6258;margin-top:6px;line-height:1.45;}";
  html += ".message{padding:14px 16px;border-radius:14px;font-size:14px;line-height:1.45;white-space:pre-line;}";
  html += ".ok{background:#e8f7ea;color:#1d5d2d;}";
  html += ".err{background:#fdeaea;color:#8d2020;}";
  html += ".info{background:#eee7db;color:#5c5349;}";
  html += ".preview-card{background:#2b1700;color:#fff7ec;}";
  html += ".preview-card h2{color:#fff7ec;}";
  html += ".preview-value{font-size:34px;font-weight:800;letter-spacing:.03em;line-height:1.05;}";
  html += ".preview-hint{font-size:13px;color:#f7d9ad;line-height:1.45;margin-top:10px;}";
  html += ".dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;}";
  html += ".releasebox{display:grid;gap:10px;margin-top:14px;padding:14px 16px;border-radius:14px;background:#f8f4ec;border:1px solid #e5dbc9;}";
  html += ".releasebox .row{font-size:14px;color:#2f2924;}";
  html += ".releasebox strong{color:#171717;}";
  html += ".release-notes{font-size:13px;color:#5c5349;line-height:1.5;white-space:pre-wrap;}";
  html += ".copyline{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;margin-top:10px;}";
  html += ".copytext{box-sizing:border-box;width:100%;padding:10px 12px;border:1px solid #d8cdbb;border-radius:12px;background:#fdfbf6;color:#2f2924;font-size:13px;overflow-wrap:anywhere;}";
  html += ".actions{display:flex;flex-wrap:wrap;gap:12px;align-items:center;}";
  html += ".form-actions{margin-top:4px;}";
  html += ".firmware-head{display:flex;justify-content:space-between;gap:12px;align-items:end;flex-wrap:wrap;}";
  html += ".firmware-version{font-size:20px;font-weight:800;color:#171717;}";
  html += ".firmware-model{font-size:13px;color:#645c53;margin-top:4px;}";
  html += "@keyframes fc-spin{to{transform:rotate(360deg)}}";
  html += ".install-progress{display:flex;align-items:center;gap:12px;padding:14px 16px;border-radius:14px;background:#fff8ef;border:1.5px solid #f7931a;margin-top:14px;}";
  html += ".install-spinner{flex-shrink:0;width:20px;height:20px;border:2.5px solid rgba(247,147,26,.25);border-top-color:#f7931a;border-radius:50%;animation:fc-spin .9s linear infinite;}";
  html += ".install-step{font-size:14px;color:#2b1700;font-weight:600;line-height:1.4;}";
  html += "textarea{width:100%;box-sizing:border-box;min-height:180px;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:16px;background:#fdfbf6;color:#171717;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;line-height:1.45;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:16px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;touch-action:manipulation;}";
  html += "button.small{padding:10px 14px;font-size:16px;}";
  html += "button.secondary{background:#ded4c2;color:#211d19;}";
  html += "button[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "a.extlink{color:#8b4f00;font-weight:700;text-decoration:none;}";
  html += "a.extlink:hover,a.extlink:focus{text-decoration:underline;}";
  html += ".subtle{font-size:13px;color:#645c53;}";
  html += "@media (max-width:640px){.hero h1{font-size:25px;}}";
  html += "</style></head><body><div id=\"focus_guard\" tabindex=\"-1\" aria-hidden=\"true\" style=\"position:fixed;top:0;left:0;width:1px;height:1px;outline:none;\"></div><div class=\"wrap\">";
  html += "<section class=\"hero\"><h1>Freedom Clock Setup</h1>";
  html += "<p>Hold 21 button for about 5 seconds to enter setup mode, or keep holding the button for about 10 seconds to clear all settings (factory reset).</p>";
  html += "<div class=\"meta\">";
  html += String("<div class=\"pill\">Device ID: ") + htmlEscape(deviceId) + "</div>";
  html += "</div></section>";

  if (statusMessage && statusMessage[0] != '\0') {
    html += "<div class=\"message ";
    html += isError ? "err" : "ok";
    html += "\" style=\"margin-top:12px;\">";
    html += htmlEscape(statusMessage);
    html += "</div>";
  }

  html += "<form id=\"config_form\" method=\"post\" action=\"/save\" autocomplete=\"off\">";

  html += "<section class=\"card\"><h2>Owner</h2><div class=\"grid\">";
  html += String("<div><label for=\"owner_name\">Owner name</label><input id=\"owner_name\" name=\"owner_name\" maxlength=\"") + String(OWNER_NAME_MAX_DISPLAY_CHARS) + "\" autocomplete=\"off\" autocapitalize=\"characters\" autocorrect=\"off\" spellcheck=\"false\" value=\"" + htmlEscape(cfg.ownerName) + "\"></div>";
  html += String("<div><label for=\"birth_year\">Birth year</label><input id=\"birth_year\" name=\"birth_year\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"1900\" max=\"2100\" autocomplete=\"off\" value=\"") + String(cfg.birthYear) + "\"></div>";
  html += String("<div><label for=\"life_expectancy_years\">Life expectancy (years)</label><input id=\"life_expectancy_years\" name=\"life_expectancy_years\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"1\" max=\"130\" autocomplete=\"off\" value=\"") + String(cfg.lifeExpectancyYears) + "\"></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Portfolio &amp; Model</h2><div class=\"grid\">";
  html += "<div><label for=\"asset_mode\">Asset mode</label><select id=\"asset_mode\" name=\"asset_mode\">";
  html += String("<option value=\"2\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_BTC_MANUAL) + ">Static BTC + price online</option>";
  html += String("<option value=\"0\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_BTC) + ">BTC via MQTT</option>";
  html += String("<option value=\"1\"") + selectedAttr(sanitizeAssetMode(cfg.assetMode) == ASSET_MODE_WEALTH) + ">Static net worth</option>";
  html += "</select></div>";
  html += String("<div id=\"manual_btc_field_wrap\"><label for=\"manual_btc_amount\">Static BTC amount</label><input id=\"manual_btc_amount\" name=\"manual_btc_amount\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" min=\"0.00000001\" step=\"0.00000001\" autocomplete=\"off\" value=\"") + htmlEscape(cfg.manualBtcAmountText) + "\"><div class=\"hint\">BTC/USD price is fetched from CoinGecko over the internet.</div></div>";
  html += "<div><label for=\"portfolio_use_mode\">Spend mode</label><select id=\"portfolio_use_mode\" name=\"portfolio_use_mode\">";
  html += String("<option value=\"0\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_SELL) + ">Sell monthly</option>";
  html += String("<option value=\"1\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_BORROW) + ">Borrow yearly</option>";
  html += "</select></div>";
  html += String("<div id=\"wealth_field_wrap\"><label for=\"default_wealth_usd\">Static wealth USD</label><input id=\"default_wealth_usd\" name=\"default_wealth_usd\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" min=\"0\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.defaultWealthUsd, 2) + "\"><div class=\"hint\">Used only in static net worth mode.</div></div>";
  html += String("<div><label for=\"monthly_exp_usd\">Monthly expenses USD</label><input id=\"monthly_exp_usd\" name=\"monthly_exp_usd\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"0\" step=\"1\" autocomplete=\"off\" value=\"") + String((int)(cfg.monthlyExpUsd + 0.5f)) + "\"></div>";
  html += String("<div><label for=\"inflation_annual_pct\">Inflation % / year</label><input id=\"inflation_annual_pct\" name=\"inflation_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.inflationAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div><label for=\"wealth_growth_annual_pct\">Portfolio growth % / year</label><input id=\"wealth_growth_annual_pct\" name=\"wealth_growth_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.wealthGrowthAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div id=\"borrow_fee_wrap\"><label for=\"borrow_fee_annual_pct\">Borrow fee % / year</label><input id=\"borrow_fee_annual_pct\" name=\"borrow_fee_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.borrowFeeAnnual * 100.0f, 2) + "\"></div>";
  html += "</div></section>";

  html += "<section class=\"card preview-card\"><h2>Freedom Time Preview</h2>";
  html += "<div id=\"freedom_preview_value\" class=\"preview-value\">--</div>";
  html += "<div id=\"freedom_preview_hint\" class=\"preview-hint\">Change the values above to preview the result before saving.</div>";
  html += "</section>";

  html += "<section class=\"card\"><h2>Display</h2><div class=\"grid\">";
  html += "<div><label for=\"display_theme_mode\">Display theme</label><select id=\"display_theme_mode\" name=\"display_theme_mode\">";
  html += String("<option value=\"1\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_DARK) + ">Dark</option>";
  html += String("<option value=\"0\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_LIGHT) + ">Light</option>";
  html += "</select></div>";
  html += String("<div><label for=\"refresh_interval_value\">Refresh every</label><div class=\"dual\"><input id=\"refresh_interval_value\" name=\"refresh_interval_value\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"1\" step=\"1\" value=\"") + String(refreshValue) + "\"><select id=\"refresh_interval_unit\" name=\"refresh_interval_unit\"><option value=\"0\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_MINUTES) + ">Minutes</option><option value=\"1\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_HOURS) + ">Hours</option><option value=\"2\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_DAYS) + ">Days</option></select></div><div id=\"refresh_interval_hint\" class=\"hint\">Default is 1 day. Shorter intervals use more battery.</div></div>";
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
  html += "<div class=\"stack\"><div><label for=\"wifi_ssid_select\">Available Wi-Fi</label><div class=\"inline\"><select id=\"wifi_ssid_select\"><option value=\"\">Loading nearby networks...</option></select><button id=\"scan_wifi_button\" class=\"secondary\" type=\"button\">Refresh Wi-Fi List</button></div></div><div class=\"hint\">Choose a scanned Wi-Fi name here, or type one manually below for hidden networks.</div></div>";
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
  html += String("<div><label for=\"mqtt_port\">MQTT port</label><input id=\"mqtt_port\" name=\"mqtt_port\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"1\" max=\"65535\" value=\"") + String(cfg.mqttPort) + "\"></div>";
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
  html += "</div></section>";
  html += "<div class=\"actions form-actions\"><button id=\"validate_button\" type=\"button\">Test Connection</button><button id=\"save_button\" type=\"submit\" disabled>Save</button></div>";
  html += "<div id=\"validation_status\" class=\"message info\" style=\"display:none;\"></div>";
  html += "</form>";

  html += "<section class=\"card\" style=\"margin-top:16px;\"><h2>Battery Calibration Log</h2>";
  html += "<div class=\"hint\">Each wake records measured battery voltage and the percent shown by the device. Copy this log when we need to tune the battery curve.</div>";
  html += "<textarea id=\"battery_log_text\" readonly>";
  html += htmlEscape(batteryLogText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_battery_log_button\" class=\"secondary\" type=\"button\">Copy Battery Log</button></div>";
  html += "<div id=\"battery_log_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "</section>";

  html += "<section class=\"card\" style=\"margin-top:16px;\"><h2>Firmware Update</h2>";
  html += "<div class=\"firmware-head\"><div><label>Current firmware</label><div class=\"firmware-version\">";
  html += FIRMWARE_VERSION;
  html += "</div><div class=\"firmware-model\">Device: ";
  html += DEVICE_MODEL_NAME;
  html += "</div></div><button id=\"release_check_button\" class=\"secondary\" type=\"button\">Check for Update</button></div>";
  html += "<div id=\"release_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "<div id=\"release_summary\" class=\"releasebox hidden\">";
  html += "<div class=\"row\"><strong>Latest release:</strong> <span id=\"release_latest_version\">--</span></div>";
  html += "<div class=\"row\"><strong>Notes:</strong></div>";
  html += "<div id=\"release_notes\" class=\"release-notes\">--</div>";
  html += "<div id=\"latest_release_url_row\" class=\"row hidden\"><strong>URL:</strong><div class=\"copyline\"><div id=\"latest_release_url_text\" class=\"copytext\">--</div><button id=\"copy_latest_release_url_button\" class=\"secondary small\" type=\"button\">Copy</button></div></div>";
  html += "</div>";
  html += "<form id=\"firmware_form\" method=\"post\" action=\"/firmware\" enctype=\"multipart/form-data\" style=\"margin-top:16px;\">";
  html += "<div><label for=\"firmware_file\">Manual firmware update</label><input id=\"firmware_file\" name=\"firmware_file\" type=\"file\" accept=\".bin,application/octet-stream\"></div>";
  html += "<div class=\"hint\">Use the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin package for this device.</div>";
  html += "<div id=\"firmware_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "<div id=\"firmware_progress\" class=\"hidden\"><div class=\"install-progress\"><div class=\"install-spinner\"></div><div id=\"firmware_step_text\" class=\"install-step\">Installing...</div></div></div>";
  html += "<div id=\"firmware_keep_data_notice\" class=\"hint hidden\">Saved data and settings stay on the device with the firmware update.</div>";
  html += "<div class=\"actions\" style=\"margin-top:14px;\"><button id=\"firmware_install_button\" type=\"button\" disabled>Install</button></div>";
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
  html += "const firmwareInstallButton=document.getElementById('firmware_install_button');";
  html += "const firmwareKeepDataNotice=document.getElementById('firmware_keep_data_notice');";
  html += "const firmwareStatus=document.getElementById('firmware_status');";
  html += "const firmwareProgress=document.getElementById('firmware_progress');";
  html += "const firmwareStepText=document.getElementById('firmware_step_text');";
  html += "const releaseCheckButton=document.getElementById('release_check_button');";
  html += "const releaseStatus=document.getElementById('release_status');";
  html += "const releaseSummary=document.getElementById('release_summary');";
  html += "const releaseLatestVersion=document.getElementById('release_latest_version');";
  html += "const releaseNotes=document.getElementById('release_notes');";
  html += "const latestReleaseUrlRow=document.getElementById('latest_release_url_row');";
  html += "const latestReleaseUrlText=document.getElementById('latest_release_url_text');";
  html += "const copyLatestReleaseUrlButton=document.getElementById('copy_latest_release_url_button');";
  html += "const batteryLogText=document.getElementById('battery_log_text');";
  html += "const copyBatteryLogButton=document.getElementById('copy_battery_log_button');";
  html += "const batteryLogStatus=document.getElementById('battery_log_status');";
  html += "const mqttPassInput=document.getElementById('mqtt_pass');";
  html += "const clearMqttPass=document.getElementById('clear_mqtt_pass');";
  html += "const freedomPreviewValue=document.getElementById('freedom_preview_value');";
  html += "const freedomPreviewHint=document.getElementById('freedom_preview_hint');";
  html += "let validatedSignature='';";
  html += "let onlineFirmwareAvailable=false;";
  html += "let previewPriceUsd=";
  html += String(previewPriceUsd, 2);
  html += ";";
  html += "let previewHasPriceUsd=";
  html += previewHasPriceUsd ? "true" : "false";
  html += ";";
  html += "let previewMqttBalanceBtc=";
  html += String(previewBalanceBtc, 8);
  html += ";";
  html += "let previewHasMqttBalanceBtc=";
  html += previewHasBalanceBtc ? "true" : "false";
  html += ";";
  html += "const SETUP_SCROLL_KEY='freedom_clock_setup_scroll_y';";
  html += "let setupUserInteracted=false;";
  html += "let validationInFlight=false;";
  html += "let releaseCheckInFlight=false;";
  html += "let firmwareInstallInFlight=false;";
  html += "let firmwareProgressInterval=null;";
  html += "let firmwareProgressStart=0;";
  html += "['pointerdown','touchstart','keydown'].forEach(function(name){window.addEventListener(name,function(){setupUserInteracted=true;},{once:true,passive:true});});";
  html += "function signature(){return form?new URLSearchParams(new FormData(form)).toString():'';}";
  html += "function setStatus(text,kind){if(!statusBox)return;if(!text){statusBox.style.display='none';statusBox.textContent='';statusBox.className='message info';return;}statusBox.textContent=text;statusBox.className='message '+(kind||'info');statusBox.style.display='block';}";
  html += "function setFirmwareStatus(text,kind){if(!firmwareStatus)return;if(!text){firmwareStatus.style.display='none';firmwareStatus.textContent='';firmwareStatus.className='message info';return;}firmwareStatus.textContent=text;firmwareStatus.className='message '+(kind||'info');firmwareStatus.style.display='block';}";
  html += "function setReleaseStatus(text,kind){if(!releaseStatus)return;if(!text){releaseStatus.style.display='none';releaseStatus.textContent='';releaseStatus.className='message info';return;}releaseStatus.textContent=text;releaseStatus.className='message '+(kind||'info');releaseStatus.style.display='block';}";
  html += "function setBatteryLogStatus(text,kind){if(!batteryLogStatus)return;if(!text){batteryLogStatus.style.display='none';batteryLogStatus.textContent='';batteryLogStatus.className='message info';return;}batteryLogStatus.textContent=text;batteryLogStatus.className='message '+(kind||'info');batteryLogStatus.style.display='block';}";
  html += "function invalidate(msg){validatedSignature='';if(saveButton)saveButton.disabled=true;if(msg)setStatus(msg,'info');else setStatus('', 'info');}";
  html += "function focusWithoutScroll(el){if(!el||!el.focus)return;try{el.focus({preventScroll:true});}catch(e){el.focus();}}";
  html += "function blurEditableFocus(){const el=document.activeElement;if(!el)return;const tag=String(el.tagName||'').toUpperCase();if((tag==='INPUT'||tag==='SELECT'||tag==='TEXTAREA')&&el.blur)el.blur();}";
  html += "function rememberScrollPosition(y){try{sessionStorage.setItem(SETUP_SCROLL_KEY,String(Math.max(0,Math.round(y||0))));}catch(e){}}";
  html += "function takeRememberedScrollPosition(){try{const raw=sessionStorage.getItem(SETUP_SCROLL_KEY);sessionStorage.removeItem(SETUP_SCROLL_KEY);const parsed=parseInt(raw||'',10);return Number.isFinite(parsed)&&parsed>0?parsed:null;}catch(e){return null;}}";
  html += "function clearRememberedScrollPositionSoon(){setTimeout(function(){try{sessionStorage.removeItem(SETUP_SCROLL_KEY);}catch(e){}},5000);}";
  html += "function restoreScrollPosition(y,focusEl){const target=Math.max(0,y||0);const restore=function(){window.scrollTo(0,target);if(focusEl)focusWithoutScroll(focusEl);else blurEditableFocus();};if(focusEl)focusWithoutScroll(focusEl);else blurEditableFocus();requestAnimationFrame(restore);[0,120].forEach(function(delay){setTimeout(restore,delay);});}";
  html += "function suppressInitialInputFocus(){if('scrollRestoration' in history)history.scrollRestoration='manual';const remembered=takeRememberedScrollPosition();if(remembered!==null){restoreScrollPosition(remembered);return;}const guard=document.getElementById('focus_guard');const run=function(){if(setupUserInteracted)return;if(guard&&guard.focus){try{guard.focus({preventScroll:true});}catch(e){}}blurEditableFocus();if((window.scrollY||document.documentElement.scrollTop||0)<80)window.scrollTo(0,0);};[0,250,750].forEach(function(delay){setTimeout(run,delay);});window.addEventListener('pageshow',function(){setTimeout(run,0);});}";
  html += "function syncOwnerUppercase(){if(ownerInput)ownerInput.value=(ownerInput.value||'').toUpperCase().slice(0,";
  html += String(OWNER_NAME_MAX_DISPLAY_CHARS);
  html += ");}";
  html += "function sanitizeNumberInput(el){if(!el)return;const mode=el.getAttribute('data-number')||'decimal';let value=String(el.value||'').replace(/,/g,'.');if(mode==='int'){el.value=value.replace(/[^0-9]/g,'');return;}value=value.replace(/[^0-9.]/g,'');const firstDot=value.indexOf('.');if(firstDot!==-1){value=value.slice(0,firstDot+1)+value.slice(firstDot+1).replace(/[.]/g,'');}el.value=value;}";
  html += "function wireNumberInputs(){document.querySelectorAll('[data-number]').forEach(function(el){sanitizeNumberInput(el);el.addEventListener('input',function(){sanitizeNumberInput(el);});el.addEventListener('paste',function(){setTimeout(function(){sanitizeNumberInput(el);},0);});});}";
  html += "function normalizeVersion(text){return String(text||'').trim().replace(/^v/i,'');}";
  html += "async function copyText(text,label){const value=String(text||'').trim();if(!value){setReleaseStatus('Nothing to copy yet.','err');return;}try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(value);}else{const area=document.createElement('textarea');area.value=value;area.setAttribute('readonly','');area.style.position='fixed';area.style.left='-9999px';document.body.appendChild(area);area.select();document.execCommand('copy');document.body.removeChild(area);}setReleaseStatus((label||'Link')+' copied. If iOS blocks clipboard access, select the URL text manually.','ok');}catch(err){setReleaseStatus('Copy failed. Select the URL text manually and copy it from there.','err');}}";
  html += "async function copyBatteryLog(){const value=String((batteryLogText&&batteryLogText.value)||'').trim();if(!value){setBatteryLogStatus('No battery log to copy yet.','err');return;}try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(value);}else{batteryLogText.focus();batteryLogText.select();document.execCommand('copy');}setBatteryLogStatus('Battery log copied.','ok');}catch(err){setBatteryLogStatus('Copy failed. Select the log text manually and copy it.','err');}}";
  html += "function hasManualFirmwareFile(){return !!(firmwareFileInput&&firmwareFileInput.files&&firmwareFileInput.files.length>0);}";
  html += "function updateFirmwareInstallButton(){const enabled=!!(onlineFirmwareAvailable||hasManualFirmwareFile());if(firmwareInstallButton)firmwareInstallButton.disabled=!enabled||firmwareInstallInFlight;if(firmwareKeepDataNotice)firmwareKeepDataNotice.classList.toggle('hidden',!enabled||firmwareInstallInFlight);}";
  html += "function hideReleaseSummary(){onlineFirmwareAvailable=false;if(releaseSummary)releaseSummary.classList.add('hidden');if(latestReleaseUrlRow)latestReleaseUrlRow.classList.add('hidden');updateFirmwareInstallButton();}";
  html += "function handleUnlockRequired(data,setter){if(data&&data.unlock_required){if(setter)setter(data.message||'Setup session expired. Enter your PIN again.','err');setTimeout(function(){window.location.replace('/');},1200);return true;}return false;}";
  html += "function renderReleaseInfo(data){hideReleaseSummary();setFirmwareStatus('', 'info');const latestTag=String((data&&data.tag)||'').trim();const notes=String((data&&data.body)||'No release notes provided.').trim()||'No release notes provided.';const url=String((data&&data.html_url)||'').trim();const assetAvailable=!!(data&&data.asset_available);const newer=!!(data&&data.newer);onlineFirmwareAvailable=assetAvailable&&newer;if(releaseLatestVersion)releaseLatestVersion.textContent=latestTag||'Unknown';if(releaseNotes)releaseNotes.textContent=notes;if(latestReleaseUrlText)latestReleaseUrlText.textContent=url||'";
  html += GITHUB_RELEASES_URL;
  html += "';const latestNormalized=normalizeVersion(latestTag);const currentNormalized=normalizeVersion('";
  html += FIRMWARE_VERSION;
  html += "');updateFirmwareInstallButton();if(!assetAvailable){setReleaseStatus('Latest release loaded, but the matching firmware package was not found. Use manual update.','err');setFirmwareStatus('Matching online firmware package was not found. Use the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin package for manual update.','err');return;}if(!newer){setReleaseStatus('You are using the latest version.','ok');return;}if(latestReleaseUrlRow)latestReleaseUrlRow.classList.remove('hidden');if(releaseSummary)releaseSummary.classList.remove('hidden');setReleaseStatus('New firmware is available.','ok');}";
  html += "function readNumber(id){const el=document.getElementById(id);const value=parseFloat(el&&el.value?el.value:'');return Number.isFinite(value)?value:0;}";
  html += "function formatMoney(value){if(!(value>=0))return '--';if(value>=1000000)return (value/1000000).toFixed(2)+' mil USD';if(value>=1000)return (value/1000).toFixed(2)+'k USD';return value.toFixed(2)+' USD';}";
  html += "function computeFreedom(usdWealth,monthlyExpenseToday,inflationAnnual,assetGrowthAnnual,useMode,borrowFeeAnnual){const out={hitCap:false,years:0,months:0,weeks:0,coveredWeeks:0};if(!(usdWealth>0)||!(monthlyExpenseToday>0))return out;if(!(inflationAnnual>=0))inflationAnnual=0;if(!(assetGrowthAnnual>-0.99))assetGrowthAnnual=-0.99;if(!(borrowFeeAnnual>-0.99))borrowFeeAnnual=-0.99;const maxYears=200;const maxMonths=12*maxYears;if(useMode==='1'){let annualExpenseMul=1+inflationAnnual;let annualAssetMul=1+assetGrowthAnnual;let annualDebtMul=1+borrowFeeAnnual;let annualExpense=monthlyExpenseToday*12;let collateralValue=usdWealth;let debt=0;let years=0;while(years<maxYears){collateralValue*=annualAssetMul;debt*=annualDebtMul;if((debt+annualExpense)>collateralValue)break;debt+=annualExpense;annualExpense*=annualExpenseMul;years++;}out.hitCap=years>=maxYears;let partialYear=0;if(!out.hitCap&&annualExpense>0&&collateralValue>debt){partialYear=(collateralValue-debt)/annualExpense;if(partialYear>0.999)partialYear=0.999;if(partialYear<0)partialYear=0;}const coveredMonthsFloat=(years+partialYear)*12;const coveredFullMonths=Math.floor(coveredMonthsFloat);const partialMonth=coveredMonthsFloat-coveredFullMonths;out.years=Math.floor(coveredFullMonths/12);out.months=coveredFullMonths%12;out.weeks=Math.max(0,Math.min(4,Math.floor(partialMonth*4.345+0.5)));out.coveredWeeks=coveredMonthsFloat*4.345;return out;}let monthlyExpenseMul=Math.pow(1+inflationAnnual,1/12);let monthlyAssetMul=Math.pow(1+assetGrowthAnnual,1/12);let monthlyExpense=monthlyExpenseToday;let remaining=usdWealth;let months=0;while(months<maxMonths){remaining*=monthlyAssetMul;if(remaining<monthlyExpense)break;remaining-=monthlyExpense;monthlyExpense*=monthlyExpenseMul;months++;}out.hitCap=months>=maxMonths;out.years=Math.floor(months/12);out.months=months%12;let partialMonth=0;if(monthlyExpense>0&&remaining>0){partialMonth=remaining/monthlyExpense;if(partialMonth>0.999)partialMonth=0.999;if(partialMonth<0)partialMonth=0;}out.weeks=Math.max(0,Math.min(4,Math.floor(partialMonth*4.345+0.5)));out.coveredWeeks=months*4.345+(partialMonth*4.345);return out;}";
  html += "function formatFreedom(result){if(result.hitCap)return 'FOREVER';if(result.years>99)return String(result.years)+'Y';const years=String(result.years).padStart(2,'0');const months=String(result.months).padStart(2,'0');return years+'Y '+months+'M '+String(result.weeks)+'W';}";
  html += "function capturePreviewMarketValues(message){const text=String(message||'');const priceMatch=text.match(/(?:BTC price|Price):\\s*([0-9]+(?:\\.[0-9]+)?)/i);if(priceMatch){const parsed=parseFloat(priceMatch[1]);if(Number.isFinite(parsed)&&parsed>0){previewPriceUsd=parsed;previewHasPriceUsd=true;}}const amountMatch=text.match(/(?:BTC amount|Amount):\\s*([0-9]+(?:\\.[0-9]+)?)/i);if(amountMatch){const parsed=parseFloat(amountMatch[1]);if(Number.isFinite(parsed)&&parsed>=0){previewMqttBalanceBtc=parsed;previewHasMqttBalanceBtc=true;}}}";
  html += "function updateFreedomPreview(){if(!freedomPreviewValue||!freedomPreviewHint)return;const assetMode=asset?asset.value:'2';const useMode=mode?mode.value:'0';let usdWealth=0;let hint='';if(assetMode==='1'){usdWealth=readNumber('default_wealth_usd');}else if(assetMode==='2'){const btc=readNumber('manual_btc_amount');if(!previewHasPriceUsd||!(previewPriceUsd>0)){freedomPreviewValue.textContent='--';freedomPreviewHint.textContent='Run Test Connection to fetch BTC/USD price.';return;}usdWealth=btc*previewPriceUsd;}else{if(!previewHasPriceUsd||!previewHasMqttBalanceBtc||!(previewPriceUsd>0)||!(previewMqttBalanceBtc>=0)){freedomPreviewValue.textContent='--';freedomPreviewHint.textContent='Run Test Connection to load BTC price and amount.';return;}usdWealth=previewMqttBalanceBtc*previewPriceUsd;}const result=computeFreedom(usdWealth,readNumber('monthly_exp_usd'),readNumber('inflation_annual_pct')/100,readNumber('wealth_growth_annual_pct')/100,useMode,readNumber('borrow_fee_annual_pct')/100);freedomPreviewValue.textContent=formatFreedom(result);if(result.hitCap)hint='FOREVER means the model reached the device cap of 200 years.';freedomPreviewHint.textContent=hint;}";
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
  html += "updateFreedomPreview();";
  html += "}";
  html += "async function refreshWifiList(){";
  html += "if(scanButton)scanButton.disabled=true;";
  html += "if(wifiSelect){wifiSelect.disabled=true;wifiSelect.innerHTML='<option value=\"\">Scanning nearby networks...</option>';}";
  html += "try{";
  html += "const response=await fetch('/wifi-list',{cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setStatus))return;";
  html += "if(!data.ok)throw new Error(data.message||'Wi-Fi scan failed.');";
  html += "const networks=Array.isArray(data.networks)?data.networks:[];";
  html += "if(wifiSelect){wifiSelect.innerHTML='';const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent=networks.length?'Choose a nearby network':'No networks found';wifiSelect.appendChild(placeholder);const currentSsid=wifiSsidInput?wifiSsidInput.value:'';networks.forEach(function(net){const option=document.createElement('option');option.value=net.ssid||'';option.textContent=(net.ssid||'');if(currentSsid&&option.value===currentSsid)option.selected=true;wifiSelect.appendChild(option);});wifiSelect.disabled=false;}";
  html += "}catch(err){if(wifiSelect){wifiSelect.innerHTML='<option value=\"\">Type your Wi-Fi name manually</option>';wifiSelect.disabled=false;}setStatus(err&&err.message?err.message:'Wi-Fi scan failed. Type the SSID manually.','err');}";
  html += "if(scanButton)scanButton.disabled=false;";
  html += "}";
  html += "async function validateCurrentSettings(event){";
  html += "if(event){event.preventDefault();event.stopPropagation();}";
  html += "if(validationInFlight)return;";
  html += "validationInFlight=true;";
  html += "const actionButton=event&&event.currentTarget?event.currentTarget:validateButton;";
  html += "const previousScrollY=window.scrollY||document.documentElement.scrollTop||0;";
  html += "rememberScrollPosition(previousScrollY);";
  html += "focusWithoutScroll(actionButton);";
  html += "window.scrollTo(0,previousScrollY);";
  html += "const currentSignature=signature();";
  html += "validatedSignature='';";
  html += "if(saveButton)saveButton.disabled=true;";
  html += "if(scanButton)scanButton.disabled=true;";
  html += "setStatus('Testing current settings. This can take a few seconds...','info');";
  html += "try{";
  html += "const response=await fetch('/validate',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:new URLSearchParams(new FormData(form)).toString(),cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setStatus))return;";
  html += "if(!data.ok){setStatus(data.message||'Validation failed.','err');return;}";
  html += "if(currentSignature!==signature()){setStatus('Validation finished, but the form changed meanwhile. Run the test again.','info');return;}";
  html += "validatedSignature=currentSignature;";
  html += "capturePreviewMarketValues(data.message||'');";
  html += "updateFreedomPreview();";
  html += "if(saveButton)saveButton.disabled=false;";
  html += "setStatus(data.message||'Validation successful.','ok');";
  html += "}catch(err){setStatus('Validation request failed. Keep this phone connected to the device Wi-Fi and try again.','err');}";
  html += "finally{validationInFlight=false;if(scanButton)scanButton.disabled=false;restoreScrollPosition(previousScrollY,actionButton);clearRememberedScrollPositionSoon();}";
  html += "}";
  html += "async function checkLatestRelease(event){";
  html += "if(event){event.preventDefault();event.stopPropagation();}";
  html += "if(releaseCheckInFlight)return;";
  html += "releaseCheckInFlight=true;";
  html += "const actionButton=event&&event.currentTarget?event.currentTarget:releaseCheckButton;";
  html += "const previousScrollY=window.scrollY||document.documentElement.scrollTop||0;";
  html += "rememberScrollPosition(previousScrollY);";
  html += "focusWithoutScroll(actionButton);";
  html += "window.scrollTo(0,previousScrollY);";
  html += "hideReleaseSummary();";
  html += "setReleaseStatus('Checking GitHub for the latest published release. This can take a few seconds...','info');";
  html += "try{";
  html += "const response=await fetch('/release-info',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:form?new URLSearchParams(new FormData(form)).toString():'',cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setReleaseStatus))return;";
  html += "if(!data.ok){setReleaseStatus(data.message||'Could not load release info.','err');return;}";
  html += "renderReleaseInfo(data);";
  html += "}catch(err){setReleaseStatus('Release check failed. Keep this phone connected and make sure the device can reach the internet over Wi-Fi.','err');}";
  html += "finally{releaseCheckInFlight=false;restoreScrollPosition(previousScrollY,actionButton);clearRememberedScrollPositionSoon();}";
  html += "}";
  html += "function validateManualFirmwareFile(){if(!firmwareFileInput||!firmwareFileInput.files||firmwareFileInput.files.length===0){setFirmwareStatus('Choose a firmware .bin file first.','err');return false;}const fileName=String((firmwareFileInput.files[0]&&firmwareFileInput.files[0].name)||'').toLowerCase();if(!fileName.endsWith('.bin')){setFirmwareStatus('Firmware file must end with .bin.','err');return false;}return true;}";
  html += "const installSteps=[{ms:0,text:'Connecting to Wi-Fi...'},{ms:7000,text:'Checking GitHub for the latest release...'},{ms:14000,text:'Downloading firmware from GitHub...'},{ms:25000,text:'Installing firmware on device...'},{ms:37000,text:'Wrapping up... device will reboot shortly'}];";
  html += "function startFirmwareProgress(){";
  html += "if(!firmwareProgress)return;";
  html += "firmwareProgressStart=Date.now();";
  html += "firmwareProgress.classList.remove('hidden');";
  html += "setFirmwareStatus('','info');";
  html += "if(firmwareStepText)firmwareStepText.textContent=installSteps[0].text;";
  html += "firmwareProgressInterval=setInterval(function(){";
  html += "const elapsed=Date.now()-firmwareProgressStart;";
  html += "let step=installSteps[0];";
  html += "for(let i=1;i<installSteps.length;i++){if(elapsed>=installSteps[i].ms)step=installSteps[i];}";
  html += "if(firmwareStepText)firmwareStepText.textContent=step.text;";
  html += "},500);}";
  html += "function stopFirmwareProgress(){";
  html += "if(firmwareProgressInterval){clearInterval(firmwareProgressInterval);firmwareProgressInterval=null;}";
  html += "if(firmwareProgress)firmwareProgress.classList.add('hidden');}";
  html += "async function installOnlineFirmware(){";
  html += "if(firmwareInstallInFlight)return;";
  html += "firmwareInstallInFlight=true;";
  html += "updateFirmwareInstallButton();";
  html += "if(releaseCheckButton)releaseCheckButton.disabled=true;";
  html += "startFirmwareProgress();";
  html += "setReleaseStatus('Installing firmware from GitHub. Keep this phone connected. The device will reboot when finished.','info');";
  html += "try{";
  html += "const response=await fetch('/firmware-online',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:form?new URLSearchParams(new FormData(form)).toString():'',cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setFirmwareStatus))return;";
  html += "if(!data.ok){const message=data.message||'Firmware install failed.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');return;}";
  html += "const message=data.message||'Firmware installed. Rebooting ...';setFirmwareStatus(message,'ok');setReleaseStatus(message,'ok');";
  html += "}catch(err){const message='Firmware install request failed. Use manual update if the device does not restart.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');}";
  html += "finally{stopFirmwareProgress();firmwareInstallInFlight=false;if(releaseCheckButton)releaseCheckButton.disabled=false;updateFirmwareInstallButton();}";
  html += "}";
  html += "function installSelectedFirmware(){if(hasManualFirmwareFile()){if(!validateManualFirmwareFile())return;if(firmwareInstallButton)firmwareInstallButton.disabled=true;if(firmwareKeepDataNotice)firmwareKeepDataNotice.classList.add('hidden');setFirmwareStatus('Uploading firmware. Keep this phone connected until the device restarts.','info');if(firmwareForm)firmwareForm.submit();return;}if(onlineFirmwareAvailable){installOnlineFirmware();return;}const message='Choose a firmware .bin file or check for a newer release first.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');";
  html += "}";
  html += "function onFormEdited(event){if(event&&event.target===wifiSelect)return;updateFreedomPreview();invalidate();}";
  html += "if(asset) asset.addEventListener('change', update);";
  html += "if(mode) mode.addEventListener('change', update);";
  html += "if(setupPinEnabled) setupPinEnabled.addEventListener('change', update);";
  html += "if(refreshUnitSelect) refreshUnitSelect.addEventListener('change', updateRefreshControls);";
  html += "if(ownerInput) ownerInput.addEventListener('input', syncOwnerUppercase);";
  html += "if(wifiPassInput) wifiPassInput.addEventListener('input', function(){if(clearWifiPass&&wifiPassInput.value)clearWifiPass.checked=false;});";
  html += "if(mqttPassInput) mqttPassInput.addEventListener('input', function(){if(clearMqttPass&&mqttPassInput.value)clearMqttPass.checked=false;});";
  html += "if(wifiSelect) wifiSelect.addEventListener('change', function(){if(wifiSsidInput)wifiSsidInput.value=wifiSelect.value||'';invalidate();});";
  html += "if(form){form.addEventListener('input', onFormEdited);form.addEventListener('change', onFormEdited);form.addEventListener('submit', function(event){if(!validatedSignature||(saveButton&&saveButton.disabled)){event.preventDefault();setStatus('Please test the current settings before saving.','err');}});}";
  html += "if(firmwareFileInput) firmwareFileInput.addEventListener('change', function(){setFirmwareStatus('', 'info');updateFirmwareInstallButton();});";
  html += "if(releaseCheckButton) releaseCheckButton.addEventListener('click', checkLatestRelease);";
  html += "if(firmwareInstallButton) firmwareInstallButton.addEventListener('click', installSelectedFirmware);";
  html += "if(copyLatestReleaseUrlButton) copyLatestReleaseUrlButton.addEventListener('click', function(){copyText(latestReleaseUrlText?latestReleaseUrlText.textContent:'','Latest release URL');});";
  html += "if(copyBatteryLogButton) copyBatteryLogButton.addEventListener('click', copyBatteryLog);";
  html += "if(scanButton) scanButton.addEventListener('click', refreshWifiList);";
  html += "if(validateButton) validateButton.addEventListener('click', validateCurrentSettings);";
  html += "suppressInitialInputFocus();";
  html += "wireNumberInputs();";
  html += "syncOwnerUppercase();";
  html += "update();";
  html += "updateFreedomPreview();";
  html += "invalidate();";
  html += "updateFirmwareInstallButton();";
  html += "refreshWifiList();";
  html += "})();";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

static constexpr int BATTERY_BODY_W = 20;
static constexpr int BATTERY_BODY_H = 10;
static constexpr int BATTERY_TIP_W = 2;
static constexpr int BATTERY_TIP_H = 5;
static constexpr int BATTERY_RIGHT_MARGIN = 2;
static constexpr int BATTERY_TEXT_GAP = 2;
static constexpr int BATTERY_MAX_TEXT_W = 24;
static constexpr int BATTERY_GROUP_MIN_X = DEVICE_DISPLAY_WIDTH
  - BATTERY_RIGHT_MARGIN
  - BATTERY_MAX_TEXT_W
  - BATTERY_TEXT_GAP
  - BATTERY_BODY_W
  - BATTERY_TIP_W;
static constexpr int BATTERY_ICON_Y = 4;
static constexpr int BATTERY_TEXT_Y = 6;

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

static void drawBatteryStatus(int deviceBatteryPct, DisplayThemeMode themeMode) {
  char pctText[8];
  snprintf(pctText, sizeof(pctText), "%d%%", clampInt(deviceBatteryPct, 0, 100));
  const int textW = estimateTextWidthSize1(pctText);
  const int textX = DEVICE_DISPLAY_WIDTH - BATTERY_RIGHT_MARGIN - textW;
  const int iconX = textX - BATTERY_TEXT_GAP - BATTERY_BODY_W - BATTERY_TIP_W;

  drawBatteryIcon(
    iconX,
    BATTERY_ICON_Y,
    deviceBatteryPct,
    BATTERY_BODY_W,
    BATTERY_BODY_H,
    BATTERY_TIP_W,
    BATTERY_TIP_H,
    themeMode
  );
  display.setCursor(textX, BATTERY_TEXT_Y);
  display.setTextSize(1);
  display.print(pctText);
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
#if DEVICE_PROFILE_E290
  const int LEFT_X = 8;
  const int NUMBER_LEFT_X = LEFT_X + 10;
  const int NUMBER_STEP_X = 62;
  const int MONTH_X = NUMBER_LEFT_X + NUMBER_STEP_X;
  const int WEEK_X = NUMBER_LEFT_X + (NUMBER_STEP_X * 2);
  const int DIVIDER_X = 214;
  const int RIGHT_X = 226;
  const int BATTERY_X = BATTERY_GROUP_MIN_X;
  const int TOP_NUMBER_Y = 23;
  const int TOP_SUFFIX_Y = 28;
  const int MID_LINE_Y = 59;
  const int LIFE_TITLE_Y = 70;
  const int BOTTOM_NUMBER_Y = 89;
  const int BOTTOM_SUFFIX_Y = 94;
  const int RIGHT_TITLE_Y = 36;
  const int RIGHT_VALUE_Y = 66;
  const int RIGHT_TITLE_X = RIGHT_X + 4;
  const int RIGHT_VALUE_X = RIGHT_X + 8;
#else
  const int LEFT_X = 4;
  const int NUMBER_LEFT_X = LEFT_X + 10;
  const int NUMBER_STEP_X = 62;
  const int MONTH_X = NUMBER_LEFT_X + NUMBER_STEP_X;
  const int WEEK_X = NUMBER_LEFT_X + (NUMBER_STEP_X * 2);
  const int DIVIDER_X = 181;
  const int RIGHT_X = 188;
  const int BATTERY_X = BATTERY_GROUP_MIN_X;
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
#endif
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

  drawBatteryStatus(deviceBatteryPct, themeMode);

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
  static constexpr int TITLE_X = 8;
  static constexpr int TITLE_Y = 12;
  static constexpr int LABEL_X = 10;
  static constexpr int VALUE_X = 142;
  static constexpr int ROW_Y0 = 30;
  static constexpr int ROW_STEP = 10;

  prepareScreen(themeMode);

  display.setTextSize(1);
  drawBatteryStatus(deviceBatteryPct, themeMode);

  display.setTextSize(1);
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("SETTINGS");

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
  snprintf(value, sizeof(value), "~%d Y", currentAgeFromBirthYear(cfg.birthYear, now));
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 6));
  display.print("LIFE EXPECTANCY:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 6));
  snprintf(value, sizeof(value), "%d Y", cfg.lifeExpectancyYears);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 7));
  display.print("SPEND MODE:");
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
  static constexpr int TITLE_Y = 12;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 44;
  static constexpr int ROW_STEP = 14;

  prepareScreen(themeMode);

  drawBatteryStatus(deviceBatteryPct, themeMode);

  display.setTextSize(1);
  formatCompactUsd(usdWealth, currentWealth, sizeof(currentWealth));
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("WEALTH CHANGE (CURRENT:");
  display.print(currentWealth);
  display.print(")");

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

static void formatFreedomDuration(bool hitCap, int years, int months, int weeks, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  if (hitCap) {
    safeCopyCString(dst, dstSize, "FOREVER");
    return;
  }
  snprintf(dst, dstSize, "%dy %dm %dw", years, months, weeks);
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
  char currentFreedomText[20];
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);

  prepareScreen(themeMode);

  drawBatteryStatus(deviceBatteryPct, themeMode);

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
  formatFreedomDuration(currentFreedomHitCap, currentYears, currentMonths, currentWeeks, currentFreedomText, sizeof(currentFreedomText));

  display.setTextSize(1);
  static constexpr int TITLE_X = 8;
  static constexpr int TITLE_Y = 12;
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("FREEDOM CHANGE (CURRENT:");
  display.print(currentFreedomText);
  display.print(")");

  static constexpr int LAST_X = 10;
  static constexpr int FREEDOM_X = 44;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 46;
  static constexpr int ROW_STEP = 14;

  display.setCursor(LAST_X, HEADER_Y);
  display.print("LAST");
  display.setCursor(FREEDOM_X, HEADER_Y);
  display.print("FREEDOM");

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

    if (currentFreedomHitCap && !previousFreedomHitCap) {
      safeCopyCString(freedomDeltaText, sizeof(freedomDeltaText), "FOREVER");
    } else {
      const float freedomDeltaWeeks = currentFreedomHitCap && previousFreedomHitCap
        ? 0.0f
        : (currentCoveredWeeks - previousCoveredWeeks);
      formatSignedWeeksDelta(freedomDeltaWeeks, freedomDeltaText, sizeof(freedomDeltaText));
    }
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

static void portalSendUnlockRequiredJson() {
  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(
    200,
    "application/json; charset=utf-8",
    "{\"ok\":false,\"unlock_required\":true,\"message\":\"Setup session expired. Enter your PIN again.\"}"
  );
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
  normalizeDecimalInputText(
    portalServer.arg("manual_btc_amount"),
    submitted.manualBtcAmountText,
    sizeof(submitted.manualBtcAmountText),
    8,
    DEFAULT_MANUAL_BTC_AMOUNT_TEXT
  );
  submitted.manualBtcAmount = strtof(submitted.manualBtcAmountText, nullptr);
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

static bool installFirmwareFromUrl(const String& firmwareUrl, char* errorBuf, size_t errorBufSize) {
  if (firmwareUrl.length() == 0) {
    snprintf(errorBuf, errorBufSize, "Firmware download URL is missing.");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(errorBuf, errorBufSize, "Wi-Fi is not connected.");
    return false;
  }

  if (!ensureClockReadyForTls(errorBuf, errorBufSize)) {
    return false;
  }

  // Resolve any redirect first (GitHub release assets redirect to a CDN).
  // The ESP32 HTTPClient does not reliably reconnect across different HTTPS
  // hosts when following redirects, so we extract the Location header with a
  // short probe request and download from the final URL directly.
  String downloadUrl = firmwareUrl;
  {
    char resolveError[80];
    TrustedWiFiClientSecure redirectClient;
    if (configureTrustedTlsClient(redirectClient, resolveError, sizeof(resolveError))) {
      HTTPClient httpProbe;
      httpProbe.setTimeout(FIRMWARE_HTTP_TIMEOUT_MS);
      httpProbe.setConnectTimeout(FIRMWARE_HTTP_TIMEOUT_MS);
      httpProbe.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      if (httpProbe.begin(redirectClient, firmwareUrl)) {
        httpProbe.addHeader("Accept", "application/octet-stream");
        httpProbe.addHeader("User-Agent", "FreedomClock/2026");
        const int probeCode = httpProbe.GET();
        if (probeCode >= 301 && probeCode <= 308) {
          const String& location = httpProbe.getLocation();
          if (location.startsWith("https://") || location.startsWith("http://")) {
            downloadUrl = location;
          }
        }
        httpProbe.end();
      }
    }
  }

  TrustedWiFiClientSecure secureClient;
  if (!configureTrustedTlsClient(secureClient, errorBuf, errorBufSize)) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(FIRMWARE_HTTP_TIMEOUT_MS);
  http.setConnectTimeout(FIRMWARE_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(secureClient, downloadUrl)) {
    snprintf(errorBuf, errorBufSize, "Could not start firmware download.");
    return false;
  }

  http.addHeader("Accept", "application/octet-stream");
  http.addHeader("User-Agent", "FreedomClock/2026");

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    const String httpError = http.errorToString(httpCode);
    if (httpError.length() > 0) {
      snprintf(errorBuf, errorBufSize, "Firmware download failed (%d: %s).", httpCode, httpError.c_str());
    } else {
      snprintf(errorBuf, errorBufSize, "Firmware download failed (%d).", httpCode);
    }
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    formatFirmwareUpdateError(errorBuf, errorBufSize);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t totalWritten = 0;
  int remaining = contentLength;
  uint32_t lastDataMs = millis();

  while (http.connected() && (remaining > 0 || contentLength <= 0)) {
    const size_t available = stream->available();
    if (available > 0) {
      const size_t toRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
      const int bytesRead = stream->readBytes(buffer, toRead);
      if (bytesRead <= 0) {
        delay(1);
        continue;
      }
      if (Update.write(buffer, (size_t)bytesRead) != (size_t)bytesRead) {
        formatFirmwareUpdateError(errorBuf, errorBufSize);
        Update.abort();
        http.end();
        return false;
      }
      totalWritten += (size_t)bytesRead;
      if (remaining > 0) remaining -= bytesRead;
      lastDataMs = millis();
      refreshPortalUnlockSession();
      continue;
    }

    if ((millis() - lastDataMs) > FIRMWARE_DOWNLOAD_IDLE_TIMEOUT_MS) {
      snprintf(errorBuf, errorBufSize, "Firmware download timed out.");
      Update.abort();
      http.end();
      return false;
    }
    delay(5);
  }

  if (contentLength > 0 && totalWritten != (size_t)contentLength) {
    snprintf(errorBuf, errorBufSize, "Firmware download ended early.");
    Update.abort();
    http.end();
    return false;
  }

  if (totalWritten == 0) {
    snprintf(errorBuf, errorBufSize, "Firmware download returned no data.");
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end(true)) {
    formatFirmwareUpdateError(errorBuf, errorBufSize);
    http.end();
    return false;
  }

  http.end();
  errorBuf[0] = '\0';
  return true;
}

static void handlePortalFirmwareOnline() {
  if (hasSetupPinConfigured(deviceConfig)) {
    if (isPortalUnlockExpired()) {
      portalUnlocked = false;
      portalUnlockedAtMs = 0;
    }
    if (!portalUnlocked) {
      portalSendUnlockRequiredJson();
      return;
    }
    refreshPortalUnlockSession();
  }

  DeviceConfig submitted = deviceConfig;
  loadSubmittedPortalConfig(submitted);

  if (!hasText(submitted.wifiSsid)) {
    portalSendJson(false, "Wi-Fi required\nAdd working Wi-Fi first to install online updates.");
    return;
  }

  if (!connectWiFi(submitted, WIFI_CONNECT_TIMEOUT_MS, true)) {
    portalSendJson(false, "Wi-Fi failed\nCheck the SSID and password, then try again.");
    return;
  }

  GitHubReleaseInfo releaseInfo;
  char errorMessage[160];
  if (!fetchLatestGitHubReleaseInfo(releaseInfo, errorMessage, sizeof(errorMessage))) {
    String error = String("Wi-Fi: OK\nRELEASE CHECK: failed\n") + errorMessage;
    portalSendJson(false, error.c_str());
    return;
  }

  String firmwareUrl;
  String packageLabel;
  if (!isReleaseNewerThanCurrent(releaseInfo)) {
    portalSendJson(false, "Latest published release is not newer than the firmware already on this device.");
    return;
  }

  if (!selectReleaseFirmwareUrl(releaseInfo, hardwareSecureBootActive, firmwareUrl, packageLabel)) {
    portalSendJson(false, "Latest release loaded, but the matching firmware package was not found.");
    return;
  }

  if (!installFirmwareFromUrl(firmwareUrl, errorMessage, sizeof(errorMessage))) {
    String error = String("Wi-Fi: OK\nRELEASE CHECK: OK\nFIRMWARE DOWNLOAD: failed\n") + errorMessage;
    portalSendJson(false, error.c_str());
    return;
  }

  checkpointPortalUnixTime();
  portalExitAction = PORTAL_EXIT_ACTION_FIRMWARE_UPDATE;
  portalSaveRequested = true;
  String success = String("Firmware updated from GitHub.\nPackage: ") + packageLabel + "\nRebooting ...";
  portalSendJson(true, success.c_str());
}

static void handlePortalWifiList() {
  if (hasSetupPinConfigured(deviceConfig)) {
    if (isPortalUnlockExpired()) {
      portalUnlocked = false;
      portalUnlockedAtMs = 0;
    }
    if (!portalUnlocked) {
      portalSendUnlockRequiredJson();
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
      portalSendUnlockRequiredJson();
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
    String successMessage = String("Wi-Fi: skipped\n")
      + "Static net worth mode does not require Wi-Fi.\n"
      + "Without Wi-Fi time sync, lifetime and coverage calculations can become less accurate."
      + sourceChangedNote;
    portalSendJson(true, successMessage.c_str());
    return;
  }

  if (!connectWiFi(submitted, WIFI_CONNECT_TIMEOUT_MS, true)) {
    portalSendJson(false, "Wi-Fi failed\nCheck the SSID and password, then try again.");
    return;
  }

  if (assetMode == ASSET_MODE_WEALTH) {
    String successMessage = String("Wi-Fi: OK")
      + sourceChangedNote;
    portalSendJson(true, successMessage.c_str());
    return;
  }

  if (isManualBtcAssetMode(assetMode)) {
    char fetchError[96];
    float validatedPrice = 0.0f;
    if (!fetchCoinGeckoBtcPriceUsd(validatedPrice, fetchError, sizeof(fetchError))) {
      String error = String("Wi-Fi: OK\nPRICE API: failed\n") + fetchError;
      portalSendJson(false, error.c_str());
      return;
    }

    char amountValue[24];
    char successMessage[256];
    formatTrimmedBtcAmount(submitted.manualBtcAmount, amountValue, sizeof(amountValue));
    snprintf(
      successMessage,
      sizeof(successMessage),
      "Wi-Fi: OK\nPRICE API: OK\n- BTC price: %.2f USD%s",
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
    portalSendJson(false, "Wi-Fi: OK\nMQTT: failed\nCheck the broker host, port, and credentials.");
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
    snprintf(priceLine, sizeof(priceLine), "- BTC price: %.2f USD", validatedPrice);
  } else {
    snprintf(priceLine, sizeof(priceLine), "- BTC price: %s", gotPrice ? "invalid number" : "no value returned");
  }
  if (balanceValid) {
    formatTrimmedBtcAmount(validatedBalance, amountValue, sizeof(amountValue));
    snprintf(amountLine, sizeof(amountLine), "- BTC amount: %s", amountValue);
  } else {
    snprintf(amountLine, sizeof(amountLine), "- BTC amount: %s", gotBalance ? "invalid number" : "no value returned");
  }

  if (!priceValid || !balanceValid) {
    String error = String("Wi-Fi: OK\nMQTT: OK\n")
      + priceLine + "\n"
      + amountLine + "\n"
      + "Use retained numeric messages on both topics.";
    portalSendJson(false, error.c_str());
    return;
  }

  String successMessage = String("Wi-Fi: OK\nMQTT: OK\n")
    + priceLine + "\n"
    + amountLine
    + sourceChangedNote;
  portalSendJson(true, successMessage.c_str());
}

static void handlePortalReleaseInfo() {
  if (hasSetupPinConfigured(deviceConfig)) {
    if (isPortalUnlockExpired()) {
      portalUnlocked = false;
      portalUnlockedAtMs = 0;
    }
    if (!portalUnlocked) {
      portalSendUnlockRequiredJson();
      return;
    }
    refreshPortalUnlockSession();
  }

  DeviceConfig submitted = deviceConfig;
  loadSubmittedPortalConfig(submitted);

  if (!hasText(submitted.wifiSsid)) {
    portalSendJson(false, "Wi-Fi required\nAdd working Wi-Fi first to check GitHub releases.");
    return;
  }

  if (!connectWiFi(submitted, WIFI_CONNECT_TIMEOUT_MS, true)) {
    portalSendJson(false, "Wi-Fi failed\nCheck the SSID and password, then try again.");
    return;
  }

  GitHubReleaseInfo releaseInfo;
  char fetchError[128];
  if (!fetchLatestGitHubReleaseInfo(releaseInfo, fetchError, sizeof(fetchError))) {
    String error = String("Wi-Fi: OK\nRELEASE CHECK: failed\n") + fetchError;
    portalSendJson(false, error.c_str());
    return;
  }

  String json = "{\"ok\":true,\"message\":\"Latest release loaded.\",\"tag\":\"";
  json += jsonEscape(releaseInfo.tagName.c_str());
  json += "\",\"name\":\"";
  json += jsonEscape(releaseInfo.name.c_str());
  json += "\",\"body\":\"";
  json += jsonEscape(releaseInfo.body.c_str());
  json += "\",\"html_url\":\"";
  json += jsonEscape(releaseInfo.htmlUrl.c_str());
  String firmwareUrl;
  String packageLabel;
  const bool hasMatchingAsset = selectReleaseFirmwareUrl(releaseInfo, hardwareSecureBootActive, firmwareUrl, packageLabel);
  json += "\",\"package\":\"";
  json += jsonEscape(packageLabel.c_str());
  json += "\",\"asset_available\":";
  json += hasMatchingAsset ? "true" : "false";
  json += ",\"newer\":";
  json += isReleaseNewerThanCurrent(releaseInfo) ? "true" : "false";
  json += "}";

  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(200, "application/json; charset=utf-8", json);
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
  portalServer.on("/firmware-online", HTTP_POST, handlePortalFirmwareOnline);
  portalServer.on("/validate", HTTP_POST, handlePortalValidate);
  portalServer.on("/release-info", HTTP_POST, handlePortalReleaseInfo);
  portalServer.on("/wifi-list", HTTP_GET, handlePortalWifiList);
  portalServer.on("/generate_204", HTTP_GET, handlePortalRedirect);
  portalServer.on("/redirect", HTTP_GET, handlePortalRedirect);
  portalServer.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  portalServer.on("/connecttest.txt", HTTP_GET, handlePortalRoot);
  portalServer.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
  portalServer.onNotFound(handlePortalRedirect);
}

static bool startConfigurationPortal() {
  portalSaveRequested = false;
  portalExitAction = PORTAL_EXIT_ACTION_NONE;
  resetPortalFirmwareUploadState();
  portalUnlocked = !hasSetupPinConfigured(deviceConfig);
  portalBootUnixBase = (lastKnownUnixTime >= 1700000000) ? lastKnownUnixTime : buildTimestamp();
  portalUnlockedAtMs = portalUnlocked ? millis() : 0;

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  buildPortalCredentials();
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

static bool parseJsonStringFieldFrom(const String& payload, const char* key, int startIndex, String& outValue, int* nextIndex = nullptr) {
  if (!key || !key[0]) return false;

  const String needle = String("\"") + key + "\"";
  const int keyIndex = payload.indexOf(needle, startIndex < 0 ? 0 : startIndex);
  if (keyIndex < 0) return false;

  int colonIndex = payload.indexOf(':', keyIndex + needle.length());
  if (colonIndex < 0) return false;
  colonIndex++;

  while (colonIndex < payload.length() && isspace((unsigned char)payload[colonIndex])) {
    colonIndex++;
  }
  if (colonIndex >= payload.length() || payload[colonIndex] != '"') return false;

  outValue = "";
  bool escaped = false;
  for (int i = colonIndex + 1; i < payload.length(); i++) {
    const char c = payload[i];
    if (escaped) {
      switch (c) {
        case 'n':
          outValue += '\n';
          break;
        case 'r':
          outValue += '\r';
          break;
        case 't':
          outValue += '\t';
          break;
        case '"':
        case '\\':
        case '/':
          outValue += c;
          break;
        case 'u':
          if ((i + 4) < payload.length()) {
            char hexBuf[5] = {0};
            for (int j = 0; j < 4; j++) {
              hexBuf[j] = payload[i + 1 + j];
            }
            char* endPtr = nullptr;
            unsigned long codePoint = strtoul(hexBuf, &endPtr, 16);
            if (endPtr == (hexBuf + 4) && codePoint <= 0x7F && isprint((int)codePoint)) {
              outValue += (char)codePoint;
            } else {
              outValue += '?';
            }
            i += 4;
          } else {
            outValue += '?';
          }
          break;
        default:
          outValue += c;
          break;
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      if (nextIndex) *nextIndex = i + 1;
      return true;
    }
    outValue += c;
  }

  return false;
}

static bool parseJsonStringField(const String& payload, const char* key, String& outValue) {
  return parseJsonStringFieldFrom(payload, key, 0, outValue, nullptr);
}

static bool parseGitHubReleaseAssetUrl(const String& payload, const String& assetNameSuffix, String& outUrl) {
  int searchIndex = 0;
  while (searchIndex < payload.length()) {
    String assetName;
    int afterName = 0;
    if (!parseJsonStringFieldFrom(payload, "name", searchIndex, assetName, &afterName)) {
      return false;
    }
    searchIndex = afterName;
    if (!assetName.endsWith(assetNameSuffix)) {
      continue;
    }

    String downloadUrl;
    if (parseJsonStringFieldFrom(payload, "browser_download_url", afterName, downloadUrl, nullptr)) {
      downloadUrl.trim();
      if (downloadUrl.length() > 0) {
        outUrl = downloadUrl;
        return true;
      }
    }
  }

  return false;
}

static uint32_t readVersionPart(const String& version, int& index) {
  while (index < version.length() && !isdigit((unsigned char)version[index])) {
    index++;
  }

  uint32_t value = 0;
  while (index < version.length() && isdigit((unsigned char)version[index])) {
    value = (value * 10U) + (uint32_t)(version[index] - '0');
    index++;
  }
  return value;
}

static int compareFirmwareVersions(String left, String right) {
  left.trim();
  right.trim();
  if (left.startsWith("v") || left.startsWith("V")) left.remove(0, 1);
  if (right.startsWith("v") || right.startsWith("V")) right.remove(0, 1);

  int leftIndex = 0;
  int rightIndex = 0;
  for (uint8_t i = 0; i < 8; i++) {
    const uint32_t leftPart = readVersionPart(left, leftIndex);
    const uint32_t rightPart = readVersionPart(right, rightIndex);
    if (leftPart > rightPart) return 1;
    if (leftPart < rightPart) return -1;
    if (leftIndex >= left.length() && rightIndex >= right.length()) return 0;
  }
  return 0;
}

static bool isReleaseNewerThanCurrent(const GitHubReleaseInfo& releaseInfo) {
  return compareFirmwareVersions(releaseInfo.tagName, FIRMWARE_VERSION) > 0;
}

static void normalizeReleaseNotesPreview(String& notes) {
  notes.replace("\r\n", "\n");
  notes.replace('\r', '\n');
  while (notes.indexOf("\n\n\n") >= 0) {
    notes.replace("\n\n\n", "\n\n");
  }
  notes.trim();
  if (notes.length() > (int)GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS) {
    notes.remove(GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS);
    notes += "\n\n...";
  }
  if (!notes.length()) {
    notes = "No release notes provided.";
  }
}

static bool parseGitHubReleaseInfo(const String& payload, GitHubReleaseInfo& outInfo) {
  if (!parseJsonStringField(payload, "tag_name", outInfo.tagName)) return false;
  parseJsonStringField(payload, "name", outInfo.name);
  parseJsonStringField(payload, "body", outInfo.body);
  parseJsonStringField(payload, "html_url", outInfo.htmlUrl);

  outInfo.tagName.trim();
  outInfo.name.trim();
  outInfo.htmlUrl.trim();

  if (!outInfo.name.length()) {
    outInfo.name = outInfo.tagName;
  }

  String version = outInfo.tagName;
  if (version.startsWith("v") || version.startsWith("V")) {
    version.remove(0, 1);
  }
  const String modelPrefix = String("FreedomClock-") + version + "-" + DEVICE_MODEL_NAME;
  if (!parseGitHubReleaseAssetUrl(payload, modelPrefix + "-manual-update-open.bin", outInfo.openBinUrl)) {
    parseGitHubReleaseAssetUrl(payload, String("FreedomClock-") + version + "-manual-update-open.bin", outInfo.openBinUrl);
  }
  if (!parseGitHubReleaseAssetUrl(payload, modelPrefix + "-manual-update-secure.bin", outInfo.secureBinUrl)) {
    parseGitHubReleaseAssetUrl(payload, String("FreedomClock-") + version + "-manual-update-secure.bin", outInfo.secureBinUrl);
  }

  normalizeReleaseNotesPreview(outInfo.body);
  return outInfo.tagName.length() > 0;
}

static bool selectReleaseFirmwareUrl(const GitHubReleaseInfo& releaseInfo, bool securePackage, String& outUrl, String& outPackageLabel) {
  outPackageLabel = String(DEVICE_MODEL_NAME) + (securePackage ? " secure package" : " open package");
  outUrl = securePackage ? releaseInfo.secureBinUrl : releaseInfo.openBinUrl;
  outUrl.trim();
  return outUrl.length() > 0;
}

static bool fetchLatestGitHubReleaseInfo(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(errorBuf, errorBufSize, "Wi-Fi is not connected.");
    return false;
  }

  if (!ensureClockReadyForTls(errorBuf, errorBufSize)) {
    return false;
  }

  TrustedWiFiClientSecure secureClient;
  if (!configureTrustedTlsClient(secureClient, errorBuf, errorBufSize)) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(PRICE_HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, GITHUB_LATEST_RELEASE_API_URL)) {
    snprintf(errorBuf, errorBufSize, "Could not start GitHub release request.");
    return false;
  }

  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", "FreedomClock/2026");
  http.addHeader("X-GitHub-Api-Version", GITHUB_API_VERSION);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    if (httpCode == HTTP_CODE_NOT_FOUND) {
      snprintf(errorBuf, errorBufSize, "No published GitHub release found yet.");
    } else if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 429) {
      snprintf(errorBuf, errorBufSize, "GitHub API rate limited. Try again later.");
    } else {
      snprintf(errorBuf, errorBufSize, "GitHub release request failed (%d).", httpCode);
    }
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (!parseGitHubReleaseInfo(payload, outInfo)) {
    snprintf(errorBuf, errorBufSize, "GitHub release response could not be parsed.");
    return false;
  }

  errorBuf[0] = '\0';
  return true;
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

  if (!ensureClockReadyForTls(errorBuf, errorBufSize)) {
    return false;
  }

  TrustedWiFiClientSecure secureClient;
  if (!configureTrustedTlsClient(secureClient, errorBuf, errorBufSize)) {
    return false;
  }

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

  SetupBootAction bootAction = detectSetupBootAction();

  display.begin();
  buildDeviceId(deviceId, sizeof(deviceId));
  initializeEncryptedNvsIfAvailable();
  portalSessionMessage[0] = '\0';
  portalSessionMessageIsError = false;

  float vbat = readBatteryVoltage();
  int pct = batteryPercentFromVoltage(vbat);
#if LOG_BATTERY_STATS
  loadBatteryLog(batteryLog);
  if (appendBatteryLogSample(batteryLog, lastKnownUnixTime, vbat, pct)) {
    saveBatteryLog(batteryLog);
  }
#else
  loadBatteryLog(batteryLog);
#endif

  if (bootAction == SETUP_BOOT_ACTION_FACTORY_RESET) {
    clearSavedDeviceConfig();
    clearWealthHistory();
    clearBatteryLog();
    clearBatteryLogInMemory(batteryLog);
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
  const bool showFreedomCheckinScreen = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) && wakeExtraPressCount == 0;
  const bool showPerformanceScreen = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) && wakeExtraPressCount == 1;
  const bool showInfoScreen = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) && wakeExtraPressCount >= 2;

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
