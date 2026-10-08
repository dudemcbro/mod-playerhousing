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

`testclient/collection_smoke.py` is the current end-to-end suite. It checks the addon-only
window, Collection counts instead of furnishing items, mouse/local ghosts, exact building
previews, right-click movement of buildings and furnishings, tilt, mannequins, sets, layouts,
visitors, logging back into an island, and one shared island per account.

`testclient/housing_smoke.py` below is retained as a reference for the pre-2.1 menu-and-bag
workflow; it is not run by the test scripts because those menus and furnishing items were
intentionally removed.

### Legacy pre-2.1 suite

`testclient/housing_smoke.py` logs in three characters (`Krookowner`, `Krookguest` and
the GM `Krookadmin`, created on first run), resets their housing, and plays it through,
checking the database and what the client sees after each step (302 checks, 303 with `PlayerHousing.Catalog = everything`):

- first login: House Key, starter furnishings, past progress unlocking pieces
- the Home menu from `.house` and from the key, going home, Krook's greeting, the
  starter wreckage
- Krook's welcome tour: the quests above his menu, each completing when it's done (going
  home, placing, turning a piece, undoing, inviting a guest), handed in one after
  another, and the last unlocking Krook's Picnic Basket
- placing with the targeting circle where it was clicked, facing you; each item sends its
  own spell, and the circles fit the pieces (small for a chair, large for a farmhouse);
  no menu after placing furniture, the menu after a building, and with `.house adjust
  all` the menu after anything, with Take it back and Keep it here; undo returns the
  item, redo places it again; no spacing rules; placing far out on the island; refusing
  spots off the island; swimmers brought back to the beach
- edit mode (the addon's keys): a click only selects, `.house shift` moves and turns a
  piece relative to your facing, a quick run of shifts is one undo step, a flood of them
  is capped, Tab picks the next piece; a ring under the selected piece (gone when edit
  mode ends), and no Move a Piece in the bags; G's ghost: a see-through copy with the
  piece's ghost model starting where the piece stands, walked over, pushed farther and
  turned, then set down there (the ring following) and undone; Escape leaving the piece
  where it was; the grid size reaching the addon
- several pieces at once: the undo history newest first and `undo 2`, adding a piece to
  the selection (a ring under each), sliding and turning the group with what stands on
  it, one undo for the run and the group still selected after it, Fwd nudging all of
  them, size waiting for one piece, a newly placed piece selected on its own, match
  height, line up, Ctrl-click taking a piece out and
  putting it back, a plain click selecting just one, picking the group up and undoing
  that; a row of three chairs and one undo for it; saving the selection as a set, the
  window's list of sets, setting it down with Move a Piece where the circle was clicked
  and deleting it; a set's name needing a letter; a lantern saved before its table
  still standing on it when set down; going to a piece; the Collection's recently placed pieces; taking one
  piece out of House Storage
- the addon's window: its lists (Collection, Placed, Island, Layouts, Guests, Visit),
  getting copies, weather by name, House Storage, and the House Key opening the window
  (or its menu when the player prefers)
- decorate mode: clickable copies, the snap rune on tables, the piece menu (turn, nudge,
  undo), putting a lantern on a table, the lantern moving and turning with its table in
  one undoable step, picking up
- size, tilt, grid and copies: a table made bigger with the lantern kept on its top (and
  the client seeing the new size), the size and tilt limits, a tilt reaching the client
  as the object's rotation, the "More turns, tilt and size" menu, "Place another like
  this" sending a see-through chair after the owner (the addon told which), set down
  ahead of the owner with the first one's turn, size and tilt, the ghost gone after, and
  the grid squaring up a new piece and nudging it one square
- full bags: pieces go to House Storage, undo takes them back out, "Take everything"
- the Collection: categories, hints with progress, "(new)" marks that clear once seen and
  the new counts on the Home menu, a piece's page (what you have, get one, get 5),
  showing unlocked pieces only, searching by name (`.house collection lamp`), a level up
  unlocking a shelter on the spot, a GM unlocking the mailbox for the owner
- figurines: a GM unlocking the Hogger Figurine, marked new in the Collection, placed as
  Hogger's own model shrunk to table size, its piece menu, no tilting, and a guest
  seeing it as a trophy
- the catalog of every object: left out with `curated`; with `everything`, the Catalog
  category counts every object and a Wanted Poster places like any piece
- the Bank Chest: unlocked at level 20, its menu, the bank opening through an unseen
  banker at the chest, a bank slot bought there, and refused from across the island;
  placing a lantern and banking it in the same packet batch (the lantern is used up, not
  banked); a guest finding it locked
- buildings: placing a faction building, the pick up choice, what counts as inside (a
  lantern in a corner does, a table past the wall doesn't), the building and what's
  inside coming back, undo; moving the building: carried as it is, the chair and lantern
  inside as see-through ghosts, all set down two yards over, then undone
- a mannequin: it takes after its owner, its menu, dressing it from the bags (the item
  leaves the bags but stays the same item), the figure holding the sword and wearing the
  pants (read the way the client reads them), undo and redo giving back the very same
  item, taking gear off, picking it up with its gear and undoing that, moving it with the
  targeting circle (the Move a Piece item used up, undo), gear mailed when the bags are
  full, a ghost set down with full bags all the same, and the gear still there after a
  relog
- saved layouts: saving the island, setting it out again (everything back where it was,
  the mannequin's gear to the bags) and undoing that in one step, renaming (a name with
  a quote), sending a copy (refused to a stranger, fine for a friend), letting visitors
  copy the layout
- ambience: rain and night set from Island settings reaching the client, a Music Box
  playing Grizzly Hills; the guest arriving to the same rain, night and music, hearing
  what the music box plays, and getting the real clock and weather back on leaving
- visitors: greeting, private islands refusing strangers, invites, the visit menu, the
  owner setting the door and a guest arriving there with one click, facing the door's way;
  signing the guestbook (once a day), the owner reading it (new, then read) and throwing a
  note out; saving a copy of the island's layout and getting the
  missing pieces, becoming a roommate (decorating, placing their own chair, moving the
  owner's table with their own undo, refused packing up; the owner picking up their
  chair sends it to their House Storage and undo takes it back; picking it up
  themselves returns it to their bags; no more changes once a guest again), liking the
  island (the owner told, owners can't like their own), the visitor log, the most liked
  islands list, and the owner told of the visit on coming home, reporting the island
  (the GM told, once per island), a GM listing and closing reports, clearing the
  greeting, hiding a public island from strangers, and inspecting it anyway, sitting on a chair, opening the owner's mailbox and
  seeing what the mannequin wears but not changing anything, private copies, privacy
  presets
- the addon messages: at login, on request, the selected piece and the undo label
- leaving the island with a ghost following: it ends and nothing moves
- pack up everything and undo, unstuck, logging out on the island and back in (an
  unfinished move's item is gone)
- a GM packing up the island: every piece in House Storage, the mannequin's sword in the
  mail, and the owner's undo list gone with the pieces
- a GM's photo tour: the first building set up with the addon told which, then the next,
  then stopping
- last, deleting a character: a fourth account's character makes the guest a roommate,
  the guest places a chair there, the character is deleted, and the chair is in the
  guest's House Storage while the island is gone

Add `--verbose` to see every chat line, and `--layout guildhouse` when the server keeps
the guild hall. The exit code is 0 only if every check passes.

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

- how things look: pieces on tabletops, building models and their collision, the
  targeting circle, mannequins wearing their gear
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
