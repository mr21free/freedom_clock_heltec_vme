#pragma once

static AssetMode sanitizeAssetMode(uint8_t rawValue);
static bool isMqttBtcAssetMode(AssetMode assetMode);
static bool isManualBtcAssetMode(AssetMode assetMode);
static bool isAnyBtcAssetMode(AssetMode assetMode);
static PortfolioUseMode sanitizePortfolioUseMode(uint8_t rawValue);
static DisplayThemeMode sanitizeThemeMode(uint8_t rawValue);
static bool isReleaseNewerThanCurrent(const GitHubReleaseInfo& releaseInfo);

static void portalSendHtml(const String& html);
static void refreshHardwareSecurityStatus();
static bool initializeEncryptedNvsIfAvailable();
static bool isPortalUnlockExpired();
static void refreshPortalUnlockSession();
static bool hasSetupPinConfigured(const DeviceConfig& cfg);
static void handlePortalFirmwareUpload();
static void handlePortalFirmwareUploadComplete();
static bool fetchLatestGitHubReleaseInfo(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize);
static bool fetchOnlineBtcPrice(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize);
static bool selectReleaseFirmwareUrl(const GitHubReleaseInfo& releaseInfo, bool securePackage, String& outUrl, String& outPackageLabel);
static bool fetchCoinGeckoInTask(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize);
static bool fetchGitHubInTask(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize);
static bool installFirmwareInTask(const String& firmwareUrl, char* errorBuf, size_t errorBufSize);

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

#include "security.h"

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

static void formatCompactValue(float currencyValue, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;

  if (currencyValue >= 1000000.0f) {
    snprintf(dst, dstSize, "%.2f mil", currencyValue / 1000000.0f);
  } else if (currencyValue >= 1000.0f) {
    snprintf(dst, dstSize, "%.2fk", currencyValue / 1000.0f);
  } else {
    snprintf(dst, dstSize, "%.2f", currencyValue);
  }
}

static void formatSignedCompactValue(int32_t deltaValue, char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  char compact[16];
  const float absValue = (deltaValue < 0) ? (float)(-(int64_t)deltaValue) : (float)deltaValue;
  formatCompactValue(absValue, compact, sizeof(compact));
  snprintf(dst, dstSize, "%c%s", (deltaValue < 0) ? '-' : '+', compact);
}

static void formatSignedCompactPrice(int32_t deltaValue, char* dst, size_t dstSize) {
  formatSignedCompactValue(deltaValue, dst, dstSize);
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

#include "history.h"

static DisplayThemeColors getDisplayThemeColors(DisplayThemeMode themeMode) {
  if (themeMode == DISPLAY_THEME_DARK) {
    return { WHITE, BLACK };
  }
  return { BLACK, WHITE };
}

static DisplayThemeMode statusScreenThemeMode() {
  return deviceConfig.configured
    ? sanitizeThemeMode(deviceConfig.displayThemeMode)
    : DISPLAY_THEME_LIGHT;
}

static bool screenRenderWindowActive = false;
static int screenRenderWindowX = 0;
static int screenRenderWindowY = 0;
static int screenRenderWindowW = DEVICE_DISPLAY_WIDTH;
static int screenRenderWindowH = DEVICE_DISPLAY_HEIGHT;

static void setScreenRenderWindow(int x, int y, int w, int h) {
  screenRenderWindowActive = true;
  screenRenderWindowX = clampInt(x, 0, DEVICE_DISPLAY_WIDTH - 1);
  screenRenderWindowY = clampInt(y, 0, DEVICE_DISPLAY_HEIGHT - 1);
  screenRenderWindowW = clampInt(w, 1, DEVICE_DISPLAY_WIDTH - screenRenderWindowX);
  screenRenderWindowH = clampInt(h, 1, DEVICE_DISPLAY_HEIGHT - screenRenderWindowY);
}

static void clearScreenRenderWindow() {
  screenRenderWindowActive = false;
  screenRenderWindowX = 0;
  screenRenderWindowY = 0;
  screenRenderWindowW = DEVICE_DISPLAY_WIDTH;
  screenRenderWindowH = DEVICE_DISPLAY_HEIGHT;
}

static void prepareScreen(DisplayThemeMode themeMode) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  display.setRotation(1);
  if (screenRenderWindowActive) {
    // Partial refresh path: keep the existing panel image and only redraw the requested window.
    display.setWindow(screenRenderWindowX, screenRenderWindowY, screenRenderWindowW, screenRenderWindowH);
    display.fillRect(screenRenderWindowX, screenRenderWindowY, screenRenderWindowW, screenRenderWindowH, theme.background);
  } else {
    // fastmodeOff() resets hardware mode but not winrot_*; this ensures update() refreshes the full screen.
    display.setWindow(0, 0, DEVICE_DISPLAY_WIDTH, DEVICE_DISPLAY_HEIGHT);
    // Clear only the drawing buffer. A physical clear briefly flashes white,
    // which is especially distracting before dark-theme screens.
    display.clearMemory();
    display.fillScreen(theme.background);
  }
  display.setTextColor(theme.foreground, theme.background);
}

#include "config_runtime.h"

static bool loadCachedBtcPriceForCurrency(uint8_t currencyCode, float& outPrice, time_t* outTimestamp = nullptr) {
  if (outTimestamp) *outTimestamp = 0;
  if (!preferences.begin(BTC_CACHE_NAMESPACE, true)) {
    return false;
  }

  char cachedPrice[VALUE_BUFFER_SIZE] = "";
  preferences.getString("price", cachedPrice, sizeof(cachedPrice));
  const uint8_t cachedCurrency = (uint8_t)preferences.getUInt("currency", (uint32_t)DEFAULT_CURRENCY_CODE);
  const time_t cachedTimestamp = (time_t)preferences.getUInt("ts", 0);
  preferences.end();

  if (cachedCurrency != sanitizeCurrencyCode(currencyCode)) {
    return false;
  }
  if (!parseNonNegativeFloatStrict(cachedPrice, outPrice, false)) {
    return false;
  }
  if (outTimestamp) *outTimestamp = cachedTimestamp;
  return true;
}

static bool saveCachedBtcPriceForCurrency(uint8_t currencyCode, float priceValue, time_t now) {
  if (!(priceValue > 0.0f)) {
    return false;
  }
  if (!preferences.begin(BTC_CACHE_NAMESPACE, false)) {
    return false;
  }

  char priceText[VALUE_BUFFER_SIZE];
  snprintf(priceText, sizeof(priceText), "%.2f", priceValue);
  preferences.putString("price", priceText);
  preferences.putUInt("currency", (uint32_t)sanitizeCurrencyCode(currencyCode));
  if (now >= 1700000000) {
    preferences.putUInt("ts", (uint32_t)now);
    lastBtcCacheUnixTime = now;
  }
  preferences.end();

  safeCopyCString(lastPriceValue, sizeof(lastPriceValue), priceText);
  return true;
}

static void loadCachedBtcData(const DeviceConfig& cfg) {
  if (!preferences.begin(BTC_CACHE_NAMESPACE, true)) {
    return;
  }

  char cachedPrice[VALUE_BUFFER_SIZE] = "";
  char cachedBalance[VALUE_BUFFER_SIZE] = "";
  preferences.getString("price", cachedPrice, sizeof(cachedPrice));
  preferences.getString("balance", cachedBalance, sizeof(cachedBalance));
  const uint8_t cachedCurrency = (uint8_t)preferences.getUInt("currency", (uint32_t)DEFAULT_CURRENCY_CODE);
  lastBtcCacheUnixTime = (time_t)preferences.getUInt("ts", 0);
  preferences.end();

  float parsed = 0.0f;
  if (cachedCurrency == sanitizeCurrencyCode(cfg.currencyCode)
      && parseNonNegativeFloatStrict(cachedPrice, parsed, false)) {
    safeCopyCString(lastPriceValue, sizeof(lastPriceValue), cachedPrice);
  } else {
    safeCopyCString(lastPriceValue, sizeof(lastPriceValue), "--");
  }
  if (parseNonNegativeFloatStrict(cachedBalance, parsed, true)) {
    safeCopyCString(lastBalanceBtc, sizeof(lastBalanceBtc), cachedBalance);
  }
}

static void saveCachedBtcData(bool savePrice, bool saveBalance, time_t now) {
  if (!savePrice && !saveBalance) {
    return;
  }
  if (!preferences.begin(BTC_CACHE_NAMESPACE, false)) {
    return;
  }

  float parsed = 0.0f;
  if (savePrice && parseNonNegativeFloatStrict(lastPriceValue, parsed, false)) {
    preferences.putString("price", lastPriceValue);
    preferences.putUInt("currency", (uint32_t)sanitizeCurrencyCode(deviceConfig.currencyCode));
  }
  if (saveBalance && parseNonNegativeFloatStrict(lastBalanceBtc, parsed, true)) {
    preferences.putString("balance", lastBalanceBtc);
  }
  if (now >= 1700000000
      && (parseNonNegativeFloatStrict(lastPriceValue, parsed, false)
          || parseNonNegativeFloatStrict(lastBalanceBtc, parsed, true))) {
    lastBtcCacheUnixTime = now;
    preferences.putUInt("ts", (uint32_t)now);
  }
  preferences.end();
}

static bool clearCachedBtcData() {
  if (!preferences.begin(BTC_CACHE_NAMESPACE, false)) {
    return false;
  }
  const bool cleared = preferences.clear();
  preferences.end();
  safeCopyCString(lastPriceValue, sizeof(lastPriceValue), "--");
  safeCopyCString(lastBalanceBtc, sizeof(lastBalanceBtc), "--");
  lastBtcCacheUnixTime = 0;
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
  if (cfg.monthlyExpenseValue <= 0.0f) {
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
    if (!hasText(cfg.topicPriceValue) || !hasText(cfg.topicBalanceBtc)) {
      snprintf(errorBuf, errorBufSize, "BTC via MQTT mode requires both MQTT topics.");
      return false;
    }
  } else if (isManualBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) {
      snprintf(errorBuf, errorBufSize, "Wi-Fi SSID is required in static BTC mode.");
      return false;
    }
    if (!(cfg.manualBtcAmount > 0.0f)) {
      snprintf(errorBuf, errorBufSize, "BTC amount must be greater than zero.");
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

static void appendPortalBrandHeader(String& html, const char* title) {
  html += "<div class=\"brand\">";
  html += "<svg class=\"brand-logo\" viewBox=\"88 88 336 336\" aria-hidden=\"true\" focusable=\"false\">";
  html += "<path fill=\"#000\" fill-rule=\"evenodd\" d=\"M 256 256 m -163.84 0 a 163.84 163.84 0 1 0 327.68 0 a 163.84 163.84 0 1 0 -327.68 0 M 256 256 m -103.84 0 a 103.84 103.84 0 1 1 207.68 0 a 103.84 103.84 0 1 1 -207.68 0\"/>";
  html += "<path fill=\"#000\" d=\"M 256 256 L 174.08 114.11 A 163.84 163.84 0 0 1 256 92.16 Z\"/>";
  html += "</svg><h1>";
  html += htmlEscape(title ? title : "FREEDOM CLOCK");
  html += "</h1></div>";
}

static String buildPortalConfirmationPage(const char* title, const char* message) {
  String html;
  html.reserve(1600);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>FREEDOM CLOCK</title>";
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
  html += "<title>FREEDOM CLOCK Setup Locked</title>";
  html += "<style>";
  html += "html{-webkit-text-size-adjust:100%;text-size-adjust:100%;}";
  html += "body{margin:0;min-height:100vh;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:860px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".brand{display:flex;align-items:center;gap:.28em;margin:0 0 8px;font-size:30px;}";
  html += ".brand-logo{width:.98em;height:.98em;width:1.3cap;height:1.3cap;flex:0 0 auto;display:block;overflow:visible;transform:translateY(-.04em);}";
  html += ".brand h1{margin:0;font-size:1em;line-height:1.05;letter-spacing:.02em;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".card{margin-top:22px;background:#fff;border-radius:18px;padding:18px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input{width:100%;box-sizing:border-box;min-height:44px;height:44px;padding:10px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:16px;line-height:1.2;background:#fdfbf6;color:#171717;letter-spacing:.08em;}";
  html += ".hint{font-size:12px;color:#6b6258;margin-top:8px;line-height:1.45;}";
  html += ".message{padding:14px 16px;border-radius:14px;font-size:14px;line-height:1.45;white-space:pre-line;margin-top:18px;}";
  html += ".ok{background:#e8f7ea;color:#1d5d2d;}";
  html += ".err{background:#fdeaea;color:#8d2020;}";
  html += ".info{background:#eee7db;color:#5c5349;}";
  html += ".actions{display:flex;gap:12px;align-items:center;margin-top:16px;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:16px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;touch-action:manipulation;}";
  html += "button[disabled],input[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "@media (max-width:640px){.brand{font-size:25px;}}";
  html += "</style></head><body><div class=\"wrap\">";
  html += "<section class=\"hero\">";
  appendPortalBrandHeader(html, "FREEDOM CLOCK");
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
  const uint8_t selectedCurrency = sanitizeCurrencyCode(cfg.currencyCode);
  const char* currencyLabel = currencyCodeLabel(selectedCurrency);
  const char* previewPriceText = (gotPrice && hasText(priceValueBuf)) ? priceValueBuf : lastPriceValue;
  const char* previewBalanceText = (gotBalance && hasText(balanceBtcBuf)) ? balanceBtcBuf : lastBalanceBtc;
  float previewPriceValue = 0.0f;
  float previewBalanceBtc = 0.0f;
  const bool previewHasPriceValue = parseNonNegativeFloatStrict(previewPriceText, previewPriceValue, false);
  const bool previewHasBalanceBtc = parseNonNegativeFloatStrict(previewBalanceText, previewBalanceBtc, true);
#if ENABLE_DEVELOPER_STATS
  const String batteryLogText = buildBatteryLogText(batteryLog);
  const String developerStatsText = buildDeveloperStatsText();
  const String historyStatsText = buildHistoryStatsText(wealthHistory, selectedCurrency);
  const String lastBootPathDiagnosticsText = loadLastBootPathDiagnosticsText();
  const String lastAppBootDiagnosticsText = loadLastAppBootDiagnosticsText();
  const String lastPortalDiagnosticsText = loadLastPortalDiagnosticsText();
#endif
  html.reserve(72000);

  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>FREEDOM CLOCK</title>";
  html += "<style>";
  html += "html{-webkit-text-size-adjust:100%;text-size-adjust:100%;}";
  html += "body{margin:0;font-family:Arial,sans-serif;background:#f3f0e8;color:#171717;}";
  html += ".wrap{max-width:860px;margin:0 auto;padding:24px 18px 48px;}";
  html += ".hero{background:#f7931a;color:#2b1700;padding:24px;border-radius:18px;box-shadow:0 14px 40px rgba(120,64,0,.18);}";
  html += ".brand{display:flex;align-items:center;gap:.28em;margin:0 0 8px;font-size:30px;}";
  html += ".brand-logo{width:.98em;height:.98em;width:1.3cap;height:1.3cap;flex:0 0 auto;display:block;overflow:visible;transform:translateY(-.04em);}";
  html += ".brand h1{margin:0;font-size:1em;line-height:1.05;letter-spacing:.02em;}";
  html += ".hero p{margin:0;line-height:1.5;color:#5b3300;}";
  html += ".meta{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px;}";
  html += ".pill{background:transparent;padding:0;border-radius:0;font-size:13px;color:#2b1700;}";
  html += "form{margin-top:22px;display:grid;gap:18px;}";
  html += ".card{background:#fff;border-radius:18px;padding:18px;box-shadow:0 12px 30px rgba(18,18,18,.08);}";
  html += ".card h2{margin:0 0 14px;font-size:18px;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;}";
  html += ".row-start{grid-column:1;}";
  html += ".display-grid{align-items:start;row-gap:10px;}";
  html += ".hidden{display:none!important;}";
  html += ".stack{display:grid;gap:10px;}";
  html += ".inline{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}";
  html += ".dual{display:grid;grid-template-columns:minmax(92px,1fr) 112px;gap:10px;align-items:start;}";
  html += ".check{display:flex;align-items:center;gap:8px;font-size:13px;color:#3a342d;margin-top:8px;}";
  html += ".check input{width:auto;margin:0;}";
  html += "label{display:block;font-size:13px;font-weight:700;margin-bottom:6px;color:#3a342d;}";
  html += "input,select{width:100%;box-sizing:border-box;min-height:44px;height:44px;padding:10px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:16px;line-height:1.2;background:#fdfbf6;color:#171717;}";
  html += "input[type=time]{-webkit-appearance:none;appearance:none;}";
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
  html += ".client-diagnostics{position:sticky;top:0;z-index:30;margin:0 0 12px;padding:14px 16px;border-radius:14px;background:#fdeaea;color:#8d2020;box-shadow:0 8px 24px rgba(141,32,32,.14);}";
  html += ".client-diagnostics h2{margin:0 0 8px;font-size:16px;}";
  html += ".client-diagnostics textarea{min-height:96px;margin-top:8px;background:#fff8f8;border-color:#efb8b8;color:#3b1111;}";
  html += ".actions{display:flex;flex-wrap:wrap;gap:12px;align-items:center;}";
  html += ".form-actions{margin-top:4px;}";
  html += ".firmware-head{display:flex;justify-content:space-between;gap:12px;align-items:end;flex-wrap:wrap;}";
  html += ".firmware-version{font-size:20px;font-weight:800;color:#171717;}";
  html += "@keyframes fc-spin{to{transform:rotate(360deg)}}";
  html += ".install-progress{display:flex;align-items:center;gap:12px;padding:14px 16px;border-radius:14px;background:#fff8ef;border:1.5px solid #f7931a;margin-top:14px;}";
  html += ".install-spinner{flex-shrink:0;width:20px;height:20px;border:2.5px solid rgba(247,147,26,.25);border-top-color:#f7931a;border-radius:50%;animation:fc-spin .9s linear infinite;}";
  html += ".install-step{font-size:14px;color:#2b1700;font-weight:600;line-height:1.4;}";
  html += ".save-overlay{position:fixed;inset:0;background:rgba(253,251,246,.94);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:18px;z-index:9999;}";
  html += ".save-overlay-spinner{width:36px;height:36px;border:3.5px solid rgba(247,147,26,.25);border-top-color:#f7931a;border-radius:50%;animation:fc-spin .9s linear infinite;}";
  html += ".save-overlay-text{font-size:17px;font-weight:700;color:#2b1700;text-align:center;line-height:1.6;}";
  html += "textarea{width:100%;box-sizing:border-box;min-height:180px;padding:11px 12px;border:1px solid #cfc6b7;border-radius:12px;font-size:16px;background:#fdfbf6;color:#171717;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;line-height:1.45;}";
  html += "button{border:none;border-radius:999px;padding:13px 18px;font-size:16px;font-weight:700;cursor:pointer;background:#f7931a;color:#2b1700;touch-action:manipulation;}";
  html += "button.small{padding:10px 14px;font-size:16px;}";
  html += "button.secondary{background:#ded4c2;color:#211d19;}";
  html += "button[disabled]{opacity:.45;cursor:not-allowed;}";
  html += "a.extlink{color:#8b4f00;font-weight:700;text-decoration:none;}";
  html += "a.extlink:hover,a.extlink:focus{text-decoration:underline;}";
  html += ".subtle{font-size:13px;color:#645c53;}";
  html += "@media (min-width:760px){.display-grid #wake_time_wrap{display:contents;}.display-grid #wake_time_wrap.hidden{display:none!important;}.display-grid #wake_time_wrap>.grid{display:contents;}.display-grid #wake_time_hint{grid-column:2 / span 2;max-width:520px;margin-top:-20px;align-self:start;}}";
  html += "@media (max-width:640px){.brand{font-size:25px;}}";
  html += "</style></head><body><input id=\"focus_guard\" tabindex=\"-1\" aria-hidden=\"true\" type=\"text\" readonly autofocus autocomplete=\"off\" style=\"position:fixed;top:0;left:0;width:1px;height:1px;opacity:0;pointer-events:none;outline:none;border:none;background:transparent;font-size:16px;\">";
  html += "<div id=\"save_overlay\" class=\"save-overlay hidden\"><div id=\"save_overlay_spinner\" class=\"save-overlay-spinner\"></div><div id=\"save_overlay_text\" class=\"save-overlay-text\">Saving settings...</div><button id=\"save_overlay_action\" class=\"secondary hidden\" type=\"button\">Continue</button></div>";
  html += "<div class=\"wrap\">";
  html += "<section id=\"client_diagnostics\" class=\"client-diagnostics hidden\"><h2>Setup page diagnostics</h2><div class=\"hint\">Something went wrong in this browser. Copy this text and share it for debugging.</div><textarea id=\"client_diagnostics_text\" readonly></textarea><div class=\"actions\" style=\"margin-top:10px;\"><button id=\"copy_client_diagnostics_button\" class=\"secondary\" type=\"button\">Copy</button><button id=\"clear_client_diagnostics_button\" class=\"secondary\" type=\"button\">Clear</button></div></section>";
  html += "<script>";
  html += "(function(){";
  html += "function showDiag(kind,msg,detail){try{var box=document.getElementById('client_diagnostics');var text=document.getElementById('client_diagnostics_text');if(!box||!text)return;var lines=[];lines.push('['+(new Date()).toISOString()+'] '+String(kind||'info')+': '+String(msg||''));if(detail)lines.push(String(detail));lines.push('url='+location.href);lines.push('ua='+navigator.userAgent);text.value=(text.value?text.value+'\\n\\n':'')+lines.join('\\n');box.className=box.className.replace(/\\bhidden\\b/g,'');}catch(e){}}";
  html += "window.fcDiag=showDiag;";
  html += "window.onerror=function(msg,url,line,col,err){showDiag('js-error',msg,(url||'')+':'+(line||0)+':'+(col||0)+(err&&err.stack?'\\n'+err.stack:''));};";
  html += "window.onunhandledrejection=function(e){var r=e&&e.reason;showDiag('promise-rejection',r&&r.message?r.message:String(r||'Unhandled promise rejection'),r&&r.stack?r.stack:'');};";
  html += "setTimeout(function(){if(!window.fcSetupReady&&!window.fcSetupFallbackReady)showDiag('setup-js-not-ready','Setup page script did not finish loading','Buttons will not work until the setup script loads.');},2500);";
  html += "})();";
  html += "</script>";
  html += "<section class=\"hero\">";
  appendPortalBrandHeader(html, "FREEDOM CLOCK");
  html += "<p>Press SETUP once to enter setup mode, or hold SETUP for about 10 seconds to clear all settings (factory reset). Use FUNCTION to cycle the clock screens.</p>";
  html += "</section>";

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
  html += "<div><label for=\"currency_code\">Currency</label><select id=\"currency_code\" name=\"currency_code\">";
  html += String("<option value=\"0\"") + selectedAttr(selectedCurrency == CURRENCY_USD) + ">USD</option>";
  html += String("<option value=\"1\"") + selectedAttr(selectedCurrency == CURRENCY_EUR) + ">EUR</option>";
  html += String("<option value=\"2\"") + selectedAttr(selectedCurrency == CURRENCY_CHF) + ">CHF</option>";
  html += "</select><div class=\"hint\">Use this currency for expenses, static wealth, BTC price, and MQTT price values.</div></div>";
  html += String("<div id=\"manual_btc_field_wrap\"><label for=\"manual_btc_amount\">BTC amount</label><input id=\"manual_btc_amount\" name=\"manual_btc_amount\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" min=\"0.00000001\" step=\"0.00000001\" autocomplete=\"off\" value=\"") + htmlEscape(cfg.manualBtcAmountText) + "\"><div id=\"manual_btc_currency_hint\" class=\"hint\">BTC/";
  html += currencyLabel;
  html += " price is fetched from CoinGecko over the internet, with mempool.space as backup.</div></div>";
  html += "<div><label for=\"portfolio_use_mode\">Spend mode</label><select id=\"portfolio_use_mode\" name=\"portfolio_use_mode\">";
  html += String("<option value=\"0\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_SELL) + ">Sell monthly</option>";
  html += String("<option value=\"1\"") + selectedAttr(sanitizePortfolioUseMode(cfg.portfolioUseMode) == PORTFOLIO_USE_MODE_BORROW) + ">Borrow yearly</option>";
  html += "</select></div>";
  html += String("<div id=\"wealth_field_wrap\"><label id=\"wealth_currency_label\" for=\"default_wealth_value\">Static wealth (") + currencyLabel + ")</label><input id=\"default_wealth_value\" name=\"default_wealth_value\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" min=\"0\" step=\"0.01\" value=\"" + formatFloatForInput(cfg.defaultWealthValue, 2) + "\"><div class=\"hint\">Used only in static net worth mode.</div></div>";
  html += String("<div><label id=\"monthly_currency_label\" for=\"monthly_exp_value\">Monthly expenses (") + currencyLabel + ")</label><input id=\"monthly_exp_value\" name=\"monthly_exp_value\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"0\" step=\"1\" autocomplete=\"off\" value=\"" + String((int)(cfg.monthlyExpenseValue + 0.5f)) + "\"></div>";
  html += String("<div><label for=\"inflation_annual_pct\">Inflation % / year</label><input id=\"inflation_annual_pct\" name=\"inflation_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.inflationAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div><label for=\"wealth_growth_annual_pct\">Portfolio growth % / year</label><input id=\"wealth_growth_annual_pct\" name=\"wealth_growth_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.wealthGrowthAnnual * 100.0f, 2) + "\"></div>";
  html += String("<div id=\"borrow_fee_wrap\"><label for=\"borrow_fee_annual_pct\">Borrow fee % / year</label><input id=\"borrow_fee_annual_pct\" name=\"borrow_fee_annual_pct\" type=\"text\" inputmode=\"decimal\" pattern=\"[0-9]*[.]?[0-9]*\" data-number=\"decimal\" step=\"0.01\" value=\"") + formatFloatForInput(cfg.borrowFeeAnnual * 100.0f, 2) + "\"></div>";
  html += "</div></section>";

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
  html += String("<div><label id=\"topic_price_currency_label\" for=\"topic_price_value\">BTC price topic (") + currencyLabel + ")</label><input id=\"topic_price_value\" name=\"topic_price_value\" maxlength=\"95\" value=\"" + htmlEscape(cfg.topicPriceValue) + "\"><div class=\"hint\">MQTT price values must use the selected currency.</div></div>";
  html += String("<div><label for=\"topic_balance_btc\">BTC balance topic</label><input id=\"topic_balance_btc\" name=\"topic_balance_btc\" maxlength=\"95\" value=\"") + htmlEscape(cfg.topicBalanceBtc) + "\"></div>";
  html += "</div></section>";

  html += "<section id=\"freedom_preview_card\" class=\"card preview-card hidden\"><h2>Freedom Time Preview</h2>";
  html += "<div id=\"freedom_preview_value\" class=\"preview-value\">--</div>";
  html += "<div id=\"freedom_preview_hint\" class=\"preview-hint\"></div>";
  html += "</section>";

  html += "<section class=\"card\"><h2>Display</h2><div class=\"grid display-grid\">";
  html += "<div><label for=\"display_theme_mode\">Display theme</label><select id=\"display_theme_mode\" name=\"display_theme_mode\">";
  html += String("<option value=\"1\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_DARK) + ">Dark</option>";
  html += String("<option value=\"0\"") + selectedAttr(sanitizeThemeMode(cfg.displayThemeMode) == DISPLAY_THEME_LIGHT) + ">Light</option>";
  html += "</select></div>";
  html += "<div><label for=\"time_display_format\">Time format</label><select id=\"time_display_format\" name=\"time_display_format\">";
  html += String("<option value=\"0\"") + selectedAttr(cfg.timeDisplayFormat == 0) + ">Years / Months / Weeks</option>";
  html += String("<option value=\"1\"") + selectedAttr(cfg.timeDisplayFormat == 1) + ">Weeks only</option>";
  html += "</select></div>";
  html += "<div><label for=\"show_battery_percent\">Battery percentage</label><select id=\"show_battery_percent\" name=\"show_battery_percent\">";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.showBatteryPercent) + ">Off</option>";
  html += String("<option value=\"1\"") + selectedAttr(cfg.showBatteryPercent) + ">On</option>";
  html += "</select></div>";
  html += String("<div><label for=\"refresh_interval_value\">Refresh interval</label><div class=\"dual\"><input id=\"refresh_interval_value\" name=\"refresh_interval_value\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]*\" data-number=\"int\" min=\"1\" step=\"1\" value=\"") + String(refreshValue) + "\"><select id=\"refresh_interval_unit\" name=\"refresh_interval_unit\"><option value=\"0\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_MINUTES) + ">Minutes</option><option value=\"1\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_HOURS) + ">Hours</option><option value=\"2\"" + selectedAttr(refreshUnit == REFRESH_INTERVAL_UNIT_DAYS) + ">Days</option></select></div><div id=\"refresh_interval_hint\" class=\"hint\">Default is 1 day. Shorter intervals use more battery.</div></div>";
  {
    const bool isOneDay = (cfg.refreshIntervalMinutes == 1440);
    char wakeTimeVal[6];
    snprintf(wakeTimeVal, sizeof(wakeTimeVal), "%02d:%02d", cfg.dailyWakeHour, cfg.dailyWakeMinute);
    html += "<div id=\"wake_time_wrap\"";
    if (!isOneDay) html += " class=\"hidden\"";
    html += ">";
    html += "<div class=\"grid\">";
    html += "<div><label for=\"wake_at_time\">Refresh at</label>";
    html += String("<input id=\"wake_at_time\" name=\"wake_at_time\" type=\"time\" value=\"") + wakeTimeVal + "\"></div>";
    html += "<div><label id=\"wake_timezone_label\" for=\"wake_timezone\">Time zone</label><select id=\"wake_timezone\" name=\"wake_timezone\" aria-labelledby=\"wake_timezone_label\">";
    for (uint8_t i = 0; i < (sizeof(TIME_ZONE_DISPLAY_ORDER) / sizeof(TIME_ZONE_DISPLAY_ORDER[0])); i++) {
      const uint8_t zoneIndex = TIME_ZONE_DISPLAY_ORDER[i];
      const TimeZoneOption& zone = TIME_ZONE_OPTIONS[zoneIndex];
      const String displayLabel = timeZoneDisplayLabel(zone);
      html += String("<option value=\"") + String(zoneIndex) + "\"";
      html += selectedAttr(cfg.dailyWakeTimeZone == zoneIndex);
      html += ">";
      html += htmlEscape(displayLabel.c_str());
      html += "</option>";
    }
    html += "</select></div>";
    html += "</div>";
    html += "<div id=\"wake_time_hint\" class=\"hint\">Daily refresh uses this local time and selected UTC zone. DST-aware zones adjust automatically.</div>";
    html += "</div>";
  }
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Privacy &amp; Security</h2><div class=\"grid\">";
  html += "<div><label for=\"show_wealth_change_screen\">Wealth change screen</label><select id=\"show_wealth_change_screen\" name=\"show_wealth_change_screen\">";
  html += String("<option value=\"1\"") + selectedAttr(cfg.showWealthChangeScreen) + ">On</option>";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.showWealthChangeScreen) + ">Off</option>";
  html += "</select><div class=\"hint\">Turn off to hide the wealth change screen that shows <span id=\"wealth_privacy_currency_label\">";
  html += currencyLabel;
  html += "</span> / BTC wealth information.</div></div>";
  html += "<div><label for=\"show_settings_screen\">Settings screen</label><select id=\"show_settings_screen\" name=\"show_settings_screen\">";
  html += String("<option value=\"1\"") + selectedAttr(cfg.showSettingsScreen) + ">On</option>";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.showSettingsScreen) + ">Off</option>";
  html += "</select><div class=\"hint\">Turn off to hide the settings change screen that shows <span id=\"settings_privacy_currency_label\">";
  html += currencyLabel;
  html += "</span> / BTC wealth information.</div></div>";
  html += "<div class=\"row-start\"><label for=\"setup_pin_enabled\">Protect setup with PIN</label><select id=\"setup_pin_enabled\" name=\"setup_pin_enabled\">";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.setupPinEnabled) + ">Off</option>";
  html += String("<option value=\"1\"") + selectedAttr(cfg.setupPinEnabled) + ">On</option>";
  html += "</select><div class=\"hint\">The clock screens stay unlocked. The PIN is only required before showing the setup page.</div></div>";
  html += "<div id=\"setup_pin_wrap\"><label for=\"setup_pin\">New setup PIN</label><input id=\"setup_pin\" name=\"setup_pin\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" placeholder=\"6 digits\"><div class=\"hint\">";
  html += hasExistingSetupPin
    ? "Leave blank to keep the current PIN. Enter a new 6-digit PIN to replace it."
    : "Enter a new 6-digit PIN if setup protection is enabled.";
  html += "</div></div>";
  html += "<div id=\"setup_pin_confirm_wrap\"><label for=\"setup_pin_confirm\">Confirm setup PIN</label><input id=\"setup_pin_confirm\" name=\"setup_pin_confirm\" type=\"password\" inputmode=\"numeric\" pattern=\"[0-9]*\" maxlength=\"6\" placeholder=\"Repeat the same 6 digits\"></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Daily Quote</h2><div class=\"grid\">";
  html += "<div><label for=\"quote_of_day_enabled\">Motivational quote</label><select id=\"quote_of_day_enabled\" name=\"quote_of_day_enabled\">";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.quoteOfDayEnabled) + ">Off</option>";
  html += String("<option value=\"1\"") + selectedAttr(cfg.quoteOfDayEnabled) + ">On</option>";
  html += "</select><div class=\"hint\">When on, a randomly selected motivational quote is shown on every refresh instead of the main clock. Cycles through all quotes before repeating.</div></div>";
  html += "</div></section>";

  html += "<section class=\"card\"><h2>Wi-Fi</h2>";
  html += "<div class=\"stack\">";
  html += "<div class=\"stack\"><div><label for=\"wifi_ssid_select\">Available Wi-Fi</label><div class=\"inline\"><select id=\"wifi_ssid_select\"><option value=\"\">Refresh to scan nearby networks</option></select><button id=\"scan_wifi_button\" class=\"secondary\" type=\"button\">Refresh Wi-Fi List</button></div></div><div class=\"hint\">Choose a scanned Wi-Fi name here, or type one manually below for hidden networks.</div></div>";
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

  html += "<div class=\"actions form-actions\"><button id=\"save_button\" type=\"submit\">Save</button></div>";
  html += "<div id=\"validation_status\" class=\"message info\" style=\"display:none;\"></div>";
  html += String("<input type=\"hidden\" id=\"auto_update_hidden\" name=\"auto_update_enabled\" value=\"") + (cfg.autoUpdateEnabled ? "1" : "0") + "\">";
  html += "</form>";

  html += "<section class=\"card\" style=\"margin-top:16px;\"><h2>Software Update</h2>";
  html += "<div class=\"grid\" style=\"margin-bottom:16px;\"><div><label for=\"auto_update_enabled\">Automatic Updates</label><select id=\"auto_update_enabled\">";
  html += String("<option value=\"1\"") + selectedAttr(cfg.autoUpdateEnabled) + ">On</option>";
  html += String("<option value=\"0\"") + selectedAttr(!cfg.autoUpdateEnabled) + ">Off</option>";
  html += "</select></div></div>";
  {
    const bool autoOn = cfg.autoUpdateEnabled;
    html += String("<div id=\"auto_update_info\"") + (autoOn ? "" : " class=\"hidden\"") + ">";
    html += "<div id=\"up_to_date_box\" class=\"message ok hidden\" style=\"margin-bottom:14px;\">";
    html += "<strong>Freedom Clock is up to date.</strong>";
    html += String("<div id=\"up_to_date_version\" style=\"color:#555;margin-top:4px;\">") + FIRMWARE_VERSION + "</div>";
    html += "</div></div>";
    html += "<div id=\"release_progress\" class=\"hidden\"><div class=\"install-progress\" style=\"margin-top:14px;\"><div class=\"install-spinner\"></div><div id=\"release_step_text\" class=\"install-step\">Checking...</div></div></div>";
    html += "<div id=\"release_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
    html += String("<div id=\"manual_update_section\" style=\"margin-top:16px;\"") + (autoOn ? " class=\"hidden\"" : "") + ">";
  }
  html += "<div class=\"firmware-head\"><div><label>Current software</label><div class=\"firmware-version\">";
  html += FIRMWARE_VERSION;
  html += "</div></div><button id=\"release_check_button\" class=\"secondary\" type=\"button\">Check for Update</button></div>";
  html += "<form id=\"firmware_form\" method=\"post\" action=\"/firmware\" enctype=\"multipart/form-data\" style=\"margin-top:16px;\">";
  html += "<div><label for=\"firmware_file\">Manual software update</label><input id=\"firmware_file\" name=\"firmware_file\" type=\"file\" accept=\".bin,application/octet-stream\"></div>";
  html += "<div class=\"hint\">Use the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin package for this device.</div>";
  html += "</form>";
  html += "</div>"; // close manual_update_section
  html += "<div id=\"release_summary\" class=\"releasebox hidden\">";
  html += "<div class=\"row\"><strong>Latest release:</strong> <span id=\"release_latest_version\">--</span></div>";
  html += "<div class=\"row\"><strong>Notes:</strong></div>";
  html += "<div id=\"release_notes\" class=\"release-notes\">--</div>";
  html += "<div id=\"latest_release_url_row\" class=\"row hidden\"><strong>URL:</strong><div class=\"copyline\"><div id=\"latest_release_url_text\" class=\"copytext\">--</div><button id=\"copy_latest_release_url_button\" class=\"secondary small\" type=\"button\">Copy</button></div></div>";
  html += "</div>";
  html += "<div id=\"firmware_status\" class=\"message info\" style=\"display:none;margin-top:14px;\"></div>";
  html += "<div id=\"firmware_progress\" class=\"hidden\"><div class=\"install-progress\"><div class=\"install-spinner\"></div><div id=\"firmware_step_text\" class=\"install-step\">Installing...</div></div></div>";
  html += "<div id=\"firmware_keep_data_notice\" class=\"hint hidden\">Saved data and settings stay on the device with the software update. If settings have not been saved yet, the device returns to setup mode after restart.</div>";
  html += "<div class=\"actions\" style=\"margin-top:14px;\"><button id=\"firmware_install_button\" type=\"button\" disabled>Install</button></div>";
  html += "</section>";

  html += "<script>";
  html += "(function(){";
  html += "function byId(id){return document.getElementById(id);}";
  html += "function diag(kind,msg,detail){if(window.fcDiag)window.fcDiag(kind,msg,detail);}";
  html += "function status(id,text,kind){var el=byId(id);if(!el)return;if(!text){el.style.display='none';el.textContent='';el.className='message info';return;}el.textContent=text;el.className='message '+(kind||'info');el.style.display='block';}";
  html += "function overlay(text){var box=byId('save_overlay');var label=byId('save_overlay_text');document.body.style.overflow='hidden';if(box)box.className=box.className.replace(/\\bhidden\\b/g,'');if(label)label.textContent=text||'Working...';}";
  html += "function overlayHtml(text){var label=byId('save_overlay_text');if(label)label.innerHTML=text||'Working...';}";
  html += "function hideOverlay(){var box=byId('save_overlay');if(box&&box.classList)box.classList.add('hidden');document.body.style.overflow='';}";
  html += "function flash(btn,text){if(!btn)return;var old=btn.textContent;btn.textContent=text;btn.disabled=true;setTimeout(function(){btn.textContent=old;btn.disabled=false;},2200);}";
  html += "function sourceFor(id){var map={copy_client_diagnostics_button:'client_diagnostics_text',copy_latest_release_url_button:'latest_release_url_text',copy_boot_path_diagnostics_button:'boot_path_diagnostics_text',copy_app_boot_diagnostics_button:'app_boot_diagnostics_text',copy_portal_diagnostics_button:'portal_diagnostics_text',copy_battery_log_button:'battery_log_text',copy_developer_stats_button:'developer_stats_text',copy_history_stats_button:'history_stats_text'};return byId(map[id]||'');}";
  html += "function readSource(el){if(!el)return '';return String(typeof el.value==='string'?el.value:el.textContent||'').trim();}";
  html += "function selectSource(el){try{if(el&&el.select){el.focus();el.select();return true;}if(el){var r=document.createRange();r.selectNodeContents(el);var s=window.getSelection();if(s){s.removeAllRanges();s.addRange(r);return true;}}}catch(e){}return false;}";
  html += "function copyFrom(btn){var src=sourceFor(btn.id);var value=readSource(src);if(!value){flash(btn,'Nothing to copy');return;}function copied(){flash(btn,'✓ Copied');}function blocked(err){diag('fallback-copy-failed','Copy failed',err&&err.message?err.message:String(err||''));selectSource(src);flash(btn,'Select + Copy');}try{if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(value).then(copied).catch(blocked);return;}selectSource(src);if(document.execCommand&&document.execCommand('copy'))copied();else blocked('copy blocked');}catch(err){blocked(err);}}";
  html += "function formParams(){var form=byId('config_form');return form?new URLSearchParams(new FormData(form)):new URLSearchParams();}";
  html += "function postJson(url,params){return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:params.toString(),cache:'no-store'}).then(function(r){return r.json();});}";
  html += "function fallbackCurrencyLabel(){var select=byId('currency_code');if(!select)return 'USD';var opt=select.options[select.selectedIndex];return opt?String(opt.textContent||'USD').trim():'USD';}";
  html += "function fallbackSyncCurrencyLabels(){var cur=fallbackCurrencyLabel();var manual=byId('manual_btc_currency_hint');var wealth=byId('wealth_currency_label');var monthly=byId('monthly_currency_label');var topic=byId('topic_price_currency_label');var wealthPrivacy=byId('wealth_privacy_currency_label');var settingsPrivacy=byId('settings_privacy_currency_label');var topicInput=byId('topic_price_value');if(manual)manual.textContent='BTC/'+cur+' price is fetched from CoinGecko over the internet, with mempool.space as backup.';if(wealth)wealth.textContent='Static wealth ('+cur+')';if(monthly)monthly.textContent='Monthly expenses ('+cur+')';if(topic)topic.textContent='BTC price topic ('+cur+')';if(wealthPrivacy)wealthPrivacy.textContent=cur;if(settingsPrivacy)settingsPrivacy.textContent=cur;if(topicInput){var value=String(topicInput.value||'').trim().toLowerCase();if(/^home\\/bitcoin\\/price\\/(usd|eur|chf)$/.test(value))topicInput.value='home/bitcoin/price/'+cur.toLowerCase();}}";
  html += "function fallbackSave(e){if(window.fcSetupReady)return;var form=e&&e.target;if(!form||form.id!=='config_form')return;e.preventDefault();overlay('Checking settings...');var params=formParams();postJson('/validate',params).then(function(data){if(data&&data.unlock_required){hideOverlay();status('validation_status',data.message||'Setup session expired. Enter your PIN again.','err');setTimeout(function(){window.location.replace('/');},1200);return;}if(!data||!data.ok){hideOverlay();status('validation_status',(data&&data.message)||'Settings check failed.','err');return;}overlay('Saving settings...');params.set('_fc_fetch','1');return postJson('/save',params).then(function(saved){if(!saved||!saved.ok){hideOverlay();status('validation_status',(saved&&saved.error)||'Failed to save settings.','err');return;}overlayHtml('Settings saved!<br>Device is restarting...');});}).catch(function(err){hideOverlay();diag('fallback-save-failed','Fallback save failed',err&&err.stack?err.stack:String(err||''));status('validation_status','Setup page action failed. See diagnostics at the top of the page.','err');});}";
  html += "function renderRelease(data){var latest=byId('release_latest_version');var notes=byId('release_notes');var url=byId('latest_release_url_text');var summary=byId('release_summary');var urlRow=byId('latest_release_url_row');window.fcFallbackOnlineFirmwareAvailable=!!(data&&data.asset_available&&data.newer);if(!data||!data.ok){if(summary&&summary.classList)summary.classList.add('hidden');if(urlRow&&urlRow.classList)urlRow.classList.add('hidden');status('release_status',(data&&data.message)||'Could not check for updates.','err');var failedInstall=byId('firmware_install_button');if(failedInstall)failedInstall.disabled=true;return;}if(latest)latest.textContent=(data&&data.tag)||'Unknown';if(notes)notes.textContent=((data&&data.body)||'No release notes provided.').trim()||'No release notes provided.';if(url)url.textContent=(data&&data.html_url)||'";
  html += GITHUB_RELEASES_URL;
  html += "';if(summary&&data&&data.newer)summary.classList.remove('hidden');if(urlRow&&data&&data.newer)urlRow.classList.remove('hidden');if(data&&data.asset_available&&data.newer)status('release_status','New software is available.','ok');else if(data&&data.asset_available)status('release_status','You are using the latest version.','ok');else status('release_status','Latest GitHub release was found, but it does not include the expected ";
  html += DEVICE_MODEL_NAME;
  html += " software package. Use manual update with the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin file.','err');var install=byId('firmware_install_button');if(install)install.disabled=!window.fcFallbackOnlineFirmwareAvailable;}";
  html += "function setProgress(id,stepId,show,text){var box=byId(id);var step=byId(stepId);if(step&&text)step.textContent=text;if(box&&box.classList)box.classList.toggle('hidden',!show);}";
  html += "function fallbackRelease(e){if(window.fcSetupReady)return;e.preventDefault();e.stopPropagation();var btn=byId('release_check_button');if(btn)btn.disabled=true;status('validation_status','','info');status('release_status','','info');setProgress('release_progress','release_step_text',true,'Checking latest software...');postJson('/release-info',formParams()).then(function(data){renderRelease(data);setProgress('release_progress','release_step_text',false,'');if(btn)btn.disabled=false;}).catch(function(err){diag('fallback-release-failed','Release check failed',err&&err.stack?err.stack:String(err||''));status('release_status','Could not check for updates. See diagnostics at the top of the page.','err');setProgress('release_progress','release_step_text',false,'');if(btn)btn.disabled=false;});}";
  html += "function fallbackInstall(e){if(window.fcSetupReady)return;e.preventDefault();var file=byId('firmware_file');var form=byId('firmware_form');if(file&&file.files&&file.files.length&&form){overlay('Uploading software. Keep this phone connected until the device restarts...');form.submit();return;}if(!window.fcFallbackOnlineFirmwareAvailable){status('firmware_status','Choose a software .bin file or check for a newer release first.','err');return;}overlay('Installing software. Keep this phone connected until the device restarts...');postJson('/firmware-online',formParams()).then(function(data){if(data&&data.ok){overlayHtml('Software installed.<br>Device is restarting...');status('firmware_status',data.message||'Software installed. Device is restarting.','ok');}else{hideOverlay();status('firmware_status',(data&&data.error)||'Software install failed.','err');}}).catch(function(err){diag('fallback-install-failed','Software install failed',err&&err.stack?err.stack:String(err||''));hideOverlay();status('firmware_status','Software install failed. See diagnostics at the top of the page.','err');});}";
  html += "function fallbackCopySelectedWifi(){var select=byId('wifi_ssid_select');var input=byId('wifi_ssid');if(select&&input&&select.value)input.value=select.value;}";
  html += "function fallbackWifiScan(e){if(window.fcSetupReady)return;e.preventDefault();var select=byId('wifi_ssid_select');if(select){select.disabled=true;select.innerHTML='<option value=\"\">Scanning nearby networks...</option>';}fetch('/wifi-list',{cache:'no-store'}).then(function(r){return r.json();}).then(function(data){if(!select)return;var current=(byId('wifi_ssid')&&byId('wifi_ssid').value)||'';var networks=data&&Array.isArray(data.networks)?data.networks:[];select.innerHTML='<option value=\"\">'+(networks.length?'Choose a nearby network':'No networks found')+'</option>';networks.forEach(function(n){var opt=document.createElement('option');opt.value=n.ssid||'';opt.textContent=n.ssid||'';if(current&&n.ssid===current)opt.selected=true;select.appendChild(opt);});select.disabled=false;fallbackCopySelectedWifi();}).catch(function(err){diag('fallback-wifi-scan-failed','Wi-Fi scan failed',err&&err.stack?err.stack:String(err||''));if(select){select.innerHTML='<option value=\"\">Scan failed - type Wi-Fi name manually</option>';select.disabled=false;}});}";
  html += "function setHidden(id,hidden){var el=byId(id);if(el&&el.classList)el.classList.toggle('hidden',!!hidden);}";
  html += "function fallbackSyncSetupState(){fallbackSyncCurrencyLabels();var asset=byId('asset_mode');var mode=byId('portfolio_use_mode');var pin=byId('setup_pin_enabled');var refreshValue=byId('refresh_interval_value');var refreshUnit=byId('refresh_interval_unit');var assetValue=asset?asset.value:'2';var modeValue=mode?mode.value:'0';setHidden('mqtt_card',assetValue!=='0');setHidden('wealth_field_wrap',assetValue!=='1');setHidden('manual_btc_field_wrap',assetValue!=='2');setHidden('borrow_fee_wrap',modeValue!=='1');var pinOn=pin&&pin.value==='1';setHidden('setup_pin_wrap',!pinOn);setHidden('setup_pin_confirm_wrap',!pinOn);var isOneDay=refreshUnit&&refreshUnit.value==='2'&&parseInt(refreshValue&&refreshValue.value?refreshValue.value:'',10)===1;setHidden('wake_time_wrap',!isOneDay);}";
  html += "document.addEventListener('submit',fallbackSave,true);";
  html += "document.addEventListener('click',function(e){var t=e.target;while(t&&t.tagName!=='BUTTON')t=t.parentNode;if(!t)return;if(/^copy_.*_button$/.test(t.id)){e.preventDefault();e.stopImmediatePropagation();copyFrom(t);return;}if(t.id==='clear_client_diagnostics_button'){e.preventDefault();var txt=byId('client_diagnostics_text');var box=byId('client_diagnostics');if(txt)txt.value='';if(box&&box.classList)box.classList.add('hidden');return;}if(window.fcSetupReady)return;if(t.id==='release_check_button')fallbackRelease(e);else if(t.id==='firmware_install_button')fallbackInstall(e);else if(t.id==='scan_wifi_button')fallbackWifiScan(e);},true);";
  html += "['asset_mode','portfolio_use_mode','currency_code','setup_pin_enabled','refresh_interval_value','refresh_interval_unit'].forEach(function(id){var el=byId(id);if(el){el.addEventListener('change',fallbackSyncSetupState);el.addEventListener('input',fallbackSyncSetupState);}});";
  html += "var fallbackWifiSelect=byId('wifi_ssid_select');if(fallbackWifiSelect){fallbackWifiSelect.addEventListener('change',fallbackCopySelectedWifi);fallbackWifiSelect.addEventListener('input',fallbackCopySelectedWifi);}";
  html += "fallbackSyncSetupState();";
  html += "window.fcSetupFallbackReady=true;";
  html += "})();";
  html += "</script>";

#if ENABLE_DEVELOPER_STATS
  html += "<section class=\"card\" style=\"margin-top:16px;\"><h2>Developer</h2>";
  html += "<div class=\"hint\">Local development tools. These exports can include sensitive device, battery, and financial history data.</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">Last boot path diagnostics</h3>";
  html += "<div class=\"hint\">Shows which early boot path the device entered: welcome, setup portal, factory reset, or app screen.</div>";
  html += "<textarea id=\"boot_path_diagnostics_text\" readonly>";
  html += htmlEscape(lastBootPathDiagnosticsText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_boot_path_diagnostics_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">Last app boot diagnostics</h3>";
  html += "<div class=\"hint\">Copy this after reproducing button, wake, or partial-refresh issues. It records reset/wake reasons and screen-render decisions without BTC amounts, passwords, or PIN data.</div>";
  html += "<textarea id=\"app_boot_diagnostics_text\" readonly>";
  html += htmlEscape(lastAppBootDiagnosticsText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_app_boot_diagnostics_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">Last setup portal diagnostics</h3>";
  html += "<div class=\"hint\">Copy this if the device unexpectedly shows SETUP ERROR or enters setup after saving settings. It records AP startup state, exit action, button pins, and the post-save restart guard.</div>";
  html += "<textarea id=\"portal_diagnostics_text\" readonly>";
  html += htmlEscape(lastPortalDiagnosticsText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_portal_diagnostics_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">Battery stats</h3>";
  html += "<div class=\"hint\">Each wake records measured battery voltage and the percent shown by the device. Copy this log when we need to tune the battery curve.</div>";
  html += "<textarea id=\"battery_log_text\" readonly>";
  html += htmlEscape(batteryLogText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_battery_log_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">Storage stats</h3>";
  html += "<div class=\"hint\">Enabled by local build flag. Use this to watch sketch, update partition, heap, NVS, and quote database size before release.</div>";
  html += "<textarea id=\"developer_stats_text\" readonly>";
  html += htmlEscape(developerStatsText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_developer_stats_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "<div style=\"margin-top:16px;\"><h3 style=\"margin:0 0 8px;font-size:16px;\">History stats</h3>";
  html += "<div class=\"hint\">Developer-only export of the stored wealth/BTC history used by the freedom change and wealth change screens. This is sensitive financial data.</div>";
  html += "<textarea id=\"history_stats_text\" readonly>";
  html += htmlEscape(historyStatsText.c_str());
  html += "</textarea>";
  html += "<div class=\"actions\" style=\"margin-top:12px;\"><button id=\"copy_history_stats_button\" class=\"secondary\" type=\"button\">Copy</button></div>";
  html += "</div>";
  html += "</section>";
#endif

  html += "<script>";
  html += "(function(){";
  html += "const form=document.getElementById('config_form');";
  html += "const clientDiagnostics=document.getElementById('client_diagnostics');";
  html += "const clientDiagnosticsText=document.getElementById('client_diagnostics_text');";
  html += "const copyClientDiagnosticsButton=document.getElementById('copy_client_diagnostics_button');";
  html += "const clearClientDiagnosticsButton=document.getElementById('clear_client_diagnostics_button');";
  html += "const ownerInput=document.getElementById('owner_name');";
  html += "const asset=document.getElementById('asset_mode');";
  html += "const mode=document.getElementById('portfolio_use_mode');";
  html += "const currencySelect=document.getElementById('currency_code');";
  html += "const manualBtcCurrencyHint=document.getElementById('manual_btc_currency_hint');";
  html += "const wealthCurrencyLabel=document.getElementById('wealth_currency_label');";
  html += "const monthlyCurrencyLabel=document.getElementById('monthly_currency_label');";
  html += "const topicPriceCurrencyLabel=document.getElementById('topic_price_currency_label');";
  html += "const wealthPrivacyCurrencyLabel=document.getElementById('wealth_privacy_currency_label');";
  html += "const settingsPrivacyCurrencyLabel=document.getElementById('settings_privacy_currency_label');";
  html += "const wealthWrap=document.getElementById('wealth_field_wrap');";
  html += "const manualBtcWrap=document.getElementById('manual_btc_field_wrap');";
  html += "const borrowWrap=document.getElementById('borrow_fee_wrap');";
  html += "const borrowInput=document.getElementById('borrow_fee_annual_pct');";
  html += "const setupPinEnabled=document.getElementById('setup_pin_enabled');";
  html += "const setupPinWrap=document.getElementById('setup_pin_wrap');";
  html += "const setupPinConfirmWrap=document.getElementById('setup_pin_confirm_wrap');";
  html += "const mqttCard=document.getElementById('mqtt_card');";
  html += "const topicPriceInput=document.getElementById('topic_price_value');";
  html += "const wifiSelect=document.getElementById('wifi_ssid_select');";
  html += "const wifiSsidInput=document.getElementById('wifi_ssid');";
  html += "const wifiPassInput=document.getElementById('wifi_pass');";
  html += "const clearWifiPass=document.getElementById('clear_wifi_pass');";
  html += "const refreshValueInput=document.getElementById('refresh_interval_value');";
  html += "const refreshUnitSelect=document.getElementById('refresh_interval_unit');";
  html += "const refreshHint=document.getElementById('refresh_interval_hint');";
  html += "const wakeTimeWrap=document.getElementById('wake_time_wrap');";
  html += "const wakeTimeHint=document.getElementById('wake_time_hint');";
  html += "const autoUpdateSelect=document.getElementById('auto_update_enabled');";
  html += "const autoUpdateInfo=document.getElementById('auto_update_info');";
  html += "const manualUpdateSection=document.getElementById('manual_update_section');";
  html += "const upToDateBox=document.getElementById('up_to_date_box');";
  html += "const upToDateVersion=document.getElementById('up_to_date_version');";
  html += "const autoUpdateHidden=document.getElementById('auto_update_hidden');";
  html += "const scanButton=document.getElementById('scan_wifi_button');";
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
  html += "const releaseProgress=document.getElementById('release_progress');";
  html += "const releaseStepText=document.getElementById('release_step_text');";
  html += "const releaseSummary=document.getElementById('release_summary');";
  html += "const releaseLatestVersion=document.getElementById('release_latest_version');";
  html += "const releaseNotes=document.getElementById('release_notes');";
  html += "const latestReleaseUrlRow=document.getElementById('latest_release_url_row');";
  html += "const latestReleaseUrlText=document.getElementById('latest_release_url_text');";
  html += "const copyLatestReleaseUrlButton=document.getElementById('copy_latest_release_url_button');";
  html += "const bootPathDiagnosticsText=document.getElementById('boot_path_diagnostics_text');";
  html += "const copyBootPathDiagnosticsButton=document.getElementById('copy_boot_path_diagnostics_button');";
  html += "const appBootDiagnosticsText=document.getElementById('app_boot_diagnostics_text');";
  html += "const copyAppBootDiagnosticsButton=document.getElementById('copy_app_boot_diagnostics_button');";
  html += "const portalDiagnosticsText=document.getElementById('portal_diagnostics_text');";
  html += "const copyPortalDiagnosticsButton=document.getElementById('copy_portal_diagnostics_button');";
  html += "const batteryLogText=document.getElementById('battery_log_text');";
  html += "const copyBatteryLogButton=document.getElementById('copy_battery_log_button');";
  html += "const developerStatsText=document.getElementById('developer_stats_text');";
  html += "const copyDeveloperStatsButton=document.getElementById('copy_developer_stats_button');";
  html += "const historyStatsText=document.getElementById('history_stats_text');";
  html += "const copyHistoryStatsButton=document.getElementById('copy_history_stats_button');";
  html += "const mqttPassInput=document.getElementById('mqtt_pass');";
  html += "const clearMqttPass=document.getElementById('clear_mqtt_pass');";
  html += "const freedomPreviewCard=document.getElementById('freedom_preview_card');";
  html += "const freedomPreviewValue=document.getElementById('freedom_preview_value');";
  html += "const freedomPreviewHint=document.getElementById('freedom_preview_hint');";
  html += "let onlineFirmwareAvailable=false;";
  html += "let previewPriceValue=";
  html += String(previewPriceValue, 2);
  html += ";";
  html += "let previewHasPriceValue=";
  html += previewHasPriceValue ? "true" : "false";
  html += ";";
  html += "let previewMqttBalanceBtc=";
  html += String(previewBalanceBtc, 8);
  html += ";";
  html += "let previewHasMqttBalanceBtc=";
  html += previewHasBalanceBtc ? "true" : "false";
  html += ";";
  html += "const setupConfiguredBeforeInstall=";
  html += cfg.configured ? "true" : "false";
  html += ";";
  html += "const SETUP_SCROLL_KEY='freedom_clock_setup_scroll_y';";
  html += "let setupUserInteracted=false;";
  html += "let releaseCheckInFlight=false;";
  html += "let saveInFlight=false;";
  html += "let autoUpdateUiInitialized=false;";
  html += "let firmwareInstallInFlight=false;";
  html += "let firmwareProgressInterval=null;";
  html += "let firmwareProgressStart=0;";
  html += "let firmwareDisconnectMonitor=null;";
  html += "let firmwareOfflineHandler=null;";
  html += "let firmwareFinishHandled=false;";
  html += "let releaseProgressInterval=null;";
  html += "let releaseProgressStart=0;";
  html += "function appendClientDiagnostic(kind,message,detail){if(window.fcDiag){window.fcDiag(kind,message,detail);return;}try{const now=new Date().toISOString();const lines=[];lines.push('['+now+'] '+(kind||'info')+': '+String(message||''));if(detail)lines.push(String(detail));lines.push('url='+location.href);lines.push('ua='+navigator.userAgent);const text=lines.join('\\n');if(clientDiagnosticsText){clientDiagnosticsText.value=(clientDiagnosticsText.value?clientDiagnosticsText.value+'\\n\\n':'')+text;}if(clientDiagnostics)clientDiagnostics.classList.remove('hidden');}catch(e){}}";
  html += "['pointerdown','touchstart','keydown'].forEach(function(name){window.addEventListener(name,function(){setupUserInteracted=true;},{once:true,passive:true});});";
  html += "function signature(){return form?new URLSearchParams(new FormData(form)).toString():'';}";
  html += "function setStatus(text,kind){if(!statusBox)return;if(!text){statusBox.style.display='none';statusBox.textContent='';statusBox.className='message info';return;}statusBox.textContent=text;statusBox.className='message '+(kind||'info');statusBox.style.display='block';if(kind==='err')setTimeout(function(){statusBox.scrollIntoView({behavior:'smooth',block:'nearest'});},60);}";
  html += "function setFirmwareStatus(text,kind){if(!firmwareStatus)return;if(!text){firmwareStatus.style.display='none';firmwareStatus.textContent='';firmwareStatus.className='message info';return;}firmwareStatus.textContent=text;firmwareStatus.className='message '+(kind||'info');firmwareStatus.style.display='block';}";
  html += "function setReleaseStatus(text,kind){if(!releaseStatus)return;if(!text){releaseStatus.style.display='none';releaseStatus.textContent='';releaseStatus.className='message info';return;}releaseStatus.textContent=text;releaseStatus.className='message '+(kind||'info');releaseStatus.style.display='block';}";
  html += "function setActionOverlayBusy(busy){const spinner=document.getElementById('save_overlay_spinner');if(spinner)spinner.classList.toggle('hidden',!busy);}";
  html += "function setActionOverlayAction(label,handler){const action=document.getElementById('save_overlay_action');if(!action)return;if(label){action.textContent=label;action.onclick=handler||null;action.classList.remove('hidden');}else{action.classList.add('hidden');action.onclick=null;}}";
  html += "function showActionOverlay(text){const overlay=document.getElementById('save_overlay');const overlayText=document.getElementById('save_overlay_text');document.body.style.overflow='hidden';setActionOverlayBusy(true);setActionOverlayAction('',null);if(overlay)overlay.classList.remove('hidden');if(overlayText)overlayText.textContent=text||'Working...';}";
  html += "function setActionOverlayText(text){const overlayText=document.getElementById('save_overlay_text');if(overlayText)overlayText.textContent=text||'Working...';}";
  html += "function setActionOverlayHtml(text){const overlayText=document.getElementById('save_overlay_text');if(overlayText)overlayText.innerHTML=text||'Working...';}";
  html += "function hideActionOverlay(){const overlay=document.getElementById('save_overlay');if(overlay)overlay.classList.add('hidden');setActionOverlayAction('',null);setActionOverlayBusy(true);document.body.style.overflow='';}";
  html += "function clearFirmwareDisconnectMonitor(){if(firmwareDisconnectMonitor){clearInterval(firmwareDisconnectMonitor);firmwareDisconnectMonitor=null;}if(firmwareOfflineHandler){window.removeEventListener('offline',firmwareOfflineHandler);firmwareOfflineHandler=null;}}";
  html += "function showFirmwareRestartedState(message){if(firmwareFinishHandled)return;firmwareFinishHandled=true;if(firmwareProgressInterval){clearInterval(firmwareProgressInterval);firmwareProgressInterval=null;}clearFirmwareDisconnectMonitor();setFirmwareStatus('Software installed. Device is restarting.','ok');setReleaseStatus('Software installed. Device is restarting.','ok');setActionOverlayBusy(true);setActionOverlayAction('',null);setActionOverlayHtml(message||'Software installed.<br>Device is restarting...');}";
  html += "function startFirmwareDisconnectMonitor(){clearFirmwareDisconnectMonitor();firmwareFinishHandled=false;firmwareOfflineHandler=function(){showFirmwareRestartedState('Device restarted and the setup Wi-Fi disconnected.');};window.addEventListener('offline',firmwareOfflineHandler,{once:true});firmwareDisconnectMonitor=setInterval(function(){if(typeof navigator!=='undefined'&&navigator.onLine===false)firmwareOfflineHandler();},1000);}";
  html += "function focusWithoutScroll(el){if(!el||!el.focus)return;try{el.focus({preventScroll:true});}catch(e){el.focus();}}";
  html += "function fetchWithTimeout(url,options,ms){var ctrl=new AbortController();var tid=setTimeout(function(){ctrl.abort();},ms);return fetch(url,Object.assign({},options,{signal:ctrl.signal})).finally(function(){clearTimeout(tid);});}";
  html += "function blurEditableFocus(){const el=document.activeElement;if(!el)return;const tag=String(el.tagName||'').toUpperCase();if((tag==='INPUT'||tag==='SELECT'||tag==='TEXTAREA')&&el.blur)el.blur();}";
  html += "function rememberScrollPosition(y){try{sessionStorage.setItem(SETUP_SCROLL_KEY,String(Math.max(0,Math.round(y||0))));}catch(e){}}";
  html += "function takeRememberedScrollPosition(){try{const raw=sessionStorage.getItem(SETUP_SCROLL_KEY);sessionStorage.removeItem(SETUP_SCROLL_KEY);const parsed=parseInt(raw||'',10);return Number.isFinite(parsed)&&parsed>0?parsed:null;}catch(e){return null;}}";
  html += "function clearRememberedScrollPositionSoon(){setTimeout(function(){try{sessionStorage.removeItem(SETUP_SCROLL_KEY);}catch(e){}},5000);}";
  html += "function restoreScrollPosition(y,focusEl){const target=Math.max(0,y||0);const restore=function(){window.scrollTo(0,target);if(focusEl)focusWithoutScroll(focusEl);else blurEditableFocus();};if(focusEl)focusWithoutScroll(focusEl);else blurEditableFocus();requestAnimationFrame(restore);[0,120].forEach(function(delay){setTimeout(restore,delay);});}";
  html += "function suppressInitialInputFocus(){if('scrollRestoration' in history)history.scrollRestoration='manual';const remembered=takeRememberedScrollPosition();if(remembered!==null){restoreScrollPosition(remembered);return;}const guard=document.getElementById('focus_guard');document.addEventListener('focusin',function onEarlyFocus(e){if(setupUserInteracted){document.removeEventListener('focusin',onEarlyFocus,true);return;}var el=e.target;if(el&&el.id==='focus_guard')return;var tag=el?String(el.tagName||'').toUpperCase():'';if(tag==='INPUT'||tag==='SELECT'||tag==='TEXTAREA'){if(el.blur)el.blur();}},true);const run=function(){if(setupUserInteracted)return;if(guard&&guard.focus){try{guard.focus({preventScroll:true});}catch(e){}}blurEditableFocus();if((window.scrollY||document.documentElement.scrollTop||0)<80)window.scrollTo(0,0);};[0,100,300,600,1000].forEach(function(delay){setTimeout(run,delay);});window.addEventListener('load',function(){[0,100,300].forEach(function(d){setTimeout(run,d);});});window.addEventListener('pageshow',function(){setTimeout(run,0);});}";
  html += "function syncOwnerUppercase(){if(ownerInput)ownerInput.value=(ownerInput.value||'').toUpperCase().slice(0,";
  html += String(OWNER_NAME_MAX_DISPLAY_CHARS);
  html += ");}";
  html += "function sanitizeNumberInput(el){if(!el)return;const mode=el.getAttribute('data-number')||'decimal';let value=String(el.value||'').replace(/,/g,'.');if(mode==='int'){el.value=value.replace(/[^0-9]/g,'');return;}value=value.replace(/[^0-9.]/g,'');const firstDot=value.indexOf('.');if(firstDot!==-1){value=value.slice(0,firstDot+1)+value.slice(firstDot+1).replace(/[.]/g,'');}el.value=value;}";
  html += "function wireNumberInputs(){document.querySelectorAll('[data-number]').forEach(function(el){sanitizeNumberInput(el);el.addEventListener('input',function(){sanitizeNumberInput(el);});el.addEventListener('paste',function(){setTimeout(function(){sanitizeNumberInput(el);},0);});});}";
  html += "function normalizeVersion(text){return String(text||'').trim().replace(/^v/i,'');}";
  html += "function flashCopyButton(btn,text,ms,disabled){if(!btn)return;const orig=btn.textContent;btn.textContent=text;if(disabled)btn.disabled=true;setTimeout(function(){btn.textContent=orig;if(disabled)btn.disabled=false;},ms||2200);}";
  html += "function selectCopySource(el){if(!el)return false;try{if(el.select){focusWithoutScroll(el);el.select();if(el.setSelectionRange)el.setSelectionRange(0,(el.value||'').length);return true;}const range=document.createRange();range.selectNodeContents(el);const sel=window.getSelection();if(sel){sel.removeAllRanges();sel.addRange(range);return true;}}catch(e){}return false;}";
  html += "async function copyText(text,label,btn,sourceEl){const value=String(text||'').trim();if(!value){flashCopyButton(btn,'Nothing to copy',1800,false);if(!btn)setReleaseStatus('Nothing to copy yet.','err');return;}try{let copied=false;if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(value);copied=true;}else{let area=null;let target=sourceEl;if(!target){area=document.createElement('textarea');area.value=value;area.setAttribute('readonly','');area.style.position='fixed';area.style.top='8px';area.style.left='8px';area.style.width='2px';area.style.height='2px';area.style.opacity='0.01';area.style.fontSize='16px';document.body.appendChild(area);target=area;}selectCopySource(target);copied=!!(document.execCommand&&document.execCommand('copy'));if(area)document.body.removeChild(area);if(!copied)throw new Error('copy blocked');}flashCopyButton(btn,'✓ Copied',2500,true);if(!btn)setReleaseStatus((label||'Text')+' copied.','ok');}catch(err){appendClientDiagnostic('copy-failed',label||'Copy failed',err&&err.message?err.message:String(err||''));if(sourceEl)selectCopySource(sourceEl);flashCopyButton(btn,'Select + Copy',2600,false);if(!btn)setReleaseStatus('Copy blocked by this browser. The text is selected; use Copy manually.','err');}}";
  html += "function hasManualFirmwareFile(){return !!(firmwareFileInput&&firmwareFileInput.files&&firmwareFileInput.files.length>0);}";
  html += "function updateFirmwareInstallButton(){const enabled=!!(onlineFirmwareAvailable||hasManualFirmwareFile());if(firmwareInstallButton)firmwareInstallButton.disabled=!enabled||firmwareInstallInFlight;if(firmwareKeepDataNotice)firmwareKeepDataNotice.classList.toggle('hidden',!enabled||firmwareInstallInFlight);}";
  html += "function hideReleaseSummary(){onlineFirmwareAvailable=false;if(releaseSummary)releaseSummary.classList.add('hidden');if(latestReleaseUrlRow)latestReleaseUrlRow.classList.add('hidden');updateFirmwareInstallButton();}";
  html += "function handleUnlockRequired(data,setter){if(data&&data.unlock_required){if(setter)setter(data.message||'Setup session expired. Enter your PIN again.','err');setTimeout(function(){window.location.replace('/');},1200);return true;}return false;}";
  html += "function renderReleaseInfo(data){hideReleaseSummary();setFirmwareStatus('', 'info');const latestTag=String((data&&data.tag)||'').trim();const notes=String((data&&data.body)||'No release notes provided.').trim()||'No release notes provided.';const url=String((data&&data.html_url)||'').trim();const assetAvailable=!!(data&&data.asset_available);const newer=!!(data&&data.newer);onlineFirmwareAvailable=assetAvailable&&newer;if(releaseLatestVersion)releaseLatestVersion.textContent=latestTag||'Unknown';if(releaseNotes)releaseNotes.textContent=notes;if(latestReleaseUrlText)latestReleaseUrlText.textContent=url||'";
  html += GITHUB_RELEASES_URL;
  html += "';const latestNormalized=normalizeVersion(latestTag);const currentNormalized=normalizeVersion('";
  html += FIRMWARE_VERSION;
  html += "');updateFirmwareInstallButton();if(!assetAvailable){setReleaseStatus('Latest GitHub release was found, but it does not include the expected ";
  html += DEVICE_MODEL_NAME;
  html += " software package. Use manual update with the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin file.','err');setFirmwareStatus('Matching online software package was not found. Use the ";
  html += DEVICE_MODEL_NAME;
  html += " .bin package for manual update.','err');return;}if(!newer){const isAuto=autoUpdateSelect&&autoUpdateSelect.value==='1';if(isAuto&&upToDateBox){upToDateBox.classList.remove('hidden');}else{setReleaseStatus('You are using the latest version.','ok');}return;}if(latestReleaseUrlRow)latestReleaseUrlRow.classList.remove('hidden');if(releaseSummary)releaseSummary.classList.remove('hidden');setReleaseStatus('New software is available.','ok');}";
  html += "function readNumber(id){const el=document.getElementById(id);const value=parseFloat(el&&el.value?el.value:'');return Number.isFinite(value)?value:0;}";
  html += "function selectedCurrencyLabel(){if(!currencySelect)return 'USD';const opt=currencySelect.options[currencySelect.selectedIndex];return opt?String(opt.textContent||'USD').trim():'USD';}";
  html += "function updateCurrencyLabels(){const cur=selectedCurrencyLabel();if(manualBtcCurrencyHint)manualBtcCurrencyHint.textContent='BTC/'+cur+' price is fetched from CoinGecko over the internet, with mempool.space as backup.';if(wealthCurrencyLabel)wealthCurrencyLabel.textContent='Static wealth ('+cur+')';if(monthlyCurrencyLabel)monthlyCurrencyLabel.textContent='Monthly expenses ('+cur+')';if(topicPriceCurrencyLabel)topicPriceCurrencyLabel.textContent='BTC price topic ('+cur+')';if(wealthPrivacyCurrencyLabel)wealthPrivacyCurrencyLabel.textContent=cur;if(settingsPrivacyCurrencyLabel)settingsPrivacyCurrencyLabel.textContent=cur;}";
  html += "function syncDefaultMqttPriceTopic(){if(!topicPriceInput)return;const value=String(topicPriceInput.value||'').trim().toLowerCase();if(!/^home\\/bitcoin\\/price\\/(usd|eur|chf)$/.test(value))return;topicPriceInput.value='home/bitcoin/price/'+selectedCurrencyLabel().toLowerCase();}";
  html += "function formatMoney(value){const cur=selectedCurrencyLabel();if(!(value>=0))return '--';if(value>=1000000)return (value/1000000).toFixed(2)+' mil '+cur;if(value>=1000)return (value/1000).toFixed(2)+'k '+cur;return value.toFixed(2)+' '+cur;}";
  html += "function computeFreedom(wealthValue,monthlyExpenseToday,inflationAnnual,assetGrowthAnnual,useMode,borrowFeeAnnual){const out={hitCap:false,years:0,months:0,weeks:0,coveredWeeks:0};if(!(wealthValue>0)||!(monthlyExpenseToday>0))return out;if(!(inflationAnnual>=0))inflationAnnual=0;if(!(assetGrowthAnnual>-0.99))assetGrowthAnnual=-0.99;if(!(borrowFeeAnnual>-0.99))borrowFeeAnnual=-0.99;const maxYears=200;const maxMonths=12*maxYears;if(useMode==='1'){let annualExpenseMul=1+inflationAnnual;let annualAssetMul=1+assetGrowthAnnual;let annualDebtMul=1+borrowFeeAnnual;let annualExpense=monthlyExpenseToday*12;let collateralValue=wealthValue;let debt=0;let years=0;while(years<maxYears){collateralValue*=annualAssetMul;debt*=annualDebtMul;if((debt+annualExpense)>collateralValue)break;debt+=annualExpense;annualExpense*=annualExpenseMul;years++;}out.hitCap=years>=maxYears;let partialYear=0;if(!out.hitCap&&annualExpense>0&&collateralValue>debt){partialYear=(collateralValue-debt)/annualExpense;if(partialYear>0.999)partialYear=0.999;if(partialYear<0)partialYear=0;}const coveredMonthsFloat=(years+partialYear)*12;const coveredFullMonths=Math.floor(coveredMonthsFloat);const partialMonth=coveredMonthsFloat-coveredFullMonths;out.years=Math.floor(coveredFullMonths/12);out.months=coveredFullMonths%12;out.weeks=Math.max(0,Math.min(4,Math.floor(partialMonth*4.345+0.5)));out.coveredWeeks=coveredMonthsFloat*4.345;return out;}let monthlyExpenseMul=Math.pow(1+inflationAnnual,1/12);let monthlyAssetMul=Math.pow(1+assetGrowthAnnual,1/12);let monthlyExpense=monthlyExpenseToday;let remaining=wealthValue;let months=0;while(months<maxMonths){remaining*=monthlyAssetMul;if(remaining<monthlyExpense)break;remaining-=monthlyExpense;monthlyExpense*=monthlyExpenseMul;months++;}out.hitCap=months>=maxMonths;out.years=Math.floor(months/12);out.months=months%12;let partialMonth=0;if(monthlyExpense>0&&remaining>0){partialMonth=remaining/monthlyExpense;if(partialMonth>0.999)partialMonth=0.999;if(partialMonth<0)partialMonth=0;}out.weeks=Math.max(0,Math.min(4,Math.floor(partialMonth*4.345+0.5)));out.coveredWeeks=months*4.345+(partialMonth*4.345);return out;}";
  html += "function formatFreedom(result){if(result.hitCap)return 'FOREVER';if(result.years>99)return String(result.years)+'Y';const years=String(result.years).padStart(2,'0');const months=String(result.months).padStart(2,'0');return years+'Y '+months+'M '+String(result.weeks)+'W';}";
  html += "function capturePreviewMarketValues(message){const text=String(message||'');const priceMatch=text.match(/(?:BTC price|Price):\\s*([0-9]+(?:\\.[0-9]+)?)/i);if(priceMatch){const parsed=parseFloat(priceMatch[1]);if(Number.isFinite(parsed)&&parsed>0){previewPriceValue=parsed;previewHasPriceValue=true;}}const amountMatch=text.match(/(?:BTC amount|Amount):\\s*([0-9]+(?:\\.[0-9]+)?)/i);if(amountMatch){const parsed=parseFloat(amountMatch[1]);if(Number.isFinite(parsed)&&parsed>=0){previewMqttBalanceBtc=parsed;previewHasMqttBalanceBtc=true;}}}";
  html += "function updateFreedomPreview(){if(!freedomPreviewValue||!freedomPreviewHint)return;const assetMode=asset?asset.value:'2';const useMode=mode?mode.value:'0';let wealthValue=0;let canPreview=false;let hint='';if(assetMode==='1'){wealthValue=readNumber('default_wealth_value');canPreview=true;}else if(assetMode==='2'){const btc=readNumber('manual_btc_amount');if(previewHasPriceValue&&previewPriceValue>0){wealthValue=btc*previewPriceValue;canPreview=true;}}else{if(previewHasPriceValue&&previewHasMqttBalanceBtc&&previewPriceValue>0&&previewMqttBalanceBtc>=0){wealthValue=previewMqttBalanceBtc*previewPriceValue;canPreview=true;}}if(!canPreview){if(freedomPreviewCard)freedomPreviewCard.classList.add('hidden');freedomPreviewValue.textContent='';freedomPreviewHint.textContent='';return;}if(freedomPreviewCard)freedomPreviewCard.classList.remove('hidden');const result=computeFreedom(wealthValue,readNumber('monthly_exp_value'),readNumber('inflation_annual_pct')/100,readNumber('wealth_growth_annual_pct')/100,useMode,readNumber('borrow_fee_annual_pct')/100);freedomPreviewValue.textContent=formatFreedom(result);if(result.hitCap)hint='FOREVER means the model reached the device cap of 200 years.';freedomPreviewHint.textContent=hint;}";
  html += "function refreshSettingsForUnit(){if(!refreshUnitSelect)return{min:15,max:10080,hint:'Default is 1 day. Shorter intervals use more battery.'};if(refreshUnitSelect.value==='2')return{min:1,max:7,hint:'Choose 1 to 7 days. Default is 1 day. Shorter intervals use more battery.'};if(refreshUnitSelect.value==='1')return{min:1,max:168,hint:'Choose 1 to 168 hours. Default is 24 hours. Shorter intervals use more battery.'};return{min:15,max:10080,hint:'Choose 15 to 10080 minutes. Default is 1440 minutes, which is 1 day. Shorter intervals use more battery.'};}";
  html += "function updateRefreshControls(clampValue){if(!refreshValueInput)return;const settings=refreshSettingsForUnit();refreshValueInput.min=String(settings.min);refreshValueInput.max=String(settings.max);refreshValueInput.step='1';let value=parseInt(refreshValueInput.value||'',10);if(clampValue!==false){if(!Number.isFinite(value))value=settings.min;if(value<settings.min)value=settings.min;if(value>settings.max)value=settings.max;refreshValueInput.value=String(value);}const currentValue=Number.isFinite(value)?value:0;if(refreshHint)refreshHint.textContent=settings.hint;const isOneDay=refreshUnitSelect&&refreshUnitSelect.value==='2'&&currentValue===1;if(wakeTimeWrap)wakeTimeWrap.classList.toggle('hidden',!isOneDay);if(wakeTimeHint)wakeTimeHint.textContent='The device refreshes once per day at this local time in the selected UTC zone. DST-aware zones adjust automatically.';}";
  html += "function onRefreshValueInput(){updateRefreshControls(false);}";
  html += "function updateAutoUpdateUI(){const isAuto=autoUpdateSelect&&autoUpdateSelect.value==='1';if(autoUpdateHidden)autoUpdateHidden.value=autoUpdateSelect?autoUpdateSelect.value:'1';if(autoUpdateInfo)autoUpdateInfo.classList.toggle('hidden',!isAuto);if(manualUpdateSection)manualUpdateSection.classList.toggle('hidden',isAuto);if(upToDateBox)upToDateBox.classList.add('hidden');hideReleaseSummary();setReleaseStatus('','info');if(isAuto&&autoUpdateUiInitialized)checkLatestRelease(null);}";
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
  html += "if(wifiSelect){wifiSelect.innerHTML='';const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent=networks.length?'Choose a nearby network':'No networks found';wifiSelect.appendChild(placeholder);const currentSsid=wifiSsidInput?wifiSsidInput.value:'';networks.forEach(function(net){const option=document.createElement('option');option.value=net.ssid||'';option.textContent=(net.ssid||'');if(currentSsid&&option.value===currentSsid)option.selected=true;wifiSelect.appendChild(option);});wifiSelect.disabled=false;if(wifiSsidInput&&wifiSelect.value)wifiSsidInput.value=wifiSelect.value;}";
  html += "}catch(err){appendClientDiagnostic('wifi-scan-failed','Wi-Fi scan failed',err&&err.stack?err.stack:String(err||''));if(wifiSelect){wifiSelect.innerHTML='<option value=\"\">Type your Wi-Fi name manually</option>';wifiSelect.disabled=false;}setStatus(err&&err.message?err.message:'Wi-Fi scan failed. Type the SSID manually.','err');}";
  html += "if(scanButton)scanButton.disabled=false;";
  html += "}";
  html += "async function checkLatestRelease(event){";
  html += "if(event){event.preventDefault();event.stopPropagation();}";
  html += "if(releaseCheckInFlight)return;";
  html += "releaseCheckInFlight=true;";
  html += "const actionButton=event&&event.currentTarget?event.currentTarget:null;";
  html += "const previousScrollY=window.scrollY||document.documentElement.scrollTop||0;";
  html += "if(actionButton){rememberScrollPosition(previousScrollY);focusWithoutScroll(actionButton);window.scrollTo(0,previousScrollY);}";
  html += "setStatus('','info');";
  html += "hideReleaseSummary();";
  html += "startReleaseProgress();";
  html += "try{";
  html += "const response=await fetch('/release-info',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:form?new URLSearchParams(new FormData(form)).toString():'',cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setReleaseStatus))return;";
  html += "if(!data.ok){setReleaseStatus(data.message||'Could not load release info.','err');return;}";
  html += "renderReleaseInfo(data);";
  html += "}catch(err){appendClientDiagnostic('release-check-failed','Release check failed',err&&err.stack?err.stack:String(err||''));setReleaseStatus('Release check failed. Keep this phone connected and make sure the device can reach the internet over Wi-Fi.','err');}";
  html += "finally{stopReleaseProgress();releaseCheckInFlight=false;if(actionButton){restoreScrollPosition(previousScrollY,actionButton);clearRememberedScrollPositionSoon();}}";
  html += "}";
  html += "function validateManualFirmwareFile(){if(!firmwareFileInput||!firmwareFileInput.files||firmwareFileInput.files.length===0){setFirmwareStatus('Choose a software .bin file first.','err');return false;}const fileName=String((firmwareFileInput.files[0]&&firmwareFileInput.files[0].name)||'').toLowerCase();if(!fileName.endsWith('.bin')){setFirmwareStatus('Software file must end with .bin.','err');return false;}return true;}";
  html += "const installSteps=[{ms:0,text:'Preparing online software update...'},{ms:6000,text:'Connecting to Wi-Fi and GitHub...'},{ms:12000,text:'Downloading and installing software...'},{ms:24000,text:'Device may restart any moment...'},{ms:40000,text:'Waiting for setup Wi-Fi to disconnect...'}];";
  html += "function startFirmwareProgress(){";
  html += "firmwareProgressStart=Date.now();";
  html += "firmwareFinishHandled=false;";
  html += "if(firmwareProgress)firmwareProgress.classList.add('hidden');";
  html += "showActionOverlay(installSteps[0].text);";
  html += "setFirmwareStatus('','info');";
  html += "if(firmwareStepText)firmwareStepText.textContent=installSteps[0].text;";
  html += "firmwareProgressInterval=setInterval(function(){";
  html += "const elapsed=Date.now()-firmwareProgressStart;";
  html += "let step=installSteps[0];";
  html += "for(let i=1;i<installSteps.length;i++){if(elapsed>=installSteps[i].ms)step=installSteps[i];}";
  html += "setActionOverlayText(step.text);";
  html += "if(firmwareStepText)firmwareStepText.textContent=step.text;";
  html += "},500);}";
  html += "function stopFirmwareProgress(){";
  html += "if(firmwareProgressInterval){clearInterval(firmwareProgressInterval);firmwareProgressInterval=null;}";
  html += "clearFirmwareDisconnectMonitor();";
  html += "if(firmwareProgress)firmwareProgress.classList.add('hidden');hideActionOverlay();}";
  html += "function finishFirmwareInstallExperience(prefix){";
  html += "if(firmwareProgressInterval){clearInterval(firmwareProgressInterval);firmwareProgressInterval=null;}";
  html += "const doneText=prefix||'Software installed.';";
  html += "showFirmwareRestartedState(doneText+'<br>Device is restarting...');";
  html += "}";
  html += "const releaseSteps=[{ms:0,text:'Checking for updates...'}];";
  html += "function startReleaseProgress(){if(!releaseProgress)return;releaseProgressStart=Date.now();releaseProgress.classList.remove('hidden');setReleaseStatus('','info');if(releaseStepText)releaseStepText.textContent=releaseSteps[0].text;releaseProgressInterval=setInterval(function(){const elapsed=Date.now()-releaseProgressStart;let step=releaseSteps[0];for(let i=1;i<releaseSteps.length;i++){if(elapsed>=releaseSteps[i].ms)step=releaseSteps[i];}if(releaseStepText)releaseStepText.textContent=step.text;},500);}";
  html += "function stopReleaseProgress(){if(releaseProgressInterval){clearInterval(releaseProgressInterval);releaseProgressInterval=null;}if(releaseProgress)releaseProgress.classList.add('hidden');}";
  html += "async function installOnlineFirmware(){";
  html += "if(firmwareInstallInFlight)return;";
  html += "firmwareInstallInFlight=true;";
  html += "updateFirmwareInstallButton();";
  html += "if(releaseCheckButton)releaseCheckButton.disabled=true;";
  html += "startFirmwareProgress();";
  html += "startFirmwareDisconnectMonitor();";
  html += "setReleaseStatus('Installing software from GitHub. Keep this phone connected. The device will restart when finished.','info');";
  html += "let keepInstallOverlay=false;";
  html += "try{";
  html += "const response=await fetch('/firmware-online',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:form?new URLSearchParams(new FormData(form)).toString():'',cache:'no-store'});";
  html += "const data=await response.json();";
  html += "if(handleUnlockRequired(data,setFirmwareStatus))return;";
  html += "if(!data.ok){const message=data.message||'Software install failed.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');return;}";
  html += "keepInstallOverlay=true;const message=data.message||'Software installed. Restarting...';setFirmwareStatus(message,'ok');setReleaseStatus(message,'ok');finishFirmwareInstallExperience('Software installed.');";
  html += "}catch(err){appendClientDiagnostic('firmware-install-failed','Software install request failed',err&&err.stack?err.stack:String(err||''));if(firmwareFinishHandled)return;const elapsed=Date.now()-firmwareProgressStart;if(elapsed>18000){keepInstallOverlay=true;showFirmwareRestartedState('Connection closed while the device was updating.<br>Device is restarting...');return;}const message='Software install request failed. Use manual update if the device does not restart.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');}";
  html += "finally{if(!keepInstallOverlay&&!firmwareFinishHandled)stopFirmwareProgress();firmwareInstallInFlight=false;if(releaseCheckButton)releaseCheckButton.disabled=false;updateFirmwareInstallButton();}";
  html += "}";
  html += "function installSelectedFirmware(){if(hasManualFirmwareFile()){if(!validateManualFirmwareFile())return;if(firmwareInstallButton)firmwareInstallButton.disabled=true;if(firmwareKeepDataNotice)firmwareKeepDataNotice.classList.add('hidden');showActionOverlay('Uploading software. Keep this phone connected until the device restarts...');setFirmwareStatus('Uploading software. Keep this phone connected until the device restarts.','info');if(firmwareForm)firmwareForm.submit();return;}if(onlineFirmwareAvailable){installOnlineFirmware();return;}const message='Choose a software .bin file or check for a newer release first.';setFirmwareStatus(message,'err');setReleaseStatus(message,'err');";
  html += "}";
  html += "function onFormEdited(event){if(event&&event.target===wifiSelect)return;updateFreedomPreview();}";
  html += "if(asset) asset.addEventListener('change', update);";
  html += "if(mode) mode.addEventListener('change', update);";
  html += "if(currencySelect) currencySelect.addEventListener('change', function(){syncDefaultMqttPriceTopic();updateCurrencyLabels();updateFreedomPreview();});";
  html += "if(setupPinEnabled) setupPinEnabled.addEventListener('change', update);";
  html += "if(refreshUnitSelect) refreshUnitSelect.addEventListener('change', function(){updateRefreshControls();});";
  html += "if(refreshValueInput) refreshValueInput.addEventListener('input', onRefreshValueInput);";
  html += "if(refreshValueInput) refreshValueInput.addEventListener('blur', function(){updateRefreshControls(true);});";
  html += "if(autoUpdateSelect) autoUpdateSelect.addEventListener('change', updateAutoUpdateUI);";
  html += "if(ownerInput) ownerInput.addEventListener('input', syncOwnerUppercase);";
  html += "if(wifiPassInput) wifiPassInput.addEventListener('input', function(){if(clearWifiPass&&wifiPassInput.value)clearWifiPass.checked=false;});";
  html += "if(mqttPassInput) mqttPassInput.addEventListener('input', function(){if(clearMqttPass&&mqttPassInput.value)clearMqttPass.checked=false;});";
  html += "if(wifiSelect){const copySelectedWifi=function(){if(wifiSsidInput&&wifiSelect.value)wifiSsidInput.value=wifiSelect.value;};wifiSelect.addEventListener('change',copySelectedWifi);wifiSelect.addEventListener('input',copySelectedWifi);}";
  html += "if(form){form.addEventListener('input', onFormEdited);form.addEventListener('change', onFormEdited);form.addEventListener('submit', async function(event){event.preventDefault();if(saveInFlight)return;saveInFlight=true;const saveOverlay=document.getElementById('save_overlay');const saveOverlayText=document.getElementById('save_overlay_text');const focusGuard=document.getElementById('focus_guard');setStatus('','info');if(focusGuard)try{focusGuard.focus({preventScroll:true});}catch(e){}document.body.style.overflow='hidden';if(saveOverlay)saveOverlay.classList.remove('hidden');if(saveOverlayText)saveOverlayText.textContent='Checking settings…';function returnFocusToSave(){const sb=document.getElementById('save_button');if(sb)try{sb.focus({preventScroll:true});}catch(e){}}function abortSave(msg){returnFocusToSave();if(saveOverlay)saveOverlay.classList.add('hidden');document.body.style.overflow='';saveInFlight=false;if(msg)setStatus(msg,'err');}try{const params=new URLSearchParams(new FormData(form));let vData;try{const vResponse=await fetchWithTimeout('/validate',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:params.toString(),cache:'no-store'},60000);vData=await vResponse.json();}catch(e){appendClientDiagnostic('validate-fetch-failed','Settings check failed',e&&e.stack?e.stack:String(e||''));abortSave(e&&e.name==='AbortError'?'Settings check timed out. Reconnect to the device Wi-Fi and try again.':'Settings check failed. Keep this phone connected and try again.');return;}if(handleUnlockRequired(vData,setStatus)){returnFocusToSave();if(saveOverlay)saveOverlay.classList.add('hidden');document.body.style.overflow='';saveInFlight=false;return;}if(!vData.ok){abortSave(vData.message||'Settings check failed.');return;}capturePreviewMarketValues(vData.message||'');updateFreedomPreview();if(saveOverlayText)saveOverlayText.textContent='Saving settings…';params.set('_fc_fetch','1');try{const sResponse=await fetchWithTimeout('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:params.toString(),cache:'no-store'},15000);const sData=await sResponse.json();if(!sData.ok){abortSave(sData.error||'Failed to save settings.');return;}}catch(e){appendClientDiagnostic('save-fetch-failed','Save request failed',e&&e.stack?e.stack:String(e||''));abortSave(e&&e.name==='AbortError'?'Save timed out. Reconnect to the device Wi-Fi and try again.':'Failed to save settings. Keep this phone connected and try again.');return;}if(saveOverlayText)saveOverlayText.innerHTML='Settings saved!<br>Device is restarting…';}catch(err){appendClientDiagnostic('save-handler-error','Save handler error',err&&err.stack?err.stack:String(err||''));abortSave('An error occurred. Please try again.');}});if(saveButton)saveButton.disabled=false;}";
  html += "if(firmwareFileInput) firmwareFileInput.addEventListener('change', function(){setFirmwareStatus('', 'info');updateFirmwareInstallButton();});";
  html += "if(releaseCheckButton) releaseCheckButton.addEventListener('click', checkLatestRelease);";
  html += "if(firmwareInstallButton) firmwareInstallButton.addEventListener('click', installSelectedFirmware);";
  html += "if(copyLatestReleaseUrlButton) copyLatestReleaseUrlButton.addEventListener('click', function(){copyText(latestReleaseUrlText?latestReleaseUrlText.textContent:'','Latest release URL',copyLatestReleaseUrlButton,latestReleaseUrlText);});";
  html += "if(copyBootPathDiagnosticsButton) copyBootPathDiagnosticsButton.addEventListener('click', function(){copyText(bootPathDiagnosticsText?bootPathDiagnosticsText.value:'','Boot path diagnostics',copyBootPathDiagnosticsButton,bootPathDiagnosticsText);});";
  html += "if(copyAppBootDiagnosticsButton) copyAppBootDiagnosticsButton.addEventListener('click', function(){copyText(appBootDiagnosticsText?appBootDiagnosticsText.value:'','App boot diagnostics',copyAppBootDiagnosticsButton,appBootDiagnosticsText);});";
  html += "if(copyPortalDiagnosticsButton) copyPortalDiagnosticsButton.addEventListener('click', function(){copyText(portalDiagnosticsText?portalDiagnosticsText.value:'','Setup portal diagnostics',copyPortalDiagnosticsButton,portalDiagnosticsText);});";
  html += "if(copyBatteryLogButton) copyBatteryLogButton.addEventListener('click', function(){copyText(batteryLogText?batteryLogText.value:'','Battery stats',copyBatteryLogButton,batteryLogText);});";
  html += "if(copyDeveloperStatsButton) copyDeveloperStatsButton.addEventListener('click', function(){copyText(developerStatsText?developerStatsText.value:'','Developer stats',copyDeveloperStatsButton,developerStatsText);});";
  html += "if(copyHistoryStatsButton) copyHistoryStatsButton.addEventListener('click', function(){copyText(historyStatsText?historyStatsText.value:'','History stats',copyHistoryStatsButton,historyStatsText);});";
  html += "if(copyClientDiagnosticsButton) copyClientDiagnosticsButton.addEventListener('click', function(){copyText(clientDiagnosticsText?clientDiagnosticsText.value:'','Setup page diagnostics',copyClientDiagnosticsButton,clientDiagnosticsText);});";
  html += "if(clearClientDiagnosticsButton) clearClientDiagnosticsButton.addEventListener('click', function(){if(clientDiagnosticsText)clientDiagnosticsText.value='';if(clientDiagnostics)clientDiagnostics.classList.add('hidden');});";
  html += "if(scanButton) scanButton.addEventListener('click', refreshWifiList);";
  html += "refreshWifiList();";
  html += "suppressInitialInputFocus();";
  html += "wireNumberInputs();";
  html += "syncOwnerUppercase();";
  html += "updateCurrencyLabels();";
  html += "update();";
  html += "updateFreedomPreview();";
  html += "updateAutoUpdateUI();autoUpdateUiInitialized=true;";
  html += "updateFirmwareInstallButton();";
  html += "window.fcSetupReady=true;";
  html += "})();";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

static constexpr int BATTERY_BODY_W = 20;
static constexpr int BATTERY_BODY_H = 10;
static constexpr int BATTERY_TIP_W = 1;
static constexpr int BATTERY_TIP_H = 5;
static constexpr int BATTERY_RIGHT_MARGIN = 3;
static constexpr int BATTERY_TEXT_GAP = 2;
static constexpr int BATTERY_MAX_TEXT_W = 24;
static constexpr int BATTERY_GROUP_MIN_X = DEVICE_DISPLAY_WIDTH
  - BATTERY_RIGHT_MARGIN
  - BATTERY_MAX_TEXT_W
  - BATTERY_TEXT_GAP
  - BATTERY_BODY_W
  - BATTERY_TIP_W;
static constexpr int BATTERY_ICON_Y = 5;
static constexpr int BATTERY_TEXT_Y = 7;

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

#include "display_screens.h"

// ============================================================
// Config Portal
// ============================================================

#pragma once

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
  submitted.monthlyExpenseValue = portalServer.arg("monthly_exp_value").toFloat();
  submitted.inflationAnnual = portalServer.arg("inflation_annual_pct").toFloat() / 100.0f;
  submitted.wealthGrowthAnnual = portalServer.arg("wealth_growth_annual_pct").toFloat() / 100.0f;
  submitted.defaultWealthValue = portalServer.arg("default_wealth_value").toFloat();
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
  submitted.currencyCode = sanitizeCurrencyCode((uint8_t)portalServer.arg("currency_code").toInt());
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
  safeCopyString(submitted.topicPriceValue, sizeof(submitted.topicPriceValue), portalServer.arg("topic_price_value"));
  safeCopyString(submitted.topicBalanceBtc, sizeof(submitted.topicBalanceBtc), portalServer.arg("topic_balance_btc"));
  submitted.autoUpdateEnabled = (portalServer.arg("auto_update_enabled") == "1");
  if (submitted.refreshIntervalMinutes == 1440 && portalServer.hasArg("wake_at_time")) {
    const String wt = portalServer.arg("wake_at_time");
    if (wt.length() >= 5 && wt.charAt(2) == ':') {
      submitted.dailyWakeHour   = (uint8_t)constrain(wt.substring(0, 2).toInt(), 0, 23);
      submitted.dailyWakeMinute = (uint8_t)constrain(wt.substring(3, 5).toInt(), 0, 59);
    }
  }
  submitted.dailyWakeTimeZone = (uint8_t)constrain(portalServer.arg("wake_timezone").toInt(), 0, (int)(sizeof(TIME_ZONE_OPTIONS) / sizeof(TIME_ZONE_OPTIONS[0])) - 1);
  submitted.quoteOfDayEnabled = portalServer.arg("quote_of_day_enabled") == "1";
  submitted.timeDisplayFormat = (uint8_t)constrain(portalServer.arg("time_display_format").toInt(), 0, 1);
  submitted.showBatteryPercent = portalServer.arg("show_battery_percent") == "1";
  submitted.showWealthChangeScreen = portalServer.arg("show_wealth_change_screen") == "1";
  submitted.showSettingsScreen = portalServer.arg("show_settings_screen") == "1";
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

  if (!setupPinMatchesStoredHash(enteredPin, deviceConfig.setupPinHash)) {
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

  const bool isFetch = portalServer.arg("_fc_fetch") == "1";

  DeviceConfig submitted = deviceConfig;
  char errorMessage[128];
  const bool requestedPinEnabled = portalServer.arg("setup_pin_enabled").toInt() == 1;
  const String requestedPin = portalServer.arg("setup_pin");
  const String requestedPinConfirm = portalServer.arg("setup_pin_confirm");

  loadSubmittedPortalConfig(submitted);
  const bool sourceChanged = didHistorySourceChange(deviceConfig, submitted);

  auto sendSaveError = [&](const char* msg) {
    if (isFetch) {
      String json = F("{\"ok\":false,\"error\":\"");
      String escaped = msg;
      escaped.replace("\"", "\\\"");
      json += escaped;
      json += F("\"}");
      portalServer.send(200, "application/json", json);
    } else {
      portalSendHtml(buildPortalPage(submitted, msg, true));
    }
  };

  if (!validateDeviceConfig(submitted, errorMessage, sizeof(errorMessage))) {
    sendSaveError(errorMessage);
    return;
  }

  if (!validatePortalPinSettings(deviceConfig, requestedPinEnabled, requestedPin, requestedPinConfirm, errorMessage, sizeof(errorMessage))) {
    sendSaveError(errorMessage);
    return;
  }

  applyPortalPinSettings(submitted, deviceConfig, requestedPinEnabled, requestedPin);

  if (!saveDeviceConfig(submitted)) {
    sendSaveError("Failed to save settings to device storage.");
    return;
  }

  DeviceConfig verifiedConfig;
  if (!loadDeviceConfig(verifiedConfig)) {
    char verifyError[220];
    snprintf(
      verifyError,
      sizeof(verifyError),
      "Settings were written, but could not be verified from device storage: %s",
      lastConfigLoadStatus
    );
    sendSaveError(verifyError);
    return;
  }

  deviceConfig = verifiedConfig;
  setupPinFailedAttempts = 0;
  setupPinLockedUntil = 0;
  if (sourceChanged) {
    clearWealthHistoryWithReason("Cleared after portfolio source or currency change");
    clearWealthHistoryInMemory(wealthHistory);
    clearCachedBtcData();
  }
  portalExitAction = PORTAL_EXIT_ACTION_SAVE_CONFIG;
  setPostSaveRestartPending(true);
  savePortalDiagnostics("save-success", "settings saved; next software restart must go to app screen", true);
  portalSaveRequested = true;
  if (isFetch) {
    portalServer.send(200, "application/json", F("{\"ok\":true}"));
  } else {
    portalSendHtml(buildPortalConfirmationPage("SETTINGS SAVED", "Restarting..."));
  }
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
  preparePostFirmwareUpdateBoot();
  portalExitAction = PORTAL_EXIT_ACTION_FIRMWARE_UPDATE;
  portalSaveRequested = true;
  portalSendHtml(buildPortalConfirmationPage(
    "FIRMWARE UPDATED",
    "Restarting..."
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

// ============================================================
// Large-stack HTTPS task helpers
//
// The Arduino loopTask has ~8 KB of stack. An mbedTLS handshake
// needs ~6 KB of call-stack on top of the existing portal-handler
// frames, overflowing the stack and triggering a panic.
//
// Each helper below launches a temporary FreeRTOS task with a
// 20 KB stack, then busy-waits (yielding via delay) until the task
// finishes. The portal handler stays on the loopTask; only the TLS
// work runs on the larger-stack task.
// ============================================================

static constexpr uint32_t HTTPS_TASK_STACK = 20480;

struct OnlinePriceTaskCtx {
  DeviceConfig cfg;
  float priceValue;
  char errorBuf[192];
  volatile bool done;
  volatile bool success;
};
static void s_onlinePriceTask(void* param) {
  auto* ctx = static_cast<OnlinePriceTaskCtx*>(param);
  ctx->success = fetchOnlineBtcPrice(ctx->cfg, ctx->priceValue, ctx->errorBuf, sizeof(ctx->errorBuf));
  ctx->done = true;
  vTaskDelete(nullptr);
}
static bool fetchCoinGeckoInTask(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize) {
  OnlinePriceTaskCtx ctx = {};
  ctx.cfg = cfg;
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(s_onlinePriceTask, "price_fetch", HTTPS_TASK_STACK, &ctx, 1, &handle) != pdPASS) {
    return fetchOnlineBtcPrice(cfg, outPrice, errorBuf, errorBufSize);
  }
  const uint32_t start = millis();
  while (!ctx.done && (millis() - start) < 30000) delay(50);
  if (!ctx.done) { vTaskDelete(handle); snprintf(errorBuf, errorBufSize, "Price fetch timed out."); return false; }
  outPrice = ctx.priceValue;
  strlcpy(errorBuf, ctx.errorBuf, errorBufSize);
  return ctx.success;
}

struct GitHubTaskCtx {
  GitHubReleaseInfo info;
  char errorBuf[160];
  volatile bool done;
  volatile bool success;
};
static void s_gitHubTask(void* param) {
  auto* ctx = static_cast<GitHubTaskCtx*>(param);
  ctx->success = fetchLatestGitHubReleaseInfo(ctx->info, ctx->errorBuf, sizeof(ctx->errorBuf));
  ctx->done = true;
  vTaskDelete(nullptr);
}
static bool fetchGitHubInTask(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize) {
  GitHubTaskCtx ctx;
  ctx.errorBuf[0] = '\0';
  ctx.done = false;
  ctx.success = false;
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(s_gitHubTask, "gh_fetch", HTTPS_TASK_STACK, &ctx, 1, &handle) != pdPASS) {
    return fetchLatestGitHubReleaseInfo(outInfo, errorBuf, errorBufSize);
  }
  const uint32_t start = millis();
  while (!ctx.done && (millis() - start) < 35000) delay(50);
  if (!ctx.done) { vTaskDelete(handle); snprintf(errorBuf, errorBufSize, "Release check timed out."); return false; }
  outInfo = ctx.info;
  strlcpy(errorBuf, ctx.errorBuf, errorBufSize);
  return ctx.success;
}

struct FirmwareTaskCtx {
  String firmwareUrl;
  char errorBuf[160];
  volatile bool done;
  volatile bool success;
};
static void s_firmwareTask(void* param) {
  auto* ctx = static_cast<FirmwareTaskCtx*>(param);
  ctx->success = installFirmwareFromUrl(ctx->firmwareUrl, ctx->errorBuf, sizeof(ctx->errorBuf));
  ctx->done = true;
  vTaskDelete(nullptr);
}
static bool installFirmwareInTask(const String& firmwareUrl, char* errorBuf, size_t errorBufSize) {
  FirmwareTaskCtx ctx;
  ctx.firmwareUrl = firmwareUrl;
  ctx.errorBuf[0] = '\0';
  ctx.done = false;
  ctx.success = false;
  TaskHandle_t handle = nullptr;
  const uint32_t fwStack = 24576; // extra headroom for firmware streaming + flash writes
  if (xTaskCreate(s_firmwareTask, "fw_install", fwStack, &ctx, 1, &handle) != pdPASS) {
    return installFirmwareFromUrl(firmwareUrl, errorBuf, errorBufSize);
  }
  const uint32_t start = millis();
  while (!ctx.done && (millis() - start) < 120000) delay(500);
  if (!ctx.done) { vTaskDelete(handle); snprintf(errorBuf, errorBufSize, "Firmware download timed out."); return false; }
  strlcpy(errorBuf, ctx.errorBuf, errorBufSize);
  return ctx.success;
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
  if (!fetchGitHubInTask(releaseInfo, errorMessage, sizeof(errorMessage))) {
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

  if (!installFirmwareInTask(firmwareUrl, errorMessage, sizeof(errorMessage))) {
    String error = String("Wi-Fi: OK\nRELEASE CHECK: OK\nFIRMWARE DOWNLOAD: failed\n") + errorMessage;
    portalSendJson(false, error.c_str());
    return;
  }

  checkpointPortalUnixTime();
  preparePostFirmwareUpdateBoot();
  portalExitAction = PORTAL_EXIT_ACTION_FIRMWARE_UPDATE;
  portalSaveRequested = true;
  String success = String("Software updated from GitHub.\nPackage: ") + packageLabel + "\nRestarting...";
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
  WiFi.scanNetworks(true);  // async — radio does the scan; we yield to the WiFi stack
  const uint32_t scanStart = millis();
  while (WiFi.scanComplete() < 0 && (millis() - scanStart) < 8000) {
    yield();
    delay(20);
  }
  const int networkCount = WiFi.scanComplete();
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
    float validatedPrice = 0.0f;
    time_t cachedPriceTime = 0;
    const bool hasCachedPrice = loadCachedBtcPriceForCurrency(submitted.currencyCode, validatedPrice, &cachedPriceTime);
    const time_t now = time(nullptr);
    const bool cachedPriceIsFresh = hasCachedPrice
      && cachedPriceTime >= 1700000000
      && now >= cachedPriceTime
      && (uint32_t)(now - cachedPriceTime) <= PORTAL_PRICE_CACHE_REUSE_SECONDS;

    if (cachedPriceIsFresh) {
      char successMessage[256];
      snprintf(
        successMessage,
        sizeof(successMessage),
        "Wi-Fi: OK\nPRICE API: skipped\n- BTC price: %.2f %s (recent cached value)%s",
        validatedPrice,
        currencyCodeLabel(submitted.currencyCode),
        sourceChangedNote
      );
      portalSendJson(true, successMessage);
      return;
    }

    char fetchError[96];
    const bool fetchedPrice = fetchCoinGeckoInTask(submitted, validatedPrice, fetchError, sizeof(fetchError));
    char successMessage[256];
    if (fetchedPrice) {
      saveCachedBtcPriceForCurrency(submitted.currencyCode, validatedPrice, now);
    } else if (!hasCachedPrice) {
      snprintf(
        successMessage,
        sizeof(successMessage),
        "Wi-Fi: OK\nPRICE API: failed\n%s\nSettings can still be saved. The clock will retry BTC price on refresh and show N/A if no price is available.%s",
        fetchError,
        sourceChangedNote
      );
      portalSendJson(true, successMessage);
      return;
    }

    if (fetchedPrice) {
      snprintf(
        successMessage,
        sizeof(successMessage),
        "Wi-Fi: OK\nPRICE API: OK\n- BTC price: %.2f %s%s",
        validatedPrice,
        currencyCodeLabel(submitted.currencyCode),
        sourceChangedNote
      );
      portalSendJson(true, successMessage);
      return;
    }

    snprintf(
      successMessage,
      sizeof(successMessage),
      "Wi-Fi: OK\nPRICE API: failed\n%s\n- BTC price: %.2f %s (cached)\nSettings can still be saved. The clock will retry BTC price on refresh.%s",
      fetchError,
      validatedPrice,
      currencyCodeLabel(submitted.currencyCode),
      sourceChangedNote
    );
    portalSendJson(true, successMessage);
    return;
  }

  gotPrice = false;
  gotBalance = false;
  priceValueBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';
  mqttValidationMode = true;
  safeCopyCString(mqttValidationPriceTopic, sizeof(mqttValidationPriceTopic), submitted.topicPriceValue);
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
  const bool priceValid = gotPrice && parseNonNegativeFloatStrict(priceValueBuf, validatedPrice, false);
  const bool balanceValid = gotBalance && parseNonNegativeFloatStrict(balanceBtcBuf, validatedBalance, true);

  char priceLine[48];
  char amountValue[24];
  char amountLine[48];
  if (priceValid) {
    snprintf(priceLine, sizeof(priceLine), "- BTC price: %.2f %s", validatedPrice, currencyCodeLabel(submitted.currencyCode));
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
  if (!fetchGitHubInTask(releaseInfo, fetchError, sizeof(fetchError))) {
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

static bool isPortalClosing() {
  return portalExitAction != PORTAL_EXIT_ACTION_NONE;
}

static void handlePortalGenerate204() {
  if (!isPortalClosing()) {
    handlePortalRedirect();
    return;
  }

  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(204, "text/plain", "");
}

static void handlePortalHotspotDetect() {
  if (!isPortalClosing()) {
    handlePortalRoot();
    return;
  }

  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Pragma", "no-cache");
  portalServer.send(
    200,
    "text/html; charset=utf-8",
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"
  );
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
  portalServer.on("/generate_204", HTTP_GET, handlePortalGenerate204);
  portalServer.on("/redirect", HTTP_GET, handlePortalRedirect);
  portalServer.on("/hotspot-detect.html", HTTP_GET, handlePortalHotspotDetect);
  portalServer.on("/connecttest.txt", HTTP_GET, handlePortalRoot);
  portalServer.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
  portalServer.onNotFound(handlePortalRedirect);
}

static bool startConfigurationPortal() {
  portalSaveRequested = false;
  portalExitAction = PORTAL_EXIT_ACTION_NONE;
  portalExitScreen = APP_SCREEN_MAIN;
  resetPortalFirmwareUploadState();
  portalUnlocked = !hasSetupPinConfigured(deviceConfig);
  portalBootUnixBase = (lastKnownUnixTime >= 1700000000) ? lastKnownUnixTime : buildTimestamp();
  portalUnlockedAtMs = portalUnlocked ? millis() : 0;
  savePortalDiagnostics("start-request", "preparing setup access point", deviceConfig.configured);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(150);
  const bool modeOk = WiFi.mode(WIFI_AP_STA);
  buildPortalCredentials();
  const bool apConfigOk = WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_GATEWAY, CONFIG_AP_SUBNET);
  if (!modeOk || !apConfigOk) {
    savePortalDiagnostics(
      "start-warning",
      !modeOk ? "WiFi.mode(WIFI_AP_STA) returned false" : "WiFi.softAPConfig returned false",
      deviceConfig.configured
    );
  }
  if (!WiFi.softAP(portalApSsid, portalApPassword)) {
    savePortalDiagnostics("start-retry", "WiFi.softAP returned false; retrying after WiFi reset", deviceConfig.configured);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(300);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_GATEWAY, CONFIG_AP_SUBNET);
    if (!WiFi.softAP(portalApSsid, portalApPassword)) {
      savePortalDiagnostics("start-failed", "WiFi.softAP returned false after retry", deviceConfig.configured);
      return false;
    }
  }

  portalDnsServer.start(CONFIG_DNS_PORT, "*", CONFIG_AP_IP);
  setupPortalRoutes();
  portalServer.begin();
  savePortalDiagnostics("start-ok", "setup access point started", deviceConfig.configured);
  drawSetupPortalReadyScreen();
  return true;
}

static void stopConfigurationPortal() {
  portalServer.stop();
  portalDnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void factoryResetAndShowWelcome() {
  clearSavedDeviceConfig();
  clearWealthHistoryWithReason("Cleared by factory reset");
  clearBatteryLog();
  clearCachedBtcData();
  clearBatteryLogInMemory(batteryLog);
  clearWealthHistoryInMemory(wealthHistory);
  applyDefaultDeviceConfig(deviceConfig);
  rtcFactoryResetPending = true;
  drawSetupPortalResetScreen();
  delay(1200);
  const uint32_t releaseWaitStartedMs = millis();
  while (digitalRead(PIN_SETUP_BUTTON) == LOW && (millis() - releaseWaitStartedMs) < 5000) {
    delay(BUTTON_POLL_DELAY_MS);
  }
  markWelcomeShown();
  drawWelcomeScreen();
  delay(500);
  goToWelcomeSleep();
}

static void runConfigurationPortal() {
  if (!startConfigurationPortal()) {
    savePortalDiagnostics("error-screen", "showing setup error because AP startup failed", deviceConfig.configured);
    drawSetupPortalErrorScreen("AP start failed");
    delay(10000);
    ESP.restart();
  }

  bool factoryResetHoldActive = false;
  uint32_t factoryResetHoldStartedMs = 0;
  while (!portalSaveRequested) {
    portalServer.handleClient();
    if (digitalRead(PIN_FUNCTION_BUTTON) == LOW) {
      delay(BUTTON_POLL_DELAY_MS);
      if (digitalRead(PIN_FUNCTION_BUTTON) == LOW) {
        if (deviceConfig.configured) {
          portalExitScreen = APP_SCREEN_FREEDOM_CHANGE;
          portalExitAction = PORTAL_EXIT_ACTION_APP_SCREEN;
          portalSaveRequested = true;
        }
        const uint32_t releaseWaitStartedMs = millis();
        while (digitalRead(PIN_FUNCTION_BUTTON) == LOW && (millis() - releaseWaitStartedMs) < 1000) {
          portalServer.handleClient();
          delay(BUTTON_POLL_DELAY_MS);
        }
        if (portalExitAction == PORTAL_EXIT_ACTION_APP_SCREEN) {
          break;
        }
      }
    }
    if (digitalRead(PIN_SETUP_BUTTON) == LOW) {
      if (!factoryResetHoldActive) {
        factoryResetHoldActive = true;
        factoryResetHoldStartedMs = millis();
      } else if ((millis() - factoryResetHoldStartedMs) >= FACTORY_RESET_HOLD_MS) {
        portalExitAction = PORTAL_EXIT_ACTION_FACTORY_RESET;
        portalSaveRequested = true;
      }
    } else {
      factoryResetHoldActive = false;
    }
    delay(CONFIG_PORTAL_DELAY_MS);
  }

  if (portalExitAction != PORTAL_EXIT_ACTION_FACTORY_RESET
      && portalExitAction != PORTAL_EXIT_ACTION_APP_SCREEN) {
    const uint32_t exitGraceStartedMs = millis();
    while ((millis() - exitGraceStartedMs) < CONFIG_PORTAL_EXIT_GRACE_MS) {
      portalServer.handleClient();
      delay(CONFIG_PORTAL_DELAY_MS);
    }
  }

  stopConfigurationPortal();
  checkpointPortalUnixTime();
  savePortalDiagnostics("exit", portalExitActionText(portalExitAction), deviceConfig.configured);
  if (portalExitAction == PORTAL_EXIT_ACTION_APP_SCREEN) {
    return;
  } else if (portalExitAction == PORTAL_EXIT_ACTION_FACTORY_RESET) {
    factoryResetAndShowWelcome();
  } else if (portalExitAction == PORTAL_EXIT_ACTION_FIRMWARE_UPDATE) {
    drawSetupPortalFirmwareUpdatedScreen();
  } else {
    drawSetupPortalSavedScreen();
  }
  delay(portalExitAction == PORTAL_EXIT_ACTION_FIRMWARE_UPDATE
    ? CONFIG_PORTAL_FIRMWARE_RESTART_DELAY_MS
    : CONFIG_PORTAL_RESTART_DELAY_MS);
  ESP.restart();
}

// ============================================================
