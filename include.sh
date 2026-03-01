#!/usr/bin/env bash

MOD_PLAYERHOUSING_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/" && pwd )"

source "$MOD_PLAYERHOUSING_ROOT/conf/conf.sh.dist"

if [ -f "$MOD_PLAYERHOUSING_ROOT/conf/conf.sh" ]; then
    source "$MOD_PLAYERHOUSING_ROOT/conf/conf.sh"
fi
