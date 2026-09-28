#!/usr/bin/env bash
# Sends one command to the worldserver console and prints the log lines it produced.
# Example: ./console.sh "account set gmlevel houseowner 3 -1"
set -euo pipefail

SERVER_DIR="${SERVER_DIR:-$HOME/acore-test-server}"
LOG="$SERVER_DIR/logs/Server.log"
before=$(wc -l < "$LOG")
tmux send-keys -t acore:world "$*" Enter
sleep "${WAIT:-2}"
tail -n +"$((before + 1))" "$LOG"
