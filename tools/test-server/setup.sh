#!/usr/bin/env bash
# Builds and configures a throwaway AzerothCore server with mod-playerhousing for testing.
#
# Written for Ubuntu 24.04 (on Bluefin, run it inside a distrobox). Safe to re-run:
# finished steps are skipped and the build is incremental.
#
#   CORE_DIR    AzerothCore checkout to build         (default: ~/azerothcore-wotlk-playerbots-custom)
#   SERVER_DIR  install prefix, data, logs, configs  (default: ~/acore-test-server)
#   BUILD_DIR   CMake build directory                (default: $SERVER_DIR/build)
#   JOBS        parallel compile jobs                (default: nproc)
#   TOOLS_BUILD none, or maps-only for the map/vmap/mmap tools that
#               tools/gm-island-cleared needs            (default: none)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(cd "$HERE/../.." && pwd)"
CORE_DIR="${CORE_DIR:-$HOME/azerothcore-wotlk-playerbots-custom}"
SERVER_DIR="${SERVER_DIR:-$HOME/acore-test-server}"
BUILD_DIR="${BUILD_DIR:-$SERVER_DIR/build}"
JOBS="${JOBS:-$(nproc)}"
TOOLS_BUILD="${TOOLS_BUILD:-none}"
CLIENT_DATA_VERSION="${CLIENT_DATA_VERSION:-v19}"
SUDO=$([ "$(id -u)" -ne 0 ] && echo sudo || true)
MYSQL=(mysql -uacore -pacore)

step() { printf '\n==> %s\n' "$*"; }
quiet_mysql() { "${MYSQL[@]}" "$@" 2> >(grep -v "Using a password" >&2); }

if [ ! -f "$CORE_DIR/acore.json" ]; then
    echo "CORE_DIR=$CORE_DIR is not an AzerothCore checkout" >&2
    exit 1
fi

step "Installing build dependencies and MySQL"
if ! command -v mysqld >/dev/null || ! dpkg -s libboost-all-dev >/dev/null 2>&1; then
    $SUDO apt-get update
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y \
        git cmake ninja-build clang ccache curl unzip tmux python3 \
        libmysqlclient-dev libreadline-dev libncurses-dev libbz2-dev zlib1g-dev libssl-dev libboost-all-dev \
        mysql-server mysql-client
fi

step "Client data $CLIENT_DATA_VERSION (dbc, maps, vmaps, mmaps)"
mkdir -p "$SERVER_DIR/data"
if ! grep -qx "INSTALLED_VERSION=$CLIENT_DATA_VERSION" "$SERVER_DIR/data/data-version" 2>/dev/null; then
    curl -L --fail --retry 4 -o "$SERVER_DIR/data/data.zip" \
        "https://github.com/wowgaming/client-data/releases/download/$CLIENT_DATA_VERSION/data.zip"
    unzip -q -o "$SERVER_DIR/data/data.zip" -d "$SERVER_DIR/data"
    rm "$SERVER_DIR/data/data.zip"
    echo "INSTALLED_VERSION=$CLIENT_DATA_VERSION" > "$SERVER_DIR/data/data-version"
else
    echo "already installed"
fi

step "Linking the module into $CORE_DIR/modules"
link="$CORE_DIR/modules/mod-playerhousing"
if [ -L "$link" ] || [ ! -e "$link" ]; then
    ln -sfn "$MODULE_DIR" "$link"
elif [ "$(cd "$link" && pwd -P)" != "$(cd "$MODULE_DIR" && pwd -P)" ]; then
    echo "note: $link is a separate copy of the module; that copy is what gets built" >&2
fi

step "Building into $SERVER_DIR (the first build takes a while)"
CC=clang CXX=clang++ cmake -S "$CORE_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$SERVER_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSCRIPTS=static -DMODULES=static -DTOOLS_BUILD="$TOOLS_BUILD" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache >/dev/null
ninja -C "$BUILD_DIR" -j"$JOBS" install | { grep -vE "^-- (Installing|Up-to-date|Set non-toolchain)" || true; }

step "Writing configs (existing ones are kept)"
etc="$SERVER_DIR/etc"
mkdir -p "$SERVER_DIR/logs"
if [ ! -f "$etc/authserver.conf" ]; then
    sed -e "s|^LogsDir = .*|LogsDir = \"$SERVER_DIR/logs\"|" "$etc/authserver.conf.dist" > "$etc/authserver.conf"
fi
if [ ! -f "$etc/worldserver.conf" ]; then
    # Warden is off because the headless test client cannot answer it.
    sed -e "s|^DataDir = .*|DataDir = \"$SERVER_DIR/data\"|" \
        -e "s|^LogsDir = .*|LogsDir = \"$SERVER_DIR/logs\"|" \
        -e "s|^SourceDirectory = .*|SourceDirectory = \"$CORE_DIR\"|" \
        -e "s|^Warden.Enabled = .*|Warden.Enabled = 0|" \
        "$etc/worldserver.conf.dist" > "$etc/worldserver.conf"
fi
if [ ! -f "$etc/modules/mod_playerhousing.conf" ]; then
    cp "$etc/modules/mod_playerhousing.conf.dist" "$etc/modules/mod_playerhousing.conf"
fi

step "Starting MySQL and creating the acore user and databases"
mysqladmin ping >/dev/null 2>&1 || $SUDO service mysql start
if ! quiet_mysql -e "SELECT 1" acore_world >/dev/null 2>&1; then
    $SUDO mysql < "$CORE_DIR/data/sql/create/create_mysql.sql"
fi

step "Populating databases (worldserver --dry-run)"
tables=$(quiet_mysql -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='acore_world'")
if [ "$tables" -lt 100 ]; then
    # Also creates the auth and characters tables (Updates.EnableDatabases = 7).
    echo "(warnings that the playerhousing scripts are not assigned are expected; the module SQL is next)"
    (cd "$SERVER_DIR/bin" && AC_DISABLE_INTERACTIVE=1 ./worldserver -c "$etc/worldserver.conf" --dry-run | tail -n 3)
else
    echo "already populated"
fi

step "Applying module SQL (safe to re-run)"
# The core's auto-updater only reads modules/<name>/data/sql/db-*, so these are applied by hand.
for f in "$MODULE_DIR"/sql/db_world/base/mod_playerhousing_world.sql \
         "$MODULE_DIR"/sql/db_world/base/mod_playerhousing_world_hotfix.sql; do
    quiet_mysql acore_world < "$f" >/dev/null
    echo "applied $(basename "$f")"
done
for f in "$MODULE_DIR"/sql/db_characters/base/mod_playerhousing_characters.sql \
         "$MODULE_DIR"/sql/db_characters/base/mod_playerhousing_characters_hotfix.sql; do
    quiet_mysql acore_characters < "$f" >/dev/null
    echo "applied $(basename "$f")"
done

step "Test accounts"
(cd "$HERE/testclient" && python3 create_account.py houseowner houseowner \
    && python3 create_account.py houseguest houseguest \
    && python3 create_account.py admin admin --gm 3)

cat <<EOF

Done. Next:
  SERVER_DIR=$SERVER_DIR $HERE/start.sh
  python3 $HERE/testclient/housing_smoke.py

Accounts: houseowner/houseowner and houseguest/houseguest (players), admin/admin (GM 3).
For a real 3.3.5a client, set realmlist.wtf to: set realmlist 127.0.0.1
EOF
