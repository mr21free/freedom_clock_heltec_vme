#pragma once

// Optional bootstrap values.
// These stay inactive unless you also opt in with:
// #define USE_SECRETS_BOOTSTRAP 1

static const char* WIFI_SSID     = "YOUR_WIFI_NAME";
static const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

static const char* MQTT_SERVER   = "mqtt.local";
static const int   MQTT_PORT     = 1883;
static const char* MQTT_USER     = "YOUR_MQTT_USER";
static const char* MQTT_PASS     = "YOUR_MQTT_PASSWORD";

// Optional developer-only test mode for the stats and ritual screens.
// Uncomment temporarily to seed one year of synthetic daily history into
// the device's normal stats storage, including a visible last-week move
// for the weekly freedom ritual screen, then flash once and remove again.
// #define ENABLE_TEST_HISTORY 1
// #define FORCE_TEST_HISTORY_ON_EVERY_BOOT 1

// Battery stats logging is enabled by default.
// Set to 0 only if you do not want the setup page to collect calibration samples.
// #define LOG_BATTERY_STATS 0
