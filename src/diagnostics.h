#pragma once

static bool diagnosticsTrackingEnabled() {
  // Keep core boot/setup diagnostics available even in production builds.
  // ENABLE_DEVELOPER_STATS only controls whether the setup page exposes
  // the copyable Developer section; it should not hide root-cause logs when
  // the device cannot enter setup.
  return true;
}

static const char* boolText(bool value) {
  return value ? "true" : "false";
}

static const char* wakeCauseText(esp_sleep_wakeup_cause_t wakeCause) {
  switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "undefined/reset";
    case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP: return "ulp";
    case ESP_SLEEP_WAKEUP_GPIO: return "gpio";
    case ESP_SLEEP_WAKEUP_UART: return "uart";
    default: return "other";
  }
}

static const char* resetReasonText(esp_reset_reason_t resetReason) {
  switch (resetReason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external-rst";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-watchdog";
    case ESP_RST_TASK_WDT: return "task-watchdog";
    case ESP_RST_WDT: return "other-watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

static const char* setupBootActionText(SetupBootAction bootAction) {
  switch (bootAction) {
    case SETUP_BOOT_ACTION_NONE: return "none";
    case SETUP_BOOT_ACTION_PORTAL: return "setup-portal";
    case SETUP_BOOT_ACTION_FACTORY_RESET: return "factory-reset";
    default: return "unknown";
  }
}

static const char* appScreenText(AppScreenId screen) {
  switch (screen) {
    case APP_SCREEN_MAIN: return "main";
    case APP_SCREEN_QUOTE: return "quote";
    case APP_SCREEN_FREEDOM_CHANGE: return "freedom-change";
    case APP_SCREEN_WEALTH_CHANGE: return "wealth-change";
    case APP_SCREEN_SETTINGS: return "settings";
    default: return "unknown";
  }
}

static const char* portalExitActionText(PortalExitAction action) {
  switch (action) {
    case PORTAL_EXIT_ACTION_NONE: return "none";
    case PORTAL_EXIT_ACTION_SAVE_CONFIG: return "save-config";
    case PORTAL_EXIT_ACTION_FIRMWARE_UPDATE: return "firmware-update";
    case PORTAL_EXIT_ACTION_FACTORY_RESET: return "factory-reset";
    case PORTAL_EXIT_ACTION_APP_SCREEN: return "app-screen";
    default: return "unknown";
  }
}

static void appendDiagnosticLine(String& out, const char* key, const char* value) {
  out += key;
  out += ": ";
  out += value;
  out += "\n";
}

static void appendDiagnosticNumber(String& out, const char* key, long value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%ld", value);
  appendDiagnosticLine(out, key, buf);
}

static void appendDiagnosticHex64(String& out, const char* key, uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)value);
  appendDiagnosticLine(out, key, buf);
}

static void appendConfigLoadDiagnostics(String& out) {
  appendDiagnosticLine(out, "Config load status", lastConfigLoadStatus);
  appendDiagnosticNumber(out, "Stored config version", (long)lastConfigStoredVersion);
  appendDiagnosticNumber(out, "Expected config version", (long)CONFIG_VERSION);
  appendDiagnosticLine(out, "Stored cfg_ok flag", boolText(lastConfigStoredOkFlag));
}

static void saveLastAppBootDiagnostics(
  esp_sleep_wakeup_cause_t wakeCause,
  esp_reset_reason_t resetReason,
  uint64_t ext1WakeStatus,
  SetupBootAction bootAction,
  bool configLoaded,
  bool functionButtonWake,
  bool setupButtonWake,
  bool homeButtonReset,
  bool portalAppScreenExit,
  AppScreenId selectedScreen,
  uint8_t wakeExtraPressCount,
  AssetMode assetMode,
  bool manualScreenRefresh,
  bool manualExternalDataRefresh,
  bool pendingFrameDrawn,
  bool pendingFrameFastMode,
  bool finalFrameFastMode,
  bool finalFrameWindowed,
  int finalFrameWindowX,
  int finalFrameWindowY,
  int finalFrameWindowW,
  int finalFrameWindowH,
  bool wifiOK,
  bool ntpSynced,
  bool mqttOK,
  bool gotPriceValue,
  bool gotBalanceValue,
  bool hasValidBtcInputs,
  bool hasFreshBtcInputs,
  bool freedomDataAvailable
) {
  if (!diagnosticsTrackingEnabled()) return;

  String out;
  out.reserve(1200);
  appendDiagnosticLine(out, "Firmware", FIRMWARE_VERSION);
  appendDiagnosticNumber(out, "Wake cause", (long)wakeCause);
  appendDiagnosticLine(out, "Wake cause label", wakeCauseText(wakeCause));
  appendDiagnosticNumber(out, "Reset reason", (long)resetReason);
  appendDiagnosticLine(out, "Reset reason label", resetReasonText(resetReason));
  appendDiagnosticHex64(out, "EXT1 wake status", ext1WakeStatus);
  appendDiagnosticLine(out, "Boot action", setupBootActionText(bootAction));
  appendDiagnosticLine(out, "Config loaded", boolText(configLoaded));
  appendConfigLoadDiagnostics(out);
  appendDiagnosticLine(out, "Function button wake", boolText(functionButtonWake));
  appendDiagnosticLine(out, "Setup button wake", boolText(setupButtonWake));
  appendDiagnosticLine(out, "Home button reset", boolText(homeButtonReset));
  appendDiagnosticLine(out, "Portal app-screen exit", boolText(portalAppScreenExit));
  appendDiagnosticLine(out, "Selected screen", appScreenText(selectedScreen));
  appendDiagnosticNumber(out, "Wake extra press count", wakeExtraPressCount);
  appendDiagnosticNumber(out, "Asset mode", (long)assetMode);
  appendDiagnosticLine(out, "Manual screen refresh", boolText(manualScreenRefresh));
  appendDiagnosticLine(out, "Manual external refresh", boolText(manualExternalDataRefresh));
  appendDiagnosticLine(out, "Pending frame drawn", boolText(pendingFrameDrawn));
  appendDiagnosticLine(out, "Pending frame fast mode", boolText(pendingFrameFastMode));
  appendDiagnosticLine(out, "Final frame fast mode", boolText(finalFrameFastMode));
  appendDiagnosticLine(out, "Final frame windowed", boolText(finalFrameWindowed));
  appendDiagnosticNumber(out, "Final frame window x", finalFrameWindowX);
  appendDiagnosticNumber(out, "Final frame window y", finalFrameWindowY);
  appendDiagnosticNumber(out, "Final frame window w", finalFrameWindowW);
  appendDiagnosticNumber(out, "Final frame window h", finalFrameWindowH);
  appendDiagnosticLine(out, "WiFi OK", boolText(wifiOK));
  appendDiagnosticNumber(out, "WiFi status", (long)lastWifiConnectStatus);
  appendDiagnosticNumber(out, "WiFi attempts", (long)lastWifiConnectAttempts);
  appendDiagnosticNumber(out, "WiFi elapsed ms", (long)lastWifiConnectElapsedMs);
  appendDiagnosticNumber(out, "WiFi RSSI", (long)lastWifiConnectRssi);
  appendDiagnosticLine(out, "WiFi AP kept", boolText(lastWifiConnectKeptPortalAp));
  appendDiagnosticLine(out, "NTP synced", boolText(ntpSynced));
  appendDiagnosticLine(out, "MQTT OK", boolText(mqttOK));
  appendDiagnosticLine(out, "Got BTC price", boolText(gotPriceValue));
  appendDiagnosticLine(out, "Got BTC balance", boolText(gotBalanceValue));
  appendDiagnosticLine(out, "Valid BTC inputs", boolText(hasValidBtcInputs));
  appendDiagnosticLine(out, "Fresh BTC inputs", boolText(hasFreshBtcInputs));
  appendDiagnosticLine(out, "Freedom data available", boolText(freedomDataAvailable));

  if (preferences.begin(DIAGNOSTICS_NAMESPACE, false)) {
    preferences.putString("app_diag", out);
    preferences.end();
  }

  Serial.println("[FreedomClock] Last app boot diagnostics");
  Serial.print(out);
  Serial.flush();
}

static void saveBootPathDiagnostics(
  esp_sleep_wakeup_cause_t wakeCause,
  esp_reset_reason_t resetReason,
  uint64_t ext1WakeStatus,
  SetupBootAction bootAction,
  const char* path,
  bool configLoaded = false,
  bool welcomeShown = false
) {
  if (!diagnosticsTrackingEnabled()) return;

  String out;
  out.reserve(520);
  appendDiagnosticLine(out, "Firmware", FIRMWARE_VERSION);
  appendDiagnosticLine(out, "Path", path ? path : "unknown");
  appendDiagnosticNumber(out, "Wake cause", (long)wakeCause);
  appendDiagnosticLine(out, "Wake cause label", wakeCauseText(wakeCause));
  appendDiagnosticNumber(out, "Reset reason", (long)resetReason);
  appendDiagnosticLine(out, "Reset reason label", resetReasonText(resetReason));
  appendDiagnosticHex64(out, "EXT1 wake status", ext1WakeStatus);
  appendDiagnosticLine(out, "Boot action", setupBootActionText(bootAction));
  appendDiagnosticLine(out, "Config loaded", boolText(configLoaded));
  appendConfigLoadDiagnostics(out);
  appendDiagnosticLine(out, "Welcome shown", boolText(welcomeShown));
  appendDiagnosticLine(out, "Function pin low", boolText(digitalRead(PIN_FUNCTION_BUTTON) == LOW));
  appendDiagnosticLine(out, "Setup pin low", boolText(digitalRead(PIN_SETUP_BUTTON) == LOW));

  if (preferences.begin(DIAGNOSTICS_NAMESPACE, false)) {
    preferences.putString("boot_diag", out);
    preferences.end();
  }

  Serial.println("[FreedomClock] Boot path diagnostics");
  Serial.print(out);
  Serial.flush();
}

static void savePortalDiagnostics(const char* event, const char* detail, bool configLoaded) {
  if (!diagnosticsTrackingEnabled()) return;

  String out;
  out.reserve(900);
  appendDiagnosticLine(out, "Firmware", FIRMWARE_VERSION);
  appendDiagnosticLine(out, "Event", event ? event : "unknown");
  appendDiagnosticLine(out, "Detail", detail ? detail : "");
  appendDiagnosticNumber(out, "Millis", (long)millis());
  appendDiagnosticLine(out, "Config loaded", boolText(configLoaded));
  appendConfigLoadDiagnostics(out);
  appendDiagnosticLine(out, "Device configured flag", boolText(deviceConfig.configured));
  appendDiagnosticLine(out, "Portal exit action", portalExitActionText(portalExitAction));
  appendDiagnosticLine(out, "Portal save requested", boolText(portalSaveRequested));
  appendDiagnosticLine(out, "RTC post-save restart pending", boolText(rtcPortalSaveRestartPending));
  appendDiagnosticLine(out, "Function pin low", boolText(digitalRead(PIN_FUNCTION_BUTTON) == LOW));
  appendDiagnosticLine(out, "Setup pin low", boolText(digitalRead(PIN_SETUP_BUTTON) == LOW));
  appendDiagnosticNumber(out, "WiFi mode", (long)WiFi.getMode());
  appendDiagnosticLine(out, "AP SSID", portalApSsid[0] ? portalApSsid : "(empty)");
  appendDiagnosticNumber(out, "AP SSID length", (long)strlen(portalApSsid));
  appendDiagnosticNumber(out, "AP password length", (long)strlen(portalApPassword));
  appendDiagnosticLine(out, "AP start failure", portalApStartFailureDetail[0] ? portalApStartFailureDetail : "none");
  appendDiagnosticLine(out, "SoftAP IP", WiFi.softAPIP().toString().c_str());
  appendDiagnosticNumber(out, "SoftAP station count", (long)WiFi.softAPgetStationNum());
  appendDiagnosticLine(out, "STA SSID configured", boolText(deviceConfig.wifiSsid[0] != '\0'));
  appendDiagnosticLine(out, "MQTT server configured", boolText(deviceConfig.mqttServer[0] != '\0'));

  if (preferences.begin(DIAGNOSTICS_NAMESPACE, false)) {
    preferences.putString("portal_diag", out);
    preferences.end();
  }

  Serial.println("[FreedomClock] Portal diagnostics");
  Serial.print(out);
  Serial.flush();
}

static String loadLastAppBootDiagnosticsText() {
  if (!diagnosticsTrackingEnabled()) {
    return "Developer diagnostics are disabled in this build.";
  }
  if (!preferences.begin(DIAGNOSTICS_NAMESPACE, true)) {
    return "Last app boot diagnostics unavailable: could not open NVS.";
  }
  String diagnostics = preferences.getString("app_diag", "No app boot diagnostics recorded yet.");
  preferences.end();
  return diagnostics;
}

static String loadLastBootPathDiagnosticsText() {
  if (!diagnosticsTrackingEnabled()) {
    return "Developer diagnostics are disabled in this build.";
  }
  if (!preferences.begin(DIAGNOSTICS_NAMESPACE, true)) {
    return "Last boot path diagnostics unavailable: could not open NVS.";
  }
  String diagnostics = preferences.getString("boot_diag", "No boot path diagnostics recorded yet.");
  preferences.end();
  return diagnostics;
}

static String loadLastPortalDiagnosticsText() {
  if (!diagnosticsTrackingEnabled()) {
    return "Developer diagnostics are disabled in this build.";
  }
  if (!preferences.begin(DIAGNOSTICS_NAMESPACE, true)) {
    return "Last setup portal diagnostics unavailable: could not open NVS.";
  }
  String diagnostics = preferences.getString("portal_diag", "No setup portal diagnostics recorded yet.");
  preferences.end();
  return diagnostics;
}

static void setPostSaveRestartPending(bool pending) {
  rtcPortalSaveRestartPending = pending;
  if (preferences.begin(DIAGNOSTICS_NAMESPACE, false)) {
    preferences.putBool("post_save", pending);
    preferences.end();
  }
}

static bool takePostSaveRestartPending() {
  bool pending = rtcPortalSaveRestartPending;
  if (preferences.begin(DIAGNOSTICS_NAMESPACE, false)) {
    pending = pending || preferences.getBool("post_save", false);
    preferences.putBool("post_save", false);
    preferences.end();
  }
  rtcPortalSaveRestartPending = false;
  return pending;
}
