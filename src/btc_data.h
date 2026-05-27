#pragma once

// MQTT callback
// ============================================================

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!topic || !payload || length == 0) return;

  const char* expectedPriceTopic = mqttValidationMode ? mqttValidationPriceTopic : deviceConfig.topicPriceValue;
  const char* expectedBalanceTopic = mqttValidationMode ? mqttValidationBalanceTopic : deviceConfig.topicBalanceBtc;

  if (hasText(expectedPriceTopic) && strcmp(topic, expectedPriceTopic) == 0) {
    safeCopy(priceValueBuf, sizeof(priceValueBuf), (const char*)payload, length);
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

  lastWifiConnectStatus = WL_IDLE_STATUS;
  lastWifiConnectAttempts = 0;
  lastWifiConnectElapsedMs = 0;
  lastWifiConnectRssi = 0;
  lastWifiConnectKeptPortalAp = keepPortalAp;

  WiFi.persistent(false);
  WiFi.setSleep(false);

  static constexpr uint8_t WIFI_CONNECT_ATTEMPTS = 2;
  for (uint8_t attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; attempt++) {
    lastWifiConnectAttempts = attempt;
    const uint32_t start = millis();

    WiFi.mode(keepPortalAp ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect(false, false);
    delay(attempt == 1 ? 150 : 350);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
      delay(WIFI_POLL_DELAY_MS);
    }

    lastWifiConnectElapsedMs += millis() - start;
    lastWifiConnectStatus = (int)WiFi.status();
    if (WiFi.status() == WL_CONNECTED) {
      lastWifiConnectRssi = WiFi.RSSI();
      return true;
    }

    WiFi.disconnect(false, false);
    delay(250);
  }

  return false;
}

static bool parseJsonNumberForKey(const String& payload, const char* keyName, float& outPrice) {
  char key[12];
  snprintf(key, sizeof(key), "\"%s\"", keyName ? keyName : "usd");
  const int currencyKeyIndex = payload.indexOf(key);
  if (currencyKeyIndex < 0) return false;

  int colonIndex = payload.indexOf(':', currencyKeyIndex);
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
  outPrice = parsedValue;
  return true;
}

static bool parseCoinGeckoPrice(const String& payload, const char* currencyParam, float& outPrice) {
  return parseJsonNumberForKey(payload, currencyParam ? currencyParam : "usd", outPrice);
}

static bool parseMempoolPrice(const String& payload, uint8_t currencyCode, float& outPrice) {
  return parseJsonNumberForKey(payload, currencyCodeLabel(currencyCode), outPrice);
}

static bool fetchCoinGeckoBtcPrice(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize) {
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
  http.setConnectTimeout(PRICE_HTTP_TIMEOUT_MS);
  String priceUrl = COINGECKO_SIMPLE_PRICE_URL_BASE;
  priceUrl += currencyCodeParam(cfg.currencyCode);
  priceUrl += "&precision=2";
  if (!http.begin(secureClient, priceUrl)) {
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

  if (!parseCoinGeckoPrice(payload, currencyCodeParam(cfg.currencyCode), outPrice)) {
    snprintf(errorBuf, errorBufSize, "CoinGecko price response could not be parsed.");
    return false;
  }

  snprintf(priceValueBuf, sizeof(priceValueBuf), "%.2f", outPrice);
  gotPrice = true;
  errorBuf[0] = '\0';
  return true;
}

static bool fetchMempoolBtcPrice(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize) {
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
  http.setConnectTimeout(PRICE_HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, MEMPOOL_PRICE_URL)) {
    snprintf(errorBuf, errorBufSize, "Could not start mempool.space price request.");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "FreedomClock/2026");
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    snprintf(errorBuf, errorBufSize, "mempool.space request failed (%d).", httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (!parseMempoolPrice(payload, cfg.currencyCode, outPrice)) {
    snprintf(errorBuf, errorBufSize, "mempool.space price response could not be parsed.");
    return false;
  }

  snprintf(priceValueBuf, sizeof(priceValueBuf), "%.2f", outPrice);
  gotPrice = true;
  errorBuf[0] = '\0';
  return true;
}

static bool fetchOnlineBtcPrice(const DeviceConfig& cfg, float& outPrice, char* errorBuf, size_t errorBufSize) {
  char coinGeckoError[96] = "";
  if (fetchCoinGeckoBtcPrice(cfg, outPrice, coinGeckoError, sizeof(coinGeckoError))) {
    return true;
  }

  char mempoolError[96] = "";
  if (fetchMempoolBtcPrice(cfg, outPrice, mempoolError, sizeof(mempoolError))) {
    return true;
  }

  snprintf(
    errorBuf,
    errorBufSize,
    "CoinGecko: %s\nmempool.space: %s",
    coinGeckoError[0] ? coinGeckoError : "failed",
    mempoolError[0] ? mempoolError : "failed"
  );
  return false;
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

  if (hasText(cfg.topicPriceValue)) mqttClient.subscribe(cfg.topicPriceValue);
  if (hasText(cfg.topicBalanceBtc)) mqttClient.subscribe(cfg.topicBalanceBtc);
  return true;
}
