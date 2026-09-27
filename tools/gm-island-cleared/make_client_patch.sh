#!/usr/bin/env bash
# Builds a client patch MPQ that hides the GM Island guild house, from your own 3.3.5a client.
#
#   make_client_patch.sh /path/to/WoW-3.3.5a/Data [patch-H.MPQ]
#
# Copy the resulting MPQ into every player's Data folder (and clear their WDB cache once).
# The server must run the matching collision/pathing data: see server_data.sh.
# Needs smpq (Ubuntu/distrobox: sudo apt install smpq) and python3.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${1:?usage: $0 /path/to/WoW/Data [output.MPQ]}"
OUTPUT="${2:-patch-H.MPQ}"
ADT='World\Maps\Kalimdor\Kalimdor_1_1.adt'

command -v smpq >/dev/null || { echo "smpq not found (sudo apt install smpq)" >&2; exit 1; }

# Base archives in the order the 3.3.5a client loads them; later ones win.
archives=()
for name in common.MPQ common-2.MPQ expansion.MPQ lichking.MPQ patch.MPQ patch-2.MPQ patch-3.MPQ; do
    [ -f "$DATA_DIR/$name" ] && archives+=("$DATA_DIR/$name")
done
if [ "${#archives[@]}" -eq 0 ]; then
    echo "no client MPQs found in $DATA_DIR" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Extract from every archive that has the tile; the last successful copy is the current one.
found=""
for archive in "${archives[@]}"; do
    rm -rf "$work/extract"
    mkdir -p "$work/extract"
    if (cd "$work/extract" && smpq -x -q "$archive" "$ADT" >/dev/null 2>&1); then
        file="$(find "$work/extract" -iname 'Kalimdor_1_1.adt' | head -n 1)"
        if [ -n "$file" ]; then
            cp "$file" "$work/Kalimdor_1_1.adt"
            found="$archive"
        fi
    fi
done
if [ -z "$found" ]; then
    echo "Kalimdor_1_1.adt not found in the client archives" >&2
    exit 1
fi
echo "using Kalimdor_1_1.adt from $(basename "$found")"

python3 "$HERE/adt_sink_wmo.py" "$work/Kalimdor_1_1.adt" "$work/Kalimdor_1_1.cleared.adt"

rm -f "$OUTPUT"
mkdir -p "$work/pack/World/Maps/Kalimdor"
cp "$work/Kalimdor_1_1.cleared.adt" "$work/pack/World/Maps/Kalimdor/Kalimdor_1_1.adt"
(cd "$work/pack" && smpq -c -q -M 2 "$work/out.MPQ" "World/Maps/Kalimdor/Kalimdor_1_1.adt")
cp "$work/out.MPQ" "$OUTPUT"
smpq -l "$OUTPUT"
echo "wrote $OUTPUT"
