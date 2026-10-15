#!/usr/bin/env bash
# Prepare the locally built test component for deterministic CI discovery.
set -euo pipefail
component=${1:?Path to ToneDock.component is required}
target="$HOME/Library/Audio/Plug-Ins/Components/ToneDock.component"
mkdir -p "$(dirname "$target")"
ditto "$component" "$target"
# The development bundle uses an ad-hoc signature. Distribution signing is a
# separate release step; this does not disable macOS security checks.
codesign --force --sign - --timestamp=none "$target"
codesign --verify --deep --strict "$target"
plutil -p "$target/Contents/Info.plist"
file "$target/Contents/MacOS/ToneDock"
# This is an isolated CI user's registrar, never the user's desktop session.
killall AudioComponentRegistrar 2>/dev/null || true
auval -a
auval -v aufx Tnd1 Tndk
