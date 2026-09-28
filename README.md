# mod-playerhousing

Private player housing for AzerothCore (WotLK): every owner gets their own phased copy of the guild house on GM Island, with owner-only editing, visiting permissions, style selection, and upgrade progression.

The plan for making housing easier to learn and use (undo, free test mode, placing
anywhere on the island, click to edit) is in [docs/UX_PLAN.md](docs/UX_PLAN.md).

## V1 feature set

- Starter house is free and defaults to **private**.
- 4 selectable styles: `human`, `gnome`, `tauren`, `undead`.
- 6 paid upgrade tiers (stage 1 to stage 6), each at **x3 cost growth**.
- Stage 0 is the furnished guild house plus a few moving-in props (lantern, bedroll, crate); each style adds its own piece at later stages.
- Owner can invite/uninvite guests to private houses.
- Guests can visit, but only owner can edit or place furniture.
- Housing steward NPC uses a **Wolvar orphan** display.
- Furniture is bought from Krook via standard **vendor window** items.
- Right click furniture item to enter placement mode, then place with instant ground-target reticle.
- Steward gossip menus handle all normal workflows.
- Optional command shortcuts:
  - `.krook add` to spawn a housing steward near you
  - `.krook add <catalogId>` to select furniture for Flare placement
  - `.krook leave` / `.krook status`
- Party bots (playerbots) follow their party into the house.

## Steward workflow

- Talk to a Housing Steward in any major city to:
  - Enter your own house
  - Visit another player's house by character name
  - Upgrade stage, toggle privacy, change style
  - Manage guest access (invite/remove by name)
  - Open furniture tools
- While inside your house, use the steward there to:
  - Open `Krook's Cranny` furniture vendor window
  - Show catalog
  - Unlock item by catalog ID
  - Right click purchased furniture item to place with `Flare` ground targeting
  - (Optional/legacy) select unlocked item and cast `Flare` manually
  - Move/remove placement by placement ID
  - List placed furniture

## Install

1. Put the module in your AzerothCore modules folder:
   - `/data/azerothcore/modules/mod-playerhousing`
2. Build with the built-in compiler workflow:
   - `cd /data/azerothcore`
   - `./acore.sh compiler build`
3. Apply SQL:
   - World: `sql/db_world/base/mod_playerhousing_world.sql`
   - World hotfix for existing installs: `sql/db_world/base/mod_playerhousing_world_hotfix.sql`
   - Characters: `sql/db_characters/base/mod_playerhousing_characters.sql`
   - Characters hotfix for existing installs: `sql/db_characters/base/mod_playerhousing_characters_hotfix.sql`
4. Copy config and adjust if needed:
   - `conf/mod_playerhousing.conf.dist` -> your server config directory.
5. Restart `worldserver`.

If you use `acore.sh` db assembly, `include.sh` + `conf/conf.sh.dist` already register the SQL directories.

## Economy and stages

`mod_playerhousing_stage` in world SQL ships with:

- Stage 0: free starter
- Stage 1: 50g
- Stage 2: 150g
- Stage 3: 450g
- Stage 4: 1350g
- Stage 5: 4050g
- Stage 6: 12150g

Each paid upgrade is exactly x3 the previous one.
Placement/bounds radius starts at 50 yards and expands by stage.

Furniture purchase economy (v1) is standard gold-buy vendor pricing on item templates.

## Where houses are

All houses are the guild house on GM Island (Kalimdor, map 1, entry room at
16224.5, 16283.5, 13.18), set in `mod_playerhousing_style`.

- Every owner gets a private copy through phasing. A house phase has bit 31 set and
  bit 0 clear, and the module turns off the core's "any shared bit" phase matching
  for it (`GLOBALHOOK_ON_BEFORE_WORLDOBJECT_SET_PHASEMASK`), so each phase value is its
  own ID. That gives one phase per owner instead of WotLK's usual 32.
- The owner and their guests share the owner's phase. Pets and summons follow the player.
- The house (steward, style pieces, furniture) is spawned when the first person arrives
  and removed when the last one leaves.
- Nobody reaches the island without going through a steward; anyone else found there
  (other than GM accounts) is sent back to where they came from.
- Logging out inside a house brings you back to where you entered from on the next
  login. A dropped connection that reconnects puts you back in the house.
- Housing furniture has server-side collision turned off: collision still compares
  phases bit by bit, so houses would otherwise block each other's placement checks.
- `tools/gm-island-cleared` can remove the guild house instead (client patch, server
  collision/pathing data and `sql/layouts/gm_island_cleared.sql`), leaving a campsite on
  open ground. The guild house is the default.
- Older installs used Pit of Saron instances. The world hotfix SQL moves styles to
  GM Island; furniture placed on the old maps stays in the database but is not shown.

## Steward appearance

- `PlayerHousing.StewardDisplayId` defaults to `25384` (Wolvar orphan).
- Change this in config if you want a different display.

## Placement safety

Placement validates:

- House boundary radius by stage
- Ground height correction (Z snap)
- LOS / collision checks vs terrain and map geometry
- Slope threshold checks
- Overlap distance checks against existing furniture
- Spawn orientation defaults to **face the player** (rotation editing is follow-up)

Relevant config keys:

- `PlayerHousing.Placement.MaxSlopeDegrees`
- `PlayerHousing.Placement.SlopeSampleDistance`
- `PlayerHousing.Placement.DefaultMinDistance`
- `PlayerHousing.Placement.DefaultCollisionRadius`

## Rollback

- World rollback:
  - `sql/db_world/base/mod_playerhousing_world_rollback.sql`
- Characters rollback:
  - `sql/db_characters/base/mod_playerhousing_characters_rollback.sql`
