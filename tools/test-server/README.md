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
on an NTFS or exFAT drive it turns labeling off for the container instead), waits for the
worldserver, runs `housing_smoke.py` inside and removes the container.
`KEEP=1` leaves it running with ports 3724/8085 published, so you can log in with a client or attach to the console
(`podman exec -it <name> tmux attach -t acore`).

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
4. Builds and installs to `$SERVER_DIR` (build dir `$SERVER_DIR/build`).
5. Writes `authserver.conf`, `worldserver.conf` and `mod_playerhousing.conf`
   (existing files are kept). Warden is disabled for the headless client.
6. Creates the `acore` MySQL user and databases, then populates them with
   `worldserver --dry-run`.
7. Applies this module's SQL. The core's auto-updater only reads
   `modules/<name>/data/sql/db-*`, so the files in `sql/` are applied here.
8. Creates accounts `houseowner` and `houseguest` (players) and `admin` (GM 3).
   Passwords match the names.

Variables: `CORE_DIR`, `SERVER_DIR`, `BUILD_DIR`, `JOBS`.

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

`testclient/housing_smoke.py` logs in two characters (`Krookowner` and
`Krookguest`, created on first run) and exercises:

- starter house and starter unlocks on first login
- `.krook`, `.krook status`, `.krook leave`, `.krook add`, `.krook add <id>`
- steward gossip menus in the city and inside the house
- entering the GM Island guild house, moving-in props, the island's own spawns hidden
- buying kits from Krook's Cranny and placing them with Flare targeting inside the house
- stage gating, upgrading, catalog placement, list, move and remove furniture
- two houses at the same spot staying invisible to each other
- privacy, invites, guest visits with the owner home, the owner coming home with a
  guest inside, guests blocked from editing
- style changes, reconnecting and relogging inside the house, normal phase afterwards,
  persistence

Add `--verbose` to see every chat line, and `--layout cleared` when the server runs the
cleared-island variant (`tools/gm-island-cleared`). Exit code is 0 only if every check passes.

## Not covered by the test

- **Party bots following into a house.** The test server is built without
  mod-playerbots, so there are no bots to try it with.
- **`.krook add` is open to every player** and summons a steward that never
  despawns, so players can spawn as many as they like, anywhere. This is a design
  question, not something the test checks.

## Troubleshooting

- `Table 'acore_world.charsections_dbc' doesn't exist`: the core expects this
  table but, before the pending update added on the fork, no SQL created it.
  Pull the fork branch that contains
  `data/sql/updates/pending_db_world/rev_1790527052762784643.sql`.
- Ports 3306, 3724 and 8085 must be free on the host.
- If the worldserver dies, its tmux window stays open with the output:
  `tmux attach -t acore`, then `Ctrl-b n` to the `world` window.
