# Test server for mod-playerhousing

Scripts to stand up a throwaway AzerothCore server with this module, plus a
headless 3.3.5a client that runs the housing features end to end and checks the
database after each step.

Everything installs under `~/acore-test-server` and uses its own MySQL databases
(`acore_auth`, `acore_characters`, `acore_world`), so keep it away from a MySQL
that already hosts a live server.

## Fastest: the prebuilt image (Podman)

The core fork publishes a test server image with the core already compiled, the client
data and populated databases inside
([`apps/test-server-image`](https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom/tree/custom/apps/test-server-image)
in azerothcore-wotlk-playerbots-custom). With it, a full test run is one command and only
this module gets compiled:

```bash
tools/test-server/podman-test.sh           # add --verbose for every chat line
```

The image is `ghcr.io/dudemcbro/acore-test-server:latest`. If its package is private,
run `podman login ghcr.io` once first, with a token that has `read:packages`.

If you can't pull that image, build it from the core fork's root
(`podman build -f apps/test-server-image/Containerfile -t acore-test-server .`, about an
hour) and point the scripts at it: `IMAGE=localhost/acore-test-server tools/test-server/podman-test.sh`
(the same `IMAGE` works for `dev-container.sh`).

It starts a throwaway container with this checkout mounted (`:Z` for SELinux hosts such as
Fedora; on an NTFS or exFAT drive it turns labeling off for the container instead), clears GM
Island's guild hall from the server data, builds the module, starts the servers with
FreeMode on, runs `collection_smoke.py` inside and removes the container.

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

## Building everything locally (without the image)

`setup.sh` is written for Ubuntu 24.04. On an immutable host (Bluefin, Silverblue and
similar), run it inside a distrobox, which shares your home directory and network with
the host:

```bash
distrobox create --name acore --image ubuntu:24.04
distrobox enter acore
```

Clone the core and the module into your home directory, then build and test:

```bash
git clone https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom.git ~/azerothcore-wotlk-playerbots-custom
git clone https://github.com/dudemcbro/mod-playerhousing.git ~/mod-playerhousing
CORE_DIR=~/azerothcore-wotlk-playerbots-custom ~/mod-playerhousing/tools/test-server/setup.sh
~/mod-playerhousing/tools/test-server/start.sh
python3 ~/mod-playerhousing/tools/test-server/testclient/collection_smoke.py
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
8. Applies this module's SQL. The core's updater only imports a module's
   `data/sql/world`, `data/sql/characters` and `data/sql/auth` folders, so the files in
   `sql/` are applied here.
9. Creates accounts `houseowner` and `houseguest` (players) and `admin` (GM 3).
   Passwords match the names.

Variables: `CORE_DIR`, `SERVER_DIR`, `BUILD_DIR`, `JOBS`, `HOUSING_LAYOUT` (`cleared`,
the default, or `guildhouse`), `TOOLS_BUILD` and `CLIENT_DATA_VERSION`.

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

More accounts: `python3 tools/test-server/testclient/create_account.py NAME PASSWORD [--gm 3]`.

## The automated test

`testclient/collection_smoke.py` is the end-to-end suite (96 checks). It logs in headless
characters (`Krookowner`, `Krookguest` and the GM `Krookadmin`, created on first run),
resets their housing, plays it through, and checks the database and what each client sees
after every step:

- getting started: no House Key at first, no way home without it, `.house key` pointing to
  Krook, Krook beside the Stormwind innkeeper giving the key and the first pieces, and
  another key after one is lost
- the window: `.house` and the House Key opening it, Collection counts instead of bag items,
  old furnishing items joining the counts
- placing on the mouse: ghosts the client moves itself, the grid, raising, walls (facing out)
  and ceilings (hanging), size and tilt (right over and upside down), turning with the
  player, setting down where the client showed it
- changing: a right-click picking a piece up, buildings carried as their real object or
  their exact see-through model, several pieces at once, sets
- the Placed list's green ring, appearing and going
- mannequins: dressing and undressing, race and a new look each time, trading gear with
  the character and back
- layouts, Krook leaving after his tour and coming when called, visitors and guests,
  logging back in on the island, and one island shared by all the account's characters

Add `--verbose` to see every chat line, and `--layout guildhouse` when the server keeps
the guild hall. The exit code is 0 only if every check passes.

`testclient/housing_smoke.py` is the suite from before the redesign (menus, bag items, House
Storage, the targeting circle). It is kept for reference and isn't run.

## The load test

`testclient/load_test.py` puts many players on their islands at once (accounts
`LOADTEST01` and up, created on first run). Each goes home, opens the island, places
pieces, turns, nudges, undoes and redoes, and visits a neighbor, while the GM account
samples `.server info`. It reports how long each kind of action took to answer, the
server's update times, the worldserver's memory, and any island that showed another
island's pieces.

All the load test's players share one Python process, so when they all go home at the same
moment, reading their arrival packets takes the process seconds, and "go home" reads
slow. One more player, `Loadprobe`, runs in a process of its own and goes home and back
all through the test: its line (`go home: server`) is what the server itself takes.

In the development container (`dev-container.sh start`; use `docker` instead of
`podman` if you started it with `ENGINE=docker`):

```bash
podman exec housing-dev python3 /opt/acore/modules/mod-playerhousing/tools/test-server/testclient/load_test.py --players 99
```

Each piece is placed the way the addon does it with the mouse: the ghost appears, a few
points where the mouse moved are sent (`--mouse-points`, ten a second), then the click.
`--hold 60` keeps everyone at home with their pieces out for a minute once all are done,
and reports the server's update times for that stretch on their own; add `--walk` to have
them run in circles meanwhile, which is what makes the server look at other islands'
pieces.

```
docker exec housing-dev python3 /opt/acore/modules/mod-playerhousing/tools/test-server/testclient/load_test.py --players 100 --pieces 60 --mouse-points 3 --hold 60 --walk
```

It waits for its characters to be out of the world before resetting them, and logs them
out cleanly at the end, so runs can follow each other. Results are in the module README
(Load).

## Not covered by the test

These need a real client:

- how things look: pieces on table tops, building models and their collision, the ghost
  following the mouse, mannequins wearing their gear
- the cleared island client patch (`tools/gm-island-cleared/make_client_patch.sh`)
- the client addon's window (its logic is tested with
  `client-addon/test/harness.lua`)

And **party bots following into a house**: the test server is built without
mod-playerbots, so there are no bots to try it with.

## Troubleshooting

- `Table 'acore_world.charsections_dbc' doesn't exist`: the core expects this
  table, but older core checkouts have no SQL that creates it. Update the core to the
  current `custom` branch, which adds
  `data/sql/updates/pending_db_world/rev_1790527052762784643.sql`.
- Ports 3306, 3724 and 8085 must be free on the host.
- If the worldserver dies, its tmux window stays open with the output:
  `tmux attach -t acore`, then `Ctrl-b n` to the `world` window.
