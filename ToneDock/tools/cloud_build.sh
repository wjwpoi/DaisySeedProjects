#!/usr/bin/env bash
# Rootless Debian cloud setup. Windows/macOS use the normal CMake commands.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps_root=${TONEDOCK_DEPS:-/workspace/tonedock-deps}
mkdir -p "$deps_root"
if [[ ! -x "$deps_root/python/bin/cmake" ]]; then
  python3 -m venv "$deps_root/python"
  "$deps_root/python/bin/python" -m pip install 'cmake==3.30.5' 'ninja==1.11.1.1'
fi
fetch_dependency() {
  local path=$1 remote=$2 commit=$3
  if [[ ! -d "$path/.git" ]]; then
    git init "$path"
    git -C "$path" remote add origin "$remote"
    git -C "$path" fetch --depth 1 origin "$commit"
    git -C "$path" checkout --detach "$commit"
  fi
  [[ "$(git -C "$path" rev-parse HEAD)" == "$commit" ]] || { echo "Unexpected dependency revision: $path" >&2; exit 1; }
  git -C "$path" diff --quiet
  git -C "$path" diff --cached --quiet
}
fetch_dependency "$deps_root/JUCE" https://github.com/juce-framework/JUCE.git 29396c22c93392d6738e021b83196283d6e4d850
fetch_dependency "$deps_root/nam" https://github.com/sdatkinson/NeuralAmpModelerCore.git 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842
git -C "$deps_root/nam" submodule update --init --depth 1
mkdir -p "$deps_root/apt/empty-parts" "$deps_root/apt/empty-sources" "$deps_root/apt/lists/partial" "$deps_root/apt/cache/archives/partial" "$deps_root/apt/log" "$deps_root/sysroot"
python3 - "$deps_root" <<'PY'
import sys
from pathlib import Path
root=Path(sys.argv[1]); apt=root/'apt'
(apt/'empty.conf').touch()
fields={
 'Dir::Etc::parts':apt/'empty-parts','Dir::Etc::main':apt/'empty.conf',
 'Dir::Etc::sourcelist':apt/'sources.list','Dir::Etc::sourceparts':apt/'empty-sources',
 'Dir::State::lists':apt/'lists','Dir::Cache':apt/'cache','Dir::Log':apt/'log',
 'Debug::NoLocking':'true','Acquire::Retries':'0','APT::Update::Error-Mode':'any'}
(apt/'apt.conf').write_text(''.join(f'{k} "{v}";\n' for k,v in fields.items()))
(apt/'sources.list').write_text('deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main\n')
PY
export APT_CONFIG="$deps_root/apt/apt.conf"
apt-get -o "Acquire::http::Proxy=${HTTP_PROXY:-}" -o "Acquire::https::Proxy=${HTTPS_PROXY:-}" update
apt-get -y --download-only -o "Acquire::http::Proxy=${HTTP_PROXY:-}" -o "Acquire::https::Proxy=${HTTPS_PROXY:-}" install \
  libasound2-dev libfreetype-dev libfontconfig-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxfixes-dev xvfb xauth
python3 - "$deps_root" <<'PY'
import sys,subprocess,shlex
from pathlib import Path
root=Path(sys.argv[1]); sysroot=root/'sysroot'
for deb in (root/'apt/cache/archives').glob('*.deb'):
 subprocess.run(['dpkg-deb','-x',str(deb),str(sysroot)],check=True)
for pc in sysroot.glob('usr/**/pkgconfig/*.pc'):
 text=pc.read_text()
 if str(sysroot) not in text:pc.write_text(text.replace('/usr',str(sysroot/'usr')))
for link in (sysroot/'usr/lib/x86_64-linux-gnu').glob('*.so'):
 if link.is_symlink() and not link.exists():
  runtime=Path('/usr/lib/x86_64-linux-gnu')/link.readlink().name
  if runtime.exists():link.unlink();link.symlink_to(runtime)
q=shlex.quote(str(root))
(root/'activate.sh').write_text(f'''export TONEDOCK_DEPS={q}
export PATH="$TONEDOCK_DEPS/python/bin:$TONEDOCK_DEPS/sysroot/usr/bin:$PATH"
export PKG_CONFIG_PATH="$TONEDOCK_DEPS/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$TONEDOCK_DEPS/sysroot/usr/share/pkgconfig${{PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}}"
export CPLUS_INCLUDE_PATH="$TONEDOCK_DEPS/sysroot/usr/include:$TONEDOCK_DEPS/sysroot/usr/include/freetype2${{CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}}"
export LIBRARY_PATH="$TONEDOCK_DEPS/sysroot/usr/lib/x86_64-linux-gnu${{LIBRARY_PATH:+:$LIBRARY_PATH}}"
export CMAKE_BUILD_PARALLEL_LEVEL=2
export XDG_CACHE_HOME="${{XDG_CACHE_HOME:-$TONEDOCK_DEPS/cache}}"
mkdir -p "$XDG_CACHE_HOME"
''')
PY
source "$deps_root/activate.sh"
pkg-config --exists freetype2 fontconfig alsa
# Configure the helper explicitly to avoid retaining negative pkg-config probes
# from an earlier attempt before local headers were installed.
cmake -S "$deps_root/JUCE" -B "$project_root/build/_deps/juce-build/tools" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$deps_root/python/bin/ninja" -DJUCE_BUILD_HELPER_TOOLS=ON \
  -DCMAKE_BUILD_TYPE=Custom -U '*juce_graphics_LINUX_DEPS*'
cmake -S "$project_root" -B "$project_root/build" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$deps_root/python/bin/ninja" -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$deps_root/JUCE" -DFETCHCONTENT_SOURCE_DIR_NAM="$deps_root/nam" \
  -DCMAKE_PREFIX_PATH="$deps_root/sysroot/usr"
cmake --build "$project_root/build" --config Release --parallel 2
xvfb-run -a ctest --test-dir "$project_root/build" --output-on-failure
