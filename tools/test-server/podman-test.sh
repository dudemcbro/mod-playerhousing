#!/usr/bin/env bash
# Runs the end-to-end housing test in a throwaway container from the prebuilt test server
# image (apps/test-server-image in the core fork): mounts this checkout, lets the container
# build the module, waits for the worldserver, then runs collection_smoke.py inside it.
#
#   IMAGE   image to use     (default: ghcr.io/dudemcbro/acore-test-server:latest)
#   ENGINE  podman or docker (default: podman)
#   KEEP=1  leave the container running afterwards, with ports 3724 and 8085 published
#   HOUSING_LAYOUT  cleared (default) or guildhouse
#   SELINUX_LABEL  label option for the module mount. Default Z, or empty when the checkout
#                  is on a drive without SELinux labels (NTFS, exFAT); labeling is then
#                  switched off for the container instead.
#
# Extra arguments go to collection_smoke.py, e.g. --verbose.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(cd "$HERE/../.." && pwd)"
IMAGE="${IMAGE:-ghcr.io/dudemcbro/acore-test-server:latest}"
ENGINE="${ENGINE:-podman}"
NAME="housing-test-$$"

options=()
if [ "${KEEP:-0}" = 1 ]; then
    options+=(-p 3724:3724 -p 8085:8085)
fi

if [ -z "${SELINUX_LABEL+set}" ]; then
    case "$(findmnt -n -o FSTYPE --target "$MODULE_DIR" 2>/dev/null)" in
        ntfs*|fuseblk|exfat|vfat|msdos) SELINUX_LABEL="" ;;
        *) SELINUX_LABEL="Z" ;;
    esac
fi
LABEL="$SELINUX_LABEL"
volume="$MODULE_DIR:/opt/acore/modules/mod-playerhousing"
if [ -n "$LABEL" ]; then
    volume="$volume:$LABEL"
else
    options+=(--security-opt label=disable)
fi

IN=/opt/acore/modules/mod-playerhousing
"$ENGINE" run -d --name "$NAME" "${options[@]}" \
    -e ACORE_ACCOUNTS="houseowner:houseowner houseguest:houseguest admin:admin:3" \
    -e HOUSING_LAYOUT="${HOUSING_LAYOUT:-cleared}" \
    -e AC_PLAYER_HOUSING_FREE_MODE=1 \
    -v "$volume" --entrypoint bash \
    "$IMAGE" "$IN/tools/test-server/container-entry.sh" >/dev/null
if [ "${KEEP:-0}" != 1 ]; then
    trap '"$ENGINE" rm -f "$NAME" >/dev/null 2>&1 || true' EXIT
fi

echo "$NAME: building the module and starting the server..."
ready=0
for _ in $(seq 1 900); do
    if "$ENGINE" exec "$NAME" grep -q "ready\.\.\." /opt/acore/server/logs/Server.log 2>/dev/null; then
        ready=1
        break
    fi
    if [ "$("$ENGINE" container inspect -f '{{.State.Running}}' "$NAME" 2>/dev/null)" != "true" ]; then
        break
    fi
    sleep 2
done
if [ "$ready" != 1 ]; then
    echo "$NAME: server did not start; last log lines:" >&2
    "$ENGINE" logs "$NAME" 2>&1 | tail -n 40 >&2
    exit 1
fi

# On a brand-new image volume the authserver can reach the realm list before the image has
# inserted its first row, then exit while the worldserver continues. Start it again after the
# databases and world are ready; on normal runs this is a no-op.
if ! "$ENGINE" exec "$NAME" pgrep -x authserver >/dev/null; then
    "$ENGINE" exec "$NAME" bash -lc \
        "tmux new-window -t acore -n auth-retry -c /opt/acore/server/bin './authserver -c /opt/acore/server/etc/authserver.conf'"
    sleep 2
fi

"$ENGINE" exec "$NAME" python3 "$IN/tools/test-server/testclient/collection_smoke.py" "$@"
