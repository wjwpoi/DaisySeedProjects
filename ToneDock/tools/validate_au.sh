#!/usr/bin/env bash
# Prepare the locally built test component for deterministic CI discovery.
set -euo pipefail
component=${1:?Path to ToneDock.component is required}
target=${2:-"$HOME/Library/Audio/Plug-Ins/Components/ToneDock.component"}
mkdir -p "$(dirname "$target")"
# The development bundle uses an ad-hoc signature. Distribution signing is a
# separate release step; this does not disable macOS security checks.
codesign --force --sign - --timestamp=none "$component"
codesign --verify --deep --strict "$component"
# Install the signed build output so the validated bundle and uploaded bundle
# contain the same signature, executable and resources.
ditto "$component" "$target"
codesign --verify --deep --strict "$target"
plutil -p "$target/Contents/Info.plist"
file "$target/Contents/MacOS/ToneDock"
for architecture in arm64 x86_64; do
    echo "Checking exported AU factory for $architecture"
    if ! nm -arch "$architecture" -gU "$target/Contents/MacOS/ToneDock" |
        awk '$NF == "_ToneDockAUFactory" {print; found=1} END {exit !found}'; then
        echo "Missing ToneDockAUFactory export for $architecture" >&2
        exit 1
    fi
done
# This is an isolated CI user's registrar, never the user's desktop session.
killall AudioComponentRegistrar 2>/dev/null || true
touch "$target"
# The registrar refreshes asynchronously after a newly installed bundle. Wait
# for discovery, then run the full validator once and preserve its result.
component_list=$(mktemp "${TMPDIR:-/tmp}/tonedock-au-list.XXXXXX")
trap 'rm -f "$component_list"' EXIT
discovered=false
for (( attempt=1; attempt<=30; ++attempt )); do
    if ! auval -a > "$component_list" 2>&1; then
        cat "$component_list"
        exit 1
    fi
    if awk '$1 == "aufx" && $2 == "Tnd1" && $3 == "Tndk" {found=1} END {exit !found}' "$component_list"; then
        discovered=true
        break
    fi
    if (( attempt == 1 || attempt == 10 || attempt == 20 )); then
        echo "Waiting for ToneDock AU registration ($attempt/30)"
    fi
    if (( attempt < 30 )); then sleep 1; fi
done
if [[ "$discovered" != true ]]; then
    cat "$component_list"
    otool -L "$target/Contents/MacOS/ToneDock" || true
    echo "AudioComponentRegistrar did not discover aufx / Tnd1 / Tndk within 30 seconds" >&2
    exit 2
fi
awk '$1 == "aufx" && $2 == "Tnd1" && $3 == "Tndk"' "$component_list"
auval -v aufx Tnd1 Tndk
