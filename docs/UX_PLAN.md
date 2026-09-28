# Player housing: UX plan

How to make housing easy to learn and easy to use, modeled on Lord of the Rings Online
and Final Fantasy XIV (without neighborhoods). The hard rules come from you:

- **Every placement can be undone, and undoing gives the item back** so it can be placed
  somewhere else.
- **Furniture can go anywhere on the owner's own copy of GM Island**, inside or outside
  the house.
- **No house levels.** What a player can put in their house unlocks as they move through
  the world: exploring, dungeons, raids, reputation, professions and so on.
- **Homes are built from pre-made buildings.** Everyone starts with wreckage (a fallen
  cart, a shredded tent) and works up to something large and grand by level 80. Picking
  a building back up always asks first.
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

Your private copy is the whole of GM Island, not just the building. Measured from the
server's map data, all of the island's land lies within 227 yd of the house, and each
owner's private copy reaches 250 yd, so beaches, the hilltop, the roof and every room are
yours to decorate. Nobody else sees any of it unless you let them in.

Right-click a furnishing, click anywhere with the targeting circle, and it appears exactly
where you clicked, facing you: any floor, stairs, the upper storeys, outdoors, and (to be
confirmed in game) tabletops and the tops of other furniture. The fine-tuning controls
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
- **Buildings unlock the same way,** rung by rung from wreckage to a grand estate
  (section 5).

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
- Your first visit lands you on the beach next to your first shelter, a fallen cart and a
  shredded tent, already standing there and yours to move or pick up.

### The ladder

Level is the spine, since it's the one kind of progress every character makes. Some
rungs also ask for a matching deed, for flavor: the Westfall farmhouse could also need
Explore Westfall, a gunship the Icecrown Citadel gunship battle. The candidates below are
real models from the game data; the content list picks the final set after looking at
each one in game.

| Level | Stage | Candidates |
| --- | --- | --- |
| 1 | Castaway wreckage | broken cart, a shredded (ruined) excavation tent, a tuskarr tarp lean-to, a wrecked rowboat |
| 10 | Rough camp | Razorfen lean-to, small human tent, goblin tents, an outhouse |
| 20 | Proper camp | medium and large human tents, orc tent, Forsaken tent, Westfall shed, water huts |
| 30 | Ruins to restore | Stormwind gypsy wagon, the burnt Westfall and Duskwood farmhouses, a broken Outland house |
| 40 | First real house | Westfall, Duskwood and Redridge farmhouses, draenei hut, winter tauren smoke hut, walk-in night elf tents |
| 50 | Homestead | barns, stables, Duskwood blacksmith, lumber mill, Redridge chapel |
| 60 | Manor | Duskwood two-story house, Old Stratholme farm, night elf druid tower, orc zeppelin house |
| 70 | Stronghold | Northrend human house, human guard tower, tall human tower, abandoned orc great hall |
| 80 | Grand estate | the island's own guild hall restored, the Alliance or Horde gunship, an Ulduar tower, a Wintergrasp tower |

The burnt farmhouse at 30 and the restored one at 40 tell a small story: fix up the ruin
you found. The client also contains `PlayerHousing\Human\HumanLevelOneTest.wmo`,
apparently Blizzard's own unfinished housing prototype. If it looks presentable in game,
it makes a fun secret unlock.

### The island's guild hall

GM Island already has a big building on it, which doesn't fit "start with a fallen
cart". Recommended: **seal the hall and make restoring it the level 80 capstone.** It
stays in view the whole time as the goal on the horizon. Until then its doors are
sealed: anyone who gets inside is moved back out with "The old guild hall is sealed.
Restore it at level 80," nothing can be placed in it, and players arrive outside it
instead of in its entry room. This needs no client patch.

The alternative is the cleared-island layout (`tools/gm-island-cleared`), which removes
the hall for a blank island, but then every player has to install a client patch.

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
  Unstuck: back to the entrance
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
   go there." Once buildings arrive (Phase 5), the first visit lands you on the beach
   next to your first shelter: a fallen cart and a shredded tent.
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
  "Unstuck" in the Home menu puts them back at the entrance, and undo and pick up fix
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

The test server config turns both on. Live servers leave them off.

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

- [x] UX plan (this document)
- [ ] Content list: which piece and building comes from which activity (next; Phase 3 needs it)
- [ ] Phase 1: the must-haves
- [ ] Phase 2: click to edit
- [ ] Phase 3: progression unlocks
- [ ] Phase 4: people
- [ ] Phase 5: buildings
- [ ] Phase 6: polish and optional extras

### Content list

Planning, not code. Can be done before or alongside Phase 1. The result is
`docs/UNLOCKS.md`, which Phase 3 turns into database rows.

- [ ] One row per unlock: the piece or building, its model from the game data, its Collection
  category, what earns it (achievement ID first, other sources only where no achievement
  fits), and the hint shown while it's locked
- [ ] Starter set everyone gets on day one
- [ ] Exploration: pieces themed on each zone or region
- [ ] Dungeons: a piece for every Classic, Burning Crusade and Wrath dungeon
- [ ] Raids: a trophy for every raid (and the bosses worth their own), figurines where
  no trophy model exists
- [ ] Reputation: faction pieces at Honored, Revered and Exalted
- [ ] Professions: working stations at skill milestones (anvil, forge, alchemy lab)
- [ ] Holidays: Brewfest, Hallow's End, Winter Veil and the rest
- [ ] Capstones: working mailbox and other useful pieces for big milestones
- [ ] Buildings ladder: every rung from wreckage to grand estate, with the level and any
  deed that unlocks each building
- [ ] Check every model exists in the game data; look at the doubtful ones in game

### Phase 1: the must-haves

- [ ] `PlayerHousing.FreeMode`
- [ ] house levels removed; one furnishing limit for everyone; gold refunds
- [ ] furnishings are items only; catalog converted; kit names and models fixed; for now
  every furnishing is available (progression arrives in Phase 3)
- [ ] pick up returns the item; House Storage for full bags
- [ ] undo and redo list; "Undo: ..." at the top of the in-house menu; `.house undo/redo`
- [ ] House Key item and `.house`; menus that change with where you are; paging for all
  lists
- [ ] steward spawns in capital cities; `.krook add` GM-only
- [ ] placement anywhere on the island with only the three rules; the exact clicked spot
  is kept; Unstuck; bringing swimmers back to the beach
- [ ] silent targeting (no Flare side effects)
- [ ] clearer messages with counts

Test additions: place, then undo, and the item is back in bags; undo the undo (redo);
pick up to full bags goes to storage and "Take all" works; with FreeMode on, every cost is
zero; old catalog placements pick up into items; a chair next to a table, a piece
outdoors on the far side of the island, and a piece on the main floor all place; a spot
past the edge of the private copy is refused.

### Phase 2: click to edit

- [ ] clickable copies of every furniture object (same model); outside decorate mode,
  working furniture keeps working (chairs sit, mailbox and crafting stations work)
- [ ] Decorate mode, piece menu (rotate, face, nudge, raise and lower, move here)

Test additions: the test client clicks a piece and drives the piece menu; rotate, nudge
and height are checked in the database; a guest clicking a chair sits and can't edit.

### Phase 3: progression unlocks

- [ ] the unlock engine: sources (achievements first, then kills, reputation,
  exploration, professions, quests, level, holidays), live unlock messages, veteran
  credit at login
- [ ] the Collection menu, with categories, counts, hints and "get a copy"
- [ ] `PlayerHousing.UnlockAll` and the GM unlock commands
- [ ] the content list loaded into the database, including working furniture and boss
  figurines

Test additions: with `UnlockAll = 0`, a new character has only the starter set; a GM
grants an achievement and the matching piece unlocks with a message; a character that
already had the achievement gets it at login; locked entries show their hint.

### Phase 4: people

- [ ] guest list you can click, invite target or party
- [ ] visit lists (party, guild, friends, invited, public)
- [ ] privacy presets (Private, Friends & guild, Public)
- [ ] arrival and invite messages

Test additions: the guest finds the owner's house in "Houses you're invited to" and gets
in with one click; "Friends & guild" lets a guild member in and keeps a stranger out.

### Phase 5: buildings

- [ ] buildings as items, placed with the targeting circle, with rotate, nudge and undo
  like furniture, and their own limit
- [ ] picking a building up asks first: the building only, or the building and the pieces
  inside it
- [ ] buildings stay visible from farther away (large-object view distance)
- [ ] the starter wreckage (fallen cart, shredded tent) placed on first visit; a new
  arrival spot on the beach
- [ ] the old guild hall sealed until it's restored at level 80 (or the cleared island,
  section 11)
- [ ] house styles retired; their props become ordinary furniture
- [ ] the buildings ladder from the content list loaded into the database

Test additions: place a building and pick it up: the prompt appears, "building only"
returns it to the bags and leaves the pieces inside where they were; undo puts it back; a
level 1 character's Collection shows the wreckage rung unlocked and hints for the rest;
getting inside the sealed hall before level 80 moves you back out; at level 80 the hall
opens.

### Phase 6: polish and optional extras

- [ ] one-time tips and the first-visit greeting
- [ ] visitor greeting message, "Pack up everything" (undoable)
- [ ] optional LOTRO-style hooks: glowing "snap here" spots on walls and tables that place
  a piece at the right height and angle in one click, for players who don't want to
  fine-tune
- [ ] optional client addon: a real furnishing window with icons, drag to place and
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
| The island's own guild hall? | Seal it and make restoring it the level 80 capstone. No client patch, and players see what they're working toward. The alternative, the cleared island, needs a client patch for every player. |
| Retire the four house styles? | Yes. Buildings replace them. |
| Building limit per island? | 10 to start, separate from furniture, set in the config. |
| What unlocks buildings: level or deeds? | Level as the spine, with a matching deed on some for flavor. |
| Add LOTRO-style hooks? | Later and optional. They help beginners, but aren't needed now that pieces can go anywhere. |
| Undo list across logouts? | No. It lasts until you leave the house; pick up covers anything older. |
