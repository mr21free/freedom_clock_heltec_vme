#pragma once

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

static constexpr float DEFAULT_MONTHLY_EXPENSE_VALUE = 10000.0f;
static constexpr float DEFAULT_INFLATION_ANNUAL = 0.02f;
static constexpr float DEFAULT_WEALTH_GROWTH_ANNUAL = 0.10f;
static constexpr float DEFAULT_WEALTH_VALUE = 1000000.0f;
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

enum CurrencyCode {
  CURRENCY_USD = 0,
  CURRENCY_EUR = 1,
  CURRENCY_CHF = 2
};

static constexpr AssetMode DEFAULT_ASSET_MODE = ASSET_MODE_BTC_MANUAL;
static constexpr PortfolioUseMode DEFAULT_PORTFOLIO_USE_MODE = PORTFOLIO_USE_MODE_SELL;
static constexpr DisplayThemeMode DEFAULT_DISPLAY_THEME_MODE = DISPLAY_THEME_LIGHT;
static constexpr CurrencyCode DEFAULT_CURRENCY_CODE = CURRENCY_USD;

static constexpr char DEFAULT_OWNER_NAME[] = "OWNER";
static constexpr int DEFAULT_OWNER_BIRTH_YEAR = 1990;
static constexpr int DEFAULT_OWNER_LIFE_EXPECTANCY_YEARS = 85;

static constexpr char DEFAULT_MQTT_SERVER[] = "mqtt.local";
static constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
static constexpr char DEFAULT_MQTT_USER[] = "";
static constexpr char DEFAULT_MQTT_PASS[] = "";
static constexpr char DEFAULT_TOPIC_PRICE_VALUE[] = "home/bitcoin/price/usd";
static constexpr char DEFAULT_TOPIC_BALANCE_BTC[] = "home/bitcoin/wallets/total_btc";

static constexpr uint16_t DEFAULT_REFRESH_INTERVAL_MINUTES = 1440;
static constexpr uint16_t MIN_REFRESH_INTERVAL_MINUTES = 15;
static constexpr uint16_t MAX_REFRESH_INTERVAL_MINUTES = 10080;
static constexpr uint64_t MICROSECONDS_PER_MINUTE = 60ULL * 1000000ULL;

// ============================================================
// Hardware pins (Vision Master E-series)
// ============================================================
static constexpr int PIN_EINK_POWER = 18;  // Vision Master Vext / display power enable, active HIGH
static constexpr int PIN_STATUS_LED = 45;  // Onboard status LED on newer Vision Master revisions
static constexpr int PIN_BAT_ADC = 7;      // VBAT_Read
static constexpr int PIN_ADC_CTRL = 46;    // ADC_Ctrl gate
static constexpr int PIN_FUNCTION_BUTTON = 21; // 21 button: wake and cycle screens
static constexpr int PIN_SETUP_BUTTON = 0;      // BOOT button: setup and factory reset
static constexpr uint64_t FUNCTION_BUTTON_WAKE_MASK = (1ULL << PIN_FUNCTION_BUTTON);
static constexpr uint64_t SETUP_BUTTON_WAKE_MASK = (1ULL << PIN_SETUP_BUTTON);
static constexpr uint64_t BUTTON_WAKE_MASK = FUNCTION_BUTTON_WAKE_MASK | SETUP_BUTTON_WAKE_MASK;

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
static constexpr uint16_t GITHUB_HTTP_TIMEOUT_MS = 30000;
static constexpr uint16_t FIRMWARE_HTTP_TIMEOUT_MS = 30000;
static constexpr uint32_t FIRMWARE_DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
static constexpr uint16_t BUTTON_POLL_DELAY_MS = 20;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
static constexpr uint32_t SECOND_PRESS_WINDOW_MS = 700;
static constexpr uint32_t CONFIG_PORTAL_DELAY_MS = 10;
static constexpr uint32_t CONFIG_PORTAL_EXIT_GRACE_MS = 2500;
static constexpr uint32_t CONFIG_PORTAL_RESTART_DELAY_MS = 1200;
static constexpr uint32_t CONFIG_PORTAL_FIRMWARE_RESTART_DELAY_MS = 3500;
static constexpr uint32_t CONFIG_PORTAL_UNLOCK_TIMEOUT_MS = 600000;
static constexpr uint16_t CONFIG_PORTAL_PORT = 80;
static constexpr uint16_t CONFIG_DNS_PORT = 53;
static constexpr size_t PORTAL_FIRMWARE_MESSAGE_SIZE = 160;
static constexpr size_t CONFIG_LOAD_STATUS_SIZE = 160;
static constexpr uint32_t PORTAL_PRICE_CACHE_REUSE_SECONDS = 900;
static constexpr char CONFIG_NAMESPACE[] = "freedomclk";
static constexpr char DIAGNOSTICS_NAMESPACE[] = "fcdiag";
static constexpr char HISTORY_NAMESPACE[] = "wealthhist";
static constexpr char BTC_CACHE_NAMESPACE[] = "btccache";
static constexpr char BATTERY_LOG_NAMESPACE[] = "batlog";
static constexpr uint32_t CONFIG_VERSION = 2;
static constexpr uint32_t HISTORY_VERSION = 2;
static constexpr uint32_t BATTERY_LOG_VERSION = 1;
static constexpr char FIRMWARE_VERSION[] = "2026.05.23.1";
static constexpr char GITHUB_REPO_SLUG[] = "mr21free/freedom_clock_heltec_vme";
static constexpr char GITHUB_RELEASES_URL[] = "https://github.com/mr21free/freedom_clock_heltec_vme/releases";
static constexpr char GITHUB_LATEST_RELEASE_API_URL[] = "https://api.github.com/repos/mr21free/freedom_clock_heltec_vme/releases/latest";
static constexpr char GITHUB_API_VERSION[] = "2022-11-28";
static constexpr char MQTT_CLIENT_ID_PREFIX[] = "FreedomClock";
static constexpr char QUOTE_NAMESPACE[] = "freedomq";
static constexpr char AP_SSID_PREFIX[] = "FREEDOM_CLOCK_";
static constexpr char AP_PASSWORD_PREFIX[] = "setup-";
static constexpr uint8_t SETUP_PIN_LENGTH = 6;
static constexpr size_t SETUP_PIN_HASH_HEX_SIZE = 65;
static constexpr uint16_t SETUP_PIN_HASH_ROUNDS = 2048;
static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";
static constexpr char COINGECKO_SIMPLE_PRICE_URL_BASE[] = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=";
static constexpr char MEMPOOL_PRICE_URL[] = "https://mempool.space/api/v1/prices";
static constexpr size_t GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS = 8192;
static constexpr long NTP_GMT_OFFSET_SECONDS = 0;
static constexpr int NTP_DAYLIGHT_OFFSET_SECONDS = 0;
static const IPAddress CONFIG_AP_IP(192, 168, 4, 1);
static const IPAddress CONFIG_AP_GATEWAY(192, 168, 4, 1);
static const IPAddress CONFIG_AP_SUBNET(255, 255, 255, 0);

static inline uint8_t sanitizeCurrencyCode(uint8_t currencyCode) {
  switch ((CurrencyCode)currencyCode) {
    case CURRENCY_USD:
    case CURRENCY_EUR:
    case CURRENCY_CHF:
      return currencyCode;
  }
  return (uint8_t)DEFAULT_CURRENCY_CODE;
}

static inline const char* currencyCodeLabel(uint8_t currencyCode) {
  switch ((CurrencyCode)sanitizeCurrencyCode(currencyCode)) {
    case CURRENCY_EUR: return "EUR";
    case CURRENCY_CHF: return "CHF";
    case CURRENCY_USD:
    default: return "USD";
  }
}

static inline const char* currencyCodeParam(uint8_t currencyCode) {
  switch ((CurrencyCode)sanitizeCurrencyCode(currencyCode)) {
    case CURRENCY_EUR: return "eur";
    case CURRENCY_CHF: return "chf";
    case CURRENCY_USD:
    default: return "usd";
  }
}
