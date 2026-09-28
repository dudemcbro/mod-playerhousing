#!/usr/bin/env bash
# Fast development loop on the prebuilt server image: one long-running container with this
# checkout mounted. A rebuild only compiles the module and restarts the worldserver.
#
#   dev-container.sh start          start it (first start builds the module, a few minutes)
#   dev-container.sh rebuild        compile the module, re-apply its SQL, restart the worldserver
#   dev-container.sh sql            re-apply the module SQL
#   dev-container.sh restart        restart the worldserver
#   dev-container.sh test [args]    run housing_smoke.py inside (e.g. --verbose)
#   dev-container.sh console CMD    run a worldserver console command
#   dev-container.sh logs           follow the server log
#   dev-container.sh shell          a shell inside the container
#   dev-container.sh stop           remove the container
#
#   IMAGE (default ghcr.io/dudemcbro/acore-test-server:latest), ENGINE (podman),
#   NAME (housing-dev), HOUSING_LAYOUT (cleared), SELINUX_LABEL (see podman-test.sh)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(cd "$HERE/../.." && pwd)"
IMAGE="${IMAGE:-ghcr.io/dudemcbro/acore-test-server:latest}"
ENGINE="${ENGINE:-podman}"
NAME="${NAME:-housing-dev}"
IN=/opt/acore/modules/mod-playerhousing

run() { "$ENGINE" exec "$NAME" bash -c "$1"; }

wait_ready() {
    for _ in $(seq 1 600); do
        if run "grep -q 'ready\.\.\.' /opt/acore/server/logs/Server.log 2>/dev/null"; then
            echo "$NAME: worldserver is up"
            return 0
        fi
        if ! run "pgrep -x worldserver >/dev/null" && run "test -s /opt/acore/server/logs/Server.log"; then
            if run "tmux capture-pane -p -t acore:world 2>/dev/null | grep -q 'Pane is dead'"; then
                echo "$NAME: worldserver exited; last lines:" >&2
                run "tail -n 30 /opt/acore/server/logs/Server.log" >&2
                return 1
            fi
        fi
        sleep 2
    done
    echo "$NAME: worldserver did not report ready" >&2
    return 1
}

apply_sql() {
    run "for f in \$(find $IN/sql/db_world -name '*.sql' ! -iname '*rollback*' | sort); do mysql acore_world < \$f; done;
         for f in \$(find $IN/sql/db_characters -name '*.sql' ! -iname '*rollback*' | sort); do mysql acore_characters < \$f; done"
}

restart_world() {
    run "if pgrep -x worldserver >/dev/null; then tmux send-keys -t acore:world 'server shutdown 1' Enter; fi
         for i in \$(seq 1 60); do pgrep -x worldserver >/dev/null || break; sleep 1; done
         pkill -9 -x worldserver 2>/dev/null || true
         rm -f /opt/acore/server/logs/Server.log
         tmux respawn-window -k -t acore:world"
    wait_ready
}

case "${1:-}" in
    start)
        LABEL="${SELINUX_LABEL-Z}"
        volume="$MODULE_DIR:$IN"
        options=()
        if [ -n "$LABEL" ]; then volume="$volume:$LABEL"; else options+=(--security-opt label=disable); fi
        "$ENGINE" run -d --name "$NAME" -p 3724:3724 -p 8085:8085 "${options[@]}" \
            -e ACORE_ACCOUNTS="houseowner:houseowner houseguest:houseguest admin:admin:3" \
            -e HOUSING_LAYOUT="${HOUSING_LAYOUT:-cleared}" \
            -e AC_PLAYER_HOUSING_FREE_MODE="${AC_PLAYER_HOUSING_FREE_MODE:-1}" \
            -e AC_PLAYER_HOUSING_UNLOCK_ALL="${AC_PLAYER_HOUSING_UNLOCK_ALL:-0}" \
            -v "$volume" --entrypoint "$IN/tools/test-server/container-entry.sh" "$IMAGE" >/dev/null
        echo "$NAME: building the module and starting the servers..."
        wait_ready
        ;;
    rebuild)
        if ! run "ninja -C /opt/acore/build install > /tmp/build.log 2>&1"; then
            run "grep -E 'error|FAILED' -A3 /tmp/build.log | head -80" >&2
            echo "build failed (full log: /tmp/build.log in the container)" >&2
            exit 1
        fi
        apply_sql
        restart_world
        ;;
    sql) apply_sql ;;
    restart) restart_world ;;
    test) shift; "$ENGINE" exec "$NAME" python3 "$IN/tools/test-server/testclient/housing_smoke.py" "$@" ;;
    console) shift; run "tmux send-keys -t acore:world '$*' Enter; sleep 1; tail -n 5 /opt/acore/server/logs/Server.log" ;;
    logs) "$ENGINE" exec "$NAME" tail -f /opt/acore/server/logs/Server.log ;;
    shell) "$ENGINE" exec -it "$NAME" bash ;;
    stop) "$ENGINE" rm -f "$NAME" >/dev/null && echo "$NAME removed" ;;
    *) sed -n '2,19p' "$0"; exit 2 ;;
esac
