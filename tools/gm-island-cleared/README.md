# Cleared GM Island (optional)

By default every house is the guild house that ships with GM Island. This folder
removes that building instead, so houses become a campsite on open ground where it
stood. It is meant for testing; the guild house layout needs none of this.

Three things have to match, or the server and clients disagree about where walls are:

| Part | What changes | Tool |
| --- | --- | --- |
| Client | The house is moved 2000 yd underground in `Kalimdor_1_1.adt`, shipped as a patch MPQ | `make_client_patch.sh` |
| Server | The house and its 68 built-in props leave the collision tile, and GM Island's pathing tile is rebuilt | `server_data.sh` |
| Database | Styles spawn a campsite on the old house plateau | `sql/layouts/gm_island_cleared.sql` |

## Server

The server side needs `mmaps_generator`, so build the tools once:

```bash
TOOLS_BUILD=maps-only CORE_DIR=~/azerothcore-wotlk-playerbots-custom tools/test-server/setup.sh
```

Then, with the worldserver stopped:

```bash
tools/gm-island-cleared/server_data.sh
mysql -uacore -pacore acore_world < sql/layouts/gm_island_cleared.sql
tools/test-server/start.sh
python3 tools/test-server/testclient/housing_smoke.py --layout cleared
```

`server_data.sh` keeps the original files as `.orig`. To go back to the guild house:

```bash
tools/gm-island-cleared/server_data.sh --restore
mysql -uacore -pacore acore_world < sql/db_world/base/mod_playerhousing_world_hotfix.sql
```

Tested here: the cleared layout passes 53/53. With the house left in the server data
but the cleared layout in the database, the checks that build across the old walls fail,
so the test does notice a mismatch.

## Client patch

This part runs on your machine, because it reads your own 3.3.5a client files:

```bash
sudo apt install smpq          # inside the distrobox
tools/gm-island-cleared/make_client_patch.sh /path/to/WoW-3.3.5a/Data patch-H.MPQ
```

Copy `patch-H.MPQ` into the `Data` folder of every client that plays on this server.
Without it, players still see (and bump into) the house while the server treats the spot
as open ground. Pick another letter if `patch-H.MPQ` is already taken.

`adt_sink_wmo.py` and the MPQ packing were checked against a synthetic tile (only the
house placement changes, the file keeps its size, and the entry is found by the client's
backslash path). They have not been run against a real client file from here, since this
environment has no WoW client, so check the island in game after the first build.
