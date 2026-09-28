# Cleared GM Island (the default)

Housing uses GM Island with its guild hall removed, so every island starts as open ground
where the hall stood (with a fallen cart and a shredded tent waiting). The island's own
trees, rocks and beach stay. To keep the hall instead, set
`PlayerHousing.Layout = "guildhouse"` and skip everything here.

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
- The prebuilt image and `podman-test.sh` / `dev-container.sh` run it at container start.

By hand, with the worldserver stopped:

```bash
SERVER_DIR=~/acore-test-server tools/gm-island-cleared/server_data.sh
```

It keeps the original files as `.orig` and can be re-run safely. To go back to the hall:

```bash
SERVER_DIR=~/acore-test-server tools/gm-island-cleared/server_data.sh --restore
```

and set `PlayerHousing.Layout = "guildhouse"`.

When a server switches to the cleared layout, anything players had placed inside the old
hall goes to their House Storage, and they get a message on their next visit.

Tested here: the end-to-end test passes 79/79 on the cleared layout.

## Client patch

Nothing to download: the patch is built from your own 3.3.5a client files, so it runs on
your machine. On Bluefin, inside a distrobox (Ubuntu):

```bash
sudo apt install smpq
tools/gm-island-cleared/make_client_patch.sh /path/to/WoW-3.3.5a/Data patch-H.MPQ
```

Copy `patch-H.MPQ` into the `Data` folder of every client that plays on this server, and
clear the client's `WDB` cache folder once. Without the patch, players still see (and bump
into) the hall while the server treats the spot as open ground. Pick another letter if
`patch-H.MPQ` is already taken.

`adt_sink_wmo.py` and the MPQ packing were checked against a synthetic tile (only the
hall's placement changes, the file keeps its size, and the entry is found by the client's
backslash path). They have not been run against a real client file from here, since this
environment has no WoW client, so check the island in game after the first build.
