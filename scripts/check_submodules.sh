#!/usr/bin/env bash
# Fail early with an actionable message when required vendored sources are absent.

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if ! command -v git >/dev/null 2>&1 || [[ ! -f .gitmodules ]]; then
    exit 0
fi

missing="$(git submodule status --recursive 2>/dev/null | awk '$1 ~ /^-/ { print $2 }')"
if [[ -n "$missing" ]]; then
    echo "Required git submodules are not initialized:" >&2
    while IFS= read -r path; do
        echo "  - $path" >&2
    done <<< "$missing"
    echo "Run: git submodule update --init --recursive" >&2
    echo "Or run scripts/setup.sh for a complete first-time setup." >&2
    exit 1
fi
