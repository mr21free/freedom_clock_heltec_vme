#pragma once

static void clearBatteryLogInMemory(BatteryLog& log) {
  memset(&log, 0, sizeof(log));
}

static void setBatteryLogStorageStatus(const char* text) {
  if (!text) text = "";
  strlcpy(batteryLogStorageStatus, text, sizeof(batteryLogStorageStatus));
}

static void setWealthHistoryStorageStatus(const char* text) {
  if (!text) text = "";
  strlcpy(wealthHistoryStorageStatus, text, sizeof(wealthHistoryStorageStatus));
}

static bool ensureLocalFileSystemMounted() {
  static bool attempted = false;
  static bool mounted = false;
  if (attempted) return mounted;

  attempted = true;
  mounted = SPIFFS.begin(false);
  if (!mounted) {
    mounted = SPIFFS.begin(true);
  }
  return mounted;
}

static bool validateBatteryLog(BatteryLog& log) {
  if (log.count > BATTERY_LOG_SAMPLES || log.nextIndex >= BATTERY_LOG_SAMPLES) {
    clearBatteryLogInMemory(log);
    return false;
  }
  return true;
}

static bool loadBatteryLogFromLegacyNvs(BatteryLog& log) {
  if (!preferences.begin(BATTERY_LOG_NAMESPACE, true)) {
    setBatteryLogStorageStatus("load failed: legacy NVS open failed");
    return false;
  }

  const size_t storedSize = preferences.getBytesLength("log");
  if (storedSize == sizeof(BatteryLog)) {
    preferences.getBytes("log", &log, sizeof(BatteryLog));
    preferences.end();
    if (!validateBatteryLog(log)) {
      setBatteryLogStorageStatus("load failed: legacy NVS data invalid");
      return false;
    }
    snprintf(
      batteryLogStorageStatus,
      sizeof(batteryLogStorageStatus),
      "loaded from legacy NVS: %u samples",
      (unsigned int)log.count
    );
    return true;
  }

  preferences.end();
  snprintf(
    batteryLogStorageStatus,
    sizeof(batteryLogStorageStatus),
    "empty: no SPIFFS file, legacy NVS size=%u",
    (unsigned int)storedSize
  );
  return false;
}

static bool loadBatteryLog(BatteryLog& log) {
  clearBatteryLogInMemory(log);

  if (!ensureLocalFileSystemMounted()) {
    return loadBatteryLogFromLegacyNvs(log);
  }

  File file = SPIFFS.open(BATTERY_LOG_FILE_PATH, FILE_READ);
  if (!file) {
    return loadBatteryLogFromLegacyNvs(log);
  }

  const size_t storedSize = file.size();
  if (storedSize != sizeof(BatteryLog)) {
    file.close();
    clearBatteryLogInMemory(log);
    snprintf(
      batteryLogStorageStatus,
      sizeof(batteryLogStorageStatus),
      "load failed: SPIFFS size=%u expected=%u",
      (unsigned int)storedSize,
      (unsigned int)sizeof(BatteryLog)
    );
    return false;
  }

  const size_t bytesRead = file.readBytes(reinterpret_cast<char*>(&log), sizeof(BatteryLog));
  file.close();
  if (bytesRead != sizeof(BatteryLog)) {
    clearBatteryLogInMemory(log);
    snprintf(
      batteryLogStorageStatus,
      sizeof(batteryLogStorageStatus),
      "load failed: SPIFFS read=%u expected=%u",
      (unsigned int)bytesRead,
      (unsigned int)sizeof(BatteryLog)
    );
    return false;
  }

  if (!validateBatteryLog(log)) {
    setBatteryLogStorageStatus("load failed: SPIFFS data invalid");
    return false;
  }

  snprintf(
    batteryLogStorageStatus,
    sizeof(batteryLogStorageStatus),
    "loaded from SPIFFS: %u samples",
    (unsigned int)log.count
  );
  return true;
}

static bool saveBatteryLog(const BatteryLog& log) {
  if (!ensureLocalFileSystemMounted()) {
    setBatteryLogStorageStatus("save failed: SPIFFS mount failed");
    return false;
  }

  SPIFFS.remove(BATTERY_LOG_FILE_PATH);
  File file = SPIFFS.open(BATTERY_LOG_FILE_PATH, FILE_WRITE);
  if (!file) {
    setBatteryLogStorageStatus("save failed: SPIFFS open failed");
    return false;
  }

  const size_t written = file.write(reinterpret_cast<const uint8_t*>(&log), sizeof(BatteryLog));
  file.close();
  const bool ok = written == sizeof(BatteryLog);
  snprintf(
    batteryLogStorageStatus,
    sizeof(batteryLogStorageStatus),
    "%s: SPIFFS wrote=%u expected=%u samples=%u",
    ok ? "saved" : "save failed",
    (unsigned int)written,
    (unsigned int)sizeof(BatteryLog),
    (unsigned int)log.count
  );
  return ok;
}

static bool clearBatteryLog() {
  bool ok = true;
  if (ensureLocalFileSystemMounted() && SPIFFS.exists(BATTERY_LOG_FILE_PATH)) {
    ok = SPIFFS.remove(BATTERY_LOG_FILE_PATH);
  }

  if (!preferences.begin(BATTERY_LOG_NAMESPACE, false)) {
    return ok;
  }
  ok = preferences.clear() && ok;
  preferences.end();
  setBatteryLogStorageStatus(ok ? "cleared" : "clear failed");
  return ok;
}

static bool appendBatteryLogSample(BatteryLog& log, time_t now, float voltage, int percent) {
  if (!(voltage > 0.0f)) return false;
  if (log.count > BATTERY_LOG_SAMPLES || log.nextIndex >= BATTERY_LOG_SAMPLES) {
    clearBatteryLogInMemory(log);
  }

  const uint16_t index = log.nextIndex;
  log.nextSampleId++;
  if (log.nextSampleId == 0) log.nextSampleId = 1;
  log.unixTime[index] = (now >= 1700000000) ? (uint32_t)now : 0;
  log.millivolts[index] = (uint16_t)clampInt((int)(voltage * 1000.0f + 0.5f), 0, 65535);
  log.percent[index] = (uint8_t)clampInt(percent, 0, 100);

  log.nextIndex = (uint16_t)((index + 1) % BATTERY_LOG_SAMPLES);
  if (log.count < BATTERY_LOG_SAMPLES) log.count++;
  return true;
}

static bool seedBatteryGaugeStateFromLog(const BatteryLog& log) {
  if (batteryGaugeStateValid || log.count == 0 || log.count > BATTERY_LOG_SAMPLES || log.nextIndex >= BATTERY_LOG_SAMPLES) {
    return false;
  }

  auto seedFromIndex = [&](uint16_t index) {
    const uint16_t millivolts = log.millivolts[index];
    if (millivolts == 0) return false;
    batteryGaugeStateValid = true;
    batteryGaugePercent = clampInt((int)log.percent[index], 0, 100);
    batteryGaugeReferenceVoltage = (float)millivolts / 1000.0f;
    return true;
  };

  uint16_t index = (log.nextIndex == 0) ? (BATTERY_LOG_SAMPLES - 1) : (uint16_t)(log.nextIndex - 1);
  for (uint16_t i = 0; i < log.count; i++) {
    if (log.unixTime[index] >= 1700000000 && seedFromIndex(index)) {
      return true;
    }
    index = (index == 0) ? (BATTERY_LOG_SAMPLES - 1) : (uint16_t)(index - 1);
  }

  index = (log.nextIndex == 0) ? (BATTERY_LOG_SAMPLES - 1) : (uint16_t)(log.nextIndex - 1);
  for (uint16_t i = 0; i < log.count; i++) {
    if (seedFromIndex(index)) {
      return true;
    }
    index = (index == 0) ? (BATTERY_LOG_SAMPLES - 1) : (uint16_t)(index - 1);
  }
  return false;
}

static String buildBatteryLogText(const BatteryLog& log) {
  String out;
  out.reserve(220 + ((size_t)log.count * 48));
  out += "Battery calibration log\n";
  out += "Format: sample, unix_time, voltage, percent\n";
  out += "Newest sample is last.\n";
  out += "Storage: ";
  out += batteryLogStorageStatus;
  out += "\n\n";

  if (log.count == 0 || log.count > BATTERY_LOG_SAMPLES) {
    out += "No battery samples recorded yet.";
    return out;
  }

  char summary[96];
  snprintf(
    summary,
    sizeof(summary),
    "Stored samples: %u / %u\n\n",
    (unsigned int)log.count,
    (unsigned int)BATTERY_LOG_SAMPLES
  );
  out += summary;

  const uint16_t firstIndex = (log.count == BATTERY_LOG_SAMPLES) ? log.nextIndex : 0;
  const uint32_t firstSampleId = (log.nextSampleId >= log.count) ? (log.nextSampleId - log.count + 1) : 1;
  for (uint16_t i = 0; i < log.count; i++) {
    const uint16_t index = (uint16_t)((firstIndex + i) % BATTERY_LOG_SAMPLES);
    char line[80];
    snprintf(
      line,
      sizeof(line),
      "#%lu, t=%lu, v=%.2fV, p=%u%%\n",
      (unsigned long)(firstSampleId + i),
      (unsigned long)log.unixTime[index],
      (double)log.millivolts[index] / 1000.0,
      (unsigned int)log.percent[index]
    );
    out += line;
  }
  return out;
}

#if ENABLE_DEVELOPER_STATS
static size_t estimateQuoteDatabaseTextBytes() {
  size_t total = 0;
  for (size_t i = 0; i < QUOTE_DB_COUNT; i++) {
    if (QUOTE_DB[i].text) total += strlen(QUOTE_DB[i].text) + 1;
    if (QUOTE_DB[i].author) total += strlen(QUOTE_DB[i].author) + 1;
  }
  return total;
}

static void appendUsageLine(String& out, const char* label, uint32_t used, uint32_t total, const char* unit) {
  char line[160];
  if (total > 0) {
    snprintf(
      line,
      sizeof(line),
      "%s: %.1f / %.1f %s used (%.1f%%)\n",
      label,
      (double)used / 1024.0,
      (double)total / 1024.0,
      unit,
      ((double)used * 100.0) / (double)total
    );
  } else {
    snprintf(line, sizeof(line), "%s: unavailable\n", label);
  }
  out += line;
}

static void appendEntryUsageLine(String& out, const char* label, uint32_t used, uint32_t total) {
  char line[160];
  if (total > 0) {
    snprintf(
      line,
      sizeof(line),
      "%s: %lu / %lu entries used (%.1f%%)\n",
      label,
      (unsigned long)used,
      (unsigned long)total,
      ((double)used * 100.0) / (double)total
    );
  } else {
    snprintf(line, sizeof(line), "%s: unavailable\n", label);
  }
  out += line;
}

static String buildDeveloperStatsText() {
  String out;
  out.reserve(1800);
  out += "Developer storage stats\n";
  out += "Firmware: ";
  out += FIRMWARE_VERSION;
  out += "\nBoard: ";
  out += DEVICE_MODEL_NAME;
  out += "\n\n";

  appendUsageLine(out, "Firmware space usage", ESP.getSketchSize(), ESP.getSketchSize() + ESP.getFreeSketchSpace(), "KB");
  out += "Quotes are compiled into firmware.\n";

  appendUsageLine(out, "Runtime memory usage", ESP.getHeapSize() - ESP.getFreeHeap(), ESP.getHeapSize(), "KB");

  out += "\nQuote database\n";
  char quoteLine[96];
  snprintf(
    quoteLine,
    sizeof(quoteLine),
    "Quotes: %u\nQuote text bytes: %lu\n",
    (unsigned int)QUOTE_DB_COUNT,
    (unsigned long)estimateQuoteDatabaseTextBytes()
  );
  out += quoteLine;

  nvs_stats_t nvsStats = {};
  if (nvs_get_stats(nullptr, &nvsStats) == ESP_OK) {
    out += "\n";
    appendEntryUsageLine(out, "Settings storage (NVS)", nvsStats.used_entries, nvsStats.total_entries);
    char nvsLine[48];
    snprintf(nvsLine, sizeof(nvsLine), "Namespaces: %u\n", (unsigned int)nvsStats.namespace_count);
    out += nvsLine;
  } else {
    out += "\nNVS stats unavailable.\n";
  }

  return out;
}
#endif

static void clearWealthHistoryInMemory(WealthHistory& history) {
  history.latestDay = 0;
  for (uint16_t i = 0; i < WEALTH_HISTORY_DAYS; i++) {
    history.dailyWealthValue[i] = WEALTH_HISTORY_EMPTY;
    history.dailyPriceValue[i] = PRICE_HISTORY_EMPTY;
    history.dailyBalanceSats[i] = BALANCE_HISTORY_EMPTY;
  }
}

static bool loadWealthHistoryFromLegacyNvs(WealthHistory& history) {
  if (!preferences.begin(HISTORY_NAMESPACE, true)) {
    setWealthHistoryStorageStatus("load failed: legacy NVS open failed");
    return false;
  }

  const uint32_t storedVersion = preferences.getUInt("ver", 0);
  if (storedVersion != HISTORY_VERSION) {
    preferences.end();
    snprintf(
      wealthHistoryStorageStatus,
      sizeof(wealthHistoryStorageStatus),
      "empty: no SPIFFS file, legacy NVS version=%lu",
      (unsigned long)storedVersion
    );
    return false;
  }

  history.latestDay = preferences.getUInt("day", 0);
  size_t wealthBytesRead = preferences.getBytes("wealth", history.dailyWealthValue, sizeof(history.dailyWealthValue));
  size_t priceBytesRead = preferences.getBytes("price", history.dailyPriceValue, sizeof(history.dailyPriceValue));
  size_t balanceBytesRead = preferences.getBytes("bal", history.dailyBalanceSats, sizeof(history.dailyBalanceSats));
  preferences.end();

  if (
    wealthBytesRead != sizeof(history.dailyWealthValue) ||
    priceBytesRead != sizeof(history.dailyPriceValue) ||
    balanceBytesRead != sizeof(history.dailyBalanceSats)
  ) {
    clearWealthHistoryInMemory(history);
    snprintf(
      wealthHistoryStorageStatus,
      sizeof(wealthHistoryStorageStatus),
      "load failed: legacy NVS sizes wealth=%u price=%u bal=%u",
      (unsigned int)wealthBytesRead,
      (unsigned int)priceBytesRead,
      (unsigned int)balanceBytesRead
    );
    return false;
  }

  snprintf(
    wealthHistoryStorageStatus,
    sizeof(wealthHistoryStorageStatus),
    "loaded from legacy NVS: latest_day=%lu",
    (unsigned long)history.latestDay
  );
  return history.latestDay > 0;
}

static void recordHistoryWriteMetadata(uint32_t latestDay);
static void recordHistoryClearMetadata(const char* reason);

static bool loadWealthHistory(WealthHistory& history) {
  clearWealthHistoryInMemory(history);

  if (!ensureLocalFileSystemMounted()) {
    return loadWealthHistoryFromLegacyNvs(history);
  }

  File file = SPIFFS.open(WEALTH_HISTORY_FILE_PATH, FILE_READ);
  if (!file) {
    return loadWealthHistoryFromLegacyNvs(history);
  }

  const size_t storedSize = file.size();
  if (storedSize != sizeof(WealthHistory)) {
    file.close();
    clearWealthHistoryInMemory(history);
    if (loadWealthHistoryFromLegacyNvs(history)) {
      return true;
    }
    snprintf(
      wealthHistoryStorageStatus,
      sizeof(wealthHistoryStorageStatus),
      "load failed: SPIFFS size=%u expected=%u",
      (unsigned int)storedSize,
      (unsigned int)sizeof(WealthHistory)
    );
    return false;
  }

  const size_t bytesRead = file.readBytes(reinterpret_cast<char*>(&history), sizeof(WealthHistory));
  file.close();
  if (bytesRead != sizeof(WealthHistory)) {
    clearWealthHistoryInMemory(history);
    snprintf(
      wealthHistoryStorageStatus,
      sizeof(wealthHistoryStorageStatus),
      "load failed: SPIFFS read=%u expected=%u",
      (unsigned int)bytesRead,
      (unsigned int)sizeof(WealthHistory)
    );
    return false;
  }

  snprintf(
    wealthHistoryStorageStatus,
    sizeof(wealthHistoryStorageStatus),
    "loaded from SPIFFS: latest_day=%lu",
    (unsigned long)history.latestDay
  );
  return history.latestDay > 0;
}

static bool saveWealthHistoryToLegacyNvs(const WealthHistory& history) {
  if (!preferences.begin(HISTORY_NAMESPACE, false)) {
    return false;
  }

  preferences.putUInt("ver", HISTORY_VERSION);
  preferences.putUInt("day", history.latestDay);
  size_t wealthBytesWritten = preferences.putBytes("wealth", history.dailyWealthValue, sizeof(history.dailyWealthValue));
  size_t priceBytesWritten = preferences.putBytes("price", history.dailyPriceValue, sizeof(history.dailyPriceValue));
  size_t balanceBytesWritten = preferences.putBytes("bal", history.dailyBalanceSats, sizeof(history.dailyBalanceSats));
  preferences.end();
  return (
    wealthBytesWritten == sizeof(history.dailyWealthValue) &&
    priceBytesWritten == sizeof(history.dailyPriceValue) &&
    balanceBytesWritten == sizeof(history.dailyBalanceSats)
  );
}

static bool saveWealthHistory(const WealthHistory& history) {
  bool saved = false;
  if (ensureLocalFileSystemMounted()) {
    SPIFFS.remove(WEALTH_HISTORY_FILE_PATH);
    File file = SPIFFS.open(WEALTH_HISTORY_FILE_PATH, FILE_WRITE);
    if (file) {
      const size_t written = file.write(reinterpret_cast<const uint8_t*>(&history), sizeof(WealthHistory));
      file.close();
      saved = written == sizeof(WealthHistory);
      snprintf(
        wealthHistoryStorageStatus,
        sizeof(wealthHistoryStorageStatus),
        "%s: SPIFFS wrote=%u expected=%u latest_day=%lu",
        saved ? "saved" : "save failed",
        (unsigned int)written,
        (unsigned int)sizeof(WealthHistory),
        (unsigned long)history.latestDay
      );
    } else {
      setWealthHistoryStorageStatus("save failed: SPIFFS open failed");
    }
  } else {
    setWealthHistoryStorageStatus("save failed: SPIFFS mount failed");
  }

  if (!saved) {
    saved = saveWealthHistoryToLegacyNvs(history);
    snprintf(
      wealthHistoryStorageStatus,
      sizeof(wealthHistoryStorageStatus),
      "%s: legacy NVS latest_day=%lu",
      saved ? "saved fallback" : "save failed fallback",
      (unsigned long)history.latestDay
    );
  }

  if (saved) {
    recordHistoryWriteMetadata(history.latestDay);
  }
  return saved;
}

static void recordHistoryWriteMetadata(uint32_t latestDay) {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    return;
  }
  const uint32_t writeCount = preferences.getUInt("hist_writes", 0);
  preferences.putUInt("hist_writes", writeCount + 1);
  preferences.putUInt("hist_write_day", latestDay);
  preferences.end();
}

static void recordHistoryClearMetadata(const char* reason) {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    return;
  }
  const uint32_t clearCount = preferences.getUInt("hist_clears", 0);
  preferences.putUInt("hist_clears", clearCount + 1);
  preferences.putString("hist_clear", reason ? reason : "unknown");
  preferences.putUInt("hist_clear_day", lastKnownUnixTime >= 1700000000 ? (uint32_t)((uint64_t)lastKnownUnixTime / 86400ULL) : 0);
  preferences.end();
}

static bool clearWealthHistory() {
  bool cleared = true;
  if (ensureLocalFileSystemMounted() && SPIFFS.exists(WEALTH_HISTORY_FILE_PATH)) {
    cleared = SPIFFS.remove(WEALTH_HISTORY_FILE_PATH);
  }

  if (!preferences.begin(HISTORY_NAMESPACE, false)) {
    return cleared;
  }
  cleared = preferences.clear() && cleared;
  preferences.end();
  return cleared;
}

static bool clearWealthHistoryWithReason(const char* reason) {
  const bool cleared = clearWealthHistory();
  if (cleared) {
    recordHistoryClearMetadata(reason);
  }
  return cleared;
}

static uint16_t countWealthHistoryRows(const WealthHistory& history) {
  uint16_t rows = 0;
  for (uint16_t i = 0; i < WEALTH_HISTORY_DAYS; i++) {
    if (
      history.dailyWealthValue[i] != WEALTH_HISTORY_EMPTY ||
      history.dailyPriceValue[i] != PRICE_HISTORY_EMPTY ||
      history.dailyBalanceSats[i] != BALANCE_HISTORY_EMPTY
    ) {
      rows++;
    }
  }
  return rows;
}

static bool backfillSparseWealthHistoryFromMetadata(WealthHistory& history) {
  if (history.latestDay == 0 || countWealthHistoryRows(history) != 1) {
    return false;
  }

  uint32_t lastWriteDay = 0;
  uint32_t lastClearDay = 0;
  if (preferences.begin(CONFIG_NAMESPACE, true)) {
    lastWriteDay = preferences.getUInt("hist_write_day", 0);
    lastClearDay = preferences.getUInt("hist_clear_day", 0);
    preferences.end();
  }
  if (lastWriteDay == 0 || lastWriteDay >= history.latestDay) {
    return false;
  }
  if (lastClearDay >= lastWriteDay && lastClearDay <= history.latestDay) {
    return false;
  }

  static constexpr uint32_t METADATA_REPAIR_MAX_DAYS = 7;
  const uint32_t missingDays = history.latestDay - lastWriteDay;
  if (missingDays == 0 || missingDays > METADATA_REPAIR_MAX_DAYS) {
    return false;
  }

  const uint16_t latestIndex = WEALTH_HISTORY_DAYS - 1;
  const int32_t latestWealth = history.dailyWealthValue[latestIndex];
  const int32_t latestPrice = history.dailyPriceValue[latestIndex];
  const int64_t latestBalance = history.dailyBalanceSats[latestIndex];
  if (
    latestWealth == WEALTH_HISTORY_EMPTY &&
    latestPrice == PRICE_HISTORY_EMPTY &&
    latestBalance == BALANCE_HISTORY_EMPTY
  ) {
    return false;
  }

  for (uint32_t daysAgo = 1; daysAgo <= missingDays && daysAgo < WEALTH_HISTORY_DAYS; daysAgo++) {
    const uint16_t index = (uint16_t)(latestIndex - daysAgo);
    history.dailyWealthValue[index] = latestWealth;
    history.dailyPriceValue[index] = latestPrice;
    history.dailyBalanceSats[index] = latestBalance;
  }
  return true;
}

static bool updateWealthHistory(
  WealthHistory& history,
  time_t now,
  float wealthValue,
  bool hasBtcBreakdown,
  float priceValue,
  float balanceBtc
) {
  if (now <= 0 || !(wealthValue >= 0.0f)) return false;

  const uint32_t currentDay = (uint32_t)((uint64_t)now / 86400ULL);
  const int32_t wealthRounded = (wealthValue >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(wealthValue + 0.5f);
  const int32_t priceRounded = (hasBtcBreakdown && priceValue < (float)INT32_MAX)
    ? (int32_t)(priceValue + 0.5f)
    : PRICE_HISTORY_EMPTY;
  const int64_t balanceSats = hasBtcBreakdown
    ? (int64_t)(balanceBtc * 100000000.0 + 0.5)
    : BALANCE_HISTORY_EMPTY;

  const bool repairedSparseHistory = (history.latestDay == currentDay)
    ? backfillSparseWealthHistoryFromMetadata(history)
    : false;

  if (history.latestDay == 0) {
    clearWealthHistoryInMemory(history);
    history.latestDay = currentDay;
    history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
    history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1] = priceRounded;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
    return true;
  }

  if (currentDay <= history.latestDay) {
    int32_t& latest = history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1];
    int32_t& latestPrice = history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1];
    int64_t& latestBalance = history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1];
    if (latest == wealthRounded && latestPrice == priceRounded && latestBalance == balanceSats) return repairedSparseHistory;
    latest = wealthRounded;
    latestPrice = priceRounded;
    latestBalance = balanceSats;
    return true;
  }

  const uint32_t dayDelta = currentDay - history.latestDay;
  const int32_t previousLatest = history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1];
  const int32_t previousLatestPrice = history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1];
  const int64_t previousLatestBalance = history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1];

  if (dayDelta >= WEALTH_HISTORY_DAYS) {
    clearWealthHistoryInMemory(history);
    history.latestDay = currentDay;
    history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
    history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1] = priceRounded;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
    return true;
  }

  memmove(
    history.dailyWealthValue,
    history.dailyWealthValue + dayDelta,
    (WEALTH_HISTORY_DAYS - dayDelta) * sizeof(history.dailyWealthValue[0])
  );
  memmove(
    history.dailyPriceValue,
    history.dailyPriceValue + dayDelta,
    (WEALTH_HISTORY_DAYS - dayDelta) * sizeof(history.dailyPriceValue[0])
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
    history.dailyWealthValue[i] = fillValue;
    history.dailyPriceValue[i] = (hasBtcBreakdown ? fillPriceValue : PRICE_HISTORY_EMPTY);
    history.dailyBalanceSats[i] = (hasBtcBreakdown ? fillBalanceValue : BALANCE_HISTORY_EMPTY);
  }
  history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1] = wealthRounded;
  history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1] = priceRounded;
  history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = balanceSats;
  history.latestDay = currentDay;
  return true;
}

static bool getHistoricalWealthValue(const WealthHistory& history, uint16_t daysAgo, int32_t& outWealthValue) {
  if (history.latestDay == 0 || daysAgo >= WEALTH_HISTORY_DAYS) return false;

  const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
  if (index < 0) return false;

  const int32_t stored = history.dailyWealthValue[index];
  if (stored == WEALTH_HISTORY_EMPTY) return false;

  outWealthValue = stored;
  return true;
}

static bool getHistoricalPriceValue(const WealthHistory& history, uint16_t daysAgo, int32_t& outPriceValue) {
  if (history.latestDay == 0 || daysAgo >= WEALTH_HISTORY_DAYS) return false;

  const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
  if (index < 0) return false;

  const int32_t stored = history.dailyPriceValue[index];
  if (stored == PRICE_HISTORY_EMPTY) return false;

  outPriceValue = stored;
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

#if ENABLE_DEVELOPER_STATS
static void appendHistoryRowValue(String& out, int32_t value, int32_t emptyValue) {
  if (value == emptyValue) {
    return;
  }
  out += String((long)value);
}

static void appendHistoryBalanceBtc(String& out, int64_t sats) {
  if (sats == BALANCE_HISTORY_EMPTY) {
    return;
  }
  out += String((double)sats / 100000000.0, 8);
}

static void appendHistorySnapshotLine(String& out, const WealthHistory& history, const char* label, uint16_t daysAgo) {
  int32_t wealthValue = 0;
  int32_t priceValue = 0;
  int64_t balanceSats = 0;
  out += label;
  out += ": ";
  if (getHistoricalWealthValue(history, daysAgo, wealthValue)) {
    out += "wealth=";
    out += String((long)wealthValue);
  } else {
    out += "wealth=N/A";
  }
  out += ", ";
  if (getHistoricalPriceValue(history, daysAgo, priceValue)) {
    out += "btc_price=";
    out += String((long)priceValue);
  } else {
    out += "btc_price=N/A";
  }
  out += ", ";
  if (getHistoricalBalanceSats(history, daysAgo, balanceSats)) {
    out += "btc_balance=";
    out += String((double)balanceSats / 100000000.0, 8);
  } else {
    out += "btc_balance=N/A";
  }
  out += "\n";
}

static void appendHistoryLifecycleStats(String& out) {
  uint32_t writeCount = 0;
  uint32_t lastWriteDay = 0;
  uint32_t clearCount = 0;
  uint32_t lastClearDay = 0;
  String lastClearReason = "N/A";

  if (preferences.begin(CONFIG_NAMESPACE, true)) {
    writeCount = preferences.getUInt("hist_writes", 0);
    lastWriteDay = preferences.getUInt("hist_write_day", 0);
    clearCount = preferences.getUInt("hist_clears", 0);
    lastClearDay = preferences.getUInt("hist_clear_day", 0);
    lastClearReason = preferences.getString("hist_clear", "N/A");
    preferences.end();
  }

  out += "Lifecycle\n";
  out += "history_write_count=";
  out += String((unsigned long)writeCount);
  out += "\nlast_write_day=";
  out += String((unsigned long)lastWriteDay);
  out += "\nlast_write_unix_midnight=";
  out += String((unsigned long)((uint64_t)lastWriteDay * 86400ULL));
  out += "\nhistory_clear_count=";
  out += String((unsigned long)clearCount);
  out += "\nlast_clear_day=";
  out += String((unsigned long)lastClearDay);
  out += "\nlast_clear_unix_midnight=";
  out += String((unsigned long)((uint64_t)lastClearDay * 86400ULL));
  out += "\nlast_clear_reason=";
  out += lastClearReason;
  out += "\n\n";
}

static String buildHistoryStatsText(const WealthHistory& history, uint8_t currencyCode) {
  String out;
  out.reserve(26000);
  out += "Freedom / wealth history stats\n";
  out += "Used by: freedom change screen and wealth change screen\n";
  out += "Currency: ";
  out += currencyCodeLabel(currencyCode);
  out += "\n";
  out += "Storage: SPIFFS file ";
  out += WEALTH_HISTORY_FILE_PATH;
  out += " with legacy NVS fallback ";
  out += HISTORY_NAMESPACE;
  out += ", version ";
  out += String((unsigned long)HISTORY_VERSION);
  out += "\nStorage status: ";
  out += wealthHistoryStorageStatus;
  out += "\nCapacity: ";
  out += String((unsigned int)WEALTH_HISTORY_DAYS);
  out += " days\n";
  out += "Format: latest stored day is days_ago=0. Unix time is midnight UTC for the stored day.\n";
  out += "Sensitive: contains wealth/BTC history. Copy only for development/debugging.\n\n";
  appendHistoryLifecycleStats(out);

  if (history.latestDay == 0) {
    out += "Status: no history loaded or recorded yet.\n";
    out += "A first history row is written after a normal clock refresh with valid time and calculable wealth data.\n";
    out += "Opening setup immediately after reset or before the first successful refresh will show no rows here.";
    return out;
  }

  const uint16_t recordedRows = countWealthHistoryRows(history);

  out += "Status: history loaded from device storage\n";
  out += "latest_day=";
  out += String((unsigned long)history.latestDay);
  out += "\nlatest_unix_midnight=";
  out += String((unsigned long)((uint64_t)history.latestDay * 86400ULL));
  out += "\nrecorded_rows=";
  out += String((unsigned int)recordedRows);
  if (recordedRows == 1) {
    out += "\ninterpretation=Only one UTC daily bucket exists. This usually means history was reset recently, or the first successful app refresh with valid time/data happened today.";
  }
  out += "\n\nScreen period snapshot\n";
  appendHistorySnapshotLine(out, history, "1D", 1);
  appendHistorySnapshotLine(out, history, "7D", 7);
  appendHistorySnapshotLine(out, history, "1M", 30);
  appendHistorySnapshotLine(out, history, "3M", 90);
  appendHistorySnapshotLine(out, history, "6M", 180);
  appendHistorySnapshotLine(out, history, "12M", 365);

  out += "\nCSV\n";
  out += "days_ago,day,unix_midnight,wealth_value,btc_price_value,btc_balance_btc\n";

  for (uint16_t daysAgo = 0; daysAgo < WEALTH_HISTORY_DAYS; daysAgo++) {
    const int index = (int)WEALTH_HISTORY_DAYS - 1 - (int)daysAgo;
    if (index < 0) break;

    const int32_t wealthValue = history.dailyWealthValue[index];
    const int32_t priceValue = history.dailyPriceValue[index];
    const int64_t balanceSats = history.dailyBalanceSats[index];
    if (wealthValue == WEALTH_HISTORY_EMPTY && priceValue == PRICE_HISTORY_EMPTY && balanceSats == BALANCE_HISTORY_EMPTY) {
      continue;
    }

    const uint32_t day = (history.latestDay > daysAgo) ? (history.latestDay - daysAgo) : 0;
    out += String((unsigned int)daysAgo);
    out += ",";
    out += String((unsigned long)day);
    out += ",";
    out += String((unsigned long)((uint64_t)day * 86400ULL));
    out += ",";
    appendHistoryRowValue(out, wealthValue, WEALTH_HISTORY_EMPTY);
    out += ",";
    appendHistoryRowValue(out, priceValue, PRICE_HISTORY_EMPTY);
    out += ",";
    appendHistoryBalanceBtc(out, balanceSats);
    out += "\n";
  }

  return out;
}
#endif

static bool maybeSeedTestWealthHistory(
  WealthHistory& history,
  time_t now,
  const DeviceConfig& cfg,
  float currentWealthValue,
  bool hasBtcBreakdown,
  float currentPriceValue,
  float currentBalanceBtc
) {
#if ENABLE_TEST_HISTORY
  if (now <= 0) return false;
  if (!FORCE_TEST_HISTORY_ON_EVERY_BOOT && history.latestDay != 0) return false;

  clearWealthHistoryInMemory(history);
  history.latestDay = (uint32_t)((uint64_t)now / 86400ULL);

  const AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  const bool seedBtcBreakdown = isAnyBtcAssetMode(assetMode);
  const float endingPriceValue = (hasBtcBreakdown && currentPriceValue > 0.0f) ? currentPriceValue : 65000.0f;
  const float endingBalanceBtc = (hasBtcBreakdown && currentBalanceBtc > 0.0f) ? currentBalanceBtc : 1.2500f;
  const float endingWealthValue = (currentWealthValue > 0.0f)
    ? currentWealthValue
    : (seedBtcBreakdown
        ? (endingPriceValue * endingBalanceBtc)
        : ((cfg.defaultWealthValue > 0.0f) ? cfg.defaultWealthValue : DEFAULT_WEALTH_VALUE));

  for (uint16_t i = 0; i < WEALTH_HISTORY_DAYS; i++) {
    const float progress = (WEALTH_HISTORY_DAYS <= 1)
      ? 1.0f
      : ((float)i / (float)(WEALTH_HISTORY_DAYS - 1));

    if (seedBtcBreakdown) {
      const float priceWave = (float)((int)(i % 21) - 10) / 10.0f;
      const float balanceWave = (float)((int)(i % 35) - 17) / 17.0f;
      float seededPriceValue = endingPriceValue * (0.72f + (0.28f * progress));
      float seededBalanceBtc = endingBalanceBtc * (0.88f + (0.12f * progress));
      seededPriceValue += endingPriceValue * 0.03f * priceWave;
      seededBalanceBtc += endingBalanceBtc * 0.01f * balanceWave;
      seededBalanceBtc += endingBalanceBtc * 0.0025f * (float)(i / 45);
      if (!(seededPriceValue > 0.0f)) seededPriceValue = endingPriceValue;
      if (!(seededBalanceBtc > 0.0f)) seededBalanceBtc = endingBalanceBtc;
      const float seededWealthValue = seededPriceValue * seededBalanceBtc;
      history.dailyPriceValue[i] = (int32_t)(seededPriceValue + 0.5f);
      history.dailyBalanceSats[i] = (int64_t)(seededBalanceBtc * 100000000.0 + 0.5);
      history.dailyWealthValue[i] = (seededWealthValue >= (float)INT32_MAX)
        ? INT32_MAX
        : (int32_t)(seededWealthValue + 0.5f);
    } else {
      const float wealthWave = (float)((int)(i % 31) - 15) / 15.0f;
      float seededWealthValue = endingWealthValue * (0.76f + (0.24f * progress));
      seededWealthValue += endingWealthValue * 0.015f * wealthWave;
      if (!(seededWealthValue >= 0.0f)) seededWealthValue = endingWealthValue;
      history.dailyWealthValue[i] = (seededWealthValue >= (float)INT32_MAX)
        ? INT32_MAX
        : (int32_t)(seededWealthValue + 0.5f);
      history.dailyPriceValue[i] = PRICE_HISTORY_EMPTY;
      history.dailyBalanceSats[i] = BALANCE_HISTORY_EMPTY;
    }
  }

  const uint16_t weeklyStartIndex = (WEALTH_HISTORY_DAYS > 8) ? (WEALTH_HISTORY_DAYS - 8) : 0;
  const uint16_t weeklySteps = (uint16_t)(WEALTH_HISTORY_DAYS - weeklyStartIndex - 1);
  if (weeklySteps > 0) {
    for (uint16_t i = weeklyStartIndex; i < WEALTH_HISTORY_DAYS; i++) {
      const float weeklyProgress = (float)(i - weeklyStartIndex) / (float)weeklySteps;
      if (seedBtcBreakdown) {
        const float weeklyPriceValue = endingPriceValue * (0.95f + (0.05f * weeklyProgress));
        const float weeklyBalanceBtc = endingBalanceBtc * (0.992f + (0.008f * weeklyProgress));
        const float weeklyWealthValue = weeklyPriceValue * weeklyBalanceBtc;
        history.dailyPriceValue[i] = (int32_t)(weeklyPriceValue + 0.5f);
        history.dailyBalanceSats[i] = (int64_t)(weeklyBalanceBtc * 100000000.0 + 0.5);
        history.dailyWealthValue[i] = (weeklyWealthValue >= (float)INT32_MAX)
          ? INT32_MAX
          : (int32_t)(weeklyWealthValue + 0.5f);
      } else {
        const float weeklyWealthValue = endingWealthValue * (0.94f + (0.06f * weeklyProgress));
        history.dailyWealthValue[i] = (weeklyWealthValue >= (float)INT32_MAX)
          ? INT32_MAX
          : (int32_t)(weeklyWealthValue + 0.5f);
      }
    }
  }

  if (seedBtcBreakdown) {
    history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1] = (int32_t)(endingPriceValue + 0.5f);
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = (int64_t)(endingBalanceBtc * 100000000.0 + 0.5);
    const float exactEndingWealthValue = endingPriceValue * endingBalanceBtc;
    history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1] = (exactEndingWealthValue >= (float)INT32_MAX)
      ? INT32_MAX
      : (int32_t)(exactEndingWealthValue + 0.5f);
  } else {
    history.dailyWealthValue[WEALTH_HISTORY_DAYS - 1] = (endingWealthValue >= (float)INT32_MAX)
      ? INT32_MAX
      : (int32_t)(endingWealthValue + 0.5f);
    history.dailyPriceValue[WEALTH_HISTORY_DAYS - 1] = PRICE_HISTORY_EMPTY;
    history.dailyBalanceSats[WEALTH_HISTORY_DAYS - 1] = BALANCE_HISTORY_EMPTY;
  }

  return saveWealthHistory(history);
#else
  (void)history;
  (void)now;
  (void)cfg;
  (void)currentWealthValue;
  (void)hasBtcBreakdown;
  (void)currentPriceValue;
  (void)currentBalanceBtc;
  return false;
#endif
}

static bool didHistorySourceChange(const DeviceConfig& oldCfg, const DeviceConfig& newCfg) {
  if (!oldCfg.configured) return false;

  const AssetMode oldMode = sanitizeAssetMode(oldCfg.assetMode);
  const AssetMode newMode = sanitizeAssetMode(newCfg.assetMode);
  if (oldMode != newMode) return true;
  if (sanitizeCurrencyCode(oldCfg.currencyCode) != sanitizeCurrencyCode(newCfg.currencyCode)) return true;

  if (isMqttBtcAssetMode(newMode)) {
    if (oldCfg.mqttPort != newCfg.mqttPort) return true;
    if (strcmp(oldCfg.mqttServer, newCfg.mqttServer) != 0) return true;
    if (strcmp(oldCfg.topicPriceValue, newCfg.topicPriceValue) != 0) return true;
    if (strcmp(oldCfg.topicBalanceBtc, newCfg.topicBalanceBtc) != 0) return true;
  }

  return false;
}
