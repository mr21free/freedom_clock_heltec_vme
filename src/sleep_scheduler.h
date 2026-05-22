#pragma once

// ============================================================
// Button handling
// ============================================================

static bool isFunctionButtonPressed() {
  return digitalRead(PIN_FUNCTION_BUTTON) == LOW;
}

static bool isSetupButtonPressed() {
  return digitalRead(PIN_SETUP_BUTTON) == LOW;
}

static bool wasFunctionButtonWake(esp_sleep_wakeup_cause_t wakeCause) {
  if (wakeCause != ESP_SLEEP_WAKEUP_EXT1) return false;
  return (esp_sleep_get_ext1_wakeup_status() & FUNCTION_BUTTON_WAKE_MASK) != 0;
}

static bool wasSetupButtonWake(esp_sleep_wakeup_cause_t wakeCause) {
  if (wakeCause != ESP_SLEEP_WAKEUP_EXT1) return false;
  return (esp_sleep_get_ext1_wakeup_status() & SETUP_BUTTON_WAKE_MASK) != 0;
}

static bool wasButtonWake(esp_sleep_wakeup_cause_t wakeCause) {
  return wasFunctionButtonWake(wakeCause) || wasSetupButtonWake(wakeCause);
}

static uint8_t detectAdditionalWakePresses(uint8_t maxAdditionalPresses = 2, uint32_t interPressTimeoutMs = SECOND_PRESS_WINDOW_MS) {
  uint8_t pressCount = 0;

  uint32_t releaseStart = millis();
  while (isFunctionButtonPressed() && (millis() - releaseStart) < interPressTimeoutMs) {
    delay(BUTTON_POLL_DELAY_MS);
  }

  while (pressCount < maxAdditionalPresses) {
    const uint32_t waitStart = millis();
    bool detected = false;

    while ((millis() - waitStart) < interPressTimeoutMs) {
      if (isFunctionButtonPressed()) {
        delay(BUTTON_POLL_DELAY_MS);
        if (!isFunctionButtonPressed()) {
          continue;
        }
        detected = true;
        pressCount++;
        while (isFunctionButtonPressed() && (millis() - waitStart) < interPressTimeoutMs) {
          delay(BUTTON_POLL_DELAY_MS);
        }
        break;
      }
      delay(BUTTON_POLL_DELAY_MS);
    }

    if (!detected) {
      break;
    }
  }

  return pressCount;
}

static SetupBootAction detectSetupBootAction(esp_sleep_wakeup_cause_t wakeCause) {
  const bool setupButtonWake = wasSetupButtonWake(wakeCause);
  if (!isSetupButtonPressed()) {
    return setupButtonWake ? SETUP_BOOT_ACTION_PORTAL : SETUP_BOOT_ACTION_NONE;
  }

  uint32_t start = millis();
  while (isSetupButtonPressed()) {
    uint32_t heldMs = millis() - start;
    if (heldMs >= FACTORY_RESET_HOLD_MS) {
      return SETUP_BOOT_ACTION_FACTORY_RESET;
    }
    delay(BUTTON_POLL_DELAY_MS);
  }

  uint32_t heldMs = millis() - start;
  if (heldMs >= FACTORY_RESET_HOLD_MS) {
    return SETUP_BOOT_ACTION_FACTORY_RESET;
  }
  return SETUP_BOOT_ACTION_PORTAL;
}

static void prepareButtonWakePins() {
  rtc_gpio_pullup_en((gpio_num_t)PIN_FUNCTION_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_FUNCTION_BUTTON);
  rtc_gpio_pullup_en((gpio_num_t)PIN_SETUP_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_SETUP_BUTTON);
}

static void enableButtonWakeup() {
  prepareButtonWakePins();
  esp_sleep_enable_ext1_wakeup_io(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
}

// ============================================================
// Deep sleep
// ============================================================

static uint64_t scheduledSleepMicros(
  uint16_t refreshIntervalMinutes,
  uint8_t wakeHour,
  uint8_t wakeMinute,
  uint8_t timeZoneIndex,
  time_t now
) {
  // Only use scheduled wake for daily refresh with a valid local time and known clock.
  if (refreshIntervalMinutes != 1440 || wakeHour > 23 || now < 1700000000) {
    return (uint64_t)refreshIntervalMinutes * MICROSECONDS_PER_MINUTE;
  }

  applyPosixTimeZone(timeZoneIndex);
  struct tm localTm = {};
  if (!localtime_r(&now, &localTm)) {
    return (uint64_t)refreshIntervalMinutes * MICROSECONDS_PER_MINUTE;
  }

  struct tm targetTm = localTm;
  targetTm.tm_hour = wakeHour;
  targetTm.tm_min = wakeMinute;
  targetTm.tm_sec = 0;
  targetTm.tm_isdst = -1;
  time_t targetUtc = mktime(&targetTm);
  if (targetUtc <= (now + 60)) {
    targetTm.tm_mday += 1;
    targetTm.tm_isdst = -1;
    targetUtc = mktime(&targetTm);
  }

  if (targetUtc <= now) {
    return (uint64_t)refreshIntervalMinutes * MICROSECONDS_PER_MINUTE;
  }
  return (uint64_t)(targetUtc - now) * 1000000ULL;
}

static void goToSleep(uint16_t refreshIntervalMinutes, uint8_t wakeHour = 255, uint8_t wakeMinute = 0, uint8_t timeZoneIndex = DEFAULT_TIME_ZONE_INDEX, time_t now = 0) {
  digitalWrite(PIN_EINK_POWER, LOW);
  enableButtonWakeup();
  uint64_t sleep_us = scheduledSleepMicros(refreshIntervalMinutes, wakeHour, wakeMinute, timeZoneIndex, now);
  esp_sleep_enable_timer_wakeup(sleep_us);
  Serial.flush();
  esp_deep_sleep_start();
}
