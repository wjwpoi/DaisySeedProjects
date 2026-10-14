#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export XDG_CONFIG_HOME=${XDG_CONFIG_HOME:-/workspace/daisy-environment/kicad/config}
export XDG_CACHE_HOME=${XDG_CACHE_HOME:-/workspace/daisy-environment/kicad/cache}
export XDG_DATA_HOME=${XDG_DATA_HOME:-/workspace/daisy-environment/kicad/data}
export KICAD9_FOOTPRINT_DIR=${KICAD9_FOOTPRINT_DIR:-/usr/share/kicad/footprints}
export KICAD9_SYMBOL_DIR=${KICAD9_SYMBOL_DIR:-/usr/share/kicad/symbols}
mkdir -p "$root/hardware/manufacturing/mainboard" "$root/hardware/manufacturing/adapter" "$root/hardware/reports"

for item in mainboard adapter; do
    if [[ $item == mainboard ]]; then name=DaisySeedPedal125b; else name=seedfx-adapter; fi
    pcb=$root/hardware/$item/$name.kicad_pcb
    sch=$root/hardware/$item/$name.kicad_sch
    out=$root/hardware/manufacturing/$item
    kicad-cli pcb drc --format json --schematic-parity -o "$root/hardware/reports/$item-drc.json" "$pcb"
    kicad-cli sch erc --format json -o "$root/hardware/reports/$item-erc.json" "$sch"
    kicad-cli sch export pdf -o "$out/schematic.pdf" "$sch"
    kicad-cli sch export netlist --format kicadxml -o "$root/hardware/reports/$item-netlist.xml" "$sch"
    kicad-cli pcb export gerbers -o "$out/gerbers/" \
      --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Paste,B.Paste,F.SilkS,B.SilkS,Edge.Cuts "$pcb"
    kicad-cli pcb export drill --format excellon --excellon-units mm \
      --generate-map --map-format pdf -o "$out/drill/" "$pcb"
    kicad-cli pcb export pdf --layers F.Fab,B.Fab,Edge.Cuts -o "$out/assembly.pdf" "$pcb"
    kicad-cli pcb export pos --format csv --units mm --smd-only -o "$out/positions.csv" "$pcb"
done
kicad-cli pcb export step --board-only --force -o "$root/mechanical/mainboard.step" \
  "$root/hardware/mainboard/DaisySeedPedal125b.kicad_pcb"
python3 - "$root" <<'PY'
import json,sys
from pathlib import Path
root=Path(sys.argv[1])
for board in ('mainboard','adapter'):
    report=json.loads((root/f'hardware/reports/{board}-drc.json').read_text())
    errors=[v for v in report['violations'] if v['severity']=='error']
    if errors or report['unconnected_items'] or report['schematic_parity']:
        raise SystemExit(f'{board}: electrical/geometric DRC failed')
    erc=json.loads((root/f'hardware/reports/{board}-erc.json').read_text())
    errors=[v for s in erc['sheets'] for v in s['violations'] if v['severity']=='error']
    if errors:raise SystemExit(f'{board}: ERC errors')
    if board=='adapter' and any(s['violations'] for s in erc['sheets']):
        raise SystemExit('Adapter: unexpected ERC warnings')
print('Both boards: no DRC errors/unconnected/parity issues; no ERC errors.')
PY
