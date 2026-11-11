#!/usr/bin/env bash
# Sets up a VloomDAW development workspace:
#   - clones vloom-engine next to this repo (../vloom-engine) if it is missing
#   - prints the next commands to build
#
# Usage: scripts/bootstrap.sh
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
engine_dir="$(dirname "$repo_root")/vloom-engine"

if [[ -d "$engine_dir/.git" ]]; then
    echo "✓ vloom-engine found at $engine_dir"
else
    echo "→ cloning vloom-engine into $engine_dir"
    git clone https://github.com/vloom-audio/vloom-engine.git "$engine_dir"
fi

if [[ -z "${QT_DIR:-}" ]]; then
    if command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
        suggestion="$(brew --prefix qt)"
    else
        suggestion="<path to Qt>/6.x/<platform>"
    fi
    echo "! QT_DIR is not set. Add this to your shell profile:"
    echo "    export QT_DIR=$suggestion"
fi

cat <<'EOF'

Next:
  cmake --preset dev
  cmake --build --preset dev
  ctest --preset dev
EOF
