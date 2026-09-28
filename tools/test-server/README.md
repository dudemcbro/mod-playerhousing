# Test server for mod-playerhousing

Scripts to stand up a throwaway AzerothCore server with this module, plus a
headless 3.3.5a client that runs the housing features end to end and checks the
database after each step.

Everything installs under `~/acore-test-server` and uses its own MySQL databases
(`acore_auth`, `acore_characters`, `acore_world`), so keep it away from a MySQL
that already hosts a live server.

## Fastest: the prebuilt image (Podman)

The core fork publishes a test server image with the core already compiled, the client
data and populated databases inside (`apps/test-server-image` in
azerothcore-wotlk-playerbots-custom). With it, a full test run is one command and only
this module gets compiled:

```bash
podman login ghcr.io -u dudemcbro      # once; token with read:packages
tools/test-server/podman-test.sh           # add --verbose for every chat line
```

It starts a throwaway container with this checkout mounted (`:Z`, for Bluefin's SELinux;
on an NTFS or exFAT drive it turns labeling off for the container instead), clears GM
Island's guild hall from the server data, builds the module, starts the servers with
FreeMode on, runs `housing_smoke.py` inside and removes the container.

- `HOUSING_LAYOUT=guildhouse` keeps the guild hall instead.
- `KEEP=1` leaves the container running with ports 3724 and 8085 published, so you can
  log in with a client or attach to the console
  (`podman exec -it <name> tmux attach -t acore`).

## Development loop: dev-container.sh

For working on the module: one long-running container from the same image, with this
checkout mounted. A rebuild only compiles the module, re-applies its SQL and restarts the
worldserver, which takes a minute or two.

```bash
tools/test-server/dev-container.sh start     # first start builds the module
tools/test-server/dev-container.sh rebuild   # after editing C++ or SQL
tools/test-server/dev-container.sh test      # the end-to-end test (add --verbose)
tools/test-server/dev-container.sh console "server info"
tools/test-server/dev-container.sh logs      # follow the server log
tools/test-server/dev-container.sh stop      # remove the container
```

Ports 3724 and 8085 are published, so a real client can log in too. The container runs
the cleared island with FreeMode on; `HOUSING_LAYOUT=guildhouse`,
`AC_PLAYER_HOUSING_FREE_MODE=0` or `AC_PLAYER_HOUSING_UNLOCK_ALL=1` on `start` change
that. `ENGINE=docker` works too.

## Quick start on Bluefin (building everything locally)

Bluefin's base image is immutable, so build inside a distrobox. It shares your
home directory and network with the host.

```bash
distrobox create --name acore --image ubuntu:24.04
distrobox enter acore
```

Inside the box, with both repos cloned into your home directory:

```bash
CORE_DIR=~/azerothcore-wotlk-playerbots-custom ~/mod-playerhousing/tools/test-server/setup.sh
~/mod-playerhousing/tools/test-server/start.sh
python3 ~/mod-playerhousing/tools/test-server/testclient/housing_smoke.py
```

The first run downloads about 3 GB of client data and compiles the core, which
takes a while. Re-running `setup.sh` skips finished steps and only rebuilds what
changed, so it is also the way to pick up module edits.

## What setup.sh does

1. Installs build packages and MySQL 8 with apt.
2. Downloads the prebuilt client data (`dbc`, `maps`, `vmaps`, `mmaps`) from
   `wowgaming/client-data` (the version `acore.sh` pins).
3. Symlinks this checkout into `$CORE_DIR/modules/mod-playerhousing`.
4. Builds and installs to `$SERVER_DIR` (build dir `$SERVER_DIR/build`), including the
   map tools the cleared island needs.
5. Writes `authserver.conf` and `worldserver.conf` (existing files are kept; Warden is
   off for the headless client) and `mod_playerhousing.conf`, rewritten every run with
   FreeMode on and the chosen layout.
6. Clears GM Island's guild hall from the server's collision and pathing data
   (`tools/gm-island-cleared/server_data.sh`), or puts it back for the guildhouse layout.
7. Creates the `acore` MySQL user and databases, then populates them with
   `worldserver --dry-run`.
8. Applies this module's SQL. The core's auto-updater only reads
   `modules/<name>/data/sql/db-*`, so the files in `sql/` are applied here.
9. Creates accounts `houseowner` and `houseguest` (players) and `admin` (GM 3).
   Passwords match the names.

Variables: `CORE_DIR`, `SERVER_DIR`, `BUILD_DIR`, `JOBS`, `HOUSING_LAYOUT` (`cleared`,
the default, or `guildhouse`) and `TOOLS_BUILD`.

## Day to day

```bash
tools/test-server/start.sh                 # MySQL + authserver + worldserver in tmux "acore"
tools/test-server/stop.sh                  # clean shutdown (saves characters)
tools/test-server/console.sh "server info" # one console command
tmux attach -t acore                       # full console, Ctrl-b d to detach
```

To play with a real client, point `realmlist.wtf` at `set realmlist 127.0.0.1`
and log in as `admin`/`admin`. Module settings live in
`~/acore-test-server/etc/modules/mod_playerhousing.conf`:

```bash
vim ~/acore-test-server/etc/modules/mod_playerhousing.conf
```

More accounts: `python3 testclient/create_account.py NAME PASSWORD [--gm 3]`.

## The automated test

`testclient/housing_smoke.py` logs in three characters (`Krookowner`, `Krookguest` and
the GM `Krookadmin`, created on first run), resets their housing, and plays it through,
checking the database and what the client sees after each step (144 checks):

- first login: House Key, starter furnishings, past progress unlocking pieces
- the Home menu from `.house` and from the key, going home, Krook's greeting, the
  starter wreckage
- placing with the targeting circle where it was clicked, facing you; each item sends its
  own spell, and the circles fit the pieces (small for a chair, large for a farmhouse);
  no menu after placing furniture, the menu after a building, and with `.house adjust
  all` the menu after anything, with Take it back and Keep it here; undo returns the
  item, redo places it again; no spacing rules; placing far out on the island; refusing
  spots off the island; swimmers brought back to the beach
- decorate mode: clickable copies, the snap rune on tables, the piece menu (turn, nudge,
  undo), putting a lantern on a table, the lantern moving and turning with its table in
  one undoable step, picking up
- size, tilt, grid and copies: a table made bigger with the lantern kept on its top (and
  the client seeing the new size), the size and tilt limits, a tilt reaching the client
  as the object's rotation, the "More turns, tilt and size" menu, "Place another like
  this" handing over a chair that lands with the first one's turn, size and tilt, and
  the grid squaring up a new piece and nudging it one square
- full bags: pieces go to House Storage, undo takes them back out, "Take everything"
- the Collection: categories, hints with progress, "(new)" marks that clear once seen and
  the new counts on the Home menu, a piece's page (what you have, get one, get 5),
  showing unlocked pieces only, searching by name (`.house collection lamp`), a level up
  unlocking a shelter on the spot, a GM unlocking the mailbox for the owner
- buildings: placing a faction building, the pick up choice, what counts as inside (a
  lantern in a corner does, a table past the wall doesn't), the building and what's
  inside coming back, undo
- a mannequin: it takes after its owner, its menu, dressing it from the bags (the item
  leaves the bags but stays the same item), the figure holding the sword and wearing the
  pants (read the way the client reads them), undo and redo giving back the very same
  item, taking gear off, picking it up with its gear and undoing that, moving it with the
  targeting circle (the Move a Piece item used up, undo), gear mailed when the bags are
  full, and the gear still there after a relog
- visitors: greeting, private islands refusing strangers, invites, the visit menu, a
  guest arriving with one click, sitting on a chair, opening the owner's mailbox and
  seeing what the mannequin wears but not changing anything, private copies, privacy
  presets
- the addon messages: at login, on request, the selected piece and the undo label
- pack up everything and undo, unstuck, logging out on the island and back in (an
  unfinished move's item is gone)

Add `--verbose` to see every chat line, and `--layout guildhouse` when the server keeps
the guild hall. The exit code is 0 only if every check passes.

## Not covered by the test

These need a real client:

- how things look: pieces on tabletops, building models and their collision, the
  targeting circle, mannequins wearing their gear
- the cleared island client patch (`tools/gm-island-cleared/make_client_patch.sh`)
- the client addon's window (its logic is tested with
  `client-addon/test/harness.lua`)

And **party bots following into a house**: the test server is built without
mod-playerbots, so there are no bots to try it with.

## Troubleshooting

- `Table 'acore_world.charsections_dbc' doesn't exist`: the core expects this
  table but, before the pending update added on the fork, no SQL created it.
  Pull the fork branch that contains
  `data/sql/updates/pending_db_world/rev_1790527052762784643.sql`.
- Ports 3306, 3724 and 8085 must be free on the host.
- If the worldserver dies, its tmux window stays open with the output:
  `tmux attach -t acore`, then `Ctrl-b n` to the `world` window.
