#!/usr/bin/env bash
# Applies the module's SQL in the right order (all files are safe to apply again). Run it
# from anywhere, with the worldserver stopped:
#
#   MYSQL="mysql -u acore -p" tools/release/apply_sql.sh
#
# MYSQL is the client command with your login (default: mysql, using ~/.my.cnf);
# WORLD_DB and CHARACTERS_DB name the databases (default acore_world, acore_characters).
# The world files go together: the base file rebuilds the piece tables the catalog and
# content files fill, so applying it alone leaves housing with no pieces.
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

apply "$WORLD_DB" "$MODULE/sql/db_world/base/mod_playerhousing_world.sql"
apply "$WORLD_DB" "$MODULE/sql/db_world/base/mod_playerhousing_world_catalog.sql"
apply "$WORLD_DB" "$MODULE/sql/db_world/base/mod_playerhousing_world_content.sql"
apply "$CHARACTERS_DB" "$MODULE/sql/db_characters/base/mod_playerhousing_characters.sql"
apply "$CHARACTERS_DB" "$MODULE/sql/db_characters/base/mod_playerhousing_characters_hotfix.sql"

# shellcheck disable=SC2086
pieces=$($MYSQL -N "$WORLD_DB" -e "SELECT COUNT(*) FROM mod_playerhousing_piece")
echo "Done: $pieces pieces defined. Start the worldserver; its log should say \"Loaded layout ... and N pieces\"."
