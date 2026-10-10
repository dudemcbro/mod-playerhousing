# Multi Realm Player Housing for AzerothCore

**Status:** proposal, not built. Today every account has one island, on GM Island. See also
[ROADMAP.md](ROADMAP.md) (2.0).

## Purpose

Expand `mod-playerhousing` from one private GM Island per account into a collection of small, private housing realms. A realm is a finished, self-contained piece of existing 3.3.5a terrain such as Fray Island, Fenris Isle, or Jaguero Isle. Each account owns one home in one realm at a time, while its furnishings, Collection, guest list, storage, and progression remain the existing shared account housing data.

The module must preserve the current experience: a player uses the House Key, arrives in a private phase of their home, builds anywhere allowed by that realm, and may invite visitors. The expansion adds place and biome choice without creating neighborhoods, duplicating the furnishing system, or exposing players to unfinished world terrain.

## Product decisions

- A realm is a small destination, approximately GM Island scale. It is not a whole leveling zone.
- Every realm is a private copy implemented with the module's existing per-home phase model. Players see their invited visitors, not other households using the same physical location.
- An account begins with one free racial homeland realm. Other realms are account-wide unlocks earned through play.
- Moving to a new realm packs placed items into the existing Collection. It never destroys, sells, or silently relocates furnishings.
- Saved layouts belong to the realm in which they were saved. A layout cannot be placed in a different realm unless it is explicitly adapted later.
- Natural geography is the visible boundary. Server containment is only a safety net beyond that geography.
- Existing GM Island homes remain valid and become the `gm_island` realm. No migration should force players to move.

## Candidate realms for the first release

The first set should prove different boundary styles without requiring new terrain assets. These locations use existing 3.3.5a geography and can be privately phased in place.

| Realm code | Place | Teleport for inspection | Boundary model | Availability |
| --- | --- | --- | --- | --- |
| `gm_island` | Cleared GM Island | `.go xyz 16240 16296 12.92 1` | Shore and open sea | Existing homes; neutral starter option |
| `fray_island` | Fray Island, off Ratchet | `.go xyz -1679.30 -4328.96 2.59 1` | Compact shoreline and open sea | Earned coastal/desert realm |
| `fenris_isle` | Fenris Isle, Silverpine | `.go xyz 736.94 727.85 36.55 0` | Island shoreline and ruined keep | Forsaken homeland realm |
| `jaguero_isle` | Jaguero Isle, Stranglethorn | `.go xyz -14740.70 -432.48 4.01 0` | Jungle shoreline and cliff edge | Earned jungle realm |
| `stonetalon_peak` | Stonetalon Peak | `.go xyz 2506.30 1470.14 262.72 1` | Sheer cliffs and one arrival path | Earned mountain realm |
| `northshire_valley` | Northshire Valley | `.go xyz -9015.92 -79.44 87.12 0` | Mountain bowl and one road | Human homeland realm after a terrain boundary pass |

Do not add a realm merely because it has a good screenshot. Each candidate needs a walk test for usable build area, collision, camera behavior, safe water, ceiling/terrain exploit paths, and whether nearby static quest content breaks the private-home illusion. Fray, Fenris, and Jaguero are the priority prototypes because they already read as complete, small destinations.

## Existing module architecture to retain

The module already solves the hard housing-specific problems:

- `mod_playerhousing_account` selects the account's home character and keeps Collection data account-wide.
- `mod_playerhousing_house` holds household settings and ambience; guests (`mod_playerhousing_acl`) and placements (`mod_playerhousing_placement`) have tables of their own.
- `Session` holds the active private phase and spawned furnishing objects for an occupied home.
- `EnterHouse`, `VisitHouse`, and `LeaveHouse` already preserve privacy, visitor access, return location, and phase restoration.
- The current `mod_playerhousing_layout` table supplies map, landing position, steward position, center, radius, and description for the global `PlayerHousing.Layout` choice.
- The current `IsOnIslandGround` and player update logic prevent placement or movement outside the GM Island circle.

Multi-realm housing changes the scope of the existing layout data. It does not replace collections, pieces, ghosts, placement validation, ambience, guests, layouts, or the client addon.

## Realm data model

Replace the global-layout concept with realm definitions and per-home selection. Keep the existing `mod_playerhousing_layout` table during one compatibility release, then rename or migrate it to `mod_playerhousing_realm`.

```sql
CREATE TABLE mod_playerhousing_realm (
  realm_id            smallint unsigned NOT NULL,
  code                varchar(32) NOT NULL,
  name                varchar(80) NOT NULL,
  map_id              int unsigned NOT NULL,
  landing_x           float NOT NULL,
  landing_y           float NOT NULL,
  landing_z           float NOT NULL,
  landing_o           float NOT NULL,
  steward_offset_x    float NOT NULL DEFAULT 7,
  steward_offset_y    float NOT NULL DEFAULT 2,
  unlock_rule_type    tinyint unsigned NOT NULL DEFAULT 0,
  unlock_param_1      int unsigned NOT NULL DEFAULT 0,
  unlock_param_2      int unsigned NOT NULL DEFAULT 0,
  starter_race_mask   int unsigned NOT NULL DEFAULT 0,
  enabled             tinyint unsigned NOT NULL DEFAULT 1,
  description         varchar(255) NOT NULL DEFAULT '',
  PRIMARY KEY (realm_id),
  UNIQUE KEY uq_mod_playerhousing_realm_code (code)
);

CREATE TABLE mod_playerhousing_realm_boundary (
  realm_id            smallint unsigned NOT NULL,
  band                tinyint unsigned NOT NULL,
  vertex_index        smallint unsigned NOT NULL,
  pos_x               float NOT NULL,
  pos_y               float NOT NULL,
  PRIMARY KEY (realm_id, band, vertex_index)
);
```

Boundary bands are polygons, not a single radius:

| Band | Meaning | Module behavior |
| --- | --- | --- |
| `0 build` | Land and approved shallow-water area | Furnishings may be placed here. |
| `1 shore` | Walkable but outside build area | Players may explore; placement is denied. |
| `2 warning` | Outer water, cliff backstop, or inaccessible slope | Show one warning: “The open sea is dangerous. Turn back.” |
| `3 rescue` | Beyond the believable edge or below safe elevation | Teleport to the realm landing point and clear movement/placement state. |

Add `realm_id SMALLINT UNSIGNED NOT NULL DEFAULT 1` to `mod_playerhousing_house`. `1` is the migrated GM Island realm. Add `realm_id` to `mod_playerhousing_saved_layout`; every existing saved layout receives `1`.

`mod_playerhousing_realm_unlock` records account-wide access to earned realms:

```sql
CREATE TABLE mod_playerhousing_realm_unlock (
  account_id          int unsigned NOT NULL,
  realm_id            smallint unsigned NOT NULL,
  unlocked_at         timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  source              varchar(48) NOT NULL DEFAULT '',
  PRIMARY KEY (account_id, realm_id)
);
```

Starter eligibility is evaluated from the character selecting the first home, but the selected realm unlock is stored for the account. The account may later unlock and move to any other realm.

## C++ changes

### Replace global layout state

`PlayerHousingMgr` currently keeps one `_layoutCode` and one `_layout`. Replace them with:

```cpp
std::unordered_map<uint16, Housing::RealmDefinition> _realms;
std::unordered_map<std::string, uint16> _realmByCode;
```

`Housing::RealmDefinition` extends the current `LayoutDefinition` with `realmId`, display name, enabled/unlock data, and the four boundary polygons. Keep a small compatibility alias while migrating SQL and names.

`Session` gains `uint16 realmId`. A session still remains keyed by home owner, because each account has only one active home. Every map lookup, steward spawn, placement check, arrival point, and containment check resolves the realm from the session/house record rather than from a module-global layout.

### Make housing-area checks session-aware

The current `IsInHousingArea` only checks map ID and one circular radius. That will be incorrect when several realms use map 0 or map 1. Split it into:

```cpp
RealmDefinition const* GetRealmForHome(ObjectGuid::LowType ownerGuid) const;
bool IsInsideBoundary(RealmDefinition const&, BoundaryBand, float x, float y) const;
bool IsInActiveHome(Player const*) const;
bool IsPlacementAllowed(Session const&, Position const&) const;
```

`IsInActiveHome` must first consult the player-to-owner tracking/session state, then verify map and the realm boundary. It must never treat an ordinary player at Northshire, Fray Island, or Silverpine as being in housing merely because they stand inside the same map coordinates.

### Entry, visiting, and phase handling

`EnterHouse` loads the owner's house record, resolves its realm, applies the existing owner phase, and teleports to that realm's landing point or an approved door position. `VisitHouse` follows the same path and therefore always lands in the owner’s selected realm.

The module should continue using the existing exact per-home phase strategy. A realm is terrain; a phase is a household. This preserves the current privacy model even when thousands of accounts select Fray Island.

### Realm changes

Add a `MoveHomeToRealm(Player*, uint16 realmId, std::string& reason)` operation.

1. Confirm the realm is enabled and unlocked for the account.
2. Refuse while the household has visitors or an active decoration ghost.
3. Save a realm-tagged “before moving” layout automatically if an empty saved-layout slot exists; otherwise offer the player a named save first.
4. Pack all placed furnishings and buildings to the existing Collection using the module’s normal return path. Mannequin gear follows the existing safe return-by-bag-or-mail behavior.
5. Write the new `realm_id` transactionally.
6. Clear the household’s active placement rows only after the collection updates succeed.
7. Enter the new realm at its landing point.

This is a move, not a cosmetic skin change. It intentionally begins the new land empty and prevents furniture coordinates from appearing in invalid terrain.

### Boundary and escape handling

Replace the fixed `center/radius` check in the player update with a realm-boundary evaluation at a modest interval, for example every 500 ms per player while in an active home.

```text
inside build/shore  -> normal play
inside warning      -> one rate-limited warning; clear decoration ghost if necessary
inside rescue       -> NearTeleportTo landing point; clear ghost/selection; message player
invalid Z/fall/void -> same rescue action immediately
```

The rescue band is a fault-tolerance feature, not the visible perimeter. The client terrain must already present water, reef, cliff, dense foliage, cave wall, or a steep mountain edge before the rescue band begins.

Do not rely on stock fatigue alone. Water walking, levitation, speed modifications, custom spells, and unusual movement states can bypass it. When a player first enters a warning band, apply a realm-local exhaustion aura or warning visual if desired; the server-side rescue polygon remains authoritative.

While in an active home, reject flight-enabling and water-bypass movement only when they would reach a realm's non-playable space. Do not implement a broad global spell blacklist. The primary defense is terrain plus the outer rescue band; spell restrictions are narrow exploit hardening.

## Player flow

### First home

On first housing entry, the House Key opens “Choose your homeland” before “Go home.” It lists only the free realm or realms associated with the character’s race. The choice is explained as a home location, not a permanent class/faction lock. GM Island remains available as the existing neutral option for migrated accounts and may remain a universally selectable starter option if desired.

### Realm guide

Add a `Homes` page to the current housing UI and `.house home` command group:

- **Current home**: realm name, short description, and “Go home.”
- **Available homes**: unlocked realms; preview, visit empty preview, and move-home actions.
- **Locked homes**: exact unlock requirement and progress, following the existing Collection language.
- **Visit a home**: unchanged; visiting always follows the owner’s realm selection.

The client addon only needs new realm rows and labels. Placement, ghost, mouse-ray, object manipulation, and the Collection protocol stay unchanged.

## Realm unlock design

Realms are style choices, not power rewards. Every realm has the same placement limits, guests, storage access, ambience controls, and available Collection. Unlocks reward travel and accomplishment without giving a larger lot or extra item power.

Suggested first rules:

| Realm | Unlock |
| --- | --- |
| Racial homeland | First free selection for the matching race; account records the choice. |
| GM Island | Existing accounts automatically retain it; new accounts may choose it as a neutral option if configured. |
| Fray Island | Explore the Northern Barrens plus a modest gold or housing-currency cost. |
| Fenris Isle | Forsaken starter selection; other races unlock by exploring Silverpine Forest and reaching Friendly with Undercity. |
| Jaguero Isle | Explore Stranglethorn Vale and complete an appropriate jungle/pirate milestone. |
| Stonetalon Peak | Explore Stonetalon Mountains and complete a mountain-oriented achievement or reputation goal. |

The same `PieceRule` evaluator can support most realm rules. Add a small `RealmRuleMet` wrapper rather than copy progression logic.

## Map and client asset requirements

### Existing terrain first

Fray, Fenris, Jaguero, Stonetalon Peak, and Northshire use terrain already in the 3.3.5a client. A new map patch is not required merely to phase players into those coordinates. The existing PlayerHousing client patch remains necessary for housing icons and placement ghosts.

Each realm still needs a server-side content review:

- suppress or phase out ordinary nearby creature and gameobject spawns in the home phase;
- preserve terrain, water, collision, and skybox that make the place feel real;
- test camera and object placement against any static WMOs;
- regenerate or validate vmaps/mmaps only when terrain or static building assets are changed.

### Terrain modifications

Use client patches only for deliberate art changes: removing Fray’s arena props, adding a dock, changing a WMO, smoothing terrain, or creating a dedicated custom map. Keep such patches per realm and versioned with the module so every player receives the same collision and visual result.

The GM Island `cleared` pipeline is the reference implementation for this packaging: client patch, matching collision/pathing data, and a documented build command. Add a sibling `tools/realms/<realm-code>/` directory only once a realm needs changed assets.

## Configuration

Retire `PlayerHousing.Layout` after migration. Add:

```ini
PlayerHousing.Realms.Enable = 1
PlayerHousing.Realms.Default = "gm_island"
PlayerHousing.Realms.AllowNeutralStarter = 1
PlayerHousing.Realms.MoveDelaySeconds = 10
PlayerHousing.Realms.SaveBeforeMove = 1
PlayerHousing.Realms.WarningCooldownSeconds = 15
PlayerHousing.Realms.RescueMessage = "A passing crew brings you back to shore."
```

`PlayerHousing.MaxFurnishings` and `PlayerHousing.MaxBuildings` remain global initially. Per-realm caps can be added later only if performance tests justify them; they should not be used as a hidden quality tier.

## Migration plan

1. Add realm tables and seed `gm_island` with the current `cleared` layout values.
2. Add `realm_id` columns with default `gm_island` to houses and saved layouts.
3. Backfill all existing homes and saved layouts to `gm_island` in one transaction.
4. Keep reading the legacy global layout only if no realm data exists, and log a migration warning. Remove the fallback in the next breaking release.
5. Preserve existing phase keys, placement rows, guest lists, ambience, storage, and return locations exactly.
6. Ship new realms disabled by default until each one passes collision and containment acceptance tests.

No existing player should lose access to their house, be moved, lose a furnishing, or have a saved layout invalidated during this migration.

## Test plan

### Automated module tests

- A migrated GM Island account enters the same coordinates and sees the same placements.
- Two owners in the same realm receive different phases and never see one another’s placed objects.
- A visitor follows the owner to the correct realm and phase.
- An ordinary player at the source location without a housing session is not considered to be in housing.
- Placement succeeds inside the build polygon and fails in shore, warning, and rescue bands.
- A player moving through each boundary receives at most one warning per cooldown and is rescued from the outer band.
- Realm moves preserve Collection counts, return mannequin gear safely, pack placements, and update the house realm atomically.
- Realm-specific saved layouts cannot be applied to a different realm.
- Logout, forced phase reset, death, teleport, and disconnect restore the correct phase/return state.

### Manual map acceptance for every realm

- Walk the entire build and shore areas at normal speed, mounted speed, swimming, water walking, and levitation.
- Test all approaches to cliffs, water, caves, high terrain, static structures, and map-edge gaps.
- Verify that a player sees geography before the warning/rescue behavior.
- Place the largest allowed building and a dense furnishing layout near every terrain type.
- Test owner, invited guest, friend/guild visitor, public visitor, roommate, GM, and playerbot behavior.
- Verify that no ordinary quest NPC, quest object, mail service, auctioneer, or hostile creature leaks into the private phase unless deliberately retained.

## Delivery order

1. Refactor layouts into realm definitions while shipping only `gm_island`; prove there is no behavior regression.
2. Add per-home `realm_id`, UI listing, unlock rows, migration, and move-home packing while only GM Island is enabled.
3. Add Fray Island as the first non-GM prototype, with polygon containment and no custom terrain patch.
4. Add Fenris Isle and Jaguero Isle after map acceptance passes.
5. Add racial homeland realms and the chosen earned mountain/ruin realms.
6. Only then add custom-map realms or recovered alpha content, which need their own client-data pipeline and stronger compatibility testing.

This order keeps the core refactor independent of art work and lets the project validate the important promise early: different homes feel geographically real, while the existing private, persistent housing system continues to work unchanged.
