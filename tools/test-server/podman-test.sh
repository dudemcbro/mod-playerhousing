#!/usr/bin/env bash
# Runs the end-to-end housing test in a throwaway container from the prebuilt test server
# image (apps/test-server-image in the core fork): mounts this checkout, lets the container
# build the module, waits for the worldserver, then runs housing_smoke.py inside it.
#
#   IMAGE   image to use     (default: ghcr.io/dudemcbro/acore-test-server:latest)
#   ENGINE  podman or docker (default: podman)
#   KEEP=1  leave the container running afterwards, with ports 3724 and 8085 published
#
# Extra arguments go to housing_smoke.py, e.g. --verbose.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(cd "$HERE/../.." && pwd)"
IMAGE="${IMAGE:-ghcr.io/dudemcbro/acore-test-server:latest}"
ENGINE="${ENGINE:-podman}"
NAME="housing-test-$$"

ports=()
if [ "${KEEP:-0}" = 1 ]; then
    ports=(-p 3724:3724 -p 8085:8085)
fi

"$ENGINE" run -d --name "$NAME" "${ports[@]}" \
    -e ACORE_ACCOUNTS="houseowner:houseowner houseguest:houseguest admin:admin:3" \
    -v "$MODULE_DIR:/opt/acore/modules/mod-playerhousing:Z" \
    "$IMAGE" >/dev/null
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

"$ENGINE" exec "$NAME" python3 /opt/acore/modules/mod-playerhousing/tools/test-server/testclient/housing_smoke.py "$@"
