# Releasing

Installers are built by GitHub Actions ([`release.yml`](../.github/workflows/release.yml)):

| Platform | File | Made with |
|---|---|---|
| Windows 10/11 x64 | `VloomDAW-<version>-windows-x64-setup.exe` | `windeployqt`, Inno Setup 6 ([`packaging/windows/VloomDAW.iss`](../packaging/windows/VloomDAW.iss)) |
| macOS 13+ (Apple silicon and Intel) | `VloomDAW-<version>-macos-universal.dmg` | `macdeployqt`, `hdiutil` |
| Linux x86_64 (glibc 2.35+) | `VloomDAW-<version>-linux-x86_64.AppImage` | linuxdeploy + Qt plugin (pinned, SHA-256 checked) |

On Windows the installed folder holds only `bin\` (`VloomDAW.exe`, its helper programs,
every DLL, `qt.conf` and the Qt plugins in `bin\plugins`), `licenses\` and the uninstaller.
The workflow stages exactly that with `cmake --install` and
`windeployqt --plugindir stage/bin/plugins`, and Inno Setup installs the stage folder as it is.

## Making a release

1. Update `VERSION` in the top-level `CMakeLists.txt` and the `CHANGELOG.md`
   section, and merge that to `main`.
2. Tag the merge commit and push the tag:
   ```sh
   git tag -a v0.2.0 -m "VloomDAW 0.2.0"
   git push origin v0.2.0
   ```
3. The workflow builds the three installers and opens a **draft** GitHub
   Release with them and `SHA256SUMS.txt`. Try the installers and edit the
   notes.
4. Sign the update on the machine that holds the key (see below), then
   publish the draft:
   ```sh
   scripts/sign-release.sh v0.2.0
   ```
   Installed copies of VloomDAW offer the release only once
   `SHA256SUMS.txt.minisig` is attached; a release published without it is
   simply not offered.

A tag with a pre-release part (`v0.3.0-rc.1`) is offered only to users who
turned on *Include beta versions*. The tag's version must be newer than the
installed one by [semver](https://semver.org) rules, and its numbers must
match `VERSION` in `CMakeLists.txt` (the workflow checks).

To get installers without releasing, run the workflow from the Actions tab
("Run workflow"): they are attached to the run as artifacts for 14 days, with
versions like `0.1.0-dev.1a2b3c4`. A pull request that changes `packaging/`
or the workflow builds them too.

## Signing the update

VloomDAW updates itself (`ui/update/`): it reads the release list from the
GitHub API, and installs a release only if

1. `SHA256SUMS.txt` has a valid [minisign](https://jedisct1.github.io/minisign/)
   signature (`SHA256SUMS.txt.minisig`) by the public key built into the
   app, `packaging/update/minisign.pub`; and
2. the installer's SHA-256 matches its line in that signed list.

The secret key never goes to GitHub or CI: a compromised workflow, token or
release can then not ship an update that installed copies accept.

### One-time setup

```sh
# Windows: scoop install minisign (or winget install jedisct1.minisign)
# macOS:   brew install minisign       Linux: your package manager
mkdir -p ~/.minisign
minisign -G -p packaging/update/minisign.pub -s ~/.minisign/vloomdaw.key
```

Choose a strong password, back up `vloomdaw.key` offline (two places), and
commit `packaging/update/minisign.pub`. CMake builds the key into the app
(`VLOOMDAW_UPDATE_KEY_FILE`); a build without the file tells users about new
versions but only links to their page.

**If the secret key is lost**, installed copies cannot be updated
automatically any more: make a new key, commit the new `minisign.pub`, and
ask users to install that release by hand once. **If it leaks**, do the same
at once: anyone holding it can sign updates that installed copies accept.

## Code signing (not yet)

The installers are not code-signed:

- **Windows:** SmartScreen shows "Windows protected your PC". Choose
  *More info* > *Run anyway*.
- **macOS:** the app is only ad-hoc signed (required to run on Apple
  silicon) and not notarised. The first time, right-click the app >
  *Open*, or allow it in *System Settings* > *Privacy & Security*.

Signing needs a Windows code-signing certificate (or Azure Trusted Signing)
and an Apple Developer ID with notarisation. When they exist, add them as
repository secrets and a signing step per platform in `release.yml`.
