#pragma once

static constexpr char LOADING_TEXT[] = "<loading>";

static int drawBatteryStatus(int deviceBatteryPct, DisplayThemeMode themeMode, bool showPercent = true) {
  int iconX;
  if (showPercent) {
    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", clampInt(deviceBatteryPct, 0, 100));
    const int textW = estimateTextWidthSize1(pctText);
    const int textX = DEVICE_DISPLAY_WIDTH - BATTERY_RIGHT_MARGIN - textW;
    iconX = textX - BATTERY_TEXT_GAP - BATTERY_BODY_W - BATTERY_TIP_W;
    display.setTextSize(1);
    display.setCursor(textX, BATTERY_TEXT_Y);
    display.print(pctText);
  } else {
    iconX = DEVICE_DISPLAY_WIDTH - BATTERY_RIGHT_MARGIN - BATTERY_BODY_W - BATTERY_TIP_W;
  }
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
  return iconX;
}

static constexpr int NO_WIFI_ICON_W = 14;
static constexpr int NO_WIFI_ICON_H = 12;
static constexpr int STATUS_ICON_GAP = 5;

static void drawNoWifiStatusIcon(int x, int y, uint16_t color, uint16_t background) {
  // Tiny Wi-Fi-with-slash icon, tuned manually for 1-bit e-ink.
  display.fillRect(x, y, NO_WIFI_ICON_W, NO_WIFI_ICON_H, background);

  display.drawLine(x + 3, y + 1, x + 10, y + 1, color);
  display.drawPixel(x + 2, y + 2, color);
  display.drawPixel(x + 11, y + 2, color);

  display.drawLine(x + 4, y + 4, x + 9, y + 4, color);
  display.drawPixel(x + 3, y + 5, color);
  display.drawPixel(x + 10, y + 5, color);

  display.drawPixel(x + 5, y + 7, color);
  display.drawPixel(x + 6, y + 6, color);
  display.drawPixel(x + 7, y + 6, color);
  display.drawPixel(x + 8, y + 7, color);

  display.fillRect(x + 6, y + 9, 2, 2, color);
  display.drawLine(x + 2, y + 1, x + 11, y + 10, color);
}

static void drawNoWifiStatusLeftOfBattery(int batteryIconX, DisplayThemeMode themeMode) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  const int iconX = batteryIconX - STATUS_ICON_GAP - NO_WIFI_ICON_W;
  if (iconX < 1) return;
  drawNoWifiStatusIcon(iconX, BATTERY_ICON_Y - 1, theme.foreground, theme.background);
}

static void drawFreedomClockLogoMark(int x, int y, int size, uint16_t color, uint16_t background) {
  if (size < 8) size = 8;

  const int cx = x + (size / 2);
  const int cy = y + (size / 2);
  int outerR = (size - 1) / 2;
  int innerR = (outerR * 63 + 50) / 100;
  if (outerR < 3) outerR = 3;
  if (innerR < 1) innerR = 1;
  if (innerR >= outerR) innerR = outerR - 1;

  display.fillCircle(cx, cy, outerR, color);
  display.fillCircle(cx, cy, innerR, background);

  // The mark points to the last five minutes before noon.
  display.fillTriangle(
    cx,
    cy,
    cx - (outerR / 2),
    cy - ((outerR * 87) / 100),
    cx,
    cy - outerR,
    color
  );
}

static int drawLogoTitleLine(const char* title, int x, int y, int textSize, DisplayThemeMode themeMode) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  const int textH = 8 * textSize;
  const int iconSize = textH + 8;
  const int iconY = y - ((iconSize - textH) / 2);
  const int gap = 6 * textSize;
  drawFreedomClockLogoMark(x, iconY, iconSize, theme.foreground, theme.background);
  const int textX = x + iconSize + gap;
  display.setTextSize(textSize);
  display.setCursor(textX, y);
  display.print(title ? title : "");
  return textX;
}

static void markWelcomeShown() {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) return;
  preferences.putBool("welcome_seen", true);
  preferences.end();
}

static void markWelcomePending() {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) return;
  preferences.putBool("welcome_seen", false);
  preferences.end();
}

static bool hasWelcomeBeenShown() {
  if (!preferences.begin(CONFIG_NAMESPACE, true)) return false;
  const bool seen = preferences.getBool("welcome_seen", false);
  preferences.end();
  return seen;
}

static void animateEllipsis(int x, int y, DisplayThemeMode themeMode);

static void preparePostFirmwareUpdateBoot() {
  if (!deviceConfig.configured) {
    markWelcomePending();
  }
}

static int drawBrandedStatusScreen(
  const char* title,
  const char* subtitle,
  bool animateSubtitleEllipsis
) {
  const DisplayThemeMode themeMode = statusScreenThemeMode();
  prepareScreen(themeMode);

  static constexpr int TITLE_SIZE = 2;
  static constexpr int TITLE_CHAR_W = 6 * TITLE_SIZE;
  static constexpr int TITLE_CHAR_H = 8 * TITLE_SIZE;
  static constexpr int TITLE_ICON_SIZE = TITLE_CHAR_H + 8;
  static constexpr int TITLE_ICON_GAP = TITLE_CHAR_W;
  const int titleLen = title ? (int)strlen(title) : 0;
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  const int centerY = DEVICE_DISPLAY_HEIGHT / 2;
  const int titleTextW = titleLen * TITLE_CHAR_W;
  const int titleGroupW = TITLE_ICON_SIZE + TITLE_ICON_GAP + titleTextW;
  const int titleX = (DEVICE_DISPLAY_WIDTH - titleGroupW) / 2;
  const int titleY = centerY - TITLE_CHAR_H - 4;
  const int titleIconY = titleY - ((TITLE_ICON_SIZE - TITLE_CHAR_H) / 2);
  drawFreedomClockLogoMark(titleX, titleIconY, TITLE_ICON_SIZE, theme.foreground, theme.background);

  display.setTextSize(TITLE_SIZE);
  const int titleTextX = titleX + TITLE_ICON_SIZE + TITLE_ICON_GAP;
  display.setCursor(titleTextX, titleY);
  display.print(title ? title : "");

  display.setTextSize(1);
  const int subtitleY = centerY + 4;
  display.setCursor(titleTextX, subtitleY);
  display.print(subtitle ? subtitle : "");

  display.update();
  if (animateSubtitleEllipsis && subtitle && subtitle[0] != '\0') {
    animateEllipsis(titleTextX + ((int)strlen(subtitle) * 6), subtitleY, themeMode);
  }
  return titleTextX;
}

static void drawWelcomeScreen() {
  drawBrandedStatusScreen("FREEDOM CLOCK", "Press any button to begin", false);
}

static void goToWelcomeSleep() {
  rtc_gpio_pullup_en((gpio_num_t)PIN_FUNCTION_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_FUNCTION_BUTTON);
  rtc_gpio_pullup_en((gpio_num_t)PIN_SETUP_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_SETUP_BUTTON);
  esp_sleep_enable_ext1_wakeup_io(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
#ifdef ARDUINO
  Serial.flush();
#endif
  esp_deep_sleep_start();
}

static void drawPortalScreen(
  const char* title,
  const char* line1,
  const char* line2,
  const char* line3,
  const char* line4,
  bool showPortalUrl = true
) {
  const DisplayThemeMode themeMode = statusScreenThemeMode();
  prepareScreen(themeMode);
  drawLogoTitleLine(title ? title : "SETUP", 9, 9, 2, themeMode);

  display.setTextSize(1);
  display.setCursor(9, 34);
  display.print(line1 ? line1 : "");
  display.setCursor(9, 48);
  display.print(line2 ? line2 : "");
  display.setCursor(9, 68);
  display.print(line3 ? line3 : "");
  display.setCursor(9, 82);
  display.print(line4 ? line4 : "");
  if (showPortalUrl) {
    display.setCursor(9, 106);
    display.print("Open http://192.168.4.1 in a browser.");
  }
  display.update();
}

static void portalQrRenderCallback(esp_qrcode_handle_t qrcode) {
  static constexpr int QUIET_PX = 4;
  const int modules = esp_qrcode_get_size(qrcode);
  // Fit the QR code within the display height leaving a small top/bottom margin.
  int modulePx = (DEVICE_DISPLAY_HEIGHT - 10 - 2 * QUIET_PX) / modules;
  if (modulePx < 2) modulePx = 2;
  if (modulePx > 4) modulePx = 4;
  const int contentPx = modules * modulePx;
  const int totalPx = contentPx + 2 * QUIET_PX;
  const int originX = DEVICE_DISPLAY_WIDTH - totalPx - 14;
  const int originY = (DEVICE_DISPLAY_HEIGHT - totalPx) / 2;
  display.fillRect(originX, originY, totalPx, totalPx, WHITE);
  for (int row = 0; row < modules; row++) {
    for (int col = 0; col < modules; col++) {
      if (esp_qrcode_get_module(qrcode, col, row)) {
        display.fillRect(
          originX + QUIET_PX + col * modulePx,
          originY + QUIET_PX + row * modulePx,
          modulePx, modulePx, BLACK
        );
      }
    }
  }
}

static void drawSetupPortalReadyScreen() {
  const DisplayThemeMode themeMode = statusScreenThemeMode();
  prepareScreen(themeMode);

  drawLogoTitleLine("SETUP", 9, 9, 2, themeMode);

  display.setTextSize(1);
  display.setCursor(9, 40);
  display.print("Scan QR to join Wi-Fi,");
  display.setCursor(9, 54);
  display.print("then open 192.168.4.1");

  static constexpr int LABEL_X = 9;
  static constexpr int VALUE_X = 51;
#if DEVICE_PROFILE_E213
  display.setCursor(LABEL_X, 80);
  display.print("Wi-Fi:");
  display.setCursor(LABEL_X, 94);
  display.print(portalApSsid);
  display.setCursor(LABEL_X, 108);
  display.print("PW:");
  display.setCursor(VALUE_X, 108);
  display.print(portalApPassword);
#else
  display.setCursor(LABEL_X, 80);
  display.print("Wi-Fi:");
  display.setCursor(VALUE_X, 80);
  display.print(portalApSsid);
  display.setCursor(LABEL_X, 94);
  display.print("PW:");
  display.setCursor(VALUE_X, 94);
  display.print(portalApPassword);
#endif

  char qrData[96];
  snprintf(qrData, sizeof(qrData), "WIFI:T:WPA;S:%s;P:%s;;", portalApSsid, portalApPassword);
  esp_qrcode_config_t qrCfg = {
    .display_func = portalQrRenderCallback,
    .max_qrcode_version = 5,
    .qrcode_ecc_level = ESP_QRCODE_ECC_LOW
  };
  esp_qrcode_generate(&qrCfg, qrData);

  display.update();
}

// Adds 3 dots one by one using fastmode.
// Call AFTER a full display.update() so the controller has the base image.
// Avoid a small setWindow() here; on dark screens the E290 can show the
// partial-window boundaries as horizontal artefacts around the dots.
// Sequence: "." appears immediately, then ".." after 0.5 s, then "..." after another 0.5 s.
static void animateEllipsis(int x, int y, DisplayThemeMode themeMode) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  display.fastmodeOn(false);
  for (int i = 0; i < 3; i++) {
    display.setTextSize(1);
    display.setTextColor(theme.foreground);
    display.setCursor(x + (i * 6), y);
    display.print(".");
    display.update();
    if (i < 2) delay(500);
  }
  display.fastmodeOff();
  display.setTextColor(theme.foreground, theme.background);
}

static void drawSetupPortalSavedScreen() {
  drawBrandedStatusScreen("SETTINGS SAVED", "Restarting ", true);
}

static void drawSetupPortalFirmwareUpdatedScreen() {
  drawBrandedStatusScreen("SOFTWARE UPDATED", "Restarting ", true);
}

static void drawSetupPortalErrorScreen(
  const char* reason,
  const char* detail1 = "",
  const char* detail2 = "",
  const char* detail3 = ""
) {
  const DisplayThemeMode themeMode = statusScreenThemeMode();
  prepareScreen(themeMode);
  drawLogoTitleLine("SETUP ERROR", 9, 9, 2, themeMode);

  display.setTextSize(1);
  display.setCursor(9, 42);
  display.print(reason && reason[0] ? reason : "Could not start setup");
  display.setCursor(9, 62);
  display.print(detail1 ? detail1 : "");
  display.setCursor(9, 82);
  display.print(detail2 ? detail2 : "");
  display.setCursor(9, 96);
  display.print(detail3 ? detail3 : "");
  display.update();
}

static void drawSetupPortalResetScreen() {
  drawBrandedStatusScreen("FACTORY RESET", "Restarting ", true);
}

// ============================================================
// Quote of the day utilities
// ============================================================

static constexpr int QUOTE_MASK_BYTES = (QUOTE_DB_COUNT + 7) / 8;

static void loadQuoteMask(uint8_t* mask) {
  memset(mask, 0, QUOTE_MASK_BYTES);
  if (!preferences.begin(QUOTE_NAMESPACE, true)) return;
  const int stored = (int)preferences.getUInt("q_total", 0);
  if (stored == QUOTE_DB_COUNT) {
    preferences.getBytes("q_mask", mask, QUOTE_MASK_BYTES);
  }
  preferences.end();
}

static void saveQuoteMask(const uint8_t* mask) {
  if (!preferences.begin(QUOTE_NAMESPACE, false)) return;
  preferences.putUInt("q_total", (uint32_t)QUOTE_DB_COUNT);
  preferences.putBytes("q_mask", mask, QUOTE_MASK_BYTES);
  preferences.end();
}

static int selectNextQuote() {
  uint8_t mask[QUOTE_MASK_BYTES];
  loadQuoteMask(mask);

  // Collect unused indices
  int unused[QUOTE_DB_COUNT];
  int unusedCount = 0;
  for (int i = 0; i < QUOTE_DB_COUNT; i++) {
    if (!(mask[i / 8] & (1 << (i % 8)))) unused[unusedCount++] = i;
  }
  // All used — reset
  if (unusedCount == 0) {
    memset(mask, 0, QUOTE_MASK_BYTES);
    for (int i = 0; i < QUOTE_DB_COUNT; i++) unused[unusedCount++] = i;
  }
  const int pick = unused[esp_random() % (uint32_t)unusedCount];
  mask[pick / 8] |= (1 << (pick % 8));
  saveQuoteMask(mask);
  return pick;
}

// ============================================================
// Word-wrap text drawing helper
// ============================================================

static void normalizeDisplayText(const char* input, char* output, size_t outputSize) {
  if (!output || outputSize == 0) return;
  size_t out = 0;
  if (!input) {
    output[0] = '\0';
    return;
  }

  for (size_t i = 0; input[i] != '\0' && out + 1 < outputSize;) {
    const uint8_t c = (uint8_t)input[i];
    if (c < 0x80) {
      output[out++] = input[i++];
      continue;
    }

    const uint8_t c1 = (uint8_t)input[i + 1];
    const uint8_t c2 = (uint8_t)input[i + 2];
    if (c == 0xC2 && c1 == 0xA0) {
      output[out++] = ' ';
      i += 2;
    } else if (c == 0xE2 && c1 == 0x80 && (c2 == 0x93 || c2 == 0x94)) {
      output[out++] = '-';
      i += 3;
    } else if (c == 0xE2 && c1 == 0x80 && (c2 == 0x98 || c2 == 0x99)) {
      output[out++] = '\'';
      i += 3;
    } else if (c == 0xE2 && c1 == 0x80 && (c2 == 0x9C || c2 == 0x9D)) {
      output[out++] = '"';
      i += 3;
    } else if (c == 0xE2 && c1 == 0x80 && c2 == 0xA6) {
      if (out + 3 >= outputSize) break;
      output[out++] = '.';
      output[out++] = '.';
      output[out++] = '.';
      i += 3;
    } else {
      output[out++] = '?';
      i++;
    }
  }
  output[out] = '\0';
}

static int drawWrappedText(const char* text, int x, int y, int maxCharsPerLine, int lineHeight, int maxLines = 20) {
  int pos = 0;
  int len = (int)strlen(text);
  int linesDrawn = 0;
  while (pos < len && linesDrawn < maxLines) {
    int remaining = len - pos;
    if (remaining <= maxCharsPerLine) {
      display.setCursor(x, y + linesDrawn * lineHeight);
      display.print(text + pos);
      linesDrawn++;
      break;
    }
    int breakAt = pos + maxCharsPerLine;
    while (breakAt > pos && text[breakAt] != ' ') breakAt--;
    if (breakAt == pos) breakAt = pos + maxCharsPerLine;
    char buf[64];
    int segLen = min(breakAt - pos, (int)sizeof(buf) - 1);
    memcpy(buf, text + pos, segLen);
    buf[segLen] = '\0';
    display.setCursor(x, y + linesDrawn * lineHeight);
    display.print(buf);
    linesDrawn++;
    pos = breakAt;
    if (pos < len && text[pos] == ' ') pos++;
  }
  return linesDrawn;
}

static int countWrappedLines(const char* text, int maxCharsPerLine) {
  int pos = 0;
  const int len = (int)strlen(text);
  int lines = 0;
  while (pos < len) {
    const int remaining = len - pos;
    if (remaining <= maxCharsPerLine) { lines++; break; }
    int breakAt = pos + maxCharsPerLine;
    while (breakAt > pos && text[breakAt] != ' ') breakAt--;
    if (breakAt == pos) breakAt = pos + maxCharsPerLine;
    lines++;
    pos = (text[breakAt] == ' ') ? breakAt + 1 : breakAt;
  }
  return lines;
}

static int drawCenteredWrappedText(const char* text, int y, int maxCharsPerLine, int lineHeight, int maxLines, int textSize) {
  char lineBuf[8][64];
  int lineCount = 0;
  int pos = 0;
  const int len = (int)strlen(text);
  const int limit = (maxLines < 8) ? maxLines : 8;
  while (pos < len && lineCount < limit) {
    const int remaining = len - pos;
    int segLen, nextPos;
    if (remaining <= maxCharsPerLine) {
      segLen = remaining;
      nextPos = len;
    } else {
      int breakAt = pos + maxCharsPerLine;
      while (breakAt > pos && text[breakAt] != ' ') breakAt--;
      if (breakAt == pos) breakAt = pos + maxCharsPerLine;
      segLen = breakAt - pos;
      nextPos = (text[breakAt] == ' ') ? breakAt + 1 : breakAt;
    }
    while (segLen > 0 && text[pos + segLen - 1] == ' ') segLen--;
    const int copyLen = (segLen < 63) ? segLen : 63;
    memcpy(lineBuf[lineCount], text + pos, copyLen);
    lineBuf[lineCount][copyLen] = '\0';
    lineCount++;
    pos = nextPos;
  }
  display.setTextSize(textSize);
  const int charW = 6 * textSize;
  for (int i = 0; i < lineCount; i++) {
    const int linePixW = (int)strlen(lineBuf[i]) * charW;
    const int x = (DEVICE_DISPLAY_WIDTH - linePixW) / 2;
    display.setCursor((x < 1) ? 1 : x, y + i * lineHeight);
    display.print(lineBuf[i]);
  }
  return lineCount;
}

// ============================================================
// Auto-update status screen
// ============================================================

static void drawAutoUpdateScreen() {
  drawBrandedStatusScreen("SOFTWARE UPDATE", "Updating software ", true);
}

static constexpr uint8_t FREEDOM_ICON_W = 10;
static constexpr uint8_t FREEDOM_ICON_H = 8;
static constexpr uint8_t LIFE_ICON_W = 9;
static constexpr uint8_t LIFE_ICON_H = 8;

static void drawFreedomIcon(int x, int y, uint16_t color) {
  // 10x8 rocket silhouette restored from v2026.05.11.1.
  display.fillRect(x + 4, y + 0, 2, 1, color);
  display.fillRect(x + 3, y + 1, 4, 1, color);
  display.fillRect(x + 2, y + 2, 6, 1, color);
  display.fillRect(x + 2, y + 3, 6, 1, color);
  display.fillRect(x + 2, y + 4, 6, 1, color);
  display.fillRect(x + 1, y + 5, 8, 1, color);
  display.fillRect(x + 0, y + 6, 2, 1, color);
  display.fillRect(x + 3, y + 6, 4, 1, color);
  display.fillRect(x + 8, y + 6, 2, 1, color);
  display.fillRect(x + 4, y + 7, 2, 1, color);
}

static void drawLifeIcon(int x, int y, uint16_t color) {
  // Filled heart restored from v2026.05.11.1.
  static constexpr uint8_t rowStart[LIFE_ICON_H] = {1, 0, 0, 0, 1, 2, 3, 4};
  static constexpr uint8_t rowWidth[LIFE_ICON_H] = {3, 9, 9, 9, 7, 5, 3, 1};

  for (uint8_t row = 0; row < LIFE_ICON_H; row++) {
    display.fillRect(x + rowStart[row], y + row, rowWidth[row], 1, color);
    if (row == 0) {
      display.fillRect(x + 5, y + row, 3, 1, color);
    }
  }
}

static void drawWarningTriangleIcon(int x, int y, uint16_t color, uint16_t background) {
  display.fillTriangle(x + 6, y, x, y + 12, x + 12, y + 12, color);
  display.fillRect(x + 6, y + 4, 1, 5, background);
  display.drawPixel(x + 6, y + 10, background);
}

static void drawSmallWarningTriangleIcon(int x, int y, uint16_t color, uint16_t background) {
  display.fillTriangle(x + 4, y, x, y + 8, x + 8, y + 8, color);
  display.fillRect(x + 4, y + 3, 1, 3, background);
  display.drawPixel(x + 4, y + 7, background);
}

// ============================================================
// Quote of the day screen
// ============================================================

static void drawQuoteScreen(
  int quoteIndex,
  int freedomYears, int freedomMonths, int freedomWeeks,
  const LifeStats& lifeStats,
  DisplayThemeMode themeMode,
  int deviceBatteryPct,
  int coveredPercent,
  uint8_t timeDisplayFormat = 0,
  bool freedomHitCap = false,
  bool showBatteryPercent = true,
  bool showFreedomDataWarning = false,
  bool freedomDataAvailable = true,
  bool showNoWifiIcon = false,
  bool externalDataPending = false
) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  prepareScreen(themeMode);
  const int batteryIconX = drawBatteryStatus(deviceBatteryPct, themeMode, showBatteryPercent);
  if (showNoWifiIcon) {
    drawNoWifiStatusLeftOfBattery(batteryIconX, themeMode);
  }

  const QuoteEntry& q = QUOTE_DB[clampInt(quoteIndex, 0, QUOTE_DB_COUNT - 1)];
  char quoteText[384];
  normalizeDisplayText(q.text, quoteText, sizeof(quoteText));

  static constexpr int LEFT_X     = 9;
  static constexpr int MARGIN_X   = 9;
  static constexpr int TOP_Y_S1   = 21;
  static constexpr int TOP_Y_S2   = 14;

#if DEVICE_PROFILE_E290
  static constexpr int CHARS_S1 = 46;
  static constexpr int CHARS_S2 = 24;
#else
  static constexpr int CHARS_S1 = 39;
  static constexpr int CHARS_S2 = 19;
#endif

  // Bottom strip: separator + one freedom-info line
  const int FREEDOM_Y = DEVICE_DISPLAY_HEIGHT - 8 - 9;
  const int SEP_Y     = FREEDOM_Y - 7;
  const int DRAW_SEP_Y = SEP_Y + 2;
  const int FOOTER_TEXT_H = 8;
  const int FOOTER_LINE_GAP = FREEDOM_Y - DRAW_SEP_Y;
  const int AUTHOR_Y = DRAW_SEP_Y - FOOTER_LINE_GAP - FOOTER_TEXT_H;

  // Choose the largest font size where the quote and author still fit. Size 2
  // uses a 14px active glyph height plus at least 1px of air between wrapped
  // lines; without that, adjacent pixel rows can visually merge on e-ink.
  const int linesAtS2 = countWrappedLines(quoteText, CHARS_S2);
  const int glyphHAtS2 = 14;
  const int lineHAtS2 = glyphHAtS2 + 1;
  const int quoteAuthorGap = 5;
  const bool useSize2 =
    linesAtS2 > 0
    && (TOP_Y_S2 + ((linesAtS2 - 1) * lineHAtS2) + glyphHAtS2 + quoteAuthorGap) <= AUTHOR_Y;
  const int textSize     = useSize2 ? 2 : 1;
  const int topY         = useSize2 ? TOP_Y_S2 : TOP_Y_S1;
  const int lineH        = useSize2 ? lineHAtS2 : 9;
  const int charsPerLine = useSize2 ? CHARS_S2 : CHARS_S1;
  const int quoteAvailH  = SEP_Y - topY - 10;
  const int maxQuoteLines = (quoteAvailH / lineH > 0) ? quoteAvailH / lineH : 1;

  // Quote — left-aligned, below battery
  display.setTextSize(textSize);
  const int linesUsed = drawWrappedText(
    quoteText, LEFT_X, topY, charsPerLine, lineH, maxQuoteLines);

  // Author — always pinned to bottom-right, just above the separator line
  if (q.author && q.author[0]) {
    display.setTextSize(1);
    const int authorW = (int)strlen(q.author) * 6;
    const int authorX = DEVICE_DISPLAY_WIDTH - authorW - MARGIN_X;
    display.setCursor((authorX < LEFT_X) ? LEFT_X : authorX, AUTHOR_Y);
    display.print(q.author);
  }

  // Separator line
  display.drawLine(MARGIN_X, DRAW_SEP_Y, DEVICE_DISPLAY_WIDTH - MARGIN_X - 1, DRAW_SEP_Y, theme.foreground);

  // Bottom strip: three-column layout
  //   Left:   [bird] [freedom value]  — left-aligned from MARGIN_X
  //   Center: [coverage %]            — centered on display
  //   Right:  [heart] [life value]    — right-aligned to MARGIN_X from right edge
  char freedomBuf[24];
  char lifeBuf[24];
  char coverageBuf[12];
  if (externalDataPending && !freedomDataAvailable) {
    safeCopyCString(freedomBuf, sizeof(freedomBuf), LOADING_TEXT);
  } else if (!freedomDataAvailable) {
    safeCopyCString(freedomBuf, sizeof(freedomBuf), "N/A");
  } else if (freedomHitCap) {
    safeCopyCString(freedomBuf, sizeof(freedomBuf), "FOREVER");
  } else if (timeDisplayFormat == 1) {
    const int totalFreedomWeeks = freedomYears * 52 + freedomMonths * 4 + freedomWeeks;
    snprintf(freedomBuf, sizeof(freedomBuf), "%dw", totalFreedomWeeks);
  } else {
    snprintf(freedomBuf, sizeof(freedomBuf), "%dY %dM %dW",
      freedomYears, freedomMonths, freedomWeeks);
  }
  if (timeDisplayFormat == 1) {
    snprintf(lifeBuf, sizeof(lifeBuf), "%s%dw",
      lifeStats.pastExpectancy ? "+" : "", lifeStats.remainingWeeks);
  } else {
    snprintf(lifeBuf, sizeof(lifeBuf), "%s%dY %dM %dW",
      lifeStats.pastExpectancy ? "+" : "",
      lifeStats.yearsLeft, lifeStats.monthsLeft, lifeStats.weeksLeftRemainder);
  }
  if (externalDataPending && !freedomDataAvailable) {
    safeCopyCString(coverageBuf, sizeof(coverageBuf), LOADING_TEXT);
  } else if (!freedomDataAvailable) {
    safeCopyCString(coverageBuf, sizeof(coverageBuf), "N/A");
  } else if (!freedomHitCap && !lifeStats.pastExpectancy) {
    snprintf(coverageBuf, sizeof(coverageBuf), "%d%%", coveredPercent);
  } else {
    coverageBuf[0] = '\0';
  }
  display.setTextSize(1);
  {
    static constexpr int GAP      = 6;

    // Left: freedom icon then freedom value, left-aligned
    drawFreedomIcon(MARGIN_X, FREEDOM_Y, theme.foreground);
    if (freedomBuf[0]) {
      display.setCursor(MARGIN_X + FREEDOM_ICON_W + GAP, FREEDOM_Y);
      display.print(freedomBuf);
    }
    if (showFreedomDataWarning && freedomBuf[0]) {
      drawSmallWarningTriangleIcon(display.getCursorX() + 4, FREEDOM_Y, theme.foreground, theme.background);
    }

    // Center: coverage percentage (omitted when freedom is infinite)
    if (coverageBuf[0]) {
      const int coverageW = (int)strlen(coverageBuf) * 6;
      display.setCursor((DEVICE_DISPLAY_WIDTH - coverageW) / 2, FREEDOM_Y);
      display.print(coverageBuf);
    }

    // Right: heart icon then life value, right-aligned to display edge
    const int lifeTextW   = (int)strlen(lifeBuf) * 6;
    const int totalRightW = LIFE_ICON_W + GAP + lifeTextW;
    const int heartX      = DEVICE_DISPLAY_WIDTH - MARGIN_X - totalRightW;
    drawLifeIcon(heartX, FREEDOM_Y, theme.foreground);
    display.setCursor(heartX + LIFE_ICON_W + GAP, FREEDOM_Y);
    display.print(lifeBuf);
  }

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
  int deviceBatteryPct,
  uint8_t timeDisplayFormat = 0,
  bool showBatteryPercent = true,
  bool showFreedomDataWarning = false,
  bool freedomDataAvailable = true,
  bool showNoWifiIcon = false,
  bool externalDataPending = false
) {
  const DisplayThemeColors theme = getDisplayThemeColors(themeMode);
  char freedomTitle[40];
  char lifeTitle[32];
  char percentText[12];
  char freedomTitleLong[48];
  char freedomTitleMedium[40];
  char freedomTitleShort[32];
  const char* safeOwnerName = hasText(ownerName) ? ownerName : DEFAULT_OWNER_NAME;
  const size_t ownerNameLen = strlen(safeOwnerName);
#if DEVICE_PROFILE_E290
  const int LEFT_X = 9;
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
  const int BOTTOM_NUMBER_Y = 85;
  const int BOTTOM_SUFFIX_Y = 90;
  const int RIGHT_TITLE_Y = 36;
  const int RIGHT_VALUE_Y = 66;
  const int RIGHT_TITLE_X = RIGHT_X + 4;
  const int RIGHT_VALUE_X = RIGHT_X + 8;
#else
  const int LEFT_X = 5;
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
  const int BOTTOM_NUMBER_Y = 81;
  const int BOTTOM_SUFFIX_Y = 86;
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

  if (lifeStats.pastExpectancy) {
    safeCopyCString(lifeTitle, sizeof(lifeTitle), "BEYOND LIFE EXPECTANCY");
  } else {
    safeCopyCString(lifeTitle, sizeof(lifeTitle), "EXPECTED LIFETIME LEFT");
  }
  const bool percentIsLoading = externalDataPending && !freedomDataAvailable;
  if (percentIsLoading) {
    safeCopyCString(percentText, sizeof(percentText), LOADING_TEXT);
  } else if (!freedomDataAvailable) {
    safeCopyCString(percentText, sizeof(percentText), "N/A");
  } else if (lifeStats.pastExpectancy) {
    safeCopyCString(percentText, sizeof(percentText), "N/A");
  } else if (coveredInfinite) {
    safeCopyCString(percentText, sizeof(percentText), "INF");
  } else {
    snprintf(percentText, sizeof(percentText), "%d%%", clampInt(coveredPercent, 0, 999));
  }

  prepareScreen(themeMode);

  display.setTextSize(1);
  display.setCursor(LEFT_X, 9);
  display.println(freedomTitle);

  const int batteryIconX = drawBatteryStatus(deviceBatteryPct, themeMode, showBatteryPercent);
  if (showNoWifiIcon) {
    drawNoWifiStatusLeftOfBattery(batteryIconX, themeMode);
  }

  display.drawLine(DIVIDER_X, 13, DIVIDER_X, 116, theme.foreground);

  int x = 0;
  int freedomWarningX = -1;
  if (externalDataPending && !freedomDataAvailable) {
    display.setTextSize(2);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y + 5);
    display.print(LOADING_TEXT);
    freedomWarningX = display.getCursorX();
  } else if (!freedomDataAvailable) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print("N/A");
    freedomWarningX = display.getCursorX();
  } else if (freedomHitCap) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print("FOREVER");
    freedomWarningX = display.getCursorX() + 4;
  } else if (freedomYears > 99) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print(freedomYears);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("Y");
    freedomWarningX = display.getCursorX() + 3;
  } else if (timeDisplayFormat == 1) {
    const int totalWeeks = freedomYears * 52 + freedomMonths * 4 + freedomWeeks;
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, TOP_NUMBER_Y);
    display.print(totalWeeks);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, TOP_SUFFIX_Y);
    display.print("W");
    freedomWarningX = display.getCursorX() + 3;
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
    freedomWarningX = display.getCursorX() + 3;
  }

  if (showFreedomDataWarning && !externalDataPending) {
    const int warningGap = freedomDataAvailable ? 15 : 9;
    int warningX = freedomWarningX + warningGap;
    if (warningX < NUMBER_LEFT_X) warningX = NUMBER_LEFT_X;
    if (warningX > (DIVIDER_X - 15)) warningX = DIVIDER_X - 15;
    drawWarningTriangleIcon(warningX, TOP_SUFFIX_Y + 2, theme.foreground, theme.background);
  }

  display.drawLine(LEFT_X, MID_LINE_Y, DIVIDER_X - 8, MID_LINE_Y, theme.foreground);

  display.setTextSize(1);
  display.setCursor(LEFT_X, LIFE_TITLE_Y);
  display.print(lifeTitle);

  if (timeDisplayFormat == 1) {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, BOTTOM_NUMBER_Y);
    if (lifeStats.pastExpectancy) display.print('+');
    display.print(lifeStats.remainingWeeks);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, BOTTOM_SUFFIX_Y);
    display.print("W");
  } else {
    display.setTextSize(3);
    display.setCursor(NUMBER_LEFT_X, BOTTOM_NUMBER_Y);
    if (lifeStats.pastExpectancy) {
      display.print('+');
    } else {
      if (lifeStats.yearsLeft < 10) display.print('0');
    }
    display.print(lifeStats.yearsLeft);
    display.setTextSize(2);
    x = display.getCursorX();
    display.setCursor(x, BOTTOM_SUFFIX_Y);
    display.print("Y");

    display.setCursor(MONTH_X, BOTTOM_NUMBER_Y);
    display.setTextSize(3);
    if (!lifeStats.pastExpectancy && lifeStats.monthsLeft < 10) display.print('0');
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
  }

  display.setTextSize(1);
  display.setCursor(RIGHT_TITLE_X, RIGHT_TITLE_Y);
  display.print("FREEDOM");
  display.setCursor(RIGHT_TITLE_X, RIGHT_TITLE_Y + 10);
  display.print("COVERAGE");

  if (percentText[0]) {
    display.setTextSize(percentIsLoading ? 1 : 2);
    display.setCursor(RIGHT_VALUE_X, RIGHT_VALUE_Y);
    display.print(percentText);
  }
  display.update();
}

static void drawInfoScreen(
  const DeviceConfig& cfg,
  float wealthValue,
  float balanceBtc,
  float priceValue,
  int deviceBatteryPct,
  time_t now,
  bool wealthDataAvailable = true,
  bool showNoWifiIcon = false,
  bool externalDataPending = false
) {
  char value[24];
  char compactValue[18];
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);
  const AssetMode assetMode = sanitizeAssetMode(cfg.assetMode);
  const PortfolioUseMode portfolioUseMode = sanitizePortfolioUseMode(cfg.portfolioUseMode);
  const char* currencyLabel = currencyCodeLabel(cfg.currencyCode);
  static constexpr int TITLE_X = 9;
  static constexpr int TITLE_Y = 13;
  static constexpr int LABEL_X = 11;
  static constexpr int VALUE_X = 142;
  static constexpr int ROW_Y0 = 29;
  static constexpr int ROW_STEP = 9;

  prepareScreen(themeMode);

  display.setTextSize(1);
  const int batteryIconX = drawBatteryStatus(deviceBatteryPct, themeMode, cfg.showBatteryPercent);
  if (showNoWifiIcon) {
    drawNoWifiStatusLeftOfBattery(batteryIconX, themeMode);
  }

  display.setTextSize(1);
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("SETTINGS");

  auto drawSettingsRow = [&](uint8_t row, const char* label, const char* rowValue) {
    const int rowY = ROW_Y0 + (ROW_STEP * (int)row);
    display.setTextSize(1);
    display.setCursor(LABEL_X, rowY);
    display.print(label ? label : "");
    display.setCursor(VALUE_X, rowY);
    display.print(rowValue ? rowValue : "");
  };

  display.setCursor(LABEL_X, ROW_Y0 + (ROW_STEP * 0));
  if (assetMode == ASSET_MODE_WEALTH) {
    drawSettingsRow(0, "ASSET TYPE:", "WEALTH");

    formatCompactValue(wealthValue, compactValue, sizeof(compactValue));
    snprintf(value, sizeof(value), "%s %s", compactValue, currencyLabel);
    drawSettingsRow(1, "NET WORTH:", value);
  } else {
    if (isManualBtcAssetMode(assetMode) && externalDataPending) {
      snprintf(value, sizeof(value), "~%.4f", cfg.manualBtcAmount);
    } else if (wealthDataAvailable) {
      snprintf(value, sizeof(value), "~%.4f", balanceBtc);
    } else if (externalDataPending) {
      safeCopyCString(value, sizeof(value), LOADING_TEXT);
    } else {
      safeCopyCString(value, sizeof(value), "N/A");
    }
    drawSettingsRow(0, "BTC AMOUNT:", value);

    if (wealthDataAvailable) {
      snprintf(value, sizeof(value), "%.0f %s", priceValue, currencyLabel);
    } else if (externalDataPending) {
      safeCopyCString(value, sizeof(value), LOADING_TEXT);
    } else {
      safeCopyCString(value, sizeof(value), "N/A");
    }
    drawSettingsRow(1, "BTC PRICE:", value);
  }

  snprintf(value, sizeof(value), "%.1f%%", cfg.wealthGrowthAnnual * 100.0f);
  drawSettingsRow(2, "PORTFOLIO GROWTH:", value);

  snprintf(value, sizeof(value), "%.1f%%", cfg.inflationAnnual * 100.0f);
  drawSettingsRow(3, "INFLATION:", value);

  formatCompactValue(cfg.monthlyExpenseValue, compactValue, sizeof(compactValue));
  snprintf(value, sizeof(value), "%s %s", compactValue, currencyLabel);
  drawSettingsRow(4, "MONTHLY EXPENSES:", value);

  formatCompactValue(cfg.monthlyIncomeValue, compactValue, sizeof(compactValue));
  snprintf(value, sizeof(value), "%s %s", compactValue, currencyLabel);
  drawSettingsRow(5, "MONTHLY INCOME:", value);

  snprintf(value, sizeof(value), "~%d Y", currentAgeFromBirthYear(cfg.birthYear, now));
  drawSettingsRow(6, "AGE NOW:", value);

  snprintf(value, sizeof(value), "%d Y", cfg.lifeExpectancyYears);
  drawSettingsRow(7, "LIFE EXPECTANCY:", value);

  if (portfolioUseMode == PORTFOLIO_USE_MODE_BORROW) {
    snprintf(value, sizeof(value), "BORROW YEARLY (%.1f%%/Y)", cfg.borrowFeeAnnual * 100.0f);
  } else {
    snprintf(value, sizeof(value), "%s", portfolioUseModeLabel(portfolioUseMode));
  }
  drawSettingsRow(8, "SPEND MODE:", value);

  drawSettingsRow(9, "FIRMWARE:", FIRMWARE_VERSION);

  display.update();
}

static void drawWealthStatsScreen(
  const DeviceConfig& cfg,
  float wealthValue,
  float balanceBtc,
  float priceValue,
  int deviceBatteryPct,
  const WealthHistory& history,
  bool wealthDataAvailable = true,
  bool showNoWifiIcon = false,
  bool externalDataPending = false
) {
  struct WealthPeriod {
    const char* shortLabel;
    uint16_t daysAgo;
  };

  static const WealthPeriod periods[] = {
    { "1D", 1 },
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
  const bool showBtcBreakdown = isAnyBtcAssetMode(assetMode);
  const int32_t currentWealthRounded = (wealthValue >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(wealthValue + 0.5f);
  const int32_t currentPriceRounded = (priceValue >= (float)INT32_MAX)
    ? INT32_MAX
    : (int32_t)(priceValue + 0.5f);
  const int64_t currentBalanceSats = (int64_t)(balanceBtc * 100000000.0 + 0.5);
  static constexpr int TITLE_X = 9;
  static constexpr int ROW_LABEL_X = 11;
  static constexpr int TITLE_Y = 13;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 44;
  static constexpr int ROW_STEP = 13;

  prepareScreen(themeMode);

  const int batteryIconX = drawBatteryStatus(deviceBatteryPct, themeMode, cfg.showBatteryPercent);
  if (showNoWifiIcon) {
    drawNoWifiStatusLeftOfBattery(batteryIconX, themeMode);
  }

  display.setTextSize(1);
  if (wealthDataAvailable) {
    formatCompactValue(wealthValue, currentWealth, sizeof(currentWealth));
  } else if (externalDataPending) {
    safeCopyCString(currentWealth, sizeof(currentWealth), LOADING_TEXT);
  } else {
    safeCopyCString(currentWealth, sizeof(currentWealth), "N/A");
  }
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("WEALTH CHANGE");
  if (externalDataPending && !wealthDataAvailable) {
    display.print(" ");
    display.print(LOADING_TEXT);
  } else if (currentWealth[0]) {
    display.print(" (CURRENT:");
    display.print(currentWealth);
    display.print(")");
  }

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
    if (externalDataPending && !wealthDataAvailable) {
      // Keep first-load rows calm; the title already communicates that values are loading.
    } else if (!wealthDataAvailable || !getHistoricalBalanceSats(history, periods[i].daysAgo, historicalBalance)) {
      display.print("N/A");
    } else {
      formatSignedBtcDelta(currentBalanceSats - historicalBalance, balanceDeltaText, sizeof(balanceDeltaText));
      display.print(balanceDeltaText);
    }

    display.setCursor(rowPriceX, rowY);
    if (externalDataPending && !wealthDataAvailable) {
      // Keep first-load rows calm; the title already communicates that values are loading.
    } else if (!wealthDataAvailable || !getHistoricalPriceValue(history, periods[i].daysAgo, historicalPrice)) {
      display.print("N/A");
    } else {
      formatSignedCompactPrice(currentPriceRounded - historicalPrice, priceDeltaText, sizeof(priceDeltaText));
      display.print(priceDeltaText);
    }

    display.setCursor(rowWealthX, rowY);
    if (externalDataPending && !wealthDataAvailable) {
      // Keep first-load rows calm; the title already communicates that values are loading.
    } else if (!wealthDataAvailable || !getHistoricalWealthValue(history, periods[i].daysAgo, historicalWealth)) {
      display.print("N/A");
    } else {
      formatSignedCompactValue(currentWealthRounded - historicalWealth, wealthDeltaText, sizeof(wealthDeltaText));
      display.print(wealthDeltaText);
    }
  }

  if (!showBtcBreakdown) {
    for (uint8_t i = 0; i < (sizeof(periods) / sizeof(periods[0])); i++) {
      const int rowY = ROW_Y0 + (ROW_STEP * i);
      display.setCursor(rowWealthX, rowY);
      if (externalDataPending && !wealthDataAvailable) {
        // Keep first-load rows calm; the title already communicates that values are loading.
      } else if (!wealthDataAvailable || !getHistoricalWealthValue(history, periods[i].daysAgo, historicalWealth)) {
        display.print("N/A");
      } else {
        formatSignedCompactValue(currentWealthRounded - historicalWealth, wealthDeltaText, sizeof(wealthDeltaText));
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
  outStats = {0, 0, 0, 0, 0, 0.0f, 0.0f, 0, false};

  if (lifeExpectancyYears <= 0) return;

  const time_t birthTime = makeUtcDate(birthYear, 1, 1);
  const time_t endTime = makeUtcDate(birthYear + lifeExpectancyYears, 1, 1);
  if (birthTime == (time_t)-1 || endTime == (time_t)-1 || endTime <= birthTime) return;

  const double totalDays = difftime(endTime, birthTime) / 86400.0;
  outStats.totalWeeks = (int)(totalDays / 7.0 + 0.5);
  outStats.totalYearsExact = (float)(totalDays / 365.25);

  if (now > endTime) {
    // Past life expectancy: compute bonus time lived beyond expected end date.
    outStats.pastExpectancy = true;
    outStats.remainingPercent = 0;
    const double extraDays = difftime(now, endTime) / 86400.0;
    outStats.remainingWeeks = (int)((extraDays + 6.999) / 7.0);
    outStats.remainingYearsExact = (float)(extraDays / 365.25);
    int d = (int)(extraDays + 0.5);
    outStats.yearsLeft = d / 365;  d %= 365;
    outStats.monthsLeft = d / 30;  d %= 30;
    outStats.weeksLeftRemainder = clampInt(d / 7, 0, 4);
  } else {
    outStats.pastExpectancy = false;
    const time_t clampedNow = (now < birthTime) ? birthTime : now;
    const double remainingDays = difftime(endTime, clampedNow) / 86400.0;
    outStats.remainingWeeks = (remainingDays <= 0.0) ? 0 : (int)((remainingDays + 6.999) / 7.0);
    outStats.remainingYearsExact = (remainingDays <= 0.0) ? 0.0f : (float)(remainingDays / 365.25);
    const double remainingRatio = (totalDays > 0.0) ? (remainingDays / totalDays) : 0.0;
    outStats.remainingPercent = clampInt((int)(remainingRatio * 100.0 + 0.5), 0, 100);
    int d = (remainingDays <= 0.0) ? 0 : (int)(remainingDays + 0.5);
    outStats.yearsLeft = d / 365;  d %= 365;
    outStats.monthsLeft = d / 30;  d %= 30;
    outStats.weeksLeftRemainder = clampInt(d / 7, 0, 4);
  }
}

static void computeLongevityWithInflation(
  float wealthValue,
  float monthlyExpenseToday,
  float monthlyIncomeToday,
  float inflationAnnual,
  float incomeGrowthAnnual,
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

  if (wealthValue <= 0.0f || monthlyExpenseToday <= 0.0f) return;
  if (monthlyIncomeToday < 0.0f) monthlyIncomeToday = 0.0f;
  if (inflationAnnual < 0.0f) inflationAnnual = 0.0f;
  if (incomeGrowthAnnual < 0.0f) incomeGrowthAnnual = 0.0f;
  if (assetGrowthAnnual < -0.99f) assetGrowthAnnual = -0.99f;
  if (borrowFeeAnnual < -0.99f) borrowFeeAnnual = -0.99f;

  static constexpr int MAX_YEARS = 200;
  static constexpr int MAX_MONTHS = 12 * MAX_YEARS;

  if (portfolioUseMode == PORTFOLIO_USE_MODE_BORROW) {
    const float annualExpenseMul = 1.0f + inflationAnnual;
    const float annualIncomeMul = 1.0f + incomeGrowthAnnual;
    const float annualAssetMul = 1.0f + assetGrowthAnnual;
    const float annualDebtMul = 1.0f + borrowFeeAnnual;

    float annualExpense = monthlyExpenseToday * 12.0f;
    float annualIncome = monthlyIncomeToday * 12.0f;
    float collateralValue = wealthValue;
    float debt = 0.0f;
    int years = 0;

    while (years < MAX_YEARS) {
      collateralValue *= annualAssetMul;
      debt *= annualDebtMul;
      const float annualShortfall = annualExpense - annualIncome;
      const float annualBorrow = annualShortfall > 0.0f ? annualShortfall : 0.0f;
      if ((debt + annualBorrow) > collateralValue) break;
      debt += annualBorrow;
      annualExpense *= annualExpenseMul;
      annualIncome *= annualIncomeMul;
      years++;
    }

    outHitCap = (years >= MAX_YEARS);

    float partialYear = 0.0f;
    const float finalAnnualShortfall = annualExpense - annualIncome;
    if (!outHitCap && finalAnnualShortfall > 0.0f && collateralValue > debt) {
      partialYear = (collateralValue - debt) / finalAnnualShortfall;
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
  const float monthlyIncomeMul = powf(1.0f + incomeGrowthAnnual, 1.0f / 12.0f);
  const float monthlyAssetMul = powf(1.0f + assetGrowthAnnual, 1.0f / 12.0f);

  float monthlyExpense = monthlyExpenseToday;
  float monthlyIncome = monthlyIncomeToday;
  float remaining = wealthValue;
  int months = 0;

  while (months < MAX_MONTHS) {
    remaining *= monthlyAssetMul;
    const float monthlyShortfall = monthlyExpense - monthlyIncome;
    const float monthlyWithdrawal = monthlyShortfall > 0.0f ? monthlyShortfall : 0.0f;
    if (remaining < monthlyWithdrawal) break;
    remaining -= monthlyWithdrawal;
    monthlyExpense *= monthlyExpenseMul;
    monthlyIncome *= monthlyIncomeMul;
    months++;
  }

  outHitCap = (months >= MAX_MONTHS);
  outYears = months / 12;
  outMonths = months % 12;

  float partialMonth = 0.0f;
  const float finalMonthlyShortfall = monthlyExpense - monthlyIncome;
  if (finalMonthlyShortfall > 0.0f && remaining > 0.0f) {
    partialMonth = remaining / finalMonthlyShortfall;
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

static void formatFreedomDuration(bool hitCap, int years, int months, int weeks, char* dst, size_t dstSize, uint8_t timeDisplayFormat = 0) {
  if (!dst || dstSize == 0) return;
  if (hitCap) {
    safeCopyCString(dst, dstSize, "FOREVER");
    return;
  }
  if (timeDisplayFormat == 1) {
    const int totalWeeks = years * 52 + months * 4 + weeks;
    snprintf(dst, dstSize, "%dw", totalWeeks);
  } else {
    snprintf(dst, dstSize, "%dy %dm %dw", years, months, weeks);
  }
}

static void drawFreedomCheckinScreen(
  const DeviceConfig& cfg,
  time_t now,
  float wealthValue,
  int deviceBatteryPct,
  const WealthHistory& history,
  uint8_t timeDisplayFormat = 0,
  bool freedomDataAvailable = true,
  bool showNoWifiIcon = false,
  bool externalDataPending = false
) {
  struct FreedomPeriod {
    const char* shortLabel;
    uint16_t daysAgo;
  };

  static const FreedomPeriod periods[] = {
    { "1D", 1 },
    { "7D", 7 },
    { "1M", 30 },
    { "3M", 90 },
    { "6M", 180 },
    { "12M", 365 }
  };

  int32_t historicalWealthValue = 0;
  char freedomDeltaText[16];
  char currentFreedomText[20];
  const DisplayThemeMode themeMode = sanitizeThemeMode(cfg.displayThemeMode);

  prepareScreen(themeMode);

  const int batteryIconX = drawBatteryStatus(deviceBatteryPct, themeMode, cfg.showBatteryPercent);
  if (showNoWifiIcon) {
    drawNoWifiStatusLeftOfBattery(batteryIconX, themeMode);
  }

  bool currentFreedomHitCap = false;
  int currentYears = 0;
  int currentMonths = 0;
  int currentWeeks = 0;
  float currentCoveredWeeks = 0.0f;
  if (freedomDataAvailable) {
    computeLongevityWithInflation(
      wealthValue,
      cfg.monthlyExpenseValue,
      cfg.monthlyIncomeValue,
      cfg.inflationAnnual,
      cfg.incomeGrowthAnnual,
      cfg.wealthGrowthAnnual,
      sanitizePortfolioUseMode(cfg.portfolioUseMode),
      cfg.borrowFeeAnnual,
      currentFreedomHitCap,
      currentYears,
      currentMonths,
      currentWeeks,
      currentCoveredWeeks
    );
    formatFreedomDuration(currentFreedomHitCap, currentYears, currentMonths, currentWeeks, currentFreedomText, sizeof(currentFreedomText), timeDisplayFormat);
  } else if (externalDataPending) {
    safeCopyCString(currentFreedomText, sizeof(currentFreedomText), LOADING_TEXT);
  } else {
    safeCopyCString(currentFreedomText, sizeof(currentFreedomText), "N/A");
  }

  display.setTextSize(1);
  static constexpr int TITLE_X = 9;
  static constexpr int TITLE_Y = 13;
  display.setCursor(TITLE_X, TITLE_Y);
  display.print("FREEDOM CHANGE");
  if (externalDataPending && !freedomDataAvailable) {
    display.print(" ");
    display.print(LOADING_TEXT);
  } else if (currentFreedomText[0]) {
    display.print(" (CURRENT:");
    display.print(currentFreedomText);
    display.print(")");
  }

  static constexpr int LAST_X = 11;
  static constexpr int FREEDOM_X = 44;
  static constexpr int HEADER_Y = 32;
  static constexpr int ROW_Y0 = 46;
  static constexpr int ROW_STEP = 13;

  display.setCursor(LAST_X, HEADER_Y);
  display.print("LAST");
  display.setCursor(FREEDOM_X, HEADER_Y);
  display.print("FREEDOM");

  for (size_t i = 0; i < (sizeof(periods) / sizeof(periods[0])); i++) {
    display.setCursor(LAST_X, ROW_Y0 + (ROW_STEP * (int)i));
    display.print(periods[i].shortLabel);

    display.setCursor(FREEDOM_X, ROW_Y0 + (ROW_STEP * (int)i));
    if (externalDataPending && !freedomDataAvailable) {
      continue;
    } else if (!freedomDataAvailable || !getHistoricalWealthValue(history, periods[i].daysAgo, historicalWealthValue)) {
      display.print("N/A");
      continue;
    }

    bool previousFreedomHitCap = false;
    int previousYears = 0;
    int previousMonths = 0;
    int previousWeeks = 0;
    float previousCoveredWeeks = 0.0f;
    computeLongevityWithInflation(
      (float)historicalWealthValue,
      cfg.monthlyExpenseValue,
      cfg.monthlyIncomeValue,
      cfg.inflationAnnual,
      cfg.incomeGrowthAnnual,
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
