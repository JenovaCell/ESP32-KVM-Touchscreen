#!/bin/bash
# Prints the CHANGELOG section for the current VERSION (used as release notes).
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(tr -d '[:space:]' < VERSION)
awk -v v="$version" '
  $0 ~ "^## \\[" v "\\]" { on = 1; next }
  on && /^## \[/ { exit }
  on { print }
' CHANGELOG.md
