#!/bin/bash
# Fails if VERSION is malformed or is not the newest entry in CHANGELOG.md.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(tr -d '[:space:]' < VERSION)
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION '$version' is not x.y.z" >&2
  exit 1
fi

newest=$(grep -m1 -E '^## \[[0-9]+\.[0-9]+\.[0-9]+\]' CHANGELOG.md | sed -E 's/^## \[([0-9.]+)\].*/\1/')
if [ "$newest" != "$version" ]; then
  echo "VERSION is $version but the newest CHANGELOG entry is ${newest:-missing}" >&2
  exit 1
fi
echo "version $version OK"
