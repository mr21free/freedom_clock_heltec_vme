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

## Public Release Flow

1. Bump `FIRMWARE_VERSION` in `Freedom_Clock_HeltecVME213.ino`.
2. Commit the source changes.
3. Push `main`.
4. Create and push the Git tag:

```bash
git tag -a v2026.05.05.8 -m "Freedom Clock 2026.05.05.8"
git push origin v2026.05.05.8
```

5. Build the public firmware package:

```bash
./FreedomClockSecurityTool.command build-manual-update --release-name freedom-clock-v2026.05.05.8
```

6. Review or edit the release notes file:

```text
docs/releases/v2026.05.05.8.md
```

7. Publish the actual GitHub Release and upload the assets:

```bash
./FreedomClockSecurityTool.command publish-github-release --release-name freedom-clock-v2026.05.05.8
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
- `publish-github-release` will update an existing release for the same tag if it already exists.
- If a release asset with the same name already exists, the tool replaces it.
- The setup page can only show release notes after a proper GitHub Release has been published.
