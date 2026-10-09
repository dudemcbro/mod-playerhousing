#!/usr/bin/env bash
# Builds the one download players need: PlayerHousing-client.zip, unzipped into the game's
# folder (the one with Wow.exe). It holds
#   Interface/AddOns/PlayerHousing/      the addon (required)
#   PlayerHousing.dll, PlayerHousingLauncher.exe   pieces follow the mouse (recommended)
#   Data/patch-H.MPQ                     icons, see-through ghosts, the cleared island
#   PLAYER_SETUP.md                      what to do with it
#
#   make_player_bundle.sh [--icons-only] [--patch <built.MPQ>] [--patch-name <name.MPQ>]
#                         [--out <file.zip>] [/path/to/WoW-3.3.5a/Data]
#
# The patch is built from a 3.3.5a client's Data folder (see tools/gm-island-cleared): use a
# client like your players' (plain 3.3.5a, plus any custom patches your server hands out), or
# pass a patch you built already with --patch. --icons-only is for the guildhouse layout.
# Needs python3, and smpq when building the patch.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MODULE="$(cd "$HERE/../.." && pwd)"
ICONS_ONLY=""
PATCH=""
PATCH_NAME="patch-H.MPQ"
OUT="$PWD/PlayerHousing-client.zip"
DATA_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --icons-only) ICONS_ONLY="--icons-only" ;;
        --patch) PATCH="$2"; shift ;;
        --patch-name) PATCH_NAME="$2"; shift ;;
        --out) OUT="$2"; shift ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) DATA_DIR="$1" ;;
    esac
    shift
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ -z "$PATCH" ]; then
    [ -n "$DATA_DIR" ] || { echo "Give the client's Data folder to build the patch, or --patch <file>." >&2; exit 1; }
    PATCH="$work/$PATCH_NAME"
    # shellcheck disable=SC2086
    "$MODULE/tools/gm-island-cleared/make_client_patch.sh" $ICONS_ONLY "$DATA_DIR" "$PATCH"
fi
[ -s "$PATCH" ] || { echo "No patch at $PATCH" >&2; exit 1; }

stage="$work/stage"
mkdir -p "$stage/Interface/AddOns" "$stage/Data"
cp -r "$MODULE/client-addon/PlayerHousing" "$stage/Interface/AddOns/"
cp "$MODULE/client-dll/bin/PlayerHousing.dll" "$MODULE/client-dll/bin/PlayerHousingLauncher.exe" "$stage/"
cp "$PATCH" "$stage/Data/$PATCH_NAME"
cp "$MODULE/docs/PLAYER_SETUP.md" "$stage/"

python3 - "$stage" "$OUT" <<'PY'
import os, sys, zipfile
stage, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(stage):
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, stage))
PY
echo "Wrote $OUT ($(du -h "$OUT" | cut -f1)). Host it where your players download the game or your patches."
