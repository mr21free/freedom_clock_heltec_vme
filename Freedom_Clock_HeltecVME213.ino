#include <WiFi.h>
#include <PubSubClient.h>
#include <heltec-eink-modules.h>
#include "esp_sleep.h"
#include <cstring>
#include <cstdlib>
#include "secrets.h"

// ============================================================
// Freedom Clock - Config
// ============================================================

// Base monthly expense in USD (today)
static constexpr float MONTHLY_EXP_USD = 10000.0f;

// Annual inflation rate (0.02 = 2%)
static constexpr float INFLATION_ANNUAL = 0.02f;

// Deep sleep interval
static constexpr uint64_t SLEEP_MINUTES = 1440;
static constexpr uint64_t MICROSECONDS_PER_MINUTE = 60ULL * 1000000ULL;

// MQTT topics
static const char* TOPIC_PRICE_USD   = "home/bitcoin/price/usd";
static const char* TOPIC_BALANCE_BTC = "home/bitcoin/wallets/total_btc";

// ============================================================
// Hardware pins (Vision Master E213)
// ============================================================
static constexpr int PIN_EINK_POWER = 45; // E-ink VCC enable
static constexpr int PIN_BAT_ADC    = 7;  // VBAT_Read
static constexpr int PIN_ADC_CTRL   = 46; // ADC_Ctrl gate

// Battery ADC constants
static constexpr float ADC_MAX    = 4095.0f;
static constexpr float ADC_REF_V  = 3.3f;
static constexpr float VBAT_SCALE = 4.9f;

static constexpr size_t VALUE_BUFFER_SIZE = 16;
static constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS = 5000;
static constexpr uint16_t MQTT_WAIT_FOR_MESSAGES_MS = 4000;
static constexpr uint16_t MQTT_LOOP_DELAY_MS = 10;
static constexpr uint16_t BATTERY_ADC_SETTLE_DELAY_MS = 5;
static constexpr uint16_t EINK_POWER_UP_DELAY_MS = 100;
static constexpr uint16_t WIFI_POLL_DELAY_MS = 250;
static constexpr uint16_t MQTT_RETRY_DELAY_MS = 500;
static constexpr char MQTT_CLIENT_ID[] = "VM_E213_FreedomClock";

// ============================================================
// RTC persisted data (survives deep sleep)
// ============================================================
RTC_DATA_ATTR char lastPriceUsd[VALUE_BUFFER_SIZE]   = "--";
RTC_DATA_ATTR char lastBalanceBtc[VALUE_BUFFER_SIZE] = "--";

// ============================================================
// Globals
// ============================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
EInkDisplay_VisionMasterE213 display;

// Incoming MQTT values stored in fixed buffers (avoid String heap churn)
static volatile bool gotPrice   = false;
static volatile bool gotBalance = false;

static char priceUsdBuf[VALUE_BUFFER_SIZE]   = "";
static char balanceBtcBuf[VALUE_BUFFER_SIZE] = "";

// ============================================================
// Utilities
// ============================================================

static float clampNonNegative(float v) {
  return (v < 0.0f) ? 0.0f : v;
}

static float parseFloatSafe(const char* s) {
  if (!s || !*s) return 0.0f;
  char* end = nullptr;
  float v = strtof(s, &end);
  if (end == s) return 0.0f; // no parse
  return clampNonNegative(v);
}

static void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  if (!dst || dstSize == 0) return;
  size_t n = (srcLen < (dstSize - 1)) ? srcLen : (dstSize - 1);
  if (src && srcLen > 0) memcpy(dst, src, n);
  dst[n] = '\0';
}

static int batteryPercentFromVoltage(float v) {
  static constexpr int   NUM_POINTS = 8;
  static constexpr float voltTable[NUM_POINTS] = { 3.20f, 3.30f, 3.60f, 3.75f, 3.85f, 3.95f, 4.05f, 4.15f };
  static constexpr int   socTable[NUM_POINTS]  = { 0,     2,     25,    50,    70,    90,    97,    100 };

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

  float v_adc  = (raw / ADC_MAX) * ADC_REF_V;
  float v_batt = v_adc * VBAT_SCALE;
  return v_batt;
}

// ============================================================
// Inflation-aware longevity calculation
// ============================================================

static void computeLongevityWithInflation(
  float usdWealth,
  float monthlyExpenseToday,
  float inflationAnnual,
  int &outYears,
  int &outMonths,
  int &outWeeks
) {
  outYears = 0;
  outMonths = 0;
  outWeeks = 0;

  if (usdWealth <= 0.0f || monthlyExpenseToday <= 0.0f) return;
  if (inflationAnnual < 0.0f) inflationAnnual = 0.0f;

  // Convert annual inflation to daily multiplier (compounded daily)
  const float dailyMul = powf(1.0f + inflationAnnual, 1.0f / 365.0f);

  // Approximate daily expense from monthly
  const float dailyExpenseBase = monthlyExpenseToday / 30.0f;

  float remaining = usdWealth;
  float dailyExpense = dailyExpenseBase;

  int days = 0;
  static constexpr int MAX_DAYS = 365 * 200; // 200 years cap

  while (days < MAX_DAYS) {
    if (remaining < dailyExpense) break;
    remaining -= dailyExpense;
    dailyExpense *= dailyMul;
    days++;
  }

  // Convert days -> years/months/weeks (simple approximation)
  outYears = days / 365;
  int remDays = days % 365;

  outMonths = remDays / 30;
  remDays = remDays % 30;
  outWeeks = remDays / 7;
}

// ============================================================
// Display Battery Icon 
// ============================================================

static void drawBatteryIcon(int x, int y, int pct) {
  // Clamp percent
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Icon size
  const int bodyW = 40;
  const int bodyH = 18;
  const int tipW  = 3;
  const int tipH  = 9;

  // Inner padding
  const int pad = 3;

  // Battery outline
  display.drawRoundRect(x, y, bodyW, bodyH, 3, BLACK);

  // Battery tip
  display.fillRect(x + bodyW, y + (bodyH - tipH) / 2, tipW, tipH, BLACK);

  // Inner area
  const int innerX = x + pad;
  const int innerY = y + pad;
  const int innerW = bodyW - (pad * 2);
  const int innerH = bodyH - (pad * 2);

  // Clear inside first
  display.fillRect(innerX, innerY, innerW, innerH, WHITE);

  // Fill based on battery %
  int fillW = (innerW * pct) / 100;
  if (fillW > 0) {
    display.fillRect(innerX, innerY, fillW, innerH, BLACK);
  }
}

// ============================================================
// Display (NO BTC balance / NO USD wealth shown)
// ============================================================

static void drawFreedomClock(
  int years,
  int months,
  int weeks,
  int pct
) {
  display.clear();
  display.setRotation(1);
  display.setTextColor(BLACK);
  
  display.setTextSize(1);
  display.setCursor(10, 8);
  display.println("FREEDOM CLOCK");

  // years
  display.setTextSize(4);
  display.setCursor(10, 42);
  if (years < 10) display.print('0');
  display.print(years);
  display.setTextSize(3);
  int x = display.getCursorX();
  display.setCursor(x, 49);
  display.print("Y");

  // months
  display.setCursor(95, 42);
  display.setTextSize(4);
  if (months < 10) display.print('0');
  display.print(months);  
  display.setTextSize(3);
  x = display.getCursorX();
  display.setCursor(x, 49);
  display.print("M");

  // weeks
  display.setCursor(185, 42);
  display.setTextSize(4);
  display.print(weeks);
  display.setTextSize(3);
  x = display.getCursorX();
  display.setCursor(x, 49);
  display.print("W");

  // Battery icon and percentage
  drawBatteryIcon(10, 92, pct);

  display.setCursor(58, 94);
  display.setTextSize(2);
  display.print(pct);
  display.print("%");

  display.update();
}

// ============================================================
// MQTT callback
// ============================================================

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!topic || !payload || length == 0) return;

  if (strcmp(topic, TOPIC_PRICE_USD) == 0) {
    safeCopy(priceUsdBuf, sizeof(priceUsdBuf), (const char*)payload, length);
    gotPrice = true;
    return;
  }

  if (strcmp(topic, TOPIC_BALANCE_BTC) == 0) {
    safeCopy(balanceBtcBuf, sizeof(balanceBtcBuf), (const char*)payload, length);
    gotBalance = true;
    return;
  }
}

// ============================================================
// WiFi + MQTT
// ============================================================

static bool connectWiFi(uint16_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(WIFI_POLL_DELAY_MS);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool connectMQTT(uint16_t timeout_ms = MQTT_CONNECT_TIMEOUT_MS) {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  uint32_t start = millis();
  while (!mqttClient.connected() && (millis() - start) < timeout_ms) {
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) break;
    delay(MQTT_RETRY_DELAY_MS);
  }
  if (!mqttClient.connected()) return false;

  mqttClient.subscribe(TOPIC_PRICE_USD);
  mqttClient.subscribe(TOPIC_BALANCE_BTC);
  return true;
}

// ============================================================
// Deep sleep
// ============================================================

static void goToSleep() {
  digitalWrite(PIN_EINK_POWER, LOW);
  uint64_t sleep_us = SLEEP_MINUTES * MICROSECONDS_PER_MINUTE;
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_deep_sleep_start();
}

// ============================================================
// Setup
// ============================================================

void setup() {
  // Power e-ink
  pinMode(PIN_EINK_POWER, OUTPUT);
  digitalWrite(PIN_EINK_POWER, HIGH);
  delay(EINK_POWER_UP_DELAY_MS);

  display.begin();

  // Battery
  float vbat = readBatteryVoltage();
  int pct = batteryPercentFromVoltage(vbat);

  // Network
  bool wifiOK = connectWiFi();

  // MQTT
  bool mqttOK = false;
  gotPrice = false;
  gotBalance = false;
  priceUsdBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';

  if (wifiOK) mqttOK = connectMQTT();

  if (mqttOK) {
    uint32_t start = millis();
    while (millis() - start < MQTT_WAIT_FOR_MESSAGES_MS) {
      mqttClient.loop();
      delay(MQTT_LOOP_DELAY_MS);
      if (gotPrice && gotBalance) break;
    }
  }

  // Fallbacks from RTC if needed
  const char* priceStr   = gotPrice   ? priceUsdBuf   : lastPriceUsd;
  const char* balanceStr = gotBalance ? balanceBtcBuf : lastBalanceBtc;

  float priceUsd   = parseFloatSafe(priceStr);
  float balanceBtc = parseFloatSafe(balanceStr);

  // Sell BTC today -> USD wealth
  float usdWealth = balanceBtc * priceUsd;

  // Longevity with inflation
  int years = 0, months = 0, weeks = 0;
  computeLongevityWithInflation(usdWealth, MONTHLY_EXP_USD, INFLATION_ANNUAL, years, months, weeks);

  drawFreedomClock(years, months, weeks, pct);

  // Persist last known values
  if (gotPrice) {
    safeCopy(lastPriceUsd, sizeof(lastPriceUsd), priceUsdBuf, strlen(priceUsdBuf));
  }
  if (gotBalance) {
    safeCopy(lastBalanceBtc, sizeof(lastBalanceBtc), balanceBtcBuf, strlen(balanceBtcBuf));
  }

  // Clean shutdown
  if (mqttClient.connected()) mqttClient.disconnect();
  WiFi.disconnect(true);

  goToSleep();
}

void loop() {
  // not used
}
