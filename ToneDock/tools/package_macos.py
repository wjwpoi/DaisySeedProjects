#!/usr/bin/env python3
"""Archive macOS bundles before artifact upload, preserving modes and symlinks."""
import argparse
import tarfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=Path('ToneDock/build'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = args.build.resolve()
bundles = [
    (build / 'ToneDockApp_artefacts/Release/ToneDock.app', 'ToneDock.app'),
    (build / 'ToneDock_artefacts/Release/AU/ToneDock.component', 'ToneDock.component'),
    (build / 'ToneDock_artefacts/Release/VST3/ToneDock.vst3', 'ToneDock.vst3'),
]
assets = bundles + [
    (build / 'notices', 'notices'),
    (root / 'packaging', 'packaging'),
    (root / 'docs', 'docs'),
    (root / 'README.md', 'README.md'),
]
for source, name in assets:
    if not source.exists():
        raise SystemExit(f'Missing package input: {source}')
for bundle, name in bundles:
    executable = bundle / 'Contents/MacOS/ToneDock'
    if not executable.is_file() or not executable.stat().st_mode & 0o111:
        raise SystemExit(f'Missing executable or execute permission: {executable}')
archive = build / 'ToneDock-macOS-development.tar.gz'
with tarfile.open(archive, 'w:gz', dereference=False) as output:
    for source, name in assets:
        output.add(source, arcname=name)
print(f'Packaged macOS bundles with executable modes in {archive}')
