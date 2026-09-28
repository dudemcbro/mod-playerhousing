# Player housing: UX plan

How to make housing easy to learn and easy to use, modeled on Lord of the Rings Online
and Final Fantasy XIV (without neighborhoods). The hard rules come from you:

- **Every placement can be undone, and undoing gives the item back** so it can be placed
  somewhere else.
- **The island starts empty.** GM Island's guild hall is removed completely, and
  furniture and buildings can go anywhere on the owner's own copy of the island.
- **No house levels.** What a player can put in their house unlocks as they move through
  the world: exploring, dungeons, raids, reputation, professions and so on.
- **Homes are built from pre-made buildings.** Everyone starts with wreckage (a fallen
  cart, a shredded tent) and works up to something large and grand by level 80. A
  building that belongs to a faction needs Exalted with that faction. Picking a building
  back up always asks first.
- **During testing everything is free.**

Everything below works with the stock 3.3.5a client (menus, bag items, the targeting
circle, the achievement window). A client addon is an optional extra at the very end,
never a requirement.

## 1. What players deal with today

From reading the module and the database, in the order a new player hits them:

| # | Problem | Where it comes from |
| --- | --- | --- |
| 1 | **No way in.** No Housing Steward is spawned anywhere, so the "talk to a steward in a city" instructions lead nowhere. The only door is typing `.krook add`, which also lets any player spawn permanent stewards anywhere. | world SQL deletes steward spawns and adds none; `.krook add` is `SEC_PLAYER` |
| 2 | **Nobody is told they own a house.** The starter house is created silently at login. | `EnsureStarterHouse(player, false, ...)` |
| 3 | **Two furniture systems with different rules.** Catalog pieces are "unlocked" for gold by typing an ID, then placed by casting Flare from the spellbook and can be placed any number of times. Kits are bought from a vendor, placed by right-clicking, and used up. | `mod_playerhousing_catalog` (21 entries) vs `mod_playerhousing_furniture_item` (7 kits) |
| 4 | **Removing furniture destroys it.** Placing a kit uses it up, and "Remove" deletes the placement, so the gold is gone. Nothing can be undone. | `PlaceFurnitureResolved` destroys the item, `RemoveFurniture` only deletes the row |
| 5 | **Editing means typing numbers.** Move and remove ask for a placement ID, which you find in a chat dump like `#3: Cozy Chair (x=16231.2, ...)`. "Move" only moves a piece to where you stand. There is no rotation. | furniture menu, `ListFurniture` |
| 6 | **Furniture can't be clicked.** Every furniture object is a type the client won't let you click (25 generic decorations and a campfire spell focus), so you can't click a piece to edit it or even sit on a chair. | `gameobject_template.type` 5 or 8 for every entry used |
| 7 | **Three kits place the wrong thing.** Hanging Lantern Kit places an orc sleeping mat, Supply Crate Kit places a lantern, Bedroll Kit places a crate. | `spawn_entry` 193684, 179977, 181302 |
| 8 | **Menus show everything everywhere.** The main menu has 10 entries in every context ("Enter my house" while inside, "Leave current house" while outside). The furniture menu has 10 more, including "(advanced)", "Cancel pending targeted placement" and three by-ID prompts. Results scroll by in chat. | `BuildMainMenu`, `BuildFurnitureMenu` |
| 9 | **People are managed by typing names.** Visiting, inviting and removing guests all need exact character names, and there is no list of who is invited. | guest menu, visit prompt |
| 10 | **Style rules hide furniture.** Some pieces only work with one style ("not available for your current style"), while the styles themselves barely differ (one or two props). | `style_mask` on catalog and kits |
| 11 | **Placing casts a real hunter spell.** Flare fires a flare, reveals stealthed enemies, triggers the global cooldown, and a chosen piece expires after 5 minutes. | `PLACEMENT_TARGET_SPELL_ID = 1543` |
| 12 | **Errors don't say what to do.** "Too close to another furniture object" doesn't say which one or how far. | `IsPlacementPointValid` |
| 13 | **Progress is bought, not earned.** Six paid house levels (50g up to 12,150g) gate furniture slots, placement distance and some catalog pieces. Nothing a player does in the world matters to their house. | `mod_playerhousing_stage`, `min_stage`, `required_stage` |
| 14 | **Long lists can crash the server.** The client allows 32 menu entries, and the core asserts on more. The "select furniture" list adds one entry per unlocked item with no paging. | `GOSSIP_MAX_MENU_ITEMS = 32` |
| 15 | **Placement rules block normal decorating.** Nine checks run on every placement. Pieces must be yards apart (a chair can't go within 4 yd of a table), the spot must be walkable, slopes over 35° are refused, and every piece is dropped onto the floor below, so nothing can go on a table, a wall or up high. Furniture must stay within 50 yd of the entry room (80 yd at the top house level), which doesn't reach the far corner of the house until level 3 and covers 16% to 35% of the island. | `IsPlacementPointValid`, `place_radius` |

## 2. Design principles

1. **One way to do each thing.** One kind of furniture (bag items), one door (the House
   Key), one list of everything you can own (the Collection).
2. **Nothing is permanent by accident.** Undo, redo, and pick up that always returns the
   item.
3. **Point and click, never type.** Lists to pick from instead of IDs and names. Typing a
   name stays only as a last resort for finding a player.
4. **Show only what applies here.** Menus change with where you are and what you're doing.
5. **Say what happened and what to do next,** in one line, with numbers ("12/200
   furnishings").
6. **Your house tells your story.** Every piece beyond the basics comes from something
   you did in the world, and you can always see how to earn the rest.
7. **A new character places their first chair within two minutes** of logging in.

## 3. How housing will work (what players learn)

Five ideas, borrowed from the two reference games:

### House Key (LOTRO's "travel to house" skill, FFXIV's estate teleport)

Every character gets a **House Key** in their bags on first login (soulbound, replaced for
free if lost). Right-click it anywhere to open the **Home menu**: go home, visit someone,
leave, decorate, your Collection, settings. `.house` opens the same menu. Stewards stay as
friendly signposts in the capital cities and inside each house (greeting and help), but
no longer stand between the player and anything.

### Furnishings are items (FFXIV)

Every decoration is a bag item named after exactly what it places ("Furnishing: Westfall
Chair"). You get copies of anything you've unlocked from your **Collection** (section 4).
Picking a piece up puts it back in your bags. The old "unlock by ID" catalog and the kit
vendor both go away. Pieces already unlocked or placed are converted (section 9).

### Placing: anywhere on your island

Your private copy is the whole of GM Island. Measured from the server's map data, all of
the island's land lies within 227 yd of where the guild hall stood, and each owner's
private copy reaches 250 yd, so every beach, the hilltop and the plateau where the hall
used to be are yours to build on. Nobody else sees any of it unless you let them in.

Right-click a furnishing, click anywhere with the targeting circle, and it appears exactly
where you clicked, facing you: the ground, the floors and stairs of your buildings, and
(to be confirmed in game) tabletops and the tops of other furniture. The fine-tuning controls
below then take a piece anywhere else: onto a wall (a piece placed against a wall faces
you, so it faces out of the wall), up to the ceiling, stacked, or floating.

Only three rules stay:

1. The spot is on your island (inside your private copy).
2. You can see it from where you're standing.
3. You're under the furnishing limit (one number for everyone, section 11).

Pieces may overlap, like FFXIV, where overlapping pieces to build new shapes is a popular
technique. No spacing, slope, "walkable" or snap-to-floor rules, and no distance limit.

### Editing: click the piece (FFXIV layout mode)

The owner toggles **Decorate mode** from the Home menu. While it's on, clicking any
placed piece opens its menu:

- Pick up (back to your bags)
- Rotate 45° left or right, or 15° for fine turns
- Face toward me
- Nudge forward, back, left or right (0.25 yd), or up and down (0.1 yd), with no height
  limit
- Move to where I'm standing
- Undo last change

Outside decorate mode, furniture behaves like the real thing: chairs and thrones can be
sat on, a mailbox takes mail, an anvil and forge work for blacksmithing. Nothing can be
moved by accident. Both reference games separate decorating from normal play this way.

### Undo and redo

Every change goes onto an undo list: place, pick up, move, rotate, nudge, and "pack up
everything". **Undo** reverses the last change exactly:

| You did | Undo does |
| --- | --- |
| Placed a piece | picks it up, and the item goes back in your bags |
| Picked a piece up | puts it back exactly where it was, using the item from your bags |
| Moved, rotated or nudged | returns it to the previous spot and angle |
| Packed up everything | puts every piece back and takes the items back |

**Redo** re-applies whatever was undone. The list holds the last 30 changes and lasts
until you leave the house. **Pick up** works on any piece at any time, so every placement
ever made can be reversed, even after the undo list is gone.

Where the item goes: into your bags. If your bags are full, it goes to **House Storage**
(FFXIV's storeroom, LOTRO's housing chest). The Home menu shows "Storage (3)" whenever
something is waiting, with "Take all" and per-item buttons. Items are never destroyed.

## 4. Unlocks from progression

House levels go away. Instead, furnishings and buildings unlock from what players do
in the world, the way LOTRO hands out trophies and decorations for quests, instances and
festivals, and FFXIV for dungeons, achievements and reputation. This section is the framework; the
full list of which piece comes from which activity is its own to-do (section 10,
"Content list").

### What players experience

- **Unlocks happen by themselves, at the moment they're earned.** Kill Onyxia and a
  message appears in the middle of the screen: "Housing unlock: The Severed Head of
  Onyxia." A chat line says where it went: "Added to your Collection. House Key,
  Collection, to get one." Nothing to turn in, nobody to visit.
- **The Collection shows everything, earned or not.** In the Home menu, by category:
  Starter, Buildings, Exploration, Dungeons, Raids, Reputation, Professions, Holidays.
  Each category
  shows a count ("Raids 3/24"). Unlocked pieces give you a copy when clicked; locked ones
  show exactly how to earn them ("Defeat Onyxia in Onyxia's Lair"). This is the
  "what can I work toward" screen both reference games have.
- **Veterans get credit for the past.** On first login after this ships, everything a
  character has already done counts, and one message sums it up: "Your past adventures
  unlocked 37 furnishings."
- **Unlocked means unlimited copies.** Take as many as you like from the Collection
  (subject to the furnishing limit when placing). Copies are items, so undo and pick up
  work exactly as in section 3.
- **Everyone starts with the basics** (chairs, tables, bedroll, lanterns, crates, rugs),
  so the house is never empty on day one.
- **Buildings unlock the same way:** shelters by level, faction buildings at Exalted with
  their faction (section 5).

### Where unlocks come from

Every one of these is something the server can already detect, both when it happens and,
for veterans, afterwards:

| Source | Example | Detected when it happens | Checked for veterans |
| --- | --- | --- | --- |
| Achievement | "Molten Core" (686), "Explore Elwynn Forest" (776), "Classic Dungeonmaster" (1283) | achievement completed | has achieved |
| Boss kill | Onyxia, Ragnaros, Kel'Thuzad | creature killed (group credit) | the game's kill statistics |
| Reputation | Exalted with Darnassus | reputation rank changed | current rank |
| Exploration | discovering a zone | zone or area changed | explored-areas record |
| Profession | Blacksmithing 300 | skill changed | current skill |
| Quest | a specific quest line's finale | quest completed | quest rewarded |
| Level | reaching level 80 | level changed | current level |
| Holiday | Brewfest, Hallow's End, Winter Veil | holiday achievement or event | has achieved |

**Recommendation: build almost everything on achievements.** WotLK already has an
achievement for every dungeon (628 Deadmines through 646 Stratholme), every raid,
exploring each zone, reputations, professions, holidays and quest counts. That gives
three things for free:

- players already know the achievement window, and it shows their progress toward each
  unlock (which subzones are left to explore, which bosses are left)
- group credit, retries and difficulty rules are handled by the game
- crediting veterans is a simple "has this character earned it" check

The other sources fill gaps where no achievement fits (a single boss, a specific quest).

### What the rewards could look like

Real models from the game data, as a taste of the content pass:

| Earned by | Unlocks |
| --- | --- |
| Onyxia's Lair (Level 60) (684) | The Severed Head of Onyxia, the trophy hung on the capital city gates |
| Blackwing Lair (685) | The Severed Head of Nefarian |
| Zul'Gurub (688) | Idol of Hakkar |
| Naxxramas (Kel'Thuzad) | Kel'Thuzad's throne, which can be sat on |
| Ulduar | Thorim's Throne |
| The Frozen Throne (4530) | Frostmourne |
| Exploring a zone | pieces from that zone (Elwynn street lamps, Mulgore totems, Tirisfal banners) |
| Exalted with a faction | that faction's banners; displayed tabards for Twenty-Five Tabards (1021) |
| Blacksmithing, Alchemy | a working anvil and forge, a working alchemy lab |
| A capstone such as reaching level 80 | a working mailbox |
| Brewfest, Hallow's End, Winter Veil | beer tent, the Headless Horseman's pumpkin table, a Winter Veil tree |
| Bosses with no trophy object | a small figurine made from the boss's own model |

Working pieces (mailbox, anvil, forge, alchemy lab) are the strongest pull: they make
the house useful, not just pretty.

### Testing it

Two switches, both on for the test server:

- `PlayerHousing.FreeMode = 1`: everything costs nothing (section 8)
- `PlayerHousing.UnlockAll = 1`: the whole Collection is unlocked, so decor can be tested
  without running raids

With `UnlockAll = 0`, GM commands test the flow itself: `.house unlock <piece|all>`,
`.house relock <piece|all>`, and `.house unlocks <character>` to see what someone has and
why.

## 5. Buildings: from wreckage to a grand estate

Players make their home out of pre-made buildings: whole models from the game, with
walls, roofs and interiors to walk into and furnish. Everyone starts with wreckage and
works up to something large and grand by level 80.

### How buildings work

- A building is an item in your bags, like furniture ("Building: Westfall Farmhouse").
  Right-click it, click a spot with the targeting circle, and it stands there facing you.
  Rotate, nudge, raise and undo work exactly as they do for furniture.
- Buildings go anywhere on your island and may overlap each other and the ground, so a
  house can sit half into a hillside, or a tent can stand inside a barn.
- **Picking a building up always asks first,** using the game's own confirmation popup:

```
Westfall Farmhouse
  Pick up the building only
      "Return the Westfall Farmhouse to your bags? The 12 pieces inside stay where
       they are."                                              [Accept] [Cancel]
  Pick up the building and the 12 pieces inside it
      "Return the Westfall Farmhouse and the 12 pieces inside it to your bags?"
                                                               [Accept] [Cancel]
```

  "Inside" means within the building's footprint: the model's size from the game data
  where it's recorded (many buildings have it), otherwise measured once during the
  content list. Undo still works afterwards, like any other change.
- Buildings have their own limit (10 to start), separate from furniture, because they're
  big to draw.
- Buildings stay visible from farther away than furniture, so a farmhouse doesn't pop in
  at 90 yd. The server has a large-object view distance for exactly this.
- Your first visit lands you on the plateau where the guild hall used to stand, next to
  your first shelter: a fallen cart and a shredded tent, already standing there and yours
  to move or pick up.

### Two kinds of buildings

- **Shelters belong to no faction and unlock by level.** They're the early ladder
  everyone climbs: wreckage, camp gear and ruins.
- **Every building that belongs to a faction needs Exalted with that faction,** the
  highest reputation tier. The Westfall farmhouse needs Exalted with Stormwind, the
  draenei hut with the Exodar, a gunship with the Alliance Vanguard or the Horde
  Expedition. Level doesn't matter for these; Exalted already means a lot of play.

This makes the real houses what they should be: the rewards of long reputation work.
Faction lines take care of themselves, because nobody can reach Exalted with the other
side's cities, while neutral factions (Booty Bay, the Argent Crusade, the Kirin Tor and so
on) are open to both. The Collection shows how close you are: "Westfall Farmhouse: reach
Exalted with Stormwind (you're Revered, 5,400/21,000)."

The candidates below are real models from the game data; the content list picks the
final set and each building's faction after looking at every one in game.

**Shelters, by level (everyone)**

| Level | Stage | Candidates |
| --- | --- | --- |
| 1 | Castaway wreckage | broken cart, a shredded (ruined) excavation tent, a tuskarr tarp lean-to, a wrecked rowboat |
| 10 | Rough camp | Razorfen lean-to, a small canvas tent, an outhouse |
| 20 | Proper camp | medium and large canvas tents, a covered wagon, water huts |
| 30 | Ruins | the burnt Westfall and Duskwood farmhouses, a broken Outland house, a ruined guard tower |

**Faction buildings, at Exalted**

| Faction | Candidates |
| --- | --- |
| Stormwind | Westfall, Duskwood and Redridge farmhouses (the ruins, restored), barns, stables, Duskwood blacksmith, lumber mill, Redridge chapel, Duskwood two-story house, human guard tower |
| Darnassus | walk-in night elf tents, night elf druid tower |
| Exodar | draenei hut |
| Alliance Vanguard | Northrend human house, tall human tower, the Alliance gunship |
| Orgrimmar | orc tents, orc zeppelin house, abandoned orc great hall and barracks |
| Thunder Bluff | tauren druid tent |
| Undercity | Forsaken tents |
| Horde Expedition | winter tauren smoke hut, the Horde gunship |
| Booty Bay (Steamwheedle Cartel) | goblin tents, the Stormwind gypsy wagon, a pirate ship run aground |
| Keepers of Time | Old Stratholme farm |
| Kirin Tor | an Ulduar tower |
| Valiance Expedition or Warsong Offensive | a Wintergrasp tower |

Ironforge, Gnomeregan, the Darkspear Trolls, Silvermoon and other factions get buildings
too wherever the content list finds suitable models.

The burnt farmhouses in the shelter ladder and the restored ones for Stormwind tell a
small story: fix up the ruin you found once Stormwind trusts you. The client also
contains `PlayerHousing\Human\HumanLevelOneTest.wmo`, apparently Blizzard's own
unfinished housing prototype. If it looks presentable in game, it makes a fun secret
unlock.

### The island: the guild hall is gone

GM Island's guild hall is removed completely, so every island starts as open ground and
everything on it is something the owner built. This is the cleared-island layout that
already exists in `tools/gm-island-cleared`, which becomes the default. Three parts have
to match, or the server and the players' games disagree about where walls are:

| Part | What changes | Status |
| --- | --- | --- |
| Players' game client | A patch file (`patch-H.MPQ`) moves the hall out of sight. Every player puts it in their WoW `Data` folder once. | Built by `make_client_patch.sh` from a 3.3.5a client. Checked against a test file only; **never yet tried with a real client**, so that's the first to-do. |
| Server data | The hall and its 68 built-in props leave the collision data, and the island's pathing is rebuilt (so party bots walk the open ground correctly). | `server_data.sh`; tested, the end-to-end test passes 104/104. |
| Database | The arrival spot moves to the old hall's plateau. | The `cleared` row of `mod_playerhousing_layout`, now the default (`PlayerHousing.Layout`); tested. |

A player without the patch would still see the hall and bump into walls that aren't
there for the server, so the patch goes with the server's other connection instructions
(realmlist and so on).

### House styles retire

The four house styles (Human Cottage, Gnome Workshop, Tauren Lodge, Undead Crypt) only
ever changed a few props. With buildings they no longer mean anything, so they retire: a
home looks like the buildings and furniture its owner picks. The props they added become
ordinary furniture (section 9).

## 6. Menus

Every menu shows a status line first, only the options that apply, and pages any list
longer than 20 entries (below the client's 32-entry limit).

**Home menu, outside your house**

```
Your island: 12/200 furnishings, 2/10 buildings, Private
  Go home
  Visit a house
  Collection (41 unlocked)
  Storage (2)                         (only when something is in storage)
  How housing works
```

**Home menu, anywhere on your own island**

```
Your island: 12/200 furnishings, 2/10 buildings, Private
  Start decorating            /  Done decorating
  Undo: placed Westfall Chair         (only when there is something to undo)
  Redo: ...                           (only after an undo)
  Collection (41 unlocked)
  House settings
  Unstuck: back to the landing spot
  Leave house
```

**Collection**

```
Collection: 41 of 180 unlocked
  Starter (12/12)
  Buildings (3/40)
  Exploration (9/40)
  Dungeons (11/36)
  Raids (3/24)
  Reputation (4/30)
  Professions (2/20)
  Holidays (0/18)
```

**A Collection category**

```
Raids: 3 of 24 unlocked
  The Severed Head of Onyxia          (click: get a copy)
  The Severed Head of Nefarian        (click: get a copy)
  Idol of Hakkar                      (click: get a copy)
  Kel'Thuzad's Throne: defeat Kel'Thuzad in Naxxramas
  Frostmourne: complete The Frozen Throne
  Next page
```

**Piece menu (click a piece while decorating)**

```
Westfall Chair  (12/200 furnishings)
  Pick up (back to your bags)
  Rotate left 45°       Rotate right 45°
  Rotate left 15°       Rotate right 15°
  Face toward me
  Nudge...              (forward, back, left, right, up, down)
  Move to where I'm standing
  Undo last change
```

Buildings get the same menu, except that picking one up asks first (section 5).

**Visit a house**

```
  Party members' houses (2)
  Guild members' houses (5)
  Friends' houses (1)
  Houses you're invited to (3)
  Public houses
  Find by character name...           (typing, last resort)
```

Each list shows only houses you're allowed into, so every entry works when clicked.

**House settings**

```
  Privacy: Private                    (click to cycle: Private, Friends & guild, Public)
  Guests (3)                          (list: click a name to remove, with confirm)
      Invite my target
      Invite my party
      Invite by name...
  Greeting for visitors               (FFXIV's estate greeting, optional)
```

Anything that costs gold uses the client's built-in confirmation popup, which shows the
amount before you accept. In test mode there is no price to show.

## 7. The rest of the experience

### Getting started (target: first chair within two minutes)

1. On first login: the House Key and three starter furnishings (chair, table, lantern)
   arrive in your bags. One chat line: "You have a house! Right-click your House Key to
   go there." Once buildings arrive (Phase 5), the first visit lands you on the old
   hall's plateau next to your first shelter: a fallen cart and a shredded tent.
2. First time home: Krook greets you with three steps (place something, click it to
   change it, undo if you don't like it), then gets out of the way.
3. One-time tips at the moment they matter: after your first placement ("Click it while
   decorating to turn it or pick it up"), after your first unlock ("Your Collection
   grows as you explore and fight"), the first time an item goes to storage. Each tip
   shows once per character.
4. "How housing works" is five short lines, always in the Home menu.

### Visiting and guests (without neighborhoods)

- Privacy presets like both reference games: **Private** (you and your guest list),
  **Friends & guild**, **Public**. Your guest list always gets in.
- When invited, the guest gets a message: "Krookowner invited you. House Key, then Visit."
- The owner sees "Krookguest arrived." Visitors see the owner's greeting.
- Guests can sit, use the mailbox and look around, never edit. Letting a guest help
  decorate (co-decorator) can come later if you want it.

### Styles

House styles retire (section 5). A home looks like the buildings and furniture its owner
picks, and nothing is ever hidden because of a style.

### Placement feel

- The targeting circle stays (it's the only native 3D pointer), but the Flare side
  effects go away. The module reads the clicked spot while the spell is being checked,
  then cancels the cast silently: no flare, no stealth reveal, no global cooldown,
  nothing expires. Hunters casting Flare outside a house are unaffected.
- Errors name the problem and the fix: "You can't see that spot from here. Walk closer."
  "Your house is full (200/200). Pick something up first."
- Every success line ends with the count: "Placed Westfall Chair (13/200)."
- Safety nets instead of rules: the server ignores furniture when players move, but the
  game client still bumps into furniture models, so a player can box themselves in.
  "Unstuck" in the Home menu puts them back at the landing spot, and undo and pick up fix
  the layout.
- Like anything in the open world, pieces more than about 90 yd away (Kalimdor's
  default view distance) appear as you walk closer.
- Swimming out past the edge of the private copy would drop a player back into the
  normal world. Instead, they're brought back to the beach with "Use your House Key to
  leave the island."

### Commands for power users and macros

`.house` opens the Home menu. `.house undo`, `.house redo`, `.house pickup` (nearest piece),
`.house rotate 45`, `.house nudge forward` and `.house home` work well as action-bar macros.
`.krook` stays as an alias. Spawning a steward (`.krook add`) becomes GM-only.

## 8. Free during testing

In `mod_playerhousing.conf`:

```
PlayerHousing.FreeMode = 1
PlayerHousing.UnlockAll = 1
```

With them on:

- copies from the Collection and anything else with a price cost nothing
- the House Key has no cast time or cooldown
- the whole Collection is unlocked (`UnlockAll`), and the Collection gets a "Give me one
  of everything" entry

The test server scripts turn FreeMode on. UnlockAll stays off there so the end-to-end
test can check the unlocks themselves; to try every piece, start the development
container with `AC_PLAYER_HOUSING_UNLOCK_ALL=1`, set it in the config, or use the GM
command `.house unlock all <character>`. Live servers leave both off.

## 9. Keeping existing houses working

- House levels are removed. The level column stays in the database, unused. Gold spent
  on upgrades is refunded by in-game mail.
- Unlocked catalog entries become the matching furnishing items, one of each, sent to
  House Storage the first time the owner opens it. They're also marked unlocked in the
  Collection, so nobody loses something they paid for.
- Existing placements stay where they are. Picking one up returns the matching item, so
  old placements are undoable too.
- The kit item IDs stay the same (so items already in bags keep working), but get the
  right models and names.
- House styles retire. The pieces each style added (the moving-in lantern, bedroll and
  crate, and each style's props) become ordinary placed furniture the owner can move or
  pick up.
- Houses and guest lists are unchanged.

## 10. To do

Tick items off here as they land. Each phase ships on its own, keeps the end-to-end test
green, and adds to it.

**Where it stands:** all six phases are built and pass the end-to-end test (104 checks, on
the cleared island, in the development container). What hasn't met a real 3.3.5a client
yet: the cleared island patch, how pieces look (on tabletops, building models and their
walls), the targeting circle, and the client addon's window. Those need your client, so
they're the next session together (see "Try it in game" below).

- [x] UX plan (this document)
- [ ] Cleared island: try the client patch with a real 3.3.5a client (needs your client)
- [x] Content list: which piece and building comes from which activity (Phase 3 needs it)
- [x] Phase 1: the must-haves
- [x] Phase 2: click to edit
- [x] Phase 3: progression unlocks
- [x] Phase 4: people
- [x] Phase 5: buildings
- [x] Phase 6: polish and optional extras

### Cleared island: try the client patch

The only part of the cleared island that has never met a real WoW client. Needs your
3.3.5a client, so it's a short session together.

- [ ] build `patch-H.MPQ` from your client with `make_client_patch.sh`
- [ ] run a server with the cleared layout and log in: the hall is gone, nothing
  invisible blocks you where it stood, and the island's own trees and rocks are still
  there
- [ ] if anything is off, fix the patch tools (the cleared island is already the default)

### Try it in game

With your client, once the patch works:

- [ ] place a few pieces from the bags; the targeting circle feels right, pieces land
  where clicked and face you
- [ ] a lantern on a table (the blue rune while decorating) sits on the tabletop
- [ ] buildings: the shelters and a faction building look right, can be walked into, and
  are seen from a distance
- [ ] building outlines that look too big for the building (their models may include
  surrounding pieces), which decide what counts as inside: Human Guard Tower (70 by 55
  yd), Ruined Guard Tower (75 by 59), Tauren Druid Tent (117 by 100), Orc Barracks (96 by
  87), Night Elf Druid Tower (71 by 73). Correct any that are wrong with a footprint in
  `tools/content/pieces.py`
- [ ] sit on a chair, use a placed mailbox and an anvil
- [ ] a mannequin: it looks like a person of the chosen race, wears the armor you give
  it and holds the weapons; changing the figure; whether it should stand frozen like a
  statue instead of breathing (a small change if so)
- [ ] the client addon (`client-addon/PlayerHousing`): the window, icons, mouse wheel
  turning, key bindings, and the preview (does each model fit the frame and turn nicely)
- [ ] the targeting circles look right for small, medium and large pieces (clear the
  `WDB` folder first so the client forgets the old circle)
- [ ] look at the doubtful models listed in the content list
- [ ] moving with the targeting circle: the Move button in the addon, the lantern going
  along with its table
- [ ] tilting: "Tilt forward" tips the front of a chair down (if it tips back, the sign of
  the rotation needs flipping in `SpawnPlacement`), and tilted pieces look right from
  every side; bigger and smaller pieces, and a lantern staying on a bigger table
- [ ] the grid: a row of fence posts placed with the grid on and "Another" lines up
- [ ] figurines: each boss looks right at table size (frozen, standing on the ground or a
  table, not sunk into it or floating); the addon preview once the creature has been
  seen; very large models (Onyxia, C'Thun, Yogg-Saron)
- [ ] the Bank Chest opens the bank window; the Music Box tunes play and replay

### Content list

Planning, not code. Can be done before or alongside Phase 1. The result is
`docs/UNLOCKS.md`, which Phase 3 turns into database rows.

- [x] One row per unlock: the piece or building, its model from the game data, its Collection
  category, what earns it (achievement ID first, other sources only where no achievement
  fits), and the hint shown while it's locked
- [x] Starter set everyone gets on day one
- [x] Exploration: pieces themed on each zone or region (every zone with an Explore
  achievement)
- [x] Dungeons: a piece for every Classic, Burning Crusade and Wrath dungeon (the Dire
  Maul wings share King of Dire Maul, the only Dire Maul achievement)
- [x] Raids: a trophy for every raid (and the bosses worth their own), figurines where
  no trophy model exists
- [x] Reputation: faction pieces at Honored, Revered and Exalted (the ten capitals, the
  main neutral factions, the Sha'tar, Aldor, Scryers, Sons of Hodir and Booty Bay)
- [x] Professions: working stations at skill milestones (anvil, forge, alchemy lab)
- [x] Holidays: Brewfest, Hallow's End, Winter Veil and the rest (Lunar Festival, Love is in
  the Air, Noblegarden, Children's Week, Midsummer, Pilgrim's Bounty, Day of the Dead,
  Pirates' Day)
- [x] Capstones: working mailbox and other useful pieces for big milestones (a barber
  chair for The Loremaster, a guild vault for looting 10,000 gold)
- [x] Buildings: the shelters by level, and each faction's buildings at Exalted, with
  every building's faction decided
- [x] Check every model exists in the game data
- [ ] Look at the doubtful ones in game (needs your client):
  - no size in the game data, so the size is a guess: Caverns of Time Hourglass, Saronite
    Bar, Basket of Corn, Cornucopia, Orange Marigolds, Candy Skulls
  - large: Soul Crucible Brazier, Obsidian Dragon Egg, Ribbon Pole, Lunar Festival Lantern
  - odd shapes: Naj'entus Spine, Karazhan Opera Moon, Stormwind Griffon Banner (may be a
    wall banner), Arcatraz Containment Jar
  - models copied from unusual objects: Kodo Graveyard Bones, Uldaman Titan Urn, Silithus
    Wind Stone, Cache of Eregos, Argent Lance Rack, Noblegarden Egg, Cornucopia
  - models no object in the game uses (never seen): Blood Furnace War Banner, Underbog
    Giant Mushroom, Coilfang Orb Lamp, Botanica Exotic Plant, Blackrock Tool Rack,
    Karazhan Supply Crate, Swamp of Sorrows Reed Plant, Dragonmaw Dragon Egg, Winterfall
    Furbolg Totem, Spellweaver's Scrying Orb, Stormwind Griffon Banner, Ornate Dwarven
    Wardrobe, Lordaeron Brazier, Silvermoon Lantern, Chromatic Dragon Egg, Glowing Zangar
    Mushroom, Cenarion Blue Lantern, Sha'tari Banner, Brunnhildar Shield, Leather Kickball
  - working pieces: the barber chair opens the barber window, the guild vault opens the
    guild bank

### Phase 1: the must-haves

- [x] `PlayerHousing.FreeMode`
- [x] the cleared island is the default: server data, database layout and landing spot;
  the test server image can run it; install steps for the client patch; anything placed
  inside the old hall goes back to its owner's House Storage with a message
- [x] house levels removed; one furnishing limit for everyone (gold refunds dropped: the
  existing data is test data only)
- [x] furnishings are items only; catalog converted; kit names and models fixed; for now
  every furnishing is available (progression arrives in Phase 3)
- [x] pick up returns the item; House Storage for full bags
- [x] undo and redo list; "Undo: ..." at the top of the in-house menu; `.house undo/redo`
- [x] House Key item and `.house`; menus that change with where you are; paging for all
  lists
- [x] steward spawns in capital cities; `.krook add` GM-only
- [x] placement anywhere on the island with only the three rules; the exact clicked spot
  is kept; Unstuck; bringing swimmers back to the beach
- [x] silent targeting (no Flare side effects)
- [x] clearer messages with counts

Test additions: place, then undo, and the item is back in bags; undo the undo (redo);
pick up to full bags goes to storage and "Take all" works; with FreeMode on, every cost is
zero; old catalog placements pick up into items; a chair next to a table, a piece
outdoors on the far side of the island, and a piece on the hilltop all place; a spot
past the edge of the private copy is refused; the test runs on the cleared island.

### Phase 2: click to edit

- [x] clickable copies of every furniture object (same model); outside decorate mode,
  working furniture keeps working (chairs sit, mailbox and crafting stations work)
- [x] Decorate mode, piece menu (rotate, face, nudge, raise and lower, move here)

Test additions: the test client clicks a piece and drives the piece menu; rotate, nudge
and height are checked in the database; a guest clicking a chair sits and can't edit.

### Phase 3: progression unlocks

- [x] the unlock engine: sources (achievements first, then kills, reputation,
  exploration, professions, quests, level, holidays), live unlock messages, veteran
  credit at login
- [x] the Collection menu, with categories, counts, hints and "get a copy"
- [x] `PlayerHousing.UnlockAll` and the GM unlock commands
- [x] the content list loaded into the database, including working furniture and boss
  figurines

Test additions: with `UnlockAll = 0`, a new character has only the starter set; a GM
grants an achievement and the matching piece unlocks with a message; a character that
already had the achievement gets it at login; locked entries show their hint.

### Phase 4: people

- [x] guest list you can click, invite target or party
- [x] visit lists (party, guild, friends, invited, public)
- [x] privacy presets (Private, Friends & guild, Public)
- [x] arrival and invite messages

Test additions: the guest finds the owner's house in "Houses you're invited to" and gets
in with one click; "Friends & guild" lets a guild member in and keeps a stranger out.

### Phase 5: buildings

- [x] buildings as items, placed with the targeting circle, with rotate, nudge and undo
  like furniture, and their own limit
- [x] picking a building up asks first: the building only, or the building and the pieces
  inside it
- [x] buildings stay visible from farther away (large-object view distance)
- [x] the starter wreckage (fallen cart, shredded tent) placed on the old hall's plateau
  on first visit
- [x] faction buildings need Exalted with their faction; the Collection shows your
  current standing toward each
- [x] house styles retired (their props were not carried over: test data only)
- [x] the buildings from the content list loaded into the database

Test additions: place a building and pick it up: the prompt appears, "building only"
returns it to the bags and leaves the pieces inside where they were; undo puts it back; a
level 1 character's Collection shows the wreckage rung unlocked and hints for the rest;
a character at Revered with Stormwind sees the Westfall farmhouse locked with their
standing, and reaching Exalted unlocks it with a message.

### Phase 6: polish and optional extras

- [x] one-time tips and the first-visit greeting
- [x] visitor greeting message, "Pack up everything" (undoable)
- [x] optional LOTRO-style hooks: glowing "snap here" spots on walls and tables that place
  a piece at the right height and angle in one click, for players who don't want to
  fine-tune
- [x] optional client addon: a real furnishing window with icons, drag to place and
  mouse-wheel rotation, talking to the same server commands. It stays optional; the
  native menus remain complete without it.

## 11. Decisions with a recommended default

| Question | Recommendation |
| --- | --- |
| Furnishing limit per house? | One number for everyone, 200 to start, set in the config. Every open house's pieces share the same few map cells on GM Island, so the limit is what keeps the server quick when many houses are open at once. Tune it after testing. |
| Unlocks per character or per account? | Per account, so alts don't have to re-earn everything, while each character keeps its own house. |
| Should copies from the Collection cost gold on live servers? | A small amount, so there's still a gold sink. FreeMode makes them free for testing. |
| One copy of trophies, or unlimited? | Unlimited. It's simpler, and the furnishing limit already caps the total. |
| Can Alliance players earn Horde pieces and the reverse? | Only through content they can do; placing is never restricted by faction. |
| House Key on live: instant, or a short cast and cooldown? | 5 second cast, no cooldown (like a mount, not a hearthstone). |
| Include the "Friends & guild" privacy level? | Yes. It covers sharing with friends without opening the house to everyone. |
| The island's own guild hall? | **Decided:** removed completely (the cleared island). Every player installs the client patch once. |
| Retire the four house styles? | Yes. Buildings replace them. |
| Building limit per island? | 10 to start, separate from furniture, set in the config. |
| What unlocks buildings? | Shelters by level; every faction building at Exalted with its faction (your rule). |
| If reputation later drops below Exalted? | The building stays unlocked, like an achievement, so nothing on the island vanishes. |
| Faction buildings per character or per account? | Per character, because reputation is per character. Other unlocks stay per account. |
| Add LOTRO-style hooks? | Later and optional. They help beginners, but aren't needed now that pieces can go anywhere. |
| Undo list across logouts? | No. It lasts until you leave the house; pick up covers anything older. |

## 12. Displaying gear

Asked for after the six phases: weapons, armor and other equipment on stands, walls and
plaques.

- [x] **Mannequins** (stands): a figure that wears real gear from the bags, every
  equippable item that shows on a body. The item leaves the bags while on display and
  comes back the same item (take off, pick up, undo; mailed when the bags are full). No
  client patch needed. Details in the README.
- [ ] **Wall plaques and weapon racks** for weapons and shields: later, as an addition to
  the client patch. Placed objects can only use models the game lists for objects, and
  only 24 of about 31,000 weapons and armor pieces have one; the patch would add an object
  version of each weapon and shield model (built from your client, like the island
  patch), and pieces would tilt flat against a wall. Helms and shoulders are made for a
  body, so they stay on mannequins.

## 13. Seeing a piece before it's placed

Asked for: a see-through copy that follows the mouse, as in retail WoW's housing. The
3.3.5 client can't do that: the server only learns the spot when you click, addons can't
read the 3D position under the cursor, and placed objects have no transparency. Built
instead:

- [x] **A circle the size of the piece**, 1 to 20 yards, instead of Flare's 10 for all.
- [x] **The placed piece is the preview**: after placing a building (or anything, per
  player setting), its menu opens with Keep it here, Take it back, turn and nudge.
- [x] **Addon preview**: hovering a piece shows its model turning and its size. The 32
  buildings made of world models (which a window can't draw) show a floor plan to scale,
  with a marker for you.
- [ ] **Pictures of those 32 buildings** for the preview, taken with your client in the
  in-game session.
- [ ] **Possible later**: a see-through hologram where you click, before placing. Units
  can be drawn see-through (stealthed party members, hologram NPCs), but only 9 of the 315
  piece models exist as unit models; the client patch would add the rest. Try one piece
  with your client first.

## 14. A catalog of (almost) every object

The goal: players earn and place most of the game's objects and buildings, or server
admins switch them all on.

**How big "everything" is.** The world database has 21,609 object templates, but they
share only 3,478 distinct models (2,987 small models and 88 building-sized world models; a
single inn chair model is behind hundreds of templates). A piece is one per model, so
"everything" is at most about 3,500 pieces.

**Database cost.** Each piece is an item, one or two object templates and a piece row:
about 15,000 small rows for everything, a few megabytes next to a 437 MB world database,
and fixed however many players there are. What grows with players stays capped by the
island limits (one row per placed piece, at most 210 per island) and the Collection (one
row per unlocked piece per account, at most about 3,500). Placed pieces on islands exist
only in memory while someone is there. So the database isn't the concern; curation is.

**Why still one template per piece, not a flag on the originals.** The original objects
are used all over the world (the Stormwind mailbox, every inn chair), many are chests,
doors, traps or quest objects that must not work on an island, and players see each
template's name when hovering it. Copies keep the originals untouched and each piece named.
Items can't be flagged either: in 3.3.5 an item's name and icon come from its template.

**The plan.**

- [ ] The content builder generates the catalog from the game data: every distinct model
  that isn't a door, trap, transport or invisible marker, named after its most common
  template, with its size, and grouped by where it's spawned (zone, dungeon, raid).
- [ ] Unlocks come from where the model appears: a dungeon's models unlock with that
  dungeon's achievement, a zone's with its exploration achievement, and so on. The
  hand-picked pieces keep their own rules.
- [ ] `PlayerHousing.Catalog = curated | everything` for admins: curated is today's list;
  everything adds the generated catalog. `UnlockAll` still unlocks it all for testing.
- [ ] Finding things among thousands: search by name in the Collection (House Key and the
  addon), and the addon's preview.
- [ ] A few big world model buildings (keeps, inns, towers) as buildings, each checked in
  game for doors and walls.
