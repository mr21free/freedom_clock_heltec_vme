# Releasing Freedom Clock

This is the clean release flow for public firmware updates.

## What Tags Do

Git tags only give you GitHub's automatic source archives like `.zip` and `.tar.gz`.

They do **not** create a proper GitHub Release page with:

- release notes
- manual firmware `.bin` assets
- checksums
- a latest-release record for the setup-page updater

The setup-page `Check Latest Release` button reads GitHub's **published Releases**, not plain tags.

## Release Rule

Build and test firmware locally first.

Only publish to GitHub Releases after explicit human approval.

During development, it is normal to:

- bump the firmware version
- build a manual update package
- test it on a device
- revise again

Do not publish every local test package. Published Releases are for versions you are comfortable showing to users.

## Local Test Flow

1. Bump `FIRMWARE_VERSION` in `Freedom_Clock_HeltecVME213.ino`.
2. Build the local firmware package:

```bash
./tools/FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.8
```

3. Upload the `open` package through the device setup page.
4. Test the firmware on hardware.
5. Edit the release notes while testing:

```text
docs/releases/v2026.05.05.8.md
```

## Public Release Flow

After the local test looks good:

1. Commit the source changes.
2. Push `main`.
3. Create and push the Git tag:

```bash
git tag -a v2026.05.05.8 -m "Freedom Clock 2026.05.05.8"
git push origin v2026.05.05.8
```

4. Dry-run the GitHub Release publish command:

```bash
./tools/FreedomClockSecurityTool.command publish-github-release --release-name freedom-clock-v2026.05.05.8
```

5. If the dry-run output looks correct, publish the real GitHub Release:

```bash
./tools/FreedomClockSecurityTool.command publish-github-release --release-name freedom-clock-v2026.05.05.8 --confirm-publish
```

That uploads:

- `FreedomClock-<version>-manual-update-open.bin`
- `FreedomClock-<version>-manual-update-secure.bin` if available
- `SHA256SUMS.txt`
- `manifest.json`
- `README.txt`

## Notes

- Use a token with repo contents/release write permission.
- The tool reads a token from `FREEDOM_CLOCK_GITHUB_TOKEN`, `GITHUB_TOKEN`, `GH_TOKEN`, or `~/.freedom-clock/github-token`.
- Without `--confirm-publish`, `publish-github-release` only prints what it would publish.
- `publish-github-release` will update an existing release for the same tag if it already exists.
- If a release asset with the same name already exists, the tool replaces it.
- The setup page can only show release notes after a proper GitHub Release has been published.
