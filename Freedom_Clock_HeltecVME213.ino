#include <WiFi.h>
#include <PubSubClient.h>
#include <heltec-eink-modules.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "secrets.h"

// ============================================================
// Freedom Clock - Config
// ============================================================

// Base monthly expense in USD (today)
static constexpr float MONTHLY_EXP_USD = 10000.0f;

// Annual inflation rate (0.02 = 2%)
static constexpr float INFLATION_ANNUAL = 0.02f;

// Assumed annual wealth growth used by the top freedom-time model.
static constexpr float WEALTH_GROWTH_ANNUAL = 0.10f;

enum AssetMode {
  ASSET_MODE_BTC = 0,
  ASSET_MODE_WEALTH = 1
};

enum PortfolioUseMode {
  PORTFOLIO_USE_MODE_SELL = 0,
  PORTFOLIO_USE_MODE_BORROW = 1
};

// Choose whether the portfolio is sourced from BTC amount + BTC price,
// or from a directly configured USD net worth value.
static constexpr AssetMode PORTFOLIO_ASSET_MODE = ASSET_MODE_BTC;
static constexpr PortfolioUseMode PORTFOLIO_USE_MODE = PORTFOLIO_USE_MODE_SELL;
static constexpr float DEFAULT_WEALTH_USD = 1000000.0f;
static constexpr float BORROW_FEE_ANNUAL = 0.08f;

// Person profile shown on the display.
// Privacy-friendly version: only birth year is stored.
// Life calculations assume 1.1.<birth year>.
static constexpr char OWNER_NAME[] = "MIKE";
static constexpr int OWNER_BIRTH_YEAR = 1980;
static constexpr int OWNER_LIFE_EXPECTANCY_YEARS = 85;

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
static constexpr int PIN_USER_BUTTON = 21; // Custom side button

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
static constexpr uint16_t NTP_SYNC_TIMEOUT_MS = 10000;
static constexpr char MQTT_CLIENT_ID_PREFIX[] = "FreedomClock";
static constexpr char NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char NTP_SERVER_2[] = "time.nist.gov";
static constexpr long NTP_GMT_OFFSET_SECONDS = 0;
static constexpr int NTP_DAYLIGHT_OFFSET_SECONDS = 0;

// ============================================================
// RTC persisted data (survives deep sleep)
// ============================================================
RTC_DATA_ATTR char lastPriceUsd[VALUE_BUFFER_SIZE]   = "--";
RTC_DATA_ATTR char lastBalanceBtc[VALUE_BUFFER_SIZE] = "--";
RTC_DATA_ATTR time_t lastKnownUnixTime = 0;

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

struct LifeStats {
  int yearsLeft;
  int monthsLeft;
  int weeksLeftRemainder;
  int totalWeeks;
  int remainingWeeks;
  int remainingPercent;
};

// ============================================================
// Utilities
// ============================================================

static float clampNonNegative(float v) {
  return (v < 0.0f) ? 0.0f : v;
}

static int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static int estimateTextWidthSize1(const char* text) {
  if (!text) return 0;
  return (int)strlen(text) * 6; // Simple estimate for default size-1 font
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

static void buildMqttClientId(char* dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  uint64_t chipId = ESP.getEfuseMac();
  unsigned long long shortId = (unsigned long long)(chipId & 0xFFFFFFULL);
  snprintf(dst, dstSize, "%s-%06llX", MQTT_CLIENT_ID_PREFIX, shortId);
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
  tmValue.tm_hour = 12; // avoid DST edge cases while converting
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
  const char* dateStr = __DATE__; // "Mmm dd yyyy"
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

static time_t estimateCurrentTime(bool ntpSynced, esp_sleep_wakeup_cause_t wakeCause) {
  if (ntpSynced) {
    time_t now = time(nullptr);
    if (now >= 1700000000) return now;
  }

  if (lastKnownUnixTime >= 1700000000) {
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
      return lastKnownUnixTime + (time_t)(SLEEP_MINUTES * 60ULL);
    }
    return lastKnownUnixTime;
  }

  return buildTimestamp();
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

// ============================================================
// Inflation-aware longevity calculation
// ============================================================

static void computeLongevityWithInflation(
  float usdWealth,
  float monthlyExpenseToday,
  float inflationAnnual,
  float assetGrowthAnnual,
  PortfolioUseMode portfolioUseMode,
  float borrowFeeAnnual,
  bool &outHitCap,
  int &outYears,
  int &outMonths,
  int &outWeeks,
  float &outCoveredWeeks
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
    // Borrow mode is modeled as yearly refinancing: once per year the
    // collateral grows, existing debt rolls with the annual fee, and one full
    // year of spending is borrowed.
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

  // Sell mode remains a simple monthly model.
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

// ============================================================
// Display Battery Icon 
// ============================================================

static void drawBatteryIcon(int x, int y, int pct, int bodyW, int bodyH, int tipW, int tipH) {
  // Clamp percent
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Inner padding
  const int pad = (bodyW >= 28) ? 3 : 2;

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
  const char* ownerName,
  bool freedomHitCap,
  int freedomYears,
  int freedomMonths,
  int freedomWeeks,
  const LifeStats& lifeStats,
  bool coveredInfinite,
  int coveredPercent,
  int deviceBatteryPct
) {
  char freedomTitle[40];
  char lifeTitle[32];
  char percentText[8];
  char freedomTitleLong[48];
  char freedomTitleMedium[40];
  char freedomTitleShort[32];
  const char* safeOwnerName = ownerName ? ownerName : "YOUR";
  const size_t ownerNameLen = strlen(safeOwnerName);
  const int LEFT_X = 4;
  const int NUMBER_STEP_X = 62;
  const int MONTH_X = LEFT_X + NUMBER_STEP_X;
  const int WEEK_X = LEFT_X + (NUMBER_STEP_X * 2);
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
  const int titleMaxWidthWide = BATTERY_X - LEFT_X - 8;   // Short names may use the full top row
  const int titleMaxWidthNarrow = DIVIDER_X - LEFT_X - 4; // Longer names should stay inside the left section
  const int titleFitSlack = 18; // Slightly permissive to match the real panel better

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
  } else if (estimateTextWidthSize1("FREEDOM CLOCK") <= titleMaxWidthWide) {
    safeCopy(freedomTitle, sizeof(freedomTitle), "FREEDOM CLOCK", strlen("FREEDOM CLOCK"));
  } else {
    safeCopy(freedomTitle, sizeof(freedomTitle), "FREEDOM CLOCK", strlen("FREEDOM CLOCK"));
  }

  snprintf(lifeTitle, sizeof(lifeTitle), "EXPECTED LIFETIME LEFT");
  if (coveredInfinite) {
    safeCopy(percentText, sizeof(percentText), "INF", strlen("INF"));
  } else {
    snprintf(percentText, sizeof(percentText), "%d%%", clampInt(coveredPercent, 0, 999));
  }

  display.clear();
  display.setRotation(1);
  display.setTextColor(BLACK);

  display.setTextSize(1);
  display.setCursor(LEFT_X, 8);
  display.println(freedomTitle);

  drawBatteryIcon(BATTERY_X, 5, deviceBatteryPct, 22, 10, 2, 5);
  display.setCursor(226, 7);
  display.setTextSize(1);
  display.print(deviceBatteryPct);
  display.print("%");

  display.drawLine(DIVIDER_X, 12, DIVIDER_X, 116, BLACK);

  int x = 0;
  if (freedomHitCap) {
    display.setTextSize(3);
    display.setCursor(LEFT_X, TOP_NUMBER_Y);
    display.print("FOREVER");
  } else if (freedomYears > 99) {
    display.setTextSize(3);
    display.setCursor(LEFT_X, TOP_NUMBER_Y);
    display.print(freedomYears);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("Y");
  } else {
    display.setTextSize(3);
    display.setCursor(LEFT_X, TOP_NUMBER_Y);
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

  display.drawLine(LEFT_X, MID_LINE_Y, DIVIDER_X - 8, MID_LINE_Y, BLACK);

  display.setTextSize(1);
  display.setCursor(LEFT_X, LIFE_TITLE_Y);
  display.print(lifeTitle);

  display.setTextSize(3);
  display.setCursor(LEFT_X, BOTTOM_NUMBER_Y);
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
  const char* ownerName,
  AssetMode assetMode,
  PortfolioUseMode portfolioUseMode,
  float usdWealth,
  float balanceBtc,
  float priceUsd,
  float wealthGrowthAnnual,
  float borrowFeeAnnual,
  float inflationAnnual,
  float monthlyExpenseUsd,
  int birthYear,
  int lifeExpectancyYears,
  int deviceBatteryPct
) {
  (void)ownerName;
  char value[24];
  static constexpr int LABEL_X = 10;
  static constexpr int VALUE_X = 142;
  static constexpr int ROW_Y0 = 5;
  static constexpr int ROW_STEP = 14;

  display.clear();
  display.setRotation(1);
  display.setTextColor(BLACK);

  display.setTextSize(1);
  drawBatteryIcon(200, 5, deviceBatteryPct, 22, 10, 2, 5);
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
    snprintf(value, sizeof(value), "%.4f", balanceBtc);
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
  snprintf(value, sizeof(value), "%.1f%%", wealthGrowthAnnual * 100.0f);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 3));
  display.print("INFLATION:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 3));
  snprintf(value, sizeof(value), "%.1f%%", inflationAnnual * 100.0f);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 4));
  display.print("MONTHLY EXPENSES USD:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 4));
  snprintf(value, sizeof(value), "%.0f", monthlyExpenseUsd);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 5));
  display.print("BIRTH YEAR:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 5));
  snprintf(value, sizeof(value), "%d", birthYear);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 6));
  display.print("LIFE EXPECTANCY:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 6));
  snprintf(value, sizeof(value), "%d Y", lifeExpectancyYears);
  display.print(value);

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 7));
  display.print("WITHDRAWAL MODE:");
  display.setCursor(VALUE_X, ROW_Y0 + (ROW_STEP * 7));
  if (portfolioUseMode == PORTFOLIO_USE_MODE_BORROW) {
    snprintf(value, sizeof(value), "BORROW YEARLY (%.1f%%/Y)", borrowFeeAnnual * 100.0f);
  } else {
    snprintf(value, sizeof(value), "SELL MONTHLY");
  }
  display.print(value);

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
  char mqttClientId[32];
  buildMqttClientId(mqttClientId, sizeof(mqttClientId));

  uint32_t start = millis();
  while (!mqttClient.connected() && (millis() - start) < timeout_ms) {
    if (mqttClient.connect(mqttClientId, MQTT_USER, MQTT_PASS)) break;
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
  rtc_gpio_pullup_en((gpio_num_t)PIN_USER_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_USER_BUTTON);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BUTTON, 0); // Wake when button pulls low
  uint64_t sleep_us = SLEEP_MINUTES * MICROSECONDS_PER_MINUTE;
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_deep_sleep_start();
}

// ============================================================
// Setup
// ============================================================

void setup() {
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  // Power e-ink
  pinMode(PIN_EINK_POWER, OUTPUT);
  digitalWrite(PIN_EINK_POWER, HIGH);
  delay(EINK_POWER_UP_DELAY_MS);

  rtc_gpio_deinit((gpio_num_t)PIN_USER_BUTTON);
  pinMode(PIN_USER_BUTTON, INPUT_PULLUP);

  display.begin();

  // Battery
  float vbat = readBatteryVoltage();
  int pct = batteryPercentFromVoltage(vbat);

  // Network
  bool wifiOK = connectWiFi();
  bool ntpSynced = false;
  if (wifiOK) {
    ntpSynced = syncClockFromNtp();
  }

  // MQTT
  bool mqttOK = false;
  gotPrice = false;
  gotBalance = false;
  priceUsdBuf[0] = '\0';
  balanceBtcBuf[0] = '\0';

  bool needsMqtt = (PORTFOLIO_ASSET_MODE == ASSET_MODE_BTC);
  if (wifiOK && needsMqtt) mqttOK = connectMQTT();

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

  float usdWealth = (PORTFOLIO_ASSET_MODE == ASSET_MODE_WEALTH)
    ? DEFAULT_WEALTH_USD
    : (balanceBtc * priceUsd);

  // Longevity with inflation
  bool freedomHitCap = false;
  int years = 0, months = 0, weeks = 0;
  float coveredWeeks = 0.0f;
  computeLongevityWithInflation(
    usdWealth,
    MONTHLY_EXP_USD,
    INFLATION_ANNUAL,
    WEALTH_GROWTH_ANNUAL,
    PORTFOLIO_USE_MODE,
    BORROW_FEE_ANNUAL,
    freedomHitCap,
    years,
    months,
    weeks,
    coveredWeeks
  );

  LifeStats lifeStats = {};
  const time_t now = estimateCurrentTime(ntpSynced, wakeCause);
  computeLifeStats(
    OWNER_BIRTH_YEAR,
    OWNER_LIFE_EXPECTANCY_YEARS,
    now,
    lifeStats
  );

  bool coveredInfinite = freedomHitCap;
  int coveredPercent = 100;
  if (lifeStats.remainingWeeks > 0) {
    float coverageRatio = coveredWeeks / (float)lifeStats.remainingWeeks;
    coveredPercent = clampInt((int)(coverageRatio * 100.0f + 0.5f), 0, 999);
  }

  bool showInfoScreen = (wakeCause == ESP_SLEEP_WAKEUP_EXT0) || (digitalRead(PIN_USER_BUTTON) == LOW);
  if (showInfoScreen) {
    drawInfoScreen(
      OWNER_NAME,
      PORTFOLIO_ASSET_MODE,
      PORTFOLIO_USE_MODE,
      usdWealth,
      balanceBtc,
      priceUsd,
      WEALTH_GROWTH_ANNUAL,
      BORROW_FEE_ANNUAL,
      INFLATION_ANNUAL,
      MONTHLY_EXP_USD,
      OWNER_BIRTH_YEAR,
      OWNER_LIFE_EXPECTANCY_YEARS,
      pct
    );
  } else {
    drawFreedomClock(OWNER_NAME, freedomHitCap, years, months, weeks, lifeStats, coveredInfinite, coveredPercent, pct);
  }

  if (now >= 1700000000) {
    lastKnownUnixTime = now;
  }

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
