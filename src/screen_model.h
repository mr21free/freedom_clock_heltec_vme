#pragma once

#include <Arduino.h>
#include "esp_sleep.h"

enum AppScreenId : uint8_t {
  APP_SCREEN_MAIN = 0,
  APP_SCREEN_QUOTE = 1,
  APP_SCREEN_FREEDOM_CHANGE = 2,
  APP_SCREEN_WEALTH_CHANGE = 3,
  APP_SCREEN_SETTINGS = 4
};

struct AppScreenOption {
  AppScreenId id;
  bool enabled;
};

static inline uint8_t buildWakeScreenOptions(const DeviceConfig& cfg, AppScreenOption* screens, uint8_t maxScreens) {
  if (!screens || maxScreens < 3) return 0;
  uint8_t count = 0;
  screens[count++] = { APP_SCREEN_FREEDOM_CHANGE, true };
  screens[count++] = { APP_SCREEN_WEALTH_CHANGE, cfg.showWealthChangeScreen };
  screens[count++] = { APP_SCREEN_SETTINGS, cfg.showSettingsScreen };
  return count;
}

static inline AppScreenId selectWakeButtonScreen(
  bool functionButtonWake,
  uint8_t wakeExtraPressCount,
  const DeviceConfig& cfg
) {
  if (!functionButtonWake) {
    return cfg.quoteOfDayEnabled ? APP_SCREEN_QUOTE : APP_SCREEN_MAIN;
  }

  AppScreenOption screens[3];
  const uint8_t screenCount = buildWakeScreenOptions(cfg, screens, 3);
  uint8_t enabledIndex = 0;
  for (uint8_t i = 0; i < screenCount; i++) {
    if (!screens[i].enabled) continue;
    if (enabledIndex == wakeExtraPressCount) {
      return screens[i].id;
    }
    enabledIndex++;
  }

  return cfg.quoteOfDayEnabled ? APP_SCREEN_QUOTE : APP_SCREEN_MAIN;
}
