#!/usr/bin/env bash
# Removes the GM Island guild house from the server's collision (vmaps) and pathing (mmaps)
# data, or puts it back with --restore. Stop the worldserver first: it reads these at startup.
#
# Needs mmaps_generator, which setup.sh builds when run with TOOLS_BUILD=maps-only.
#
#   SERVER_DIR  server install with data/ and bin/  (default: ~/acore-test-server)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="${SERVER_DIR:-$HOME/acore-test-server}"
DATA="$SERVER_DIR/data"
GENERATOR="${MMAPS_GENERATOR:-$SERVER_DIR/bin/mmaps_generator}"
CONFIG="${MMAPS_CONFIG:-$SERVER_DIR/bin/mmaps-config.yaml}"
MMTILE="$DATA/mmaps/0010101.mmtile"   # Kalimdor tile 1,1: GM Island

if [ "${1:-}" = "--restore" ]; then
    python3 "$HERE/strip_guildhouse_vmap.py" "$DATA" --restore
    if [ -f "$MMTILE.orig" ]; then
        cp "$MMTILE.orig" "$MMTILE"
        echo "restored $MMTILE"
    fi
    exit 0
fi

if [ ! -x "$GENERATOR" ] || [ ! -f "$CONFIG" ]; then
    echo "mmaps_generator not found at $GENERATOR; re-run setup.sh with TOOLS_BUILD=maps-only" >&2
    exit 1
fi

python3 "$HERE/strip_guildhouse_vmap.py" "$DATA"

# Rebuild only GM Island's navmesh tile from the stripped collision data.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ln -s "$DATA/maps" "$work/maps"
ln -s "$DATA/vmaps" "$work/vmaps"
mkdir "$work/mmaps"
cp "$CONFIG" "$work/mmaps-config.yaml"
(cd "$work" && "$GENERATOR" 1 --tile 1,1 --config mmaps-config.yaml --silent)

[ -f "$MMTILE.orig" ] || cp "$MMTILE" "$MMTILE.orig"
cp "$work/mmaps/0010101.mmtile" "$MMTILE"
echo "rebuilt $MMTILE without the guild house (original kept as .orig)"
