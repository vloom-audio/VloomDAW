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
   Release with them and `SHA256SUMS.txt`. Try the installers and edit the
   notes.
4. Approve the signing: the run's **sign the release** job waits for a
   reviewer of the `release-signing` environment (Actions > the run >
   *Review deployments* > *Approve and deploy*). Approve only a tag you
   pushed, after trying its installers. It checks the installers against
   `SHA256SUMS.txt`, signs the list with the update key, verifies the
   signature against `packaging/update/minisign.pub` and attaches
   `SHA256SUMS.txt.minisig` to the draft.
5. Publish the draft.

Installed copies of VloomDAW offer the release only once
`SHA256SUMS.txt.minisig` is attached; a release published without it is
simply not offered. Without CI signing (or if the environment isn't set
up), sign on the machine that holds the key instead:
`scripts/sign-release.sh v0.2.0`.

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

### Signing in CI (needs an approval)

The `sign` job in `release.yml` reads the key from the **`release-signing`**
environment, which only a run that a maintainer approves can open. Set it up
once (Settings > Environments, or the commands below):

1. Create the environment with **Required reviewers** (the maintainers who may
   release), **Prevent self-review** off if you release alone, and
   **Deployment branches and tags** limited to the tag pattern `v*`, so a
   branch or pull request can never reach it.
2. Add the environment secrets (not repository secrets):
   ```sh
   gh secret set MINISIGN_SECRET_KEY --env release-signing < ~/.minisign/vloomdaw.key
   gh secret set MINISIGN_PASSWORD --env release-signing   # prompts; not saved in shell history
   ```
3. Keep two-factor authentication on for every account that can approve or
   change environments, and keep the offline copies of the key.

Trade-off: with the key in GitHub, someone who controls a maintainer account
(or the repository settings) can sign an update; the approval step and the
tag rule stop everything short of that. The job writes the key only to the
runner's temporary folder, removes it after signing, and runs no third-party
action other than `actions/checkout` (pinned).

**If the secret key is lost**, installed copies cannot be updated
automatically any more: make a new key, commit the new `minisign.pub`, and
ask users to install that release by hand once. **If it leaks**, do the same
at once: anyone holding it can sign updates that installed copies accept;
also replace `MINISIGN_SECRET_KEY` and `MINISIGN_PASSWORD` in the
`release-signing` environment.

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
