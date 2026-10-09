# Server setup

For server admins: from nothing to players building on their islands. It takes about an
hour, most of it compiling. Each step says how to check it worked.

Just want to try it? [tools/test-server](../tools/test-server/README.md) runs a complete
server with the module in a container, no setup needed.

## What you need

- An AzerothCore WotLK (3.3.5a) server you build yourself. The module uses only standard
  script hooks, so no core patches; it is developed on
  [azerothcore-wotlk-playerbots-custom](https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom)
  and works alongside mod-playerbots (bots never get housing).
- The core's map tools (`mmaps_generator`), built with `-DTOOLS_BUILD=maps-only` or `all`.
- A 3.3.5a client, to build the client patch your players install. Use a client like
  theirs: plain 3.3.5a, plus any custom patches your server hands out.
- `smpq` and Python 3 for the patch. On Ubuntu or Debian: `sudo apt install smpq python3`
  (on an immutable desktop such as Bazzite or Bluefin, inside a distrobox).

## 1. Build the module into the server

```bash
cd <azerothcore>/modules
git clone https://github.com/dudemcbro/mod-playerhousing.git
cd <azerothcore>/build
cmake .. <your usual options, with -DTOOLS_BUILD=maps-only if the map tools aren't built yet>
cmake --build . -j"$(nproc)"
cmake --install .
```

Check: `etc/modules/mod_playerhousing.conf.dist` exists in the server's install folder.

## 2. Configure

With the worldserver stopped:

```bash
cd <server>/etc/modules
cp mod_playerhousing.conf.dist mod_playerhousing.conf
```

The defaults suit a live server. The ones worth a look (all listed in the
[README](../README.md#configuration)):

| Setting | Default | Change it when |
| --- | --- | --- |
| `PlayerHousing.MaxFurnishings`, `MaxBuildings` | 200, 10 | You want smaller or bigger islands (lower is lighter on a busy server) |
| `PlayerHousing.DefaultPrivacy` | private | New islands should start open to friends or everyone |
| `PlayerHousing.FreeMode`, `UnlockAll` | 0, 0 | Only on a test server: everything free and unlocked |
| `PlayerHousing.Layout` | cleared | You'd rather keep GM Island's guild hall (`guildhouse`; skip step 4's server data) |

Also check that `worldserver.conf` doesn't set `AddonChannel = 0`: the addon talks to the
server over it.

## 3. Apply the SQL

With the worldserver stopped, from the module folder:

```bash
MYSQL="mysql -u <user> -p" tools/release/apply_sql.sh
```

It applies the five files in the right order (every one is safe to apply again) and says how
many pieces are defined (about 2,400, counting the optional catalog). The worldserver's own updater doesn't pick these up,
because they live in `sql/`, not `data/sql/`.

## 4. Clear the island in the server data

The default island is GM Island without its guild hall. The server's collision and pathing
have to match, or players walk into walls they can't see. With the worldserver stopped:

```bash
SERVER_DIR=<server> tools/gm-island-cleared/server_data.sh
```

`SERVER_DIR` is the server's install folder (the one with `bin/` and `data/`). It keeps the
originals as `.orig`; `--restore` puts the hall back. Details:
[tools/gm-island-cleared](../tools/gm-island-cleared/README.md).

## 5. Start and check

Start the worldserver. Its log should say:

```
mod-playerhousing: Loaded layout 'cleared' and 340 pieces.
```

(About 340 with the default curated Collection; around 2,400 with
`PlayerHousing.Catalog = "everything"`.)

If it says definitions failed to load, step 3 was incomplete: run `apply_sql.sh` again.

## 6. Make the player download

One zip with everything a player needs, built from your 3.3.5a client's `Data` folder:

```bash
tools/release/make_player_bundle.sh /path/to/WoW-3.3.5a/Data
```

It writes `PlayerHousing-client.zip`: the addon, PlayerHousing.dll and its launcher, the
client patch (`Data/patch-H.MPQ`) and [PLAYER_SETUP.md](PLAYER_SETUP.md). Host it next to your
client download or patches, and point players to it. Options:

- `--patch-name patch-Z.MPQ` if your server already ships a `patch-H.MPQ`. The patch
  should load after any other patch that carries `Item.dbc`, `ItemDisplayInfo.dbc`,
  `CreatureDisplayInfo.dbc` or `CreatureModelData.dbc`, so give it a later letter.
- `--icons-only` for the `guildhouse` layout.
- `--patch <file>` to pack a patch you built already.

## 7. Try it as a player

Install the zip on your own client ([PLAYER_SETUP.md](PLAYER_SETUP.md)), log in, and:

1. Talk to Krook beside any capital's innkeeper and ask for a house: you get a House Key.
2. Right-click the key, **Go home**: you land on the cleared island.
3. Click a piece in the Collection: it follows the mouse, a click sets it down.

## Updating

```bash
cd <azerothcore>/modules/mod-playerhousing
git pull
cd <azerothcore>/build && cmake --build . -j"$(nproc)" && cmake --install .
```

Then, with the worldserver stopped, run `tools/release/apply_sql.sh` again and start it.
When the update changed the addon, the DLL or the content (`tools/content/`,
`client-addon/`, `client-dll/bin/`), run `make_player_bundle.sh` again and tell players to
install the new zip over the old one (and clear their `Cache` folder once).

## When something's wrong

| What you see | Why, and the fix |
| --- | --- |
| Log: definitions failed to load, housing disabled | Only part of the world SQL went in. Run `apply_sql.sh` again, restart. |
| Players walk into an invisible wall where the hall stood | The server data step was skipped (step 4), or the player has no client patch. |
| Players see the hall but walk through it | The player is missing the client patch, or another patch loads after it and wins. |
| Housing items are red question marks | Same: the client patch is missing or loads too early. |
| `.house` says the window needs the addon | The player hasn't installed the addon, or it's disabled on the character screen's AddOns button. |
| Pieces follow the player instead of the mouse | The DLL isn't loaded: the game wasn't started with `PlayerHousingLauncher.exe`, or `AddonChannel = 0`. |
| Bots have no islands | On purpose: playerbots never get housing. |

Uninstalling is in the [README](../README.md#uninstall).
