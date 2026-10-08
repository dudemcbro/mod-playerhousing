# Cleared GM Island (the default)

Housing uses GM Island with its guild hall removed, so every island starts as open ground
where the hall stood (with a fallen cart and a shredded tent waiting). The island's own
trees, rocks and beach stay. To keep the hall instead, set
`PlayerHousing.Layout = "guildhouse"` and skip everything here except the client patch,
built with `--icons-only` (see [Item icons](#item-icons)).

Three things have to match, or the server and clients disagree about where walls are:

| Part | What changes | How |
| --- | --- | --- |
| Client | The hall is moved 2000 yd underground in `Kalimdor_1_1.adt`, shipped as a patch MPQ | `make_client_patch.sh`, once per client |
| Server | The hall and its 68 built-in props leave the collision tile, and GM Island's pathing tile is rebuilt | `server_data.sh`, once per server |
| Database | Where players land and where Krook stands | the `cleared` row of `mod_playerhousing_layout`, picked by `PlayerHousing.Layout` |

## Server

`server_data.sh` needs `mmaps_generator` from the core's map tools
(`-DTOOLS_BUILD=maps-only`). The test server scripts take care of it:

- `tools/test-server/setup.sh` builds the map tools and runs `server_data.sh` by default.
- `podman-test.sh` and `dev-container.sh` run it at container start (through
  `container-entry.sh`).

By hand, from the module directory with the worldserver stopped, point `SERVER_DIR` at
the server's install directory (the one with `bin/` and `data/`; the default is the test
server's `~/acore-test-server`). It looks for `bin/mmaps_generator` and
`bin/mmaps-config.yaml` there; `MMAPS_GENERATOR` and `MMAPS_CONFIG` override them:

```bash
SERVER_DIR=/path/to/server tools/gm-island-cleared/server_data.sh
```

It keeps the original files as `.orig` and can be re-run safely. To go back to the hall:

```bash
SERVER_DIR=/path/to/server tools/gm-island-cleared/server_data.sh --restore
```

and set `PlayerHousing.Layout = "guildhouse"`.

When a server switches to the cleared layout, anything players had placed inside the old
hall goes back to their Collection, and they get a message on their next visit.

## Client patch

Nothing to download: the patch is built from your own 3.3.5a client files, so it runs on
your machine. On Ubuntu or Debian (on an immutable host, inside a distrobox):

```bash
sudo apt install smpq
tools/gm-island-cleared/make_client_patch.sh /path/to/WoW-3.3.5a/Data patch-H.MPQ
```

The folder can be the client's install folder or its `Data` folder, in any letter case,
and any `patch-*.MPQ` the client already has is read too. Copy `patch-H.MPQ` into the
`Data` folder of every client that plays on this server, and clear the client's `WDB`
cache folder once. Without the patch, players still see (and bump into) the hall while the
server treats the spot as open ground, housing items show as question marks, and the
see-through ghosts of pieces being placed can't be seen. Pick another letter if
`patch-H.MPQ` is already taken. Rebuild and recopy the patch whenever the module's content
changes, so new pieces get their icons and ghosts.

## Item icons

The 3.3.5a client draws an item's bag icon from its own `Item.dbc`, so items that only
exist on the server show a red question mark. The patch adds every housing item to the
client's `Item.dbc`, and adds `ItemDisplayInfo.dbc` rows for the spell and achievement
icons some pieces use (display ids 190000 and up; other icons reuse the display of an item
that already has them). The rows come from `client_items.tsv`, which
`tools/content/build_content.py` writes; `dbc_add_items.py` adds them to the client's own
copies of the two files, so nothing else in them changes.

If another patch in the client also carries one of these files and loads after
`patch-H.MPQ`, the script warns: its copy would win and the icons would be lost, so give
the output a later letter than that patch.

For a server on the guildhouse layout, `make_client_patch.sh --icons-only` builds a patch
with the icons (and ghosts) alone.

## Ghosts

A piece being placed or moved follows its player as a see-through copy of itself: a
creature with the piece's model (see the main README). The client only draws creatures
from its own `CreatureDisplayInfo.dbc` and `CreatureModelData.dbc`, so the patch adds a
creature model and a see-through display (opacity 150 of 255) for each piece's model, and a
see-through copy of the figurines' and the mannequin's displays, all with ids from 61100 (a
piece's ghost is 60000 plus its item's offset from 900000; the catalog's from 100000). The
rows come from `client_items.tsv` too; `dbc_add_ghosts.py` adds them to the client's own
copies of the two files. The server has the same rows in its world database. On a server
whose players keep an older patch, `PlayerHousing.Ghosts = 0` carries pieces as they are
instead.

A building whose model is an M2 uses the same exact see-through model as a furnishing. A
world-model (`.wmo`) building can't be a creature's model, so while held it is shown as its
real, collisionless game object instead. It is solid rather than translucent, but its real
doors, porches and outline make its front and footprint visible. Older patches used generic
blue blocks for these buildings; the server no longer asks the client to display those.

The old block generator remains in the repository for patch compatibility and tests. Like
Blizzard's own doodads its generated models have no sequence lookup table, avoiding a hang
in patches from before 2026-10-07 when a block was asked to use a non-Stand animation.

`adt_sink_wmo.py` and the MPQ packing were checked against a synthetic tile (only the
hall's placement changes, the file keeps its size, and the entry is found by the client's
backslash path), and the whole patch was since built from a real client and checked in
game. The item tables were checked against the server's copies of the client files.
