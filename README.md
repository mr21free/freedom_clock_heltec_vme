# Freedom Clock

Freedom Clock is a low-power e-ink device for the Heltec Vision Master E213 that turns savings into time.

It has four e-ink screens:
- screen 1: expected freedom time, expected lifetime left, and freedom coverage
- screen 2: input parameters plus firmware version
- screen 3: current wealth and period change stats
- screen 4: freedom change across key lookback periods

The project supports both:
- `BTC` mode: wealth is derived from BTC amount and BTC/USD price
- `WEALTH` mode: wealth is entered directly in USD

BTC can now be sourced in two ways:
- `Static BTC + online price`: you enter the BTC amount manually and the device fetches BTC/USD price from CoinGecko over the internet
- `BTC via MQTT`: BTC amount and BTC/USD price both come from your local MQTT topics

It also supports two withdrawal models:
- `sell`: the portfolio is spent down monthly while wealth growth and inflation are applied
- `borrow`: the portfolio is kept as collateral and refinanced yearly with an annual borrowing fee

It also supports two display themes:
- `light`: white background with black text
- `dark`: black background with white text

## Photos

### Main screen

![Freedom Clock main screen](photos/freedomclock_showcase_screen_1.jpeg)

### Inputs and details screen

![Freedom Clock inputs and details screen](photos/freedomclock_showcase_screen_2.jpeg)

## What The Device Shows

Screen 1:
- owner-specific freedom title
- freedom time in `Y / M / W`
- expected lifetime left in `Y / M / W`
- freedom coverage as a percentage
- device battery percent
- selected light or dark theme

Screen 2:
- shown by a single press of the custom `GPIO21` button
- can also wake the device from deep sleep
- shows asset type, BTC or net worth details, growth, inflation, monthly spending, age now, life expectancy, withdrawal mode, and firmware version

Screen 3:
- opened by a quick double press of `GPIO21` during the wake window
- shows current wealth in USD
- shows wealth change across `7D`, `1M`, `3M`, `6M`, and `12M`
- in `BTC` mode it also shows BTC amount change and BTC price change

Screen 4:
- opened by a quick triple press of `GPIO21` during the wake window
- shows freedom change across `7D`, `1M`, `3M`, `6M`, and `12M`

## Privacy And OPSEC

The current design is intentionally local-first:
- no cloud service is required
- Wi-Fi and MQTT credentials are stored only on the device after setup
- `secrets.h` remains gitignored and is optional
- only the birth year is stored, not the full date of birth
- BTC mode expects local MQTT topics, so it can run fully on a home network

What this means in practice:
- the repository is reasonably safe to share if `secrets.h` stays private
- the device still depends on the trust model of your local Wi-Fi and MQTT broker
- the normal user flow is now on-device local setup instead of firmware edits

Recommended before publishing your own fork:
- replace the tracked sample owner values with your own only in a private local copy
- keep `secrets.h` untracked and never paste live Wi-Fi or MQTT credentials into screenshots
- prefer placeholder hosts such as `mqtt.local` in docs and examples

## Hardware

- Heltec Vision Master E213
- 2.13" e-ink display, `250 x 122`
- ESP32-S3
- optional 3.7V LiPo battery

Product page:
- https://heltec.org/project/vision-master-e213/

## Data Sources

In `BTC` mode, the sketch subscribes to:

```text
home/bitcoin/price/usd
home/bitcoin/wallets/total_btc
```

Use retained MQTT messages so the device receives the latest values quickly after waking.

In `WEALTH` mode, MQTT market data is not required for the main calculation.

In `Static BTC + online price` mode, MQTT is not required, but Wi-Fi internet access is required for live price refreshes.

## Main Firmware Configuration

Most users should not need to edit firmware constants anymore.

On first boot, or whenever the device has no saved config, it starts a local setup access point:
- SSID: `Freedom_Clock_<DEVICE_ID>`
- password: `setup-<DEVICE_ID>`
- portal URL: `http://192.168.4.1`

From that local page you can configure:
- owner name, birth year, and life expectancy
- refresh interval in minutes, hours, or days, defaulting to once per day
- monthly expenses, inflation, growth, borrow fee, and static net worth
- asset mode, including static BTC amount for the static BTC mode, withdrawal mode, and display theme
- optional 6-digit PIN protection for future setup access
- Wi-Fi credentials
- MQTT server, credentials, and local BTC topics

The portal now also reports the board security state at runtime:
- `OPEN`: setup PIN is active, but hardware flash protection is not
- `PARTIAL`: some hardening is active, but not the full production chain
- `FULL`: flash encryption, encrypted NVS, and secure boot are all active

The setup portal also helps validate the configuration before saving:
- it can scan nearby Wi-Fi SSIDs so users usually only need to enter the password
- saved Wi-Fi and MQTT passwords are no longer shown back in plaintext; leave those fields blank to keep them, or enter a new value to replace them
- save stays locked until the current form passes validation
- in `WEALTH` mode, Wi-Fi is optional, but recommended because time sync keeps lifetime and coverage calculations more accurate
- in `WEALTH` mode, validation can still test Wi-Fi connectivity if you provide it
- in `BTC via MQTT` mode, validation checks Wi-Fi, MQTT login, both topics, and numeric payload format
- in `Static BTC + online price` mode, validation checks Wi-Fi, your static BTC amount, and a live BTC/USD price fetch from CoinGecko

If setup PIN protection is enabled:
- the normal clock screens stay unlocked
- the setup portal first shows a PIN unlock page before any saved values can be viewed or edited
- wrong PIN attempts trigger growing delays to slow brute-force tries
- the unlocked setup session times out after a short idle period and asks for the PIN again
- factory reset still clears the PIN along with the rest of the saved configuration

After saving, the settings are stored in ESP32 `Preferences` and the device reboots into normal clock mode.

The setup page also includes a `Firmware Update` section:
- upload a Freedom Clock `.bin` file directly from your phone or laptop
- the device writes the new app image and reboots
- saved settings stay on the device
- security-hardened devices should use signed update files
- a `Check Latest Release` button can fetch the newest published GitHub Release and show its release notes directly on the setup page
- that release check needs working Wi-Fi with internet access while the portal is open

To reopen setup mode later:
- hold the side button while waking the device, then release after about 3 seconds

To clear saved settings and start fresh:
- hold the side button while waking the device, then keep holding for about 10 seconds

The firmware still contains safe defaults in [Freedom_Clock_HeltecVME213.ino](Freedom_Clock_HeltecVME213.ino), mainly as fallbacks and portal prefill values:

```cpp
static constexpr float DEFAULT_MONTHLY_EXP_USD = 10000.0f;
static constexpr float DEFAULT_INFLATION_ANNUAL = 0.02f;
static constexpr float DEFAULT_WEALTH_GROWTH_ANNUAL = 0.10f;
static constexpr float DEFAULT_WEALTH_USD = 1000000.0f;
static constexpr float DEFAULT_BORROW_FEE_ANNUAL = 0.08f;
static constexpr float DEFAULT_MANUAL_BTC_AMOUNT = 0.1f;

static constexpr char DEFAULT_OWNER_NAME[] = "OWNER";
static constexpr int DEFAULT_OWNER_BIRTH_YEAR = 1990;
static constexpr int DEFAULT_OWNER_LIFE_EXPECTANCY_YEARS = 85;
```

Current sample defaults are:
- owner name: `OWNER`
- static BTC amount: `0.1 BTC`
- static wealth: `1000000 USD`
- monthly spending: `10000 USD`
- borrow fee: `8%`
- display theme: `DARK`
- birth year: `1990`
- life expectancy: `85`

Theme options:
- `DARK`: black screen with white text
- `LIGHT`: white screen with black text

## Secrets

`secrets.h` is optional now.

By default, it is not used to prefill customer-facing setup values. That keeps first boot and factory reset behavior generic and safer for a shipped device.

If you want factory or developer bootstrap values from `secrets.h`, opt in explicitly with:

```cpp
#define FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP 1
```

Then the local values below are used as private first-boot defaults before the user saves real settings on the device.

```cpp
#pragma once

static const char* WIFI_SSID   = "YOUR_WIFI_NAME";
static const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";

static const char* MQTT_SERVER = "mqtt.local";
static const int   MQTT_PORT   = 1883;
static const char* MQTT_USER   = "YOUR_MQTT_USER";
static const char* MQTT_PASS   = "YOUR_MQTT_PASSWORD";
```

## Manual Firmware Updates

The first end-user update path is intentionally simple:
- users download a Freedom Clock `.bin` file
- users join the device setup Wi-Fi
- users open `http://192.168.4.1`
- users upload the file in `Firmware Update`

This keeps updates local and easy, without asking normal users to run the production security tool.

Build public manual-update packages on your Mac with:

```bash
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.8
```

That creates a gitignored output folder under:

```text
provisioning-workdir/public-updates/<release-name>/
```

Typical contents:
- `FreedomClock-<version>-manual-update-open.bin`
- `FreedomClock-<version>-manual-update-secure.bin` if your Secure Boot signing key exists on that Mac
- `SHA256SUMS.txt`
- `manifest.json`
- `README.txt`

Recommended publishing model:
- keep the source code, docs, partition table, updater UI, and Mac security tool public on GitHub
- build and test manual update `.bin` files locally first
- publish `.bin` files in GitHub Releases only after explicit release approval
- never publish anything from `provisioning-workdir/` except the final user-facing release files you explicitly choose to share
- never publish device keys, signing keys, or per-device encrypted bundles

Today there is no automatic online updater yet, so manual upload through the setup page is the main user-friendly path.

## Production Security

This repository now includes a custom [partitions.csv](partitions.csv) with an `nvs_keys` partition so the firmware can auto-enable encrypted NVS when the hardware has already been provisioned for flash encryption.

For the repo-side production boundary in one place, see [docs/PRODUCTION_SECURITY.md](docs/PRODUCTION_SECURITY.md).

What this means:
- on a production-hardened device, saved Wi-Fi and MQTT secrets can be protected at rest
- on a normal Arduino-flashed dev board, the setup PIN still helps, but the board is not fully hardware-hardened
- if you provision hardware security after the device already had plaintext settings on it, treat that as a fresh setup and re-enter the config
- this firmware does not include automatic online OTA yet, so fully locked production devices still need a deliberate release strategy

Important limitation:
- a normal Arduino IDE upload still does not burn the eFuses needed for flash encryption or secure boot
- the Heltec/Arduino boot flow uses a prebuilt bootloader, so full Secure Boot V2 still needs a dedicated production provisioning step outside this sketch
- the platform does support a sketch-local `bootloader.bin`, so a production build can replace the default bootloader once you have a secure-boot-capable one

Recommended production target:
- flash encryption in release mode
- encrypted NVS active
- secure boot enabled
- unique per-device keys

Factory reset clears saved settings and history, but it does not undo hardware eFuses once a device has been provisioned.

### Mac provisioning tool

This repo now includes a purpose-built Mac tool for production provisioning:
- [tools/FreedomClockSecurityTool.command](tools/FreedomClockSecurityTool.command)
- [tools/freedom_clock_security_tool.py](tools/freedom_clock_security_tool.py)

It creates and uses a gitignored workspace at `provisioning-workdir/` for:
- release bundles
- public manual-update packages
- per-device flash-encryption keys
- the Secure Boot signing key
- the optional local ESP-IDF checkout used to build the secure-boot-capable bootloader

Typical flow:

```bash
./tools/FreedomClockSecurityTool.command doctor
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.8
./tools/FreedomClockSecurityTool.command bootstrap-idf
./tools/FreedomClockSecurityTool.command build-secure-boot-project
./tools/FreedomClockSecurityTool.command dry-run-provisioning --port /dev/cu.usbmodemXXXX --device-id fc-stage-001
./tools/FreedomClockSecurityTool.command provision-staging --port /dev/cu.usbmodemXXXX --device-id fc-stage-001
./tools/FreedomClockSecurityTool.command provision-production --port /dev/cu.usbmodemXXXX --device-id fc-001
```

Later wired updates for an already locked device use the same tool:

```bash
./tools/FreedomClockSecurityTool.command update-secure-device --port /dev/cu.usbmodemXXXX --device-id fc-001
```

Notes:
- `build-manual-update` creates user-facing `.bin` files for the setup-page `Firmware Update` flow. Publish those on GitHub Releases, not the whole gitignored workspace.
- the setup-page release checker reads the latest published GitHub Release, not just a git tag, so publish an actual GitHub Release when you want users to see update notes there.
- `publish-github-release` creates or updates the real GitHub Release page and uploads the firmware assets, checksums, and manifest only when run with `--confirm-publish`.
- `provision-production` defaults to `full` mode: flash encryption, signed bootloader + app, and final secure-download lock.
- `dry-run-provisioning` prepares the release, keys, and encrypted bundle, then prints the exact board mutations it would perform without touching the board.
- `provision-staging` is the safer first-hardware-trial profile: full security path, but no final secure-download lock and no digest-slot revocation.
- `--mode flash-only` exists for controlled testing, but the intended shipping target is `full`.
- by default the tool keeps the two spare Secure Boot digest slots unused, so you keep some room for future signing-key rotation; only use `--revoke-unused-digests` if you really want the stricter one-key-only posture.
- the firmware now supports local manual app uploads through the setup page; use the `open` package for normal devices and the `secure` package for security-hardened devices.
- keep the cable-based `update-secure-device` path as the manufacturer fallback while automatic online updating is still not built.

For the human release workflow, see [docs/RELEASING.md](docs/RELEASING.md).

## Testing The 3rd And 4th Screens

If you want to test the stats screen and the weekly freedom ritual screen immediately on hardware, you can temporarily seed one year of fake daily history into the same on-device storage the real stats use.

Add this to your local `secrets.h` before flashing:

```cpp
#define FREEDOM_CLOCK_ENABLE_TEST_HISTORY 1
#define FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED 1
```

What it does:
- generates synthetic daily history for the last `366` days
- writes it into the normal `Preferences` history store
- uses the current mode, so `WEALTH` mode seeds only total USD wealth and `BTC` mode also seeds BTC balance and BTC price history
- gives the most recent week a clearer directional move, so the weekly ritual screen is easier to review

Important:
- leave `FREEDOM_CLOCK_FORCE_TEST_HISTORY_RESEED` enabled only while testing, because it overwrites history on every boot
- when you are done, remove those defines and flash again
- factory reset clears the seeded history, just like real history

## Build And Flash

For normal day-to-day firmware development, keep using Arduino IDE or `arduino-cli`.

1. Install the Heltec ESP32 board support and libraries.
2. Open `Freedom_Clock_HeltecVME213.ino` in Arduino IDE.
3. Select the Heltec Vision Master E213 board.
4. Optionally add `secrets.h` and `#define FREEDOM_CLOCK_USE_SECRETS_BOOTSTRAP 1` if you want private bootstrap defaults.
5. Flash the device.
6. Join the device setup Wi-Fi on first boot and save your real configuration.

## Current Status

What is already in good shape:
- local-first design
- local setup portal with on-device persisted settings
- RTC fallback for price, balance, and time
- unique MQTT client id per device
- unique device ID used for setup Wi-Fi naming
- second screen and button wake support
- compile verification for `Heltec-esp32:esp32:heltec_vision_master_e_213`

Remaining hardening opportunities:
- no automated tests
- no explicit stale-data warning on the screen when MQTT values are old
- full Secure Boot V2 still requires a production bootloader/provisioning workflow outside Arduino IDE

## License

MIT
