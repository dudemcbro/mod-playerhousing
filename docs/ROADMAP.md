# Roadmap: 1.0 to 2.0

Each release after 1.0 centers on one core system, so it can be built, tested and shipped
on its own. Only what's missing is listed; what 1.0 already has is at the end.

## 1.0: the first release

**Scope:** one housing map (GM Island, cleared, one private copy per account) and the
current catalog. The game systems are done (see "Already in 1.0"); what's left is release
work:

| Item | Why |
| --- | --- |
| GM Island's guild hall back on the regular map | The client patch and server data remove it from the map itself, so the regular GM Island has no hall either. Spawn it as a type 33 building in the normal phase at its original spot. |
| In-game setup check | The player download and its guide are done (`tools/release/make_player_bundle.sh`, `docs/PLAYER_SETUP.md`); still missing is a check in game that says what's missing (addon, DLL, patch). |
| WoW hanging on exit (Lutris launcher) | Players will hit it on day one. |
| Preview gaps | A few pieces still frame badly or show nothing (Lich King figurine). |
| Tests | Retire or port `housing_smoke.py` (it still drives the old menus); fix the flaky first "go home" check. |
| Tag `v1.0.0` | With the SQL, the client files and the upgrade notes in the release. |

## 1.1: islands as instances

**Core system:** where islands live. The biggest gap left, and what 2.0 stands on.

Today every island is a phase of the same spot on Kalimdor: one map thread runs them all,
and every moving player looks at every island's pieces (README, "Thousands of players").
Past a few hundred open islands that slows all of Kalimdor.

- A map of its own: a Map.dbc row and a WDT listing GM Island's tiles in the client patch;
  server maps, vmaps and mmaps extracted for it.
- One instance per open island: each sees only its own pieces, and islands update in
  parallel (`MapUpdate.Threads`).
- Every island, layout and set moves over unchanged; the phase code stays one release as a
  fallback.
- Load test at 300 and 1000 open islands.

## 1.2: placing and editing

**Core system:** the mouse and the held piece.

- Tilted pieces on walls (today a tilted piece won't stick to a wall).
- Align by mouse: a held piece snaps to the edge, height or turn of the piece beside it
  (the `match` and `row` commands exist; this brings them to the mouse).
- Mirror a selection or set.
- The Placed-list ring seen only by whoever chose it.
- A keybinding page in the addon's options, defaults shown.

## 1.3: progression and people

**Core system:** reasons to come back, and others to share it with.

- Krook's quest chains past the welcome tour: small tasks that teach a system and give a
  piece.
- Collection milestones (25, 50, 100 of a category) with a title or a piece.
- More profession pieces: 8 today, at least one for every profession.
- Open house: a time window when anyone may visit, announced to friends and guild.
- Roommate permission levels: place only, edit their own pieces, edit everything.
- A guild island: one island a guild shares, decorated by its officers.

## 1.4: mannequins and figures

**Core system:** the figures on an island.

- Poses (taken out of 1.0): only animations every playable race's model has, held so every
  viewer sees the same frame. Kneel, sit, sleep and dead are stand states; the rest are
  emote states (notes in `src/HousingStands.cpp`). The open problem is holding one chosen
  frame the same for every client.
- Weapons drawn or sheathed.
- Mount and pet display stands, from the player's own collection.
- A plain wooden mannequin (no race features).

## 2.0: more than one place to live

**Headline:** choose where your home is.

- Several housing maps, each a cleared piece of the world the client already has (a forest
  glade, a coastline, a mountain shelf). [MULTI_REALM_HOUSING_DESIGN.md](MULTI_REALM_HOUSING_DESIGN.md)
  proposes these as phased "realms" in place (Fray Island, Fenris Isle, Jaguero Isle and
  more); whether each is a phase or an instance (1.1) is still to decide.
- Move between maps keeping your Collection; layouts that fit are set out again, pieces
  that don't go back to the Collection.
- Map-specific pieces and Krook quests.
- Poses finished (1.4).

## Already in 1.0

- **The island:** one per account, private by phase; logging out there logs you back in
  there; weather, time of day and music; a moveable door (landing spot); unstuck.
- **Getting started:** Krook by every capital's innkeeper gives House Keys (and new ones);
  his welcome tour.
- **The Collection:** 2,350+ unlockable pieces from exploration, dungeons, raids,
  reputation, professions, holidays, Wintergrasp, capstone achievements and figurines;
  counts instead of bag items; extra copies for gold; sort and search; previews.
- **Placing:** on the mouse; turn, raise, tilt and size with the wheel; walls and
  ceilings; on table tops; grid; place another; multi-select with Ctrl; undo and redo with
  a history list.
- **Saving:** layouts (save, set out, send to a friend, copy from an island that allows
  it, get the missing pieces) and sets (a selection set down anywhere).
- **Working pieces:** chairs to sit on, campfire, Bank Chest, Mailbox, crafting stations,
  Music Box.
- **Mannequins:** a character sheet with drag and drop gear, race, man or woman, new look,
  Trade gear.
- **People:** privacy (private, friends and guild, public); guests and roommates; visit
  lists (party, guild, friends, invited, public, most liked); likes; visitor log;
  greeting; guestbook with owner delete.
- **Moderation:** reports, GM inspect, clear greeting, hide, pack up.

## Ground rules for every release

- Playerbots never get housing.
- One system per action: no second way to do the same thing.
- All placing and editing from the mouse; the window lists, it doesn't steer.
- Every change undoable, nothing lost or duplicated.
- The addon harness and the end-to-end test pass before a deploy; world SQL deploys apply
  the base, catalog and content files together.
