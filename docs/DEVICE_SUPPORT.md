# Device Support

Freedom Clock should support the Heltec Vision Master E-series as one product line, not as separate forks.

## Supported Boards

- `E213`: 2.13 inch e-ink, `250 x 122`
- `E290`: 2.90 inch e-ink, `296 x 128`

## How The Firmware Knows The Board

The board does not appear to expose a reliable runtime identity that the sketch can ask for after flashing.

Instead, the Heltec Arduino board package provides compile-time board macros when you select the board in Arduino IDE or `arduino-cli`:

- `HELTEC_VISION_MASTER_E_213` for E213 builds
- `HELTEC_VISION_MASTER_E290` for E290 builds

Freedom Clock uses those macros to select the device profile and display driver:

- `EInkDisplay_VisionMasterE213` for E213
- `EInkDisplay_VisionMasterE290` for E290

That means the project can stay as one source codebase, but each hardware model should get its own compiled `.bin` from the same release.

With the helper tool, build both manual update packages from the same version:

```bash
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v<version> --board e213
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v<version> --board e290
```

## Preferred Architecture

Keep one shared firmware and one release line:

- shared calculations
- shared setup portal
- shared storage format
- shared firmware update flow
- device-specific display profile for screen size, title positions, columns, and spacing

That keeps user settings and release notes unified while still allowing each screen to be laid out properly for the physical display.

## Sketch Naming

The Arduino sketch is device-neutral:

```text
Freedom_Clock_HeltecVME/
  Freedom_Clock_HeltecVME.ino
```

The original E213-specific filename was retired after E290 hardware testing, so Arduino IDE users can open one sketch for both supported devices.

## Implementation Notes

1. Keep calculations and history code display-independent.
2. Keep screen rendering shared where possible, with per-profile layout constants when the panel size needs tuning.
3. Build and test one `.bin` per hardware target from the same release tag.
4. Keep release notes user-facing: one version, with assets named clearly for `E213` and `E290`.

## Current State

The firmware selects the correct display driver at compile time for E213 or E290.

Both E213 and E290 have been hardware-tested with the shared source. The main screen has E290-specific spacing for the wider panel, while setup, calculations, history, and firmware-update logic stay shared.

## Firmware Release Assets

Future public releases should use model-specific filenames:

```text
FreedomClock-<version>-E213-manual-update-open.bin
FreedomClock-<version>-E290-manual-update-open.bin
FreedomClock-<version>-E213-manual-update-secure.bin
FreedomClock-<version>-E290-manual-update-secure.bin
```

The setup page checks the board model and prefers the matching asset. Legacy generic asset names are still accepted as a fallback for older releases.
