# Production Security Workflow

## Short Answer

No, a normal Arduino IDE upload of `Freedom_Clock_HeltecVME213.ino` does **not** enable:
- flash encryption
- encrypted NVS at the hardware level
- Secure Boot V2

What a normal upload does give you:
- the new `partitions.csv` with `nvs_keys`
- firmware that can detect the hardware security state
- firmware that can auto-start encrypted NVS **if** flash encryption has already been provisioned on the board

## Purpose-Built Mac Tool

This repo now includes a host-side provisioning tool:
- [FreedomClockSecurityTool.command](FreedomClockSecurityTool.command)
- [tools/freedom_clock_security_tool.py](tools/freedom_clock_security_tool.py)

The tool is meant to replace the fragile copy-paste workflow with one guided path on your Mac.

It manages:
- Arduino release builds
- public manual-update packages for the setup-page updater
- per-device flash-encryption keys
- the Secure Boot signing key and digest
- a local helper ESP-IDF project that builds a Secure-Boot-capable second-stage bootloader
- signed and encrypted release bundles
- one-shot production provisioning
- later serial updates for already locked devices

Everything sensitive lands in the gitignored `provisioning-workdir/` directory.

### Quickstart

Check the local host toolchain:

```bash
./FreedomClockSecurityTool.command doctor
```

Build the user-facing manual firmware update package:

```bash
./FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.5
```

Bootstrap a local ESP-IDF checkout for the secure-boot helper bootloader:

```bash
./FreedomClockSecurityTool.command bootstrap-idf
./FreedomClockSecurityTool.command build-secure-boot-project
```

Do a no-risk dry run first:

```bash
./FreedomClockSecurityTool.command dry-run-provisioning \
  --port /dev/cu.usbmodemXXXX \
  --device-id fc-stage-001
```

Provision a safer staging board first:

```bash
./FreedomClockSecurityTool.command provision-staging \
  --port /dev/cu.usbmodemXXXX \
  --device-id fc-stage-001
```

Provision a real production device:

```bash
./FreedomClockSecurityTool.command provision-production \
  --port /dev/cu.usbmodemXXXX \
  --device-id fc-001
```

Provision a staging board without the final secure-download lock:

```bash
./FreedomClockSecurityTool.command provision-production \
  --port /dev/cu.usbmodemXXXX \
  --device-id fc-stage-001 \
  --skip-final-secure-download
```

Update an already locked production device later over cable:

```bash
./FreedomClockSecurityTool.command update-secure-device \
  --port /dev/cu.usbmodemXXXX \
  --device-id fc-001
```

Important behavior:
- `build-manual-update` creates the public `.bin` files meant for normal users to upload through the setup page.
- `provision-production` defaults to `full` mode, which means flash encryption + Secure Boot V2.
- `dry-run-provisioning` runs the safe host-side preparation and then prints the exact irreversible commands it would run against the board, without touching the board.
- `provision-staging` uses the safer first-trial profile: full security path, but no final secure-download lock and no digest-slot revocation.
- `--mode flash-only` exists, but it is for controlled experiments and staging, not the intended shipping target.
- the tool asks for typed confirmation before it burns irreversible eFuses, unless you pass `--yes-i-understand`.
- by default the tool keeps the two spare Secure Boot digest slots available for future key rotation; add `--revoke-unused-digests` only if you deliberately want a stricter one-key-only device policy.
- there is no automatic online OTA in the firmware yet.
- the first user-friendly release path is local manual upload through the setup page.
- the serial `update-secure-device` path remains the manufacturer fallback for locked hardware.

## What Goes Public On GitHub

Safe to publish:
- firmware source
- setup-page updater code
- partition table
- Mac provisioning tool source
- documentation
- final user-facing release files you intentionally put into GitHub Releases

Do not publish:
- anything inside `provisioning-workdir/` by default
- Secure Boot signing keys
- per-device flash-encryption keys
- per-device encrypted bundles
- any personal `secrets.h`

## Simple Explanation

### Flash encryption

The ESP32-S3 stores a secret flash key in one-time programmable hardware eFuses.

After that:
- the chip writes flash in encrypted form
- the chip decrypts it automatically when reading
- the user does not type any password
- if someone dumps raw flash, they should not get readable secrets

### Encrypted NVS

`Preferences` / NVS is where this device stores:
- Wi-Fi credentials
- MQTT credentials
- setup PIN hash
- user settings

With the current firmware and [partitions.csv](partitions.csv), once flash encryption is active:
- the device can create NVS encryption keys in the `nvs_keys` partition
- the default NVS partition can then be opened in encrypted mode
- the app code still uses normal NVS APIs, but the storage at rest is protected

### Secure Boot V2

Secure Boot V2 does not hide data. It stops the device from booting unauthorized firmware.

It works like this:
- you keep a private signing key off the device
- the device stores only a digest of the public key in eFuse
- bootloader and app images must be signed
- the chip verifies signatures on every boot

The end user does not get a decryption key. This is a manufacturer / developer responsibility.

## Current Repo Status

This repository is now prepared for the storage side:
- [partitions.csv](partitions.csv) includes `nvs_keys`
- the firmware checks flash encryption and secure boot at runtime
- the firmware will attempt encrypted NVS initialization automatically when the board has been provisioned for flash encryption

This repository is **not** yet a full secure-production build system by itself.

Main reason:
- the Heltec Arduino platform uses a prebuilt bootloader by default
- Secure Boot V2 requires a secure-boot-capable bootloader
- full production provisioning therefore needs an external security workflow

## Important Reality About Updates

The current firmware now supports a local manual updater through the setup page, but it still does **not** implement automatic online OTA yet.

That means the realistic update paths today are:

1. End-user local upload through the setup page using a public `.bin` release file
2. Secure serial update as the manufacturer fallback for locked devices
3. Automatic online OTA later, if you choose to build it

Because automatic online OTA is not implemented yet, I still recommend testing the full release flow carefully on staging hardware before mass-producing fully locked customer devices.

## Recommended Device Roles

Use three classes of boards:

### 1. Development boards

Use these for daily work:
- normal Arduino IDE uploads
- no hardware security eFuses burned
- easy recovery

### 2. Staging boards

Use these to test the production workflow:
- same hardware
- security steps tested end-to-end
- can be wiped or replaced if something goes wrong

### 3. Production boards

Only provision these once:
- firmware is release-ready
- signing-key handling is decided
- update path is decided

## Recommended Workflow For This Project

### Option A: Safe Practical Path Right Now

This is the best path with the current codebase:

1. Keep doing day-to-day development on unsecured boards in Arduino IDE.
2. Freeze a release candidate.
3. Provision one staging board with production security.
4. Test boot, setup, PIN, MQTT, static BTC mode, reset behavior, and recovery.
5. Only then provision real customer devices.

This avoids bricking your main dev board and keeps the release process sane.

## Step-By-Step Production Flow

### Phase 0: Prepare Tools

You already have the needed Espressif tools via the Heltec Arduino package.

On your Mac they are available under:

```text
~/Library/Arduino15/packages/Heltec-esp32/tools/esptool_py/5.2.0/
```

Important tools:
- `esptool`
- `espefuse`
- `espsecure`

You can also install generic versions with `pip install esptool`.

### Phase 1: Finish Development In Arduino

Keep using Arduino IDE or `arduino-cli` for feature work.

At release time:
- compile the firmware
- export the build artifacts
- keep the exact app binary, partition table, and version together

The current repo already carries the partition layout we want:
- [partitions.csv](partitions.csv)

### Phase 2: Decide Update Strategy Before Lock-Down

You need to choose this **before** shipping.

#### Strategy 1: Bench serial updates

Choose this if:
- you update devices yourself
- you are okay with physical access for updates

Requirements:
- keep UART ROM download available in secure mode
- keep a host-side copy of each device's flash-encryption key
- sign and pre-encrypt release binaries before flashing

#### Strategy 2: Future OTA updates

Choose this if:
- customers should update without cables

Requirements:
- implement OTA in the firmware first
- keep signing infrastructure
- preferably do not rely on raw Arduino IDE uploads anymore

Because OTA does not exist in this firmware today, Strategy 1 is the only exact workflow you can execute immediately.

### Phase 3: Generate Production Keys

Generate a Secure Boot V2 signing key.

Recommended example:

```bash
openssl genrsa -out secure_boot_signing_key.pem 3072
```

Or with Espressif tool:

```bash
espsecure generate-signing-key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
```

Generate a per-device flash-encryption key.

For AES-XTS 512-bit:

```bash
espsecure generate-flash-encryption-key --keylen 512 my_flash_encryption_key.bin
```

Generate the secure-boot public key digest:

```bash
espsecure digest-sbv2-public-key --keyfile secure_boot_signing_key.pem --output digest.bin
```

Operational rules:
- keep the signing key offline and backed up
- generate a unique flash-encryption key per device
- never commit these files to the repo

### Phase 4: Start From A Blank Board

Provision from a clean board:

```bash
esptool --port PORT erase-flash
```

Do this on staging boards first, then production boards.

### Phase 5: Burn Flash-Encryption eFuses First

This order matters:
- flash encryption first
- secure boot second

Burn the flash-encryption key into a free key block:

```bash
espefuse --port PORT burn-key BLOCK my_flash_encryption_key.bin XTS_AES_256_KEY
```

Enable flash encryption in production style:

```bash
espefuse --port PORT --chip esp32s3 burn-efuse SPI_BOOT_CRYPT_CNT 7
```

Then burn the production hardening eFuses you want, for example:
- `DIS_DOWNLOAD_ICACHE`
- `DIS_DOWNLOAD_DCACHE`
- `HARD_DIS_JTAG`
- `DIS_DIRECT_BOOT`
- `DIS_USB_JTAG`
- `DIS_DOWNLOAD_MANUAL_ENCRYPT`

Example pattern:

```bash
espefuse --port PORT burn-efuse EFUSE_NAME 0x1
```

Then write-protect those security settings as needed.

### Phase 6: Build A Secure-Boot-Capable Bootloader

This is the most important boundary:

You cannot get full Secure Boot V2 from a plain Arduino upload alone, because the Arduino package uses a prebuilt default bootloader.

For production you need a bootloader built with:
- Secure Boot enabled
- flash encryption release configuration
- UART ROM download mode set to the policy you chose

Practical recommendation:
- create a separate ESP-IDF production build project
- use Arduino as a component there, or build a dedicated release wrapper around this firmware logic

In that production build, configure:
- Enable hardware Secure Boot in bootloader
- Secure Boot v2
- RSA signing
- Enable flash encryption on boot
- Release mode
- UART ROM download mode:
  - `Secure mode` if you still want controlled serial updates
  - `Disabled` only if you are completely sure you never need serial recovery

### Phase 7: Burn Secure-Boot eFuses

Burn the secure-boot public-key digest:

```bash
espefuse --port PORT --chip esp32s3 burn-key BLOCK digest.bin SECURE_BOOT_DIGEST0
```

Enable Secure Boot:

```bash
espefuse --port PORT --chip esp32s3 burn-efuse SECURE_BOOT_EN
```

Then lock down related policy eFuses as appropriate.

Important:
- after secure boot, the secure-boot digest must stay readable by software
- this is why flash encryption must be enabled first

### Phase 8: Sign And Encrypt Release Binaries

With production bootloader and app binaries built:

Sign bootloader and app:

```bash
espsecure sign-data --version 2 --keyfile secure_boot_signing_key.pem --output bootloader-signed.bin build/bootloader/bootloader.bin
espsecure sign-data --version 2 --keyfile secure_boot_signing_key.pem --output app-signed.bin build/app.bin
```

Encrypt the binaries with the device's flash-encryption key:

```bash
espsecure encrypt-flash-data --aes-xts --keyfile my_flash_encryption_key.bin --address 0x0 --output bootloader-enc.bin bootloader-signed.bin
espsecure encrypt-flash-data --aes-xts --keyfile my_flash_encryption_key.bin --address 0x8000 --output partition-table-enc.bin partition-table.bin
espsecure encrypt-flash-data --aes-xts --keyfile my_flash_encryption_key.bin --address 0x10000 --output app-enc.bin app-signed.bin
```

Notes:
- use the correct addresses for your actual layout
- ciphertext depends on flash offset
- if the offset is wrong, the device will not boot

### Phase 9: Flash The Production Images

Flash the encrypted/signed release set:

```bash
esptool --port PORT write-flash \
  0x0 bootloader-enc.bin \
  0x8000 partition-table-enc.bin \
  0x10000 app-enc.bin
```

Also flash any other required partitions according to your final release layout.

### Phase 10: First Boot

Do not interrupt power during first secured boot.

After boot:
- connect to the setup portal
- verify the Security pill
- verify setup still works
- verify reset timing
- verify saved secrets survive reboot

Target result:
- portal shows `Security: FULL`

## What Happens To NVS In This Repo

Once flash encryption is active and the `nvs_keys` partition exists:
- this firmware will look for the NVS key partition
- generate keys if needed
- initialize encrypted NVS

That means the NVS part is already integrated into the current app logic.

## How You Continue Updating Firmware After Lock-Down

### If you keep serial update capability

You can continue updating, but not with normal Arduino IDE one-click upload.

You must:
- build release firmware
- sign it with the Secure Boot key
- encrypt it with that device's flash-encryption key
- flash it with `esptool`

This is workable for bench/service updates, but it is more operationally heavy.

### If you disable serial recovery

Then you must have OTA before shipping, or you effectively have no practical field update path.

### Best practice

Keep two lanes:
- open dev boards for fast iteration
- locked production boards only for signed releases

## What I Recommend For Freedom Clock Right Now

With the firmware as it exists today:

1. Keep your main development board unsecured.
2. Use one sacrificial staging board to test the security workflow.
3. Do not fully mass-provision customer devices until you decide:
   - serial signed+encrypted bench updates, or
   - adding OTA
4. If you want the strongest current path without adding OTA first:
   - keep secure UART download available
   - use per-device flash-encryption keys
   - keep a secure host-side key database
   - sign and pre-encrypt every release manually

## Sources

- Flash Encryption: https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/flash-encryption.html
- Secure Boot v2: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html
- NVS Encryption: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_encryption.html
- Security Features Enablement Workflows: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/security-features-enablement-workflows.html
