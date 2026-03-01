# mod-playerhousing

Private, instanced player housing for AzerothCore (WotLK), with owner-only editing, visiting permissions, style selection, and upgrade progression.

## V1 feature set

- Starter house is free and defaults to **private**.
- 4 selectable styles: `human`, `gnome`, `tauren`, `undead`.
- 6 paid upgrade tiers (stage 1 to stage 6), each at **x3 cost growth**.
- Stage 0 starts as a cozy campsite template (tent + campfire + bedroll + crate + lantern).
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
- Styles use their own instance maps via `mod_playerhousing_style.map_id`. `PlayerHousing.MapId` is fallback only.

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

## Map selection (testing defaults)

Styles are wired to these instance maps in `mod_playerhousing_style`:

- Human: `658` (Pit of Saron, snowy campsite start area)
- Gnome: `658` (Pit of Saron, snowy campsite start area)
- Tauren: `658` (Pit of Saron, snowy campsite start area)
- Undead: `658` (Pit of Saron, snowy campsite start area)

Swap `map_id`/spawn coords per style to change the housing zone.

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
