#!/usr/bin/env bash
# Entrypoint for the prebuilt server image (apps/test-server-image in the core fork) when
# testing this module: prepares GM Island for the chosen layout, then hands over to the
# image's own entrypoint, which builds the module, applies its SQL and starts the servers.
#
#   HOUSING_LAYOUT  cleared (default: guild hall removed) or guildhouse
#
# Module settings come from environment variables, e.g. AC_PLAYER_HOUSING_FREE_MODE=1.
set -euo pipefail

MODULE=/opt/acore/modules/mod-playerhousing
export SERVER_DIR=/opt/acore/server
LAYOUT="${HOUSING_LAYOUT:-cleared}"
export AC_PLAYER_HOUSING_LAYOUT="$LAYOUT"

if [ "$LAYOUT" = cleared ]; then
    "$MODULE/tools/gm-island-cleared/server_data.sh"
fi

exec acore-entrypoint "$@"
