# Player housing: UX plan

How to make housing easy to learn and easy to use, modeled on Lord of the Rings Online
and Final Fantasy XIV (without neighborhoods). Two hard rules come from you:

- **Every placement can be undone, and undoing gives the item back** so it can be placed
  somewhere else.
- **During testing everything is free.**

Everything below works with the stock 3.3.5a client (menus, vendor windows, targeting
circle, bag items). A client addon is an optional extra at the very end, never a
requirement.

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
| 12 | **Errors don't say what to do.** "Too close to another furniture object" doesn't say which one or how far. "Outside your current house bounds" doesn't mention that upgrading helps. | `IsPlacementPointValid` |
| 13 | **Everything costs gold, with no test switch.** Upgrades (50g to 12,150g), unlocks and kits. | stage, catalog and item tables |
| 14 | **Long lists can crash the server.** The client allows 32 menu entries, and the core asserts on more. The "select furniture" list adds one entry per unlocked item with no paging. | `GOSSIP_MAX_MENU_ITEMS = 32` |
| 15 | **Placement rules block normal decorating.** Nine checks run on every placement. Pieces must be yards apart (a chair can't go within 4 yd of a table), the spot must be walkable, slopes over 35° are refused, and every piece is dropped onto the floor below, so nothing can go on a table, a wall or up high. Furniture must stay within 50 yd of the house (80 yd at stage 6), which is 16% to 35% of the island. | `IsPlacementPointValid`, `place_radius` |

## 2. Design principles

1. **One way to do each thing.** One kind of furniture (bag items), one door (the House
   Key), one place to change settings.
2. **Nothing is permanent by accident.** Undo, redo, and pick up that always returns the
   item.
3. **Point and click, never type.** Lists to pick from instead of IDs and names. Typing a
   name stays only as a last resort for finding a player.
4. **Show only what applies here.** Menus change with where you are and what you're doing.
5. **Say what happened and what to do next,** in one line, with numbers ("4/10 furnishings").
6. **A new character places their first chair within two minutes** of logging in.

## 3. How housing will work (what players learn)

Five ideas, borrowed from the two reference games:

### House Key (LOTRO's "travel to house" skill, FFXIV's estate teleport)

Every character gets a **House Key** in their bags on first login (soulbound, replaced for
free if lost). Right-click it anywhere to open the **Home menu**: go home, visit someone,
leave, decorate, settings. `.house` opens the same menu. Stewards stay as friendly
signposts in the capital cities and inside each house (shop and help), but no longer
stand between the player and anything.

### Furnishings are items (FFXIV)

Every decoration is a bag item named after exactly what it places ("Furnishing: Westfall
Chair"). The shop sells them. Picking a piece up puts it back in your bags. The old
"unlock by ID" catalog goes away. Pieces already unlocked or placed are converted
(section 7).

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
3. You're under your furnishing limit.

Pieces may overlap, like FFXIV, where overlapping pieces to build new shapes is a popular
technique. No spacing, slope, "walkable" or snap-to-floor rules.

### Editing: click the piece (FFXIV layout mode)

The owner toggles **Decorate mode** from the Home menu. While it's on:

- clicking any placed piece opens its menu:
  - Pick up (back to your bags)
  - Rotate 45° left or right, or 15° for fine turns
  - Face toward me
  - Nudge forward, back, left or right (0.25 yd), or up and down (0.1 yd), with no
    height limit
  - Move to where I'm standing
  - Undo last change

Outside decorate mode, chairs and benches can be sat on by everyone, and nothing can be
moved by accident. Both reference games separate decorating from normal play this way.

### Undo and redo

Every change goes onto an undo list: place, pick up, move, rotate, nudge, and
"pack up everything". **Undo** reverses the last change exactly:

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

## 4. Menus

Every menu shows a status line first, only the options that apply, and pages any list
longer than 20 entries (below the client's 32-entry limit).

**Home menu, outside your house**

```
Your house: Human Cottage, stage 1, 4/10 furnishings, Private
  Go home
  Visit a house
  Storage (2)                         (only when something is in storage)
  How housing works
```

**Home menu, anywhere on your own island**

```
Your house: Human Cottage, stage 1, 4/10 furnishings, Private
  Start decorating            /  Done decorating
  Undo: placed Westfall Chair         (only when there is something to undo)
  Redo: ...                           (only after an undo)
  Shop furnishings                    (calls Krook over)
  House settings
  Unstuck: back to the entrance
  Leave house
```

**Piece menu (click a piece while decorating)**

```
Westfall Chair  (4/10 furnishings)
  Pick up (back to your bags)
  Rotate left 45°       Rotate right 45°
  Rotate left 15°       Rotate right 15°
  Face toward me
  Nudge...              (forward, back, left, right, up, down)
  Move to where I'm standing
  Undo last change
```

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
  Style: Human Cottage                (list of styles with a one-line description)
  Upgrade to stage 2: 18 furnishings (now 10)   150g
  Greeting for visitors               (FFXIV's estate greeting, optional)
```

Paid actions use the client's built-in confirmation popup, which shows the gold amount
before you accept. In test mode there is no price to show.

## 5. The rest of the experience

### Getting started (target: first chair within two minutes)

1. On first login: the House Key and three starter furnishings (chair, table, lantern)
   arrive in your bags. One chat line: "You have a house! Right-click your House Key to
   go there."
2. First time home: Krook greets you with three steps (place something, click it to
   change it, undo if you don't like it), then gets out of the way.
3. One-time tips at the moment they matter: after your first placement ("Click it while
   decorating to turn it or pick it up"), the first time you hit the limit, the first
   time an item goes to storage. Each tip shows once per character.
4. "How housing works" is five short lines, always in the Home menu.

### Visiting and guests (without neighborhoods)

- Privacy presets like both reference games: **Private** (you and your guest list),
  **Friends & guild**, **Public**. Your guest list always gets in.
- When invited, the guest gets a message: "Krookowner invited you. House Key, then Visit."
- The owner sees "Krookguest arrived." Visitors see the owner's greeting.
- Guests can sit and look around, never edit. Letting a guest help decorate
  (co-decorator) can come later if you want it.

### Styles and stages

- **Style** changes the look of the house (like FFXIV's interior fixtures). All
  furnishings work with every style, so style never hides items.
- **The whole island is yours from day one.** The 50 to 80 yd radius goes away.
- **Stages raise the furnishing limit.** That is the one thing an upgrade buys, so it's
  easy to understand. The limits (5 at stage 0 up to 72 at stage 6) were sized for one
  building and should grow with a whole island to fill (section 9).
- The upgrade option says what you get before you pay.

### Placement feel

- The targeting circle stays (it's the only native 3D pointer), but the Flare side
  effects go away. The module reads the clicked spot while the spell is being checked,
  then cancels the cast silently: no flare, no stealth reveal, no global cooldown,
  nothing expires. Hunters casting Flare outside a house are unaffected.
- Errors name the problem and the fix: "You can't see that spot from here. Walk closer."
  "House is full (10/10). Pick something up or upgrade to stage 2 (18)."
- Every success line ends with the count: "Placed Westfall Chair (5/10)."
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

## 6. Free during testing

One switch in `mod_playerhousing.conf`:

```
PlayerHousing.FreeMode = 1
```

With it on:

- upgrades, style changes and the shop cost nothing, and the shop window shows free
  prices
- the House Key has no cast time or cooldown
- the shop gets a "Give me one of everything" entry

The test server config turns it on by default. Live servers leave it off.

## 7. Keeping existing houses working

- Unlocked catalog entries become the matching furnishing items, one of each, sent to
  House Storage the first time the owner opens it.
- Existing placements stay where they are. Picking one up returns the matching item, so
  old placements are undoable too.
- The kit item IDs stay the same (so bags and vendors keep working), but get the right
  models and names.
- Houses and guest lists are unchanged.

## 8. Build order

Each phase ships on its own, keeps the end-to-end test green, and adds to it.

### Phase 1: the must-haves (undo, free, one kind of furniture)

- `PlayerHousing.FreeMode`
- furnishings are items only; catalog converted; kit names and models fixed
- pick up returns the item; House Storage for full bags
- undo and redo list; "Undo: ..." at the top of the in-house menu; `.house undo/redo`
- House Key item and `.house`; menus that change with where you are; paging for all lists
- steward spawns in capital cities; `.krook add` GM-only
- placement anywhere on the island with only the three rules; the exact clicked spot is
  kept; Unstuck; bringing swimmers back to the beach
- silent targeting (no Flare side effects)
- clearer messages with counts

Test additions: place, then undo, and the item is back in bags; undo the undo (redo);
pick up to full bags goes to storage and "Take all" works; with FreeMode on, every cost is
zero; old catalog placements pick up into items; a chair next to a table, a piece
outdoors on the far side of the island, and a piece on the main floor all place; a spot
out at sea is refused.

### Phase 2: click to edit

- clickable copies of every furniture object (same model), and sittable chairs
- Decorate mode, piece menu (rotate, face, nudge, raise and lower, move here)
- a curated set of wall and tabletop pieces (banners, shields, torches, candles, books,
  bottles) now that pieces can go anywhere

Test additions: the test client clicks a piece (CMSG_GAMEOBJ_USE) and drives the piece
menu; rotate, nudge and height are checked in the database; a guest clicking a chair
sits and can't edit.

### Phase 3: people

- guest list you can click, invite target or party, visit lists, privacy presets,
  arrival and invite messages

Test additions: the guest finds the owner's house in "Houses you're invited to" and gets
in with one click; "Friends & guild" lets a guild member in and keeps a stranger out.

### Phase 4: polish and optional extras

- one-time tips and the first-visit greeting
- visitor greeting message, "Pack up everything" (undoable)
- optional LOTRO-style hooks: glowing "snap here" spots on walls and tables that place a
  piece at the right height and angle in one click, for players who don't want to
  fine-tune
- optional client addon: a real furnishing window with icons, drag to place and
  mouse-wheel rotation, talking to the same server commands. It stays optional; the
  native menus remain complete without it.

## 9. Decisions with a recommended default

| Question | Recommendation |
| --- | --- |
| Keep gold costs on live servers? | Yes for upgrades and the shop, no for style changes. FreeMode covers testing. |
| House Key on live: instant, or a short cast and cooldown? | 5 second cast, no cooldown (like a mount, not a hearthstone). |
| Include the "Friends & guild" privacy level? | Yes. It covers sharing with friends without opening the house to everyone. |
| Raise the furnishing limits now that the whole island is usable? | Yes, roughly triple them (15 at stage 0 up to about 200 at stage 6). They live in the stage table, so they're easy to tune after testing. |
| Add LOTRO-style hooks? | Later and optional. They help beginners, but aren't needed now that pieces can go anywhere. |
| Undo list across logouts? | No. It lasts until you leave the house; pick up covers anything older. |
