#!/usr/bin/env bash
# Stops worldserver cleanly (players are saved), then authserver and the tmux session.
set -uo pipefail

if tmux has-session -t acore 2>/dev/null; then
    if pgrep -x worldserver >/dev/null; then
        tmux send-keys -t acore:world "server shutdown 1" Enter
        for _ in $(seq 1 60); do
            pgrep -x worldserver >/dev/null || break
            sleep 1
        done
    fi
    tmux kill-session -t acore
fi
pkill -x authserver 2>/dev/null || true
echo "stopped"
