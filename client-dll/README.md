# PlayerHousing.dll: place pieces with the mouse

World of Warcraft 3.3.5a can't tell an addon where the mouse points in the world, so without
help a piece being placed follows you and you walk it into place. This small DLL adds that one
thing. With it, and the Player Housing addon, a piece follows your mouse cursor and a click
sets it down: on the floor, on a table, or on a wall (it turns to face out from the wall).

It is recommended, not required, and only for Wow.exe 3.3.5a build 12340. Everything else in the module works
without it. The addon sends where the mouse points over AzerothCore's addon command channel,
which is on unless the server sets `AddonChannel = 0` in worldserver.conf; without it the
addon doesn't use the mouse (it checks when you log in), and pieces follow you as before.

## Install (each player)

Players get it in the server's PlayerHousing-client.zip (see
[docs/PLAYER_SETUP.md](../docs/PLAYER_SETUP.md)). By hand:

1. Copy `bin/PlayerHousing.dll` and `bin/PlayerHousingLauncher.exe` into the game's folder,
   next to `Wow.exe`.
2. Start the game with `PlayerHousingLauncher.exe` instead of `Wow.exe`. Anything you would
   pass to Wow.exe goes after it. Under Wine: `wine PlayerHousingLauncher.exe` from the game
   folder; in Lutris, Bottles or Steam, change the game's executable to
   `PlayerHousingLauncher.exe` and keep the same Wine prefix and settings.
3. In game, check it's there: `/run print(PlayerHousing_DLLInfo())` should print
   `PlayerHousing.dll 4: ready (Wow.exe 3.3.5.12340) ...`. `PlayerHousing.log`, next to the
   DLL, says the same.

The launcher starts Wow.exe paused, loads the DLL into it, and lets it carry on. Wow.exe itself
is never changed. If the DLL can't load, the game still starts, with a message saying why, and
pieces float ahead of the player instead.

## Using it

Click a piece in the housing window's Collection, or right-click a placed piece: it follows
the mouse over the world (floor, table top, a wall facing out, under a ceiling), and a click
sets it down. The mouse and key controls are in the main
[README](../README.md#client-addon).

## What it does, exactly

It adds these to the game's Lua, and nothing else:

- `PlayerHousing_CursorWorld([fx, fy [, flags [, reach]]])` returns `x, y, z` of the first
  thing under the cursor (ground, buildings, furniture with collision; not creatures or
  players), then `nx, ny, nz`, which way that surface faces, when it can tell. `fx, fy` is
  the cursor as a fraction of WorldFrame from its bottom-left corner (without them, of the
  game window). `flags` are the game's line of sight flags (0x100111), `reach` how far to
  look (200 yards).
- `PlayerHousing_DLLInfo()` returns a line saying what the DLL is doing.
- `PlayerHousingDLL` is its version.

To answer, it reads the camera and asks the game's own line of sight function (the one the
game uses for nameplates behind walls). The ray from the camera through the cursor is worked
out with the game's own world-to-screen projection, so it matches the picture exactly at any
field of view or window shape. To add the functions, it hooks the game's per-frame update
once (with [MinHook](https://github.com/TsudaKageyu/minhook)) and adds them whenever a new
Lua starts (logging in, `/reload`). It reads nothing else, sends nothing, and changes nothing
in the game's memory besides that hook and the two values that let Lua call a function from
outside Wow.exe (the same ones [awesome_wotlk](https://github.com/FrostAtom/awesome_wotlk)
sets). It works alongside awesome_wotlk.

Before touching anything it checks that Wow.exe's version is 3.3.5.12340. With any other
build it does nothing, and `PlayerHousing_DLLInfo()` says so.

Servers with Warden set to kick for modified clients might object to the hook; the module's
own servers don't need Warden for anything here. On such a server, leave the DLL out.

## Building it

`bin/` holds prebuilt copies. To build them yourself (32-bit Windows, like Wow.exe), with
MinGW-w64, or in a throwaway container with podman or docker:

```
client-dll/build.sh
```

It also runs the ray test (`test/cursor_ray_test.cpp`: 40 000 checks with made-up cameras). Then
`test/run_wine_test.sh` loads the DLL under Wine into a stand-in for Wow.exe
(`test/fake_game.cpp`, with fakes of the game functions at their real addresses) and checks the
hook, the functions in Lua, the ray, and the launcher starting a game with the DLL. It needs
Wine and MinGW, or runs in a container with podman or docker.

| File | |
|---|---|
| `src/PlayerHousing.cpp` | The DLL: the game's addresses, the hook, the Lua functions |
| `src/CursorRay.h` | The ray through the cursor, from the game's projection (no Windows or game code) |
| `src/Launcher.cpp` | PlayerHousingLauncher.exe |
| `minhook/` | MinHook's 32-bit sources (BSD licence, `minhook/LICENSE.txt`) |
| `test/` | The ray test, the stand-in game, and the Wine test |
