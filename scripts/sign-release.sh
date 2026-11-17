#!/usr/bin/env bash
# Signs a drafted GitHub release so the app's updater accepts it.
#
#   scripts/sign-release.sh v0.2.0 [secret key file]
#
# Downloads the release's installers and SHA256SUMS.txt, checks that the
# list matches the files, signs it with the offline minisign key and uploads
# SHA256SUMS.txt.minisig to the release. VloomDAW offers an update only when
# that signature verifies against packaging/update/minisign.pub, so run this
# after trying the installers and before publishing the draft.
#
# The secret key defaults to $MINISIGN_SECRET_KEY, then ~/.minisign/vloomdaw.key.
# Needs: gh (logged in), minisign, sha256sum or shasum. See docs/releasing.md.
set -euo pipefail

readonly repo="vloom-audio/VloomDAW"

tag="${1:-}"
if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
    echo "usage: $0 v<major>.<minor>.<patch>[-<pre-release>] [secret key file]" >&2
    exit 2
fi
secret_key="${2:-${MINISIGN_SECRET_KEY:-$HOME/.minisign/vloomdaw.key}}"

cd "$(dirname "$0")/.."
readonly public_key="packaging/update/minisign.pub"

for tool in gh minisign; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found" >&2; exit 1; }
done
[[ -f "$secret_key" ]] || { echo "error: no secret key at $secret_key" >&2; exit 1; }
[[ -f "$public_key" ]] || { echo "error: $public_key is missing (see docs/releasing.md)" >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then
    check_sums=(sha256sum --check --strict)
else
    check_sums=(shasum -a 256 --check --strict)
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "Downloading $tag from $repo…"
gh release download "$tag" --repo "$repo" --dir "$work" --pattern SHA256SUMS.txt --pattern 'VloomDAW-*'

# Vouch only for what was checked: every listed file must be here and match.
(cd "$work" && "${check_sums[@]}" SHA256SUMS.txt)

minisign -S -s "$secret_key" -m "$work/SHA256SUMS.txt" -t "VloomDAW $tag SHA256SUMS.txt"
# The signature must verify with the key built into the app.
minisign -V -p "$public_key" -m "$work/SHA256SUMS.txt"

gh release upload "$tag" --repo "$repo" --clobber "$work/SHA256SUMS.txt.minisig"
echo "Signed: $tag now carries SHA256SUMS.txt.minisig. Publish the draft when ready."
