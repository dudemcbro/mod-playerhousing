#!/usr/bin/env bash
# Builds the housing client patch MPQ from your own 3.3.5a client: it hides the GM Island
# guild house, and adds the housing items to the client so bags show their icons.
#
#   make_client_patch.sh [--icons-only] /path/to/WoW-3.3.5a/Data [patch-H.MPQ]
#
# The folder may be the client's install folder or its Data folder, in any letter case.
# --icons-only leaves the island alone, for servers on the guildhouse layout.
# Copy the resulting MPQ into every player's Data folder (and clear their WDB cache once).
# The server must run the matching collision/pathing data: see server_data.sh.
# Needs smpq (Ubuntu/distrobox: sudo apt install smpq) and python3.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISLAND=1
if [ "${1:-}" = "--icons-only" ]; then
    ISLAND=0
    shift
fi
DATA_DIR="${1:?usage: $0 [--icons-only] /path/to/WoW/Data [output.MPQ]}"
OUTPUT="${2:-patch-H.MPQ}"
ADT='World\Maps\Kalimdor\Kalimdor_1_1.adt'

command -v smpq >/dev/null || { echo "smpq not found (sudo apt install smpq)" >&2; exit 1; }

# Linux file names are case sensitive and client folders come in any case (Data or data,
# common.MPQ or common.mpq), so every lookup ignores case.
find_ci() { find "$1" -maxdepth 1 -type f -iname "$2" 2>/dev/null | head -n 1; }
DATA_DIR="$(cd "$DATA_DIR" && pwd)"
if [ -z "$(find "$DATA_DIR" -maxdepth 1 -type f -iname '*.mpq' 2>/dev/null | head -n 1)" ]; then
    sub="$(find "$DATA_DIR" -maxdepth 1 -type d -iname data 2>/dev/null | head -n 1)"
    [ -n "$sub" ] && DATA_DIR="$sub"
fi
# The locale folder (enUS, deDE...) holds the game's data tables.
LOCALE_DIR="$(find "$DATA_DIR" -mindepth 1 -maxdepth 1 -type d -regextype posix-extended -iregex '.*/[a-z]{4}' 2>/dev/null | head -n 1)"
LOC="$(basename "${LOCALE_DIR:-none}")"

# Archives in the order the 3.3.5a client loads them; later ones win. Other patches load after
# the standard ones, in name order; those that load after this patch are marked, since their
# copy of a file would win over ours.
out_lc="$(basename "$OUTPUT")"
out_lc="${out_lc,,}"
archives=()
declare -A seen=() later=()
add() {
    local file
    [ -n "$1" ] || return 0
    file="$(find_ci "$1" "$2")"
    if [ -n "$file" ]; then
        archives+=("$file")
        seen["${file,,}"]=1
    fi
    return 0
}
for name in common common-2 expansion lichking; do add "$DATA_DIR" "$name.mpq"; done
for name in locale speech expansion-locale lichking-locale expansion-speech lichking-speech; do add "$LOCALE_DIR" "$name-$LOC.mpq"; done
add "$DATA_DIR" patch.mpq
add "$LOCALE_DIR" "patch-$LOC.mpq"
for n in 2 3; do
    add "$DATA_DIR" "patch-$n.mpq"
    add "$LOCALE_DIR" "patch-$LOC-$n.mpq"
done
extras() {
    [ -n "$1" ] || return 0
    find "$1" -maxdepth 1 -type f -iname "$2" 2>/dev/null | LC_ALL=C sort -f
}
while IFS= read -r file; do
    [ -n "$file" ] || continue
    lc="$(basename "$file")"
    lc="${lc,,}"
    [ -n "${seen[${file,,}]:-}" ] && continue
    [ "$(dirname "$file")" = "$DATA_DIR" ] && [ "$lc" = "$out_lc" ] && continue
    archives+=("$file")
    if [ "$(dirname "$file")" != "$DATA_DIR" ] || [[ "$lc" > "$out_lc" ]]; then
        later["$file"]=1
    fi
done < <(extras "$DATA_DIR" 'patch-*.mpq'; extras "$LOCALE_DIR" "patch-$LOC-*.mpq")
if [ "${#archives[@]}" -eq 0 ]; then
    echo "no client MPQs found in $DATA_DIR" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Copies the client's current version of a file (from the last archive that has it) to $2.
latest() {
    local wanted="$1" out="$2" base found="" archive file
    base="$(basename "${wanted//\\//}")"
    for archive in "${archives[@]}"; do
        rm -rf "$work/extract"
        mkdir -p "$work/extract"
        if (cd "$work/extract" && smpq -x -q "$archive" "$wanted" >/dev/null 2>&1); then
            file="$(find "$work/extract" -type f -iname "$base" | head -n 1)"
            if [ -n "$file" ]; then
                cp "$file" "$out"
                found="$archive"
            fi
        fi
    done
    if [ -z "$found" ]; then
        echo "$base not found in the client archives" >&2
        exit 1
    fi
    echo "using $base from $(basename "$found")"
    if [ -n "${later[$found]:-}" ]; then
        echo "warning: $(basename "$found") also has $base and loads after $(basename "$OUTPUT")," >&2
        echo "so the client would use its copy instead. Give the output a later letter than that patch." >&2
    fi
}

files=()
if [ "$ISLAND" = 1 ]; then
    latest "$ADT" "$work/Kalimdor_1_1.adt"
    mkdir -p "$work/pack/World/Maps/Kalimdor"
    python3 "$HERE/adt_sink_wmo.py" "$work/Kalimdor_1_1.adt" "$work/pack/World/Maps/Kalimdor/Kalimdor_1_1.adt"
    files+=("World/Maps/Kalimdor/Kalimdor_1_1.adt")
fi

latest 'DBFilesClient\Item.dbc' "$work/Item.dbc"
latest 'DBFilesClient\ItemDisplayInfo.dbc' "$work/ItemDisplayInfo.dbc"
mkdir -p "$work/pack/DBFilesClient"
python3 "$HERE/dbc_add_items.py" "$HERE/client_items.tsv" "$work/Item.dbc" "$work/ItemDisplayInfo.dbc" "$work/pack/DBFilesClient"
files+=("DBFilesClient/Item.dbc" "DBFilesClient/ItemDisplayInfo.dbc")

rm -f "$OUTPUT"
(cd "$work/pack" && smpq -c -q -M 2 "$work/out.MPQ" "${files[@]}")
cp "$work/out.MPQ" "$OUTPUT"
smpq -l "$OUTPUT"
echo "wrote $OUTPUT"
