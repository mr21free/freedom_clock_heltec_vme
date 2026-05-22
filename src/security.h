#pragma once

static time_t buildTimestamp();

static bool isSixDigitPin(const char* pin) {
  if (!pin) return false;
  for (uint8_t i = 0; i < SETUP_PIN_LENGTH; i++) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }
  return pin[SETUP_PIN_LENGTH] == '\0';
}

static void bytesToHex(const uint8_t* src, size_t srcSize, char* dst, size_t dstSize) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  if (!dst || dstSize == 0) return;

  size_t maxBytes = (dstSize - 1) / 2;
  if (maxBytes > srcSize) maxBytes = srcSize;
  for (size_t i = 0; i < maxBytes; i++) {
    dst[i * 2] = HEX_DIGITS[(src[i] >> 4) & 0x0F];
    dst[i * 2 + 1] = HEX_DIGITS[src[i] & 0x0F];
  }
  dst[maxBytes * 2] = '\0';
}

static void computeSetupPinHashForDeviceId(const char* pin, const char* idForHash, char* outHashHex, size_t outHashHexSize) {
  if (!outHashHex || outHashHexSize == 0) return;
  outHashHex[0] = '\0';
  if (!isSixDigitPin(pin)) return;
  if (!idForHash || idForHash[0] == '\0') return;

  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);

  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)"FreedomClockPIN|", 17);
  mbedtls_sha256_update(&ctx, (const unsigned char*)idForHash, strlen(idForHash));
  mbedtls_sha256_update(&ctx, (const unsigned char*)"|", 1);
  mbedtls_sha256_update(&ctx, (const unsigned char*)pin, strlen(pin));
  mbedtls_sha256_finish(&ctx, digest);

  for (uint16_t round = 0; round < SETUP_PIN_HASH_ROUNDS; round++) {
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, digest, sizeof(digest));
    mbedtls_sha256_update(&ctx, (const unsigned char*)pin, strlen(pin));
    mbedtls_sha256_update(&ctx, (const unsigned char*)idForHash, strlen(idForHash));
    mbedtls_sha256_finish(&ctx, digest);
  }

  mbedtls_sha256_free(&ctx);
  bytesToHex(digest, sizeof(digest), outHashHex, outHashHexSize);
}

static void computeSetupPinHash(const char* pin, char* outHashHex, size_t outHashHexSize) {
  computeSetupPinHashForDeviceId(pin, deviceId, outHashHex, outHashHexSize);
}

static bool setupPinMatchesStoredHash(const char* pin, const char* storedHash) {
  if (!isSixDigitPin(pin) || !hasText(storedHash)) return false;

  char computedHash[SETUP_PIN_HASH_HEX_SIZE];
  computeSetupPinHash(pin, computedHash, sizeof(computedHash));
  if (strcmp(computedHash, storedHash) == 0) {
    return true;
  }

  char legacyDeviceId[7];
  snprintf(legacyDeviceId, sizeof(legacyDeviceId), "%06llX", (unsigned long long)(ESP.getEfuseMac() & 0xFFFFFFULL));
  computeSetupPinHashForDeviceId(pin, legacyDeviceId, computedHash, sizeof(computedHash));
  if (strcmp(computedHash, storedHash) == 0) {
    return true;
  }

  char fullMacDeviceId[13];
  snprintf(fullMacDeviceId, sizeof(fullMacDeviceId), "%012llX", (unsigned long long)ESP.getEfuseMac());
  computeSetupPinHashForDeviceId(pin, fullMacDeviceId, computedHash, sizeof(computedHash));
  return strcmp(computedHash, storedHash) == 0;
}

static void refreshHardwareSecurityStatus() {
  hardwareFlashEncryptionActive = esp_flash_encryption_enabled();
  hardwareFlashEncryptionReleaseMode = hardwareFlashEncryptionActive && esp_flash_encryption_cfg_verify_release_mode();
  hardwareSecureBootActive = esp_secure_boot_enabled();
  hardwareNvsKeysPartitionPresent = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
    nullptr
  ) != nullptr;
}

static esp_err_t reinitializePlainNvsStorage() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err == ESP_OK) {
      err = nvs_flash_init();
    }
  }
  return err;
}

static bool initializeEncryptedNvsIfAvailable() {
  refreshHardwareSecurityStatus();
  secureNvsActive = false;

  if (!hardwareFlashEncryptionActive || !hardwareNvsKeysPartitionPresent) {
    return false;
  }

  const esp_partition_t* nvsKeysPart = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
    nullptr
  );
  if (!nvsKeysPart) {
    refreshHardwareSecurityStatus();
    return false;
  }

  esp_err_t err = ESP_OK;
  nvs_sec_cfg_t nvsSecCfg = {};
  err = nvs_flash_read_security_cfg(nvsKeysPart, &nvsSecCfg);
  if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
    err = nvs_flash_generate_keys(nvsKeysPart, &nvsSecCfg);
  }

  if (err == ESP_OK) {
    nvs_flash_deinit();
    err = nvs_flash_secure_init(&nvsSecCfg);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      esp_err_t eraseErr = nvs_flash_erase();
      if (eraseErr == ESP_OK) {
        err = nvs_flash_secure_init(&nvsSecCfg);
      } else {
        err = eraseErr;
      }
    }
  }

  if (err == ESP_OK) {
    secureNvsActive = true;
    refreshHardwareSecurityStatus();
    return true;
  }

  nvs_flash_deinit();
  reinitializePlainNvsStorage();
  refreshHardwareSecurityStatus();
  return false;
}

static time_t estimateCurrentTime(bool ntpSynced, esp_sleep_wakeup_cause_t wakeCause, uint16_t refreshIntervalMinutes) {
  if (ntpSynced) {
    time_t now = time(nullptr);
    if (now >= 1700000000) return now;
  }

  if (lastKnownUnixTime >= 1700000000) {
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
      return lastKnownUnixTime + (time_t)((uint64_t)refreshIntervalMinutes * 60ULL);
    }
    return lastKnownUnixTime;
  }

  return buildTimestamp();
}

static time_t portalCurrentUnixTime() {
  time_t base = portalBootUnixBase;
  if (base < 1700000000) {
    base = (lastKnownUnixTime >= 1700000000) ? lastKnownUnixTime : buildTimestamp();
  }
  return base + (time_t)(millis() / 1000UL);
}

static void checkpointPortalUnixTime() {
  time_t now = portalCurrentUnixTime();
  if (now > lastKnownUnixTime) {
    lastKnownUnixTime = now;
  }
}

static bool isPortalUnlockExpired() {
  if (!portalUnlocked) return true;
  return (millis() - portalUnlockedAtMs) > CONFIG_PORTAL_UNLOCK_TIMEOUT_MS;
}

static void refreshPortalUnlockSession() {
  if (portalUnlocked) {
    portalUnlockedAtMs = millis();
  }
}

static bool requirePortalUnlock(const char* message = nullptr) {
  if (!hasSetupPinConfigured(deviceConfig)) {
    portalUnlocked = true;
    portalUnlockedAtMs = millis();
    return false;
  }

  if (isPortalUnlockExpired()) {
    portalUnlocked = false;
    portalUnlockedAtMs = 0;
  }

  if (!portalUnlocked) {
    portalSendHtml(buildPortalUnlockPage(message, message != nullptr));
    return true;
  }

  refreshPortalUnlockSession();
  return false;
}

static bool hasSetupPinConfigured(const DeviceConfig& cfg) {
  return cfg.setupPinEnabled && hasText(cfg.setupPinHash);
}

static uint32_t setupPinLockRemainingSeconds() {
  if (setupPinLockedUntil <= 0) return 0;
  const time_t now = portalCurrentUnixTime();
  if (setupPinLockedUntil <= now) return 0;
  return (uint32_t)(setupPinLockedUntil - now);
}
