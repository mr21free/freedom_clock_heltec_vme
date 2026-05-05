#pragma once

// Optional bootstrap values.
// These stay inactive unless you also opt in with:
// #define FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP 1
// That keeps normal first boot and factory reset behavior generic.

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
// #define FREEDOM_CLOCK_ENABLE_TEST_HISTORY 1
// #define FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED 1
