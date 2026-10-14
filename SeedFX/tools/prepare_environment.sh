#!/usr/bin/env bash
set -euo pipefail

# Cloud tasks are already isolated. Use this checkout; do not create a worktree.
seedfx_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_root=$(cd "$seedfx_root/.." && pwd)
tool_root=/workspace/daisy-environment
gcc_bin=$tool_root/toolchains/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin
sdk_root=$tool_root/esp-idf
export XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-$tool_root/kicad/config}
export XDG_CACHE_HOME=${XDG_CACHE_HOME:-$tool_root/kicad/cache}
export XDG_DATA_HOME=${XDG_DATA_HOME:-$tool_root/kicad/data}
mkdir -p "$tool_root/downloads" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME"
archive=$tool_root/downloads/xpack-arm-none-eabi-gcc-10.3.1-2.3-linux-x64.tar.gz
if [[ ! -f $archive ]]; then
    curl --fail --show-error --location --max-time 240 \
      https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v10.3.1-2.3/xpack-arm-none-eabi-gcc-10.3.1-2.3-linux-x64.tar.gz \
      -o "$archive"
fi
printf '%s  %s\n' 559dcf1c2dfddac513110fe23da0ef254032c09967eaa901f075515d51818719 "$archive" | sha256sum --check -
if [[ ! -x $gcc_bin/arm-none-eabi-gcc ]]; then
    mkdir -p "$tool_root/toolchains"
    tar -xzf "$archive" -C "$tool_root/toolchains"
fi
[[ $($gcc_bin/arm-none-eabi-gcc -dumpfullversion) == 10.3.1 ]]
if git -C "$repo_root" submodule status --recursive | awk '/^[+U]/ {found=1} END {exit !found}'; then
    echo 'Submodule revisions differ from the recorded commits; preserve local work and investigate.' >&2
    exit 1
fi
git -C "$repo_root" submodule update --init --recursive

if [[ ! -d $sdk_root/.git ]]; then
    git clone --branch v5.4.2 --depth 1 --recursive --shallow-submodules \
      https://github.com/espressif/esp-idf.git "$sdk_root"
fi
[[ $(git -C "$sdk_root" describe --tags --exact-match) == v5.4.2 ]]
export IDF_TOOLS_PATH=$tool_root/esp-tools
python3 "$sdk_root/tools/idf_tools.py" install --targets esp32c3
# Espressif's optional dl.espressif.com constraints/wheel service is blocked in
# this cloud policy. Use the supported primary PyPI route and our tested lock.
# TLS and tool/archive checksums remain enabled.
PIP_CONSTRAINT="$seedfx_root/tools/esp-python.lock" \
  python3 "$sdk_root/tools/idf_tools.py" install-python-env --no-constraints --extra-wheels-url ''

if [[ ! -x $tool_root/dev-python/bin/python ]]; then
    python3 -m venv "$tool_root/dev-python"
fi
PIP_CACHE_DIR=$tool_root/pip-cache "$tool_root/dev-python/bin/pip" install -r "$seedfx_root/tools/cad-python.lock"
command -v kicad-cli >/dev/null
/usr/bin/python3 -c 'import pcbnew; print(pcbnew.GetBuildVersion())'

export GCC_PATH=$gcc_bin
export PATH=$gcc_bin:$tool_root/dev-python/bin:$PATH
make -C "$repo_root/Software/GuitarPedal/dependencies/libDaisy" -j4
make -C "$seedfx_root" test audio
export IDF_PYTHON_CHECK_CONSTRAINTS=no
source "$sdk_root/export.sh"
idf.py -C "$seedfx_root/firmware/wireless" build
echo 'SeedFX host checks and both firmware builds completed.'
