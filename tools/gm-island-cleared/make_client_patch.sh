#!/usr/bin/env bash
# Builds a client patch MPQ that hides the GM Island guild house, from your own 3.3.5a client.
#
#   make_client_patch.sh /path/to/WoW-3.3.5a/Data [patch-H.MPQ]
#
# The folder may be the client's install folder or its Data folder, in any letter case.
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

# Linux file names are case sensitive and client folders come in any case (Data or data,
# common.MPQ or common.mpq), so every lookup ignores case.
if [ -z "$(find "$DATA_DIR" -maxdepth 1 -type f -iname '*.mpq' 2>/dev/null | head -n 1)" ]; then
    sub="$(find "$DATA_DIR" -maxdepth 1 -type d -iname data 2>/dev/null | head -n 1)"
    [ -n "$sub" ] && DATA_DIR="$sub"
fi

# Base archives in the order the 3.3.5a client loads them; later ones win. Any other
# patch-*.MPQ the client already has follows, in name order, since those load after.
standard=(common.mpq common-2.mpq expansion.mpq lichking.mpq patch.mpq patch-2.mpq patch-3.mpq)
skip=" ${standard[*]} "
out_lc="$(basename "$OUTPUT")"
out_lc="${out_lc,,}"
archives=()
for name in "${standard[@]}"; do
    file="$(find "$DATA_DIR" -maxdepth 1 -type f -iname "$name" 2>/dev/null | head -n 1)"
    [ -n "$file" ] && archives+=("$file")
done
while IFS= read -r file; do
    lc="$(basename "$file")"
    lc="${lc,,}"
    case "$skip" in *" $lc "*) continue ;; esac
    [ "$lc" = "$out_lc" ] && continue
    archives+=("$file")
done < <(find "$DATA_DIR" -maxdepth 1 -type f -iname 'patch-*.mpq' 2>/dev/null | LC_ALL=C sort -f)
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
found_lc="$(basename "$found")"
found_lc="${found_lc,,}"
case "$skip" in
    *" $found_lc "*) ;;
    *) if [[ "$found_lc" > "$out_lc" ]]; then
           echo "warning: $(basename "$found") also replaces this tile and loads after $(basename "$OUTPUT")," >&2
           echo "so the client would still show the hall. Give the output a later letter than that patch." >&2
       fi ;;
esac

python3 "$HERE/adt_sink_wmo.py" "$work/Kalimdor_1_1.adt" "$work/Kalimdor_1_1.cleared.adt"

rm -f "$OUTPUT"
mkdir -p "$work/pack/World/Maps/Kalimdor"
cp "$work/Kalimdor_1_1.cleared.adt" "$work/pack/World/Maps/Kalimdor/Kalimdor_1_1.adt"
(cd "$work/pack" && smpq -c -q -M 2 "$work/out.MPQ" "World/Maps/Kalimdor/Kalimdor_1_1.adt")
cp "$work/out.MPQ" "$OUTPUT"
smpq -l "$OUTPUT"
echo "wrote $OUTPUT"
