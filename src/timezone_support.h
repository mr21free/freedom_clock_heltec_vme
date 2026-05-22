#pragma once

#include <Arduino.h>
#include <cstdlib>
#include <ctime>

struct TimeZoneOption {
  const char* label;
  const char* zoneName;
  const char* posixTz;
  int16_t standardOffsetMinutes;
  bool observesDst;
};

static constexpr TimeZoneOption TIME_ZONE_OPTIONS[] = {
  { "Zurich / Central Europe", "Central European Time", "CET-1CEST,M3.5.0,M10.5.0/3", 60, true },
  { "Baker Island", "Fixed offset", "UTC+12", -720, false },
  { "American Samoa / Midway", "Fixed offset", "UTC+11", -660, false },
  { "Hawaii", "Fixed offset", "UTC+10", -600, false },
  { "Alaska Time", "Alaska Time", "AKST9AKDT,M3.2.0,M11.1.0", -540, true },
  { "Pacific Time", "Pacific Time", "PST8PDT,M3.2.0,M11.1.0", -480, true },
  { "Mountain Time", "Mountain Time", "MST7MDT,M3.2.0,M11.1.0", -420, true },
  { "Central Time", "Central Time", "CST6CDT,M3.2.0,M11.1.0", -360, true },
  { "Eastern Time", "Eastern Time", "EST5EDT,M3.2.0,M11.1.0", -300, true },
  { "Atlantic / Caribbean", "Fixed offset", "UTC+4", -240, false },
  { "Argentina / Sao Paulo", "Fixed offset", "UTC+3", -180, false },
  { "South Georgia", "Fixed offset", "UTC+2", -120, false },
  { "Cape Verde", "Fixed offset", "UTC+1", -60, false },
  { "Coordinated Universal Time", "Fixed offset", "UTC0", 0, false },
  { "Eastern Europe", "Eastern European Time", "EET-2EEST,M3.5.0/3,M10.5.0/4", 120, true },
  { "Moscow / East Africa", "Fixed offset", "UTC-3", 180, false },
  { "Dubai / Gulf", "Fixed offset", "UTC-4", 240, false },
  { "Pakistan / Uzbekistan", "Fixed offset", "UTC-5", 300, false },
  { "Bangladesh / Bhutan", "Fixed offset", "UTC-6", 360, false },
  { "Bangkok / Indochina", "Fixed offset", "UTC-7", 420, false },
  { "Singapore / China", "Fixed offset", "UTC-8", 480, false },
  { "Tokyo / Korea", "Fixed offset", "UTC-9", 540, false },
  { "Sydney / Australian Eastern", "Australian Eastern Time", "AEST-10AEDT,M10.1.0,M4.1.0/3", 600, true },
  { "Solomon Islands", "Fixed offset", "UTC-11", 660, false },
  { "Marshall Islands", "Fixed offset", "UTC-12", 720, false },
  { "Tonga / Samoa", "Fixed offset", "UTC-13", 780, false },
  { "Line Islands", "Fixed offset", "UTC-14", 840, false }
};

static constexpr uint8_t TIME_ZONE_DISPLAY_ORDER[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26
};

static constexpr uint8_t DEFAULT_TIME_ZONE_INDEX = 0;

static_assert(
  (sizeof(TIME_ZONE_DISPLAY_ORDER) / sizeof(TIME_ZONE_DISPLAY_ORDER[0])) == (sizeof(TIME_ZONE_OPTIONS) / sizeof(TIME_ZONE_OPTIONS[0])),
  "Time zone display order must include every time zone option."
);

static inline const TimeZoneOption& selectedTimeZoneOption(uint8_t timeZoneIndex) {
  if (timeZoneIndex >= (sizeof(TIME_ZONE_OPTIONS) / sizeof(TIME_ZONE_OPTIONS[0]))) {
    timeZoneIndex = DEFAULT_TIME_ZONE_INDEX;
  }
  return TIME_ZONE_OPTIONS[timeZoneIndex];
}

static inline void applyPosixTimeZone(uint8_t timeZoneIndex) {
  const TimeZoneOption& zone = selectedTimeZoneOption(timeZoneIndex);
  setenv("TZ", zone.posixTz, 1);
  tzset();
}

static inline String formatUtcOffsetLabel(int16_t offsetMinutes) {
  const char sign = offsetMinutes < 0 ? '-' : '+';
  int totalMinutes = abs((int)offsetMinutes);
  const int hours = totalMinutes / 60;
  const int minutes = totalMinutes % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", sign, hours, minutes);
  return String(buf);
}

static inline String timeZoneDisplayLabel(const TimeZoneOption& zone) {
  String label = formatUtcOffsetLabel(zone.standardOffsetMinutes);
  label += " - ";
  label += zone.label;
  label += " (";
  if (zone.observesDst) {
    label += zone.zoneName;
    label += ", auto DST";
  } else {
    label += "no DST";
  }
  label += ")";
  return label;
}
