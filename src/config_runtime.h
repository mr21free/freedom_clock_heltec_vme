#pragma once

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
  uint32_t hash = 2166136261UL;
  for (uint8_t i = 0; i < 6; i++) {
    hash ^= (uint8_t)((chipId >> (i * 8)) & 0xFF);
    hash *= 16777619UL;
  }

  static constexpr uint32_t BASE36_6 = 2176782336UL; // 36^6
  uint32_t value = hash % BASE36_6;
  char id[7];
  id[6] = '\0';
  for (int i = 5; i >= 0; i--) {
    const uint8_t digit = value % 36;
    id[i] = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
    value /= 36;
  }
  safeCopyCString(dst, dstSize, id);
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

static void normalizeDeviceConfig(DeviceConfig& cfg) {
  if (!hasText(cfg.ownerName)) {
    safeCopyCString(cfg.ownerName, sizeof(cfg.ownerName), DEFAULT_OWNER_NAME);
  }
  uppercaseAsciiInPlace(cfg.ownerName);
  truncateCString(cfg.ownerName, OWNER_NAME_MAX_DISPLAY_CHARS);

  cfg.birthYear = clampInt(cfg.birthYear, 1900, 2100);
  cfg.lifeExpectancyYears = clampInt(cfg.lifeExpectancyYears, 1, 130);
  cfg.currencyCode = sanitizeCurrencyCode(cfg.currencyCode);

  if (!(cfg.monthlyExpenseValue >= 0.0f)) cfg.monthlyExpenseValue = DEFAULT_MONTHLY_EXPENSE_VALUE;
  if (!(cfg.monthlyIncomeValue >= 0.0f)) cfg.monthlyIncomeValue = DEFAULT_MONTHLY_INCOME_VALUE;
  if (!(cfg.inflationAnnual >= 0.0f)) cfg.inflationAnnual = DEFAULT_INFLATION_ANNUAL;
  if (!(cfg.incomeGrowthAnnual >= 0.0f)) cfg.incomeGrowthAnnual = DEFAULT_INCOME_GROWTH_ANNUAL;
  if (!(cfg.wealthGrowthAnnual > -1.0f)) cfg.wealthGrowthAnnual = DEFAULT_WEALTH_GROWTH_ANNUAL;
  if (!(cfg.defaultWealthValue >= 0.0f)) cfg.defaultWealthValue = DEFAULT_WEALTH_VALUE;
  if (!(cfg.manualBtcAmount >= 0.0f)) cfg.manualBtcAmount = DEFAULT_MANUAL_BTC_AMOUNT;
  if (!(cfg.borrowFeeAnnual > -1.0f)) cfg.borrowFeeAnnual = DEFAULT_BORROW_FEE_ANNUAL;

  cfg.monthlyExpenseValue = clampNonNegative(cfg.monthlyExpenseValue);
  cfg.monthlyIncomeValue = clampNonNegative(cfg.monthlyIncomeValue);
  cfg.inflationAnnual = clampNonNegative(cfg.inflationAnnual);
  cfg.incomeGrowthAnnual = clampNonNegative(cfg.incomeGrowthAnnual);
  cfg.defaultWealthValue = clampNonNegative(cfg.defaultWealthValue);
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
  if (cfg.dailyWakeHour > 23) cfg.dailyWakeHour = 7;
  if (cfg.dailyWakeMinute > 59) cfg.dailyWakeMinute = 0;
  if (cfg.dailyWakeTimeZone >= (sizeof(TIME_ZONE_OPTIONS) / sizeof(TIME_ZONE_OPTIONS[0]))) {
    cfg.dailyWakeTimeZone = DEFAULT_TIME_ZONE_INDEX;
  }
  if (cfg.timeDisplayFormat > 1) cfg.timeDisplayFormat = 0;
}

static void applyDefaultDeviceConfig(DeviceConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.configured = false;
  safeCopyCString(cfg.ownerName, sizeof(cfg.ownerName), DEFAULT_OWNER_NAME);
  cfg.birthYear = DEFAULT_OWNER_BIRTH_YEAR;
  cfg.lifeExpectancyYears = DEFAULT_OWNER_LIFE_EXPECTANCY_YEARS;
  cfg.monthlyExpenseValue = DEFAULT_MONTHLY_EXPENSE_VALUE;
  cfg.monthlyIncomeValue = DEFAULT_MONTHLY_INCOME_VALUE;
  cfg.inflationAnnual = DEFAULT_INFLATION_ANNUAL;
  cfg.incomeGrowthAnnual = DEFAULT_INCOME_GROWTH_ANNUAL;
  cfg.wealthGrowthAnnual = DEFAULT_WEALTH_GROWTH_ANNUAL;
  cfg.defaultWealthValue = DEFAULT_WEALTH_VALUE;
  cfg.manualBtcAmount = DEFAULT_MANUAL_BTC_AMOUNT;
  safeCopyCString(cfg.manualBtcAmountText, sizeof(cfg.manualBtcAmountText), DEFAULT_MANUAL_BTC_AMOUNT_TEXT);
  cfg.borrowFeeAnnual = DEFAULT_BORROW_FEE_ANNUAL;
  cfg.assetMode = (uint8_t)DEFAULT_ASSET_MODE;
  cfg.portfolioUseMode = (uint8_t)DEFAULT_PORTFOLIO_USE_MODE;
  cfg.currencyCode = (uint8_t)DEFAULT_CURRENCY_CODE;
  cfg.displayThemeMode = (uint8_t)DEFAULT_DISPLAY_THEME_MODE;
  cfg.refreshIntervalMinutes = DEFAULT_REFRESH_INTERVAL_MINUTES;
  cfg.setupPinEnabled = false;
  cfg.mqttPort = DEFAULT_MQTT_PORT;
  cfg.autoUpdateEnabled = false;
  cfg.dailyWakeHour = 7;
  cfg.dailyWakeMinute = 0;
  cfg.dailyWakeTimeZone = DEFAULT_TIME_ZONE_INDEX;
  cfg.quoteOfDayEnabled = false;
  cfg.timeDisplayFormat = 0;
  cfg.showBatteryPercent = false;
  cfg.showWealthChangeScreen = true;
  cfg.showSettingsScreen = true;
  safeCopyCString(cfg.mqttServer, sizeof(cfg.mqttServer), DEFAULT_MQTT_SERVER);
  safeCopyCString(cfg.mqttUser, sizeof(cfg.mqttUser), DEFAULT_MQTT_USER);
  safeCopyCString(cfg.mqttPass, sizeof(cfg.mqttPass), DEFAULT_MQTT_PASS);
  safeCopyCString(cfg.topicPriceValue, sizeof(cfg.topicPriceValue), DEFAULT_TOPIC_PRICE_VALUE);
  safeCopyCString(cfg.topicBalanceBtc, sizeof(cfg.topicBalanceBtc), DEFAULT_TOPIC_BALANCE_BTC);
#if HAS_SECRETS && USE_SECRETS_BOOTSTRAP
  safeCopyCString(cfg.wifiSsid, sizeof(cfg.wifiSsid), FREEDOM_WIFI_SSID);
  safeCopyCString(cfg.wifiPass, sizeof(cfg.wifiPass), FREEDOM_WIFI_PASS);
  safeCopyCString(cfg.mqttServer, sizeof(cfg.mqttServer), FREEDOM_MQTT_SERVER);
  cfg.mqttPort = FREEDOM_MQTT_PORT;
  safeCopyCString(cfg.mqttUser, sizeof(cfg.mqttUser), FREEDOM_MQTT_USER);
  safeCopyCString(cfg.mqttPass, sizeof(cfg.mqttPass), FREEDOM_MQTT_PASS);
#endif

  normalizeDeviceConfig(cfg);
}

static bool describeDeviceConfigComplete(const DeviceConfig& cfg, char* reason, size_t reasonSize) {
  auto setReason = [&](const char* value) {
    safeCopyCString(reason, reasonSize, value);
    return false;
  };

  if (!cfg.configured) return setReason("cfg_ok false");
  if (!hasText(cfg.ownerName)) return setReason("missing owner");
  if (cfg.birthYear < 1900 || cfg.birthYear > 2100) return setReason("invalid birth year");
  if (cfg.lifeExpectancyYears <= 0) return setReason("invalid life expectancy");
  if (cfg.monthlyExpenseValue <= 0.0f) return setReason("invalid monthly expense");

  AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  if (isMqttBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) return setReason("missing Wi-Fi SSID for MQTT mode");
    if (!hasText(cfg.mqttServer)) return setReason("missing MQTT server");
    if (cfg.mqttPort == 0) return setReason("invalid MQTT port");
    if (!hasText(cfg.topicPriceValue)) return setReason("missing MQTT price topic");
    if (!hasText(cfg.topicBalanceBtc)) return setReason("missing MQTT balance topic");
  } else if (isManualBtcAssetMode(assetMode)) {
    if (!hasText(cfg.wifiSsid)) return setReason("missing Wi-Fi SSID for static BTC mode");
    if (!(cfg.manualBtcAmount > 0.0f)) return setReason("invalid BTC amount");
  }

  safeCopyCString(reason, reasonSize, "ok");
  return true;
}

static bool isDeviceConfigComplete(const DeviceConfig& cfg) {
  char reason[64];
  return describeDeviceConfigComplete(cfg, reason, sizeof(reason));
}

static bool loadDeviceConfig(DeviceConfig& cfg) {
  applyDefaultDeviceConfig(cfg);
  safeCopyCString(lastConfigLoadStatus, sizeof(lastConfigLoadStatus), "load started");
  lastConfigStoredVersion = 0;
  lastConfigStoredOkFlag = false;

  if (!preferences.begin(CONFIG_NAMESPACE, true)) {
    safeCopyCString(lastConfigLoadStatus, sizeof(lastConfigLoadStatus), "NVS begin failed");
    return false;
  }

  uint32_t storedVersion = preferences.getUInt("cfg_ver", 0);
  lastConfigStoredVersion = storedVersion;
  if (storedVersion != CONFIG_VERSION) {
    preferences.end();
    snprintf(
      lastConfigLoadStatus,
      sizeof(lastConfigLoadStatus),
      "version mismatch stored=%lu expected=%lu",
      (unsigned long)storedVersion,
      (unsigned long)CONFIG_VERSION
    );
    return false;
  }

  cfg.configured = preferences.getBool("cfg_ok", false);
  lastConfigStoredOkFlag = cfg.configured;
  preferences.getString("owner", cfg.ownerName, sizeof(cfg.ownerName));
  cfg.birthYear = (int)preferences.getUInt("birth", (uint32_t)cfg.birthYear);
  cfg.lifeExpectancyYears = (int)preferences.getUInt("lifeexp", (uint32_t)cfg.lifeExpectancyYears);
  cfg.monthlyExpenseValue = preferences.getFloat("monthval", cfg.monthlyExpenseValue);
  cfg.monthlyIncomeValue = preferences.getFloat("incomeval", cfg.monthlyIncomeValue);
  cfg.inflationAnnual = preferences.getFloat("infl", cfg.inflationAnnual);
  cfg.incomeGrowthAnnual = preferences.getFloat("incgrow", cfg.incomeGrowthAnnual);
  cfg.wealthGrowthAnnual = preferences.getFloat("growth", cfg.wealthGrowthAnnual);
  cfg.defaultWealthValue = preferences.getFloat("wealth", cfg.defaultWealthValue);
  cfg.manualBtcAmount = preferences.getFloat("manbtc", cfg.manualBtcAmount);
  if (preferences.getString("manbtctxt", cfg.manualBtcAmountText, sizeof(cfg.manualBtcAmountText)) == 0) {
    cfg.manualBtcAmountText[0] = '\0';
  }
  cfg.borrowFeeAnnual = preferences.getFloat("borrowfee", cfg.borrowFeeAnnual);
  cfg.assetMode = (uint8_t)preferences.getUInt("asset", cfg.assetMode);
  cfg.portfolioUseMode = (uint8_t)preferences.getUInt("usemode", cfg.portfolioUseMode);
  cfg.currencyCode = (uint8_t)preferences.getUInt("currency", cfg.currencyCode);
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
  preferences.getString("topicprice", cfg.topicPriceValue, sizeof(cfg.topicPriceValue));
  preferences.getString("topicbal", cfg.topicBalanceBtc, sizeof(cfg.topicBalanceBtc));
  cfg.autoUpdateEnabled = preferences.getBool("auto_upd", cfg.autoUpdateEnabled);
  cfg.dailyWakeHour = (uint8_t)preferences.getUInt("wake_hr", cfg.dailyWakeHour);
  cfg.dailyWakeMinute = (uint8_t)preferences.getUInt("wake_min", cfg.dailyWakeMinute);
  cfg.dailyWakeTimeZone = (uint8_t)preferences.getUInt("wake_tz", cfg.dailyWakeTimeZone);
  cfg.quoteOfDayEnabled = preferences.getBool("quote_en", cfg.quoteOfDayEnabled);
  cfg.timeDisplayFormat = (uint8_t)preferences.getUInt("time_fmt", cfg.timeDisplayFormat);
  cfg.showBatteryPercent = preferences.getBool("batt_pct_show", cfg.showBatteryPercent);
  cfg.showWealthChangeScreen = preferences.getBool("show_wealth", cfg.showWealthChangeScreen);
  cfg.showSettingsScreen = preferences.getBool("show_settings", cfg.showSettingsScreen);
  setupPinFailedAttempts = preferences.getUInt("pin_fail", setupPinFailedAttempts);
  setupPinLockedUntil = (time_t)preferences.getUInt("pin_lock", (uint32_t)setupPinLockedUntil);
  preferences.end();

  normalizeDeviceConfig(cfg);
  char reason[96];
  const bool complete = describeDeviceConfigComplete(cfg, reason, sizeof(reason));
  if (complete) {
    safeCopyCString(lastConfigLoadStatus, sizeof(lastConfigLoadStatus), "ok");
  } else {
    snprintf(lastConfigLoadStatus, sizeof(lastConfigLoadStatus), "incomplete: %s", reason);
  }
  return complete;
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
  preferences.putFloat("monthval", cfg.monthlyExpenseValue);
  preferences.putFloat("incomeval", cfg.monthlyIncomeValue);
  preferences.putFloat("infl", cfg.inflationAnnual);
  preferences.putFloat("incgrow", cfg.incomeGrowthAnnual);
  preferences.putFloat("growth", cfg.wealthGrowthAnnual);
  preferences.putFloat("wealth", cfg.defaultWealthValue);
  preferences.putFloat("manbtc", cfg.manualBtcAmount);
  preferences.putString("manbtctxt", cfg.manualBtcAmountText);
  preferences.putFloat("borrowfee", cfg.borrowFeeAnnual);
  preferences.putUInt("asset", (uint32_t)cfg.assetMode);
  preferences.putUInt("usemode", (uint32_t)cfg.portfolioUseMode);
  preferences.putUInt("currency", (uint32_t)cfg.currencyCode);
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
  preferences.putString("topicprice", cfg.topicPriceValue);
  preferences.putString("topicbal", cfg.topicBalanceBtc);
  preferences.putBool("auto_upd", cfg.autoUpdateEnabled);
  preferences.putUInt("wake_hr", (uint32_t)cfg.dailyWakeHour);
  preferences.putUInt("wake_min", (uint32_t)cfg.dailyWakeMinute);
  preferences.putUInt("wake_tz", (uint32_t)cfg.dailyWakeTimeZone);
  preferences.putBool("quote_en", cfg.quoteOfDayEnabled);
  preferences.putUInt("time_fmt", (uint32_t)cfg.timeDisplayFormat);
  preferences.putBool("batt_pct_show", cfg.showBatteryPercent);
  preferences.putBool("show_wealth", cfg.showWealthChangeScreen);
  preferences.putBool("show_settings", cfg.showSettingsScreen);
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
