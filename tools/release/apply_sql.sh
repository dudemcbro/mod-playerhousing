#!/usr/bin/env bash
# Applies the module's SQL by hand, in the right order (all files are safe to apply again).
# The worldserver's updater does this by itself at startup (Updates.EnableDatabases, on by
# default), so this is only for a server with the updater off. Run it from anywhere, with
# the worldserver stopped:
#
#   MYSQL="mysql -u acore -p" tools/release/apply_sql.sh
#
# MYSQL is the client command with your login (default: mysql, using ~/.my.cnf);
# WORLD_DB and CHARACTERS_DB name the databases (default acore_world, acore_characters).
set -euo pipefail

MODULE="$(cd "$(dirname "$0")/../.." && pwd)"
MYSQL="${MYSQL:-mysql}"
WORLD_DB="${WORLD_DB:-acore_world}"
CHARACTERS_DB="${CHARACTERS_DB:-acore_characters}"

apply() {
    echo "$1 <- ${2#"$MODULE"/}"
    # shellcheck disable=SC2086
    $MYSQL "$1" < "$2" > /dev/null
}

apply "$WORLD_DB" "$MODULE/data/sql/world/mod_playerhousing_world.sql"
apply "$WORLD_DB" "$MODULE/data/sql/world/mod_playerhousing_world_catalog.sql"
apply "$WORLD_DB" "$MODULE/data/sql/world/mod_playerhousing_world_content.sql"
apply "$CHARACTERS_DB" "$MODULE/data/sql/characters/mod_playerhousing_characters.sql"
apply "$CHARACTERS_DB" "$MODULE/data/sql/characters/mod_playerhousing_characters_hotfix.sql"

# shellcheck disable=SC2086
pieces=$($MYSQL -N "$WORLD_DB" -e "SELECT COUNT(*) FROM mod_playerhousing_piece")
echo "Done: $pieces pieces defined. Start the worldserver; its log should say \"Loaded layout ... and N pieces\"."
