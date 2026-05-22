#pragma once

static bool parseJsonStringFieldFrom(const String& payload, const char* key, int startIndex, String& outValue, int* nextIndex = nullptr) {
  if (!key || !key[0]) return false;

  const String needle = String("\"") + key + "\"";
  const int keyIndex = payload.indexOf(needle, startIndex < 0 ? 0 : startIndex);
  if (keyIndex < 0) return false;

  int colonIndex = payload.indexOf(':', keyIndex + needle.length());
  if (colonIndex < 0) return false;
  colonIndex++;

  while (colonIndex < payload.length() && isspace((unsigned char)payload[colonIndex])) {
    colonIndex++;
  }
  if (colonIndex >= payload.length() || payload[colonIndex] != '"') return false;

  outValue = "";
  bool escaped = false;
  for (int i = colonIndex + 1; i < payload.length(); i++) {
    const char c = payload[i];
    if (escaped) {
      switch (c) {
        case 'n':
          outValue += '\n';
          break;
        case 'r':
          outValue += '\r';
          break;
        case 't':
          outValue += '\t';
          break;
        case '"':
        case '\\':
        case '/':
          outValue += c;
          break;
        case 'u':
          if ((i + 4) < payload.length()) {
            char hexBuf[5] = {0};
            for (int j = 0; j < 4; j++) {
              hexBuf[j] = payload[i + 1 + j];
            }
            char* endPtr = nullptr;
            unsigned long codePoint = strtoul(hexBuf, &endPtr, 16);
            if (endPtr == (hexBuf + 4) && codePoint <= 0x7F && isprint((int)codePoint)) {
              outValue += (char)codePoint;
            } else {
              outValue += '?';
            }
            i += 4;
          } else {
            outValue += '?';
          }
          break;
        default:
          outValue += c;
          break;
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      if (nextIndex) *nextIndex = i + 1;
      return true;
    }
    outValue += c;
  }

  return false;
}

static bool parseJsonStringField(const String& payload, const char* key, String& outValue) {
  return parseJsonStringFieldFrom(payload, key, 0, outValue, nullptr);
}

static bool parseGitHubReleaseAssetUrl(const String& payload, const String& assetNameSuffix, String& outUrl) {
  int searchIndex = 0;
  while (searchIndex < payload.length()) {
    String assetName;
    int afterName = 0;
    if (!parseJsonStringFieldFrom(payload, "name", searchIndex, assetName, &afterName)) {
      return false;
    }
    searchIndex = afterName;
    if (!assetName.endsWith(assetNameSuffix)) {
      continue;
    }

    String downloadUrl;
    if (parseJsonStringFieldFrom(payload, "browser_download_url", afterName, downloadUrl, nullptr)) {
      downloadUrl.trim();
      if (downloadUrl.length() > 0) {
        outUrl = downloadUrl;
        return true;
      }
    }
  }

  return false;
}

static uint32_t readVersionPart(const String& version, int& index) {
  while (index < version.length() && !isdigit((unsigned char)version[index])) {
    index++;
  }

  uint32_t value = 0;
  while (index < version.length() && isdigit((unsigned char)version[index])) {
    value = (value * 10U) + (uint32_t)(version[index] - '0');
    index++;
  }
  return value;
}

static int compareFirmwareVersions(String left, String right) {
  left.trim();
  right.trim();
  if (left.startsWith("v") || left.startsWith("V")) left.remove(0, 1);
  if (right.startsWith("v") || right.startsWith("V")) right.remove(0, 1);

  int leftIndex = 0;
  int rightIndex = 0;
  for (uint8_t i = 0; i < 8; i++) {
    const uint32_t leftPart = readVersionPart(left, leftIndex);
    const uint32_t rightPart = readVersionPart(right, rightIndex);
    if (leftPart > rightPart) return 1;
    if (leftPart < rightPart) return -1;
    if (leftIndex >= left.length() && rightIndex >= right.length()) return 0;
  }
  return 0;
}

static bool isReleaseNewerThanCurrent(const GitHubReleaseInfo& releaseInfo) {
  return compareFirmwareVersions(releaseInfo.tagName, FIRMWARE_VERSION) > 0;
}

static void normalizeReleaseNotesPreview(String& notes) {
  notes.replace("\r\n", "\n");
  notes.replace('\r', '\n');
  while (notes.indexOf("\n\n\n") >= 0) {
    notes.replace("\n\n\n", "\n\n");
  }
  notes.trim();
  if (notes.length() > (int)GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS) {
    notes.remove(GITHUB_RELEASE_NOTES_PREVIEW_MAX_CHARS);
    notes += "\n\n...";
  }
  if (!notes.length()) {
    notes = "No release notes provided.";
  }
}

static bool parseGitHubReleaseInfo(const String& payload, GitHubReleaseInfo& outInfo) {
  if (!parseJsonStringField(payload, "tag_name", outInfo.tagName)) return false;
  parseJsonStringField(payload, "name", outInfo.name);
  parseJsonStringField(payload, "body", outInfo.body);
  parseJsonStringField(payload, "html_url", outInfo.htmlUrl);

  outInfo.tagName.trim();
  outInfo.name.trim();
  outInfo.htmlUrl.trim();

  if (!outInfo.name.length()) {
    outInfo.name = outInfo.tagName;
  }

  String version = outInfo.tagName;
  if (version.startsWith("v") || version.startsWith("V")) {
    version.remove(0, 1);
  }
  const String modelPrefix = String("FreedomClock-") + version + "-" + DEVICE_MODEL_NAME;
  if (!parseGitHubReleaseAssetUrl(payload, modelPrefix + "-manual-update-open.bin", outInfo.openBinUrl)) {
    parseGitHubReleaseAssetUrl(payload, String("FreedomClock-") + version + "-manual-update-open.bin", outInfo.openBinUrl);
  }
  if (!parseGitHubReleaseAssetUrl(payload, modelPrefix + "-manual-update-secure.bin", outInfo.secureBinUrl)) {
    parseGitHubReleaseAssetUrl(payload, String("FreedomClock-") + version + "-manual-update-secure.bin", outInfo.secureBinUrl);
  }

  normalizeReleaseNotesPreview(outInfo.body);
  return outInfo.tagName.length() > 0;
}

static bool selectReleaseFirmwareUrl(const GitHubReleaseInfo& releaseInfo, bool securePackage, String& outUrl, String& outPackageLabel) {
  outPackageLabel = String(DEVICE_MODEL_NAME) + (securePackage ? " secure package" : " open package");
  outUrl = securePackage ? releaseInfo.secureBinUrl : releaseInfo.openBinUrl;
  outUrl.trim();
  return outUrl.length() > 0;
}

static bool fetchLatestGitHubReleaseInfo(GitHubReleaseInfo& outInfo, char* errorBuf, size_t errorBufSize) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(errorBuf, errorBufSize, "Wi-Fi is not connected.");
    return false;
  }

  if (!ensureClockReadyForTls(errorBuf, errorBufSize)) {
    return false;
  }

  TrustedWiFiClientSecure secureClient;
  if (!configureTrustedTlsClient(secureClient, errorBuf, errorBufSize)) {
    return false;
  }

  HTTPClient http;
  http.setTimeout(PRICE_HTTP_TIMEOUT_MS);
  if (!http.begin(secureClient, GITHUB_LATEST_RELEASE_API_URL)) {
    snprintf(errorBuf, errorBufSize, "Could not start GitHub release request.");
    return false;
  }

  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("User-Agent", "FreedomClock/2026");
  http.addHeader("X-GitHub-Api-Version", GITHUB_API_VERSION);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    if (httpCode == HTTP_CODE_NOT_FOUND) {
      snprintf(errorBuf, errorBufSize, "No published GitHub release found yet.");
    } else if (httpCode == HTTP_CODE_FORBIDDEN || httpCode == 429) {
      snprintf(errorBuf, errorBufSize, "GitHub API rate limited. Try again later.");
    } else {
      snprintf(errorBuf, errorBufSize, "GitHub release request failed (%d).", httpCode);
    }
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (!parseGitHubReleaseInfo(payload, outInfo)) {
    snprintf(errorBuf, errorBufSize, "GitHub release response could not be parsed.");
    return false;
  }

  errorBuf[0] = '\0';
  return true;
}

