#!/usr/bin/env bash
# Starts MySQL, authserver and worldserver in a tmux session named "acore".
# Attach with: tmux attach -t acore   (Ctrl-b n / Ctrl-b p switch windows, Ctrl-b d detaches)
set -euo pipefail

SERVER_DIR="${SERVER_DIR:-$HOME/acore-test-server}"
SUDO=$([ "$(id -u)" -ne 0 ] && echo sudo || true)
LOG="$SERVER_DIR/logs/Server.log"
mkdir -p "$SERVER_DIR/logs"

mysqladmin ping >/dev/null 2>&1 || $SUDO service mysql start >/dev/null

if tmux has-session -t acore 2>/dev/null; then
    echo "tmux session 'acore' is already running"
    exit 0
fi

rm -f "$LOG"
tmux new-session -d -s acore -n auth -c "$SERVER_DIR/bin" "./authserver -c $SERVER_DIR/etc/authserver.conf"
# Keep windows open after a crash so the output can still be read.
tmux set-option -g remain-on-exit on
tmux new-window -t acore -n world -c "$SERVER_DIR/bin" "./worldserver -c $SERVER_DIR/etc/worldserver.conf"
echo "waiting for worldserver to finish loading..."

for _ in $(seq 1 600); do
    if grep -q "ready\.\.\." "$LOG" 2>/dev/null; then
        echo "worldserver is up (console: tmux attach -t acore)"
        exit 0
    fi
    if ! pgrep -x worldserver >/dev/null; then
        echo "worldserver exited during startup; see: tmux attach -t acore" >&2
        exit 1
    fi
    sleep 1
done
echo "worldserver did not report ready; see: tmux attach -t acore" >&2
exit 1
