# Secure Device Setup

This guide is for builders who want to ship or sell Freedom Clock devices with hardware security enabled.

If you are only using a device normally, you do not need this file. Use the setup page and normal firmware update flow instead.

## What This Protects

The secure build path is meant to protect saved device data if someone gets physical access to the hardware and tries to dump or replace flash.

It can enable:
- flash encryption, so raw flash dumps are not readable
- encrypted NVS, so saved Wi-Fi, MQTT, setup PIN hash, and settings are protected at rest
- Secure Boot V2, so the device only boots signed firmware

It does not hide what is visible on the e-ink display. If the screen shows a name, age, BTC amount, or wealth estimate, someone holding the device can still see it.

## Simple Explanation

Flash encryption is handled by the ESP32-S3 itself. The chip stores a secret hardware key in one-time programmable eFuses. Users do not type or receive a decryption key.

Encrypted NVS protects the `Preferences` storage used by the firmware. The app still reads and writes settings normally, but the stored bytes are encrypted when the device has been provisioned correctly.

Secure Boot V2 verifies firmware signatures during boot. You keep the private signing key off the device. The device stores only a public-key digest in eFuse.

## Important Reality

A normal Arduino IDE upload of `Freedom_Clock_HeltecVME213.ino` does not enable flash encryption or Secure Boot V2.

Arduino uploads are still perfect for development, but hardware security needs a separate provisioning step because it burns one-way eFuses.

Factory reset clears saved settings and history. It does not undo burned eFuses.

## Recommended Device Roles

Keep this simple:
- `dev`: an unlocked board for normal Arduino IDE development
- `secure test`: one board used to test the full secure flow before shipping anything
- `production`: locked devices you are comfortable giving to real users

Do not burn security eFuses on your only development board.

## Tool

This repo includes a Mac helper tool:
- [../tools/FreedomClockSecurityTool.command](../tools/FreedomClockSecurityTool.command)
- [../tools/freedom_clock_security_tool.py](../tools/freedom_clock_security_tool.py)

The tool writes generated keys, release bundles, and per-device artifacts into the gitignored `provisioning-workdir/` directory.

Never commit:
- `secrets.h`
- anything from `provisioning-workdir/` unless it is a final public `.bin` release asset you intentionally publish
- Secure Boot signing keys
- per-device flash-encryption keys
- GitHub API tokens

## Public Firmware Packages

Build normal user-facing update files with:

```bash
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.9
```

This creates files under:

```text
provisioning-workdir/public-updates/freedom-clock-v2026.05.05.9/
```

Typical output:
- `FreedomClock-<version>-manual-update-open.bin`
- `FreedomClock-<version>-manual-update-secure.bin` if a signing key exists locally
- `SHA256SUMS.txt`
- `manifest.json`
- `README.txt`

Use the `open` package for normal unlocked devices. Use the `secure` package for security-hardened devices.

## Secure Test Flow

Run the host checks first:

```bash
./tools/FreedomClockSecurityTool.command doctor
```

Prepare the secure boot helper pieces:

```bash
./tools/FreedomClockSecurityTool.command bootstrap-idf
./tools/FreedomClockSecurityTool.command build-secure-boot-project
```

Do a no-risk dry run:

```bash
./tools/FreedomClockSecurityTool.command dry-run-provisioning --port /dev/cu.usbmodemXXXX --device-id fc-test-001
```

Then test one secure board:

```bash
./tools/FreedomClockSecurityTool.command provision-secure-test --port /dev/cu.usbmodemXXXX --device-id fc-test-001
```

This follows the security path while avoiding the most final lock settings, which makes the first hardware trial safer. The old `provision-staging` command still exists as a compatibility alias.

## Production Flow

Only use this after you have tested the exact firmware on a secure test device:

```bash
./tools/FreedomClockSecurityTool.command provision-production --port /dev/cu.usbmodemXXXX --device-id fc-001
```

By default this targets:
- flash encryption in release mode
- encrypted NVS
- Secure Boot V2
- signed bootloader and app
- final secure-download lock

The tool asks for typed confirmation before it burns irreversible eFuses unless you explicitly pass `--yes-i-understand`.

## Updating Locked Devices

Normal users should update through the setup page:
- direct `Install Latest Firmware` if the device has working Wi-Fi with internet access
- manual `.bin` upload if the phone/laptop already downloaded the firmware file

For a manufacturer fallback over USB:

```bash
./tools/FreedomClockSecurityTool.command update-secure-device --port /dev/cu.usbmodemXXXX --device-id fc-001
```

Keep the signing key backed up securely. If you lose the key used for Secure Boot updates, you may not be able to update locked devices.

## GitHub Releases

The setup page checks real GitHub Releases, not plain git tags.

Release rule:
- build and test locally first
- publish a GitHub Release only after explicit human approval
- attach only final user-facing `.bin` files, checksums, manifest, and release notes

The publish command is dry-run by default:

```bash
./tools/FreedomClockSecurityTool.command publish-github-release --release-name freedom-clock-v2026.05.05.9
```

Only this publishes:

```bash
./tools/FreedomClockSecurityTool.command publish-github-release --release-name freedom-clock-v2026.05.05.9 --confirm-publish
```

## Recovery Expectations

Before provisioning production devices, assume:
- eFuses are one-way
- factory reset does not undo hardware security
- a wrong production flow can make normal Arduino uploads stop working
- lost signing keys can block future secure updates

That is why the recommended path is dev board first, secure test board second, production devices last.
