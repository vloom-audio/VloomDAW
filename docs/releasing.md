# Releasing

Installers are built by GitHub Actions ([`release.yml`](../.github/workflows/release.yml)):

| Platform | File | Made with |
|---|---|---|
| Windows 10/11 x64 | `VloomDAW-<version>-windows-x64-setup.exe` | `windeployqt`, Inno Setup 6 ([`packaging/windows/VloomDAW.iss`](../packaging/windows/VloomDAW.iss)) |
| macOS 13+ (Apple silicon and Intel) | `VloomDAW-<version>-macos-universal.dmg` | `macdeployqt`, `hdiutil` |
| Linux x86_64 (glibc 2.35+) | `VloomDAW-<version>-linux-x86_64.AppImage` | linuxdeploy + Qt plugin (pinned, SHA-256 checked) |

## Making a release

1. Update `VERSION` in the top-level `CMakeLists.txt` and the `CHANGELOG.md`
   section, and merge that to `main`.
2. Tag the merge commit and push the tag:
   ```sh
   git tag -a v0.2.0 -m "VloomDAW 0.2.0"
   git push origin v0.2.0
   ```
3. The workflow builds the three installers and opens a **draft** GitHub
   Release with them and `SHA256SUMS.txt`. Try the installers, edit the
   notes, then publish the draft.

To get installers without releasing, run the workflow from the Actions tab
("Run workflow"): they are attached to the run as artifacts for 14 days, with
versions like `0.1.0-dev.1a2b3c4`. A pull request that changes `packaging/`
or the workflow builds them too.

## Signing (not yet)

The installers are not code-signed:

- **Windows:** SmartScreen shows "Windows protected your PC". Choose
  *More info* > *Run anyway*.
- **macOS:** the app is only ad-hoc signed (required to run on Apple
  silicon) and not notarised. The first time, right-click the app >
  *Open*, or allow it in *System Settings* > *Privacy & Security*.

Signing needs a Windows code-signing certificate (or Azure Trusted Signing)
and an Apple Developer ID with notarisation. When they exist, add them as
repository secrets and a signing step per platform in `release.yml`.
