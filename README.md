# mod-playerhousing

Player housing for AzerothCore (WotLK 3.3.5a), modeled on Lord of the Rings Online and Final
Fantasy XIV without the neighborhoods. Every character gets a private copy of GM Island to
build on. Furnishings and buildings are ordinary items: right-click one, click where it
should go, done. Every change can be undone, and anything picked up goes back to your bags.
What you can own grows as you play: exploring, dungeons, raids, reputation, professions and
holidays all add pieces to your Collection, and buildings climb from a broken cart and a
shredded tent at level 1 to faction halls at Exalted.

The design and the reasoning behind it are in [docs/UX_PLAN.md](docs/UX_PLAN.md). Every
piece and what unlocks it is listed in [docs/UNLOCKS.md](docs/UNLOCKS.md).

## For players

### Getting started

- On your first login you get a **House Key** and a chair, a table and a lantern.
- Right-click the House Key (or type `.house`) for the Home menu. **Go home** takes you to
  your island. A fallen cart and a shredded tent are waiting, and Krook the steward says
  hello.
- Lost the key? Krook stands beside the innkeeper in every capital (Stormwind, Ironforge,
  Darnassus, the Exodar, Orgrimmar, Thunder Bluff, the Undercity, Silvermoon, Shattrath
  and Dalaran), or use the Home menu, "I lost my House Key".

### Krook's welcome tour

Krook (in every capital beside the innkeeper, and on your island) offers five short
quests that walk you through housing, each done the moment you do the thing:

1. **Home Sweet Island**: use your House Key to go home.
2. **Making It Yours**: place a furnishing.
3. **A Fresh Look**: start decorating, then turn, nudge or move a piece.
4. **Nothing Is Ever Lost**: undo a change.
5. **Open House**: invite a guest, or open your island to friends or everyone.

Each gives a little silver; the last unlocks **Krook's Picnic Basket**. The quests show
above Krook's menu, so skipping them costs nothing.

### Placing things

1. Right-click a furnishing or building in your bags. The targeting circle is the size of
   the piece (from 1 yard for a candle to 20 for a manor), so you can see the room it takes.
2. Click where it should go. It lands on that exact spot, facing you.

After placing a building, its menu opens by itself: Keep it here, Take it back, turn or
nudge it. The piece standing there is the preview. Island settings (or `.house adjust`)
switches this to every piece, or off.

That's the whole flow. You can place anywhere on your island, indoors or out, up to the
beach. The only rules: it has to be on your island, you have to be able to see the spot,
and each island holds up to 200 furnishings and 10 buildings (both set in the config).
Swim too far out and you're brought back to the beach.

### Changing things

- **Start decorating** (Home menu, or `.house decorate`). Now click any piece to open its
  menu: turn it, face it toward you, move it to where you stand, nudge it, raise or lower
  it, or pick it up.
- **Move with the targeting circle** hands you a Move a Piece item with a circle the size
  of the piece: click the new spot. Whatever stands on it (a lantern on a table, all the
  furniture in a building) goes along, and one undo puts it all back.
- **More turns, tilt and size...** turns by 90, 15 or 5 degrees, tilts it 5 degrees at a
  time (forward, back, or to its left or right), and makes it bigger or smaller a tenth
  at a time. What stands on it keeps its place on the bigger or smaller top. The server
  sets how far sizes and tilts go (half to double size and 45 degrees by default), and
  mannequins always stand upright.
- **Place another like this** puts one more of the same piece in your bags (from your
  bags, House Storage, or a new copy from the Collection). The next one you place gets
  the first one's turn, size and tilt: handy for rows of fence posts or matching chairs.
- **Grid** (Island settings, or `.house grid 1`): new pieces land on a grid and face
  straight or diagonal, moves land on it too, and nudges go one square at a time. Pieces
  put on a table top aren't squared up, so they stay on the table.
- Tables, crates and shelves grow a blue rune while decorating. Click the rune to put a
  small piece (a lantern, a candle, a book) right on top.
- **Done decorating** puts everything back to normal: chairs can be sat on, mailboxes and
  crafting stations work.
- **Undo** and **Redo** are at the top of the Home menu, with the change they'd undo
  spelled out ("Undo: placed Westfall Chair"). Undo remembers your last 30 changes while
  you're on the island.
- Picked-up pieces go back to your bags. When your bags are full they wait in **House
  Storage** (Home menu), and "Take everything" empties it.
- Buildings ask first: pick up the building only, or the building and everything inside.
- **Pack up everything** (while decorating) returns every piece at once, and can be undone
  too.

### Figurines

The Collection's **Figurines**: 21 trophies, from a Kobold and Hogger to Onyxia, Illidan
and the Lich King. Each is the creature's own model, shrunk to fit on a table (about a
yard long) and frozen mid-pose. Defeat the boss to unlock it, or hold its dungeon or raid
achievement, so past victories count. Figurines go on tables like other small pieces,
resize like anything else, and don't tilt. Visitors who click one learn whose trophy it
is.

### The Bank Chest

Unlocked at level 20 (or by buying 7 bank slots). Place it anywhere, and click it when
you're not decorating: **Open my bank** brings up your own bank, right there, and **House
Storage** lists the pieces waiting there. It works the way a banker does: only near the
chest, for a few minutes after you open it. Visitors find it locked.

### Weather, time of day and music

Island settings, Island ambience. Each island keeps its own:

- **Weather**: clear, fog, light rain, rain, thunderstorm, light snow, snow, blizzard or
  sandstorm.
- **Time of day**: the server's clock, or always dawn, midday, dusk or night.
- **Music**: place a **Music Box** (unlocked at level 10) and click it to pick a tune:
  the capital cities, taverns, Dalaran, Nagrand, Grizzly Hills and more.

Everyone on the island sees and hears them, visitors included, and nobody else does.
Leaving puts back the real clock and the weather where you land.

### Saved layouts

Home menu, Saved layouts (or `.house layout`). Save your island as it is, try something
new, and set the old one back out whenever you like. Each character keeps up to 5.

- **Set it out** packs up the island and places the layout: every piece where it stood,
  turned, sized and tilted the same, lanterns back on their tables. It uses your own
  pieces (from the island, your bags and House Storage); ones you don't have are left
  out, and the message says which. One undo puts the island back as it was.
- A layout's page counts what's missing and gets the ones you've unlocked in one click
  ("Get the 3 missing pieces"), and says how many are still locked.
- **Send a copy** to someone in your party or guild, or a friend who has you on their
  list. **Visitors may copy my layout** (Island settings) lets anyone visiting save a
  copy from the Home menu. Layouts hold no items: whoever sets one out places their own.
- Mannequins come back bare: their gear goes to your bags when the island is packed up.

### Mannequins: show off your gear

The Mannequin (in everyone's starter set) is a stand for armor and weapons. Place it like
any piece, then click it:

- **Put gear on...** lists the armor, weapons, shields, shirts and tabards in your bags.
  Pick one and the mannequin wears it; the item leaves your bags while it's on display,
  enchants and gems included. Putting something on an occupied slot swaps them.
- **Take off** puts an item back in your bags (Krook mails it to you if your bags are
  full). Picking up the mannequin returns it with everything it wears.
- **Figure** changes the body: every playable race, man or woman. A new mannequin takes
  after you.
- Every change can be undone, and undo gives back the very same item.
- Visitors can click it to see what it's wearing, but can't change anything.

Rings, necklaces, trinkets and relics don't show on a body, so they aren't offered.

### The Collection

House Key, Collection. It lists every piece by category with your progress, for example
"Buildings (6/49, 2 new)". Click an unlocked piece for its page: how many you have (in
your bags, in storage, placed) and Get one or Get 5 (free with FreeMode, a small gold
cost otherwise). A locked piece tells you how to earn it, with your progress so far
("Reach level 20 (you're level 15)", "Exalted with Stormwind (you're Revered)").

- **Search by name** (or `.house collection lamp`) finds pieces in every category,
  locked ones included, so you can see what's out there.
- Pieces you've unlocked but not looked at yet are marked **(new)**, and the Home menu
  and the Collection count them. Seeing one in a list is enough to clear the mark.
- **Showing all pieces / unlocked only**: hide what you haven't earned yet, for a shorter
  list of what you can place right now.
- Unlocks happen the moment you earn them, with a message.
- Things you did before the module was installed count: they unlock at your next login.
- Unlocks are shared by all your characters, except faction buildings, which need Exalted
  with their faction on the character that places them.

### Visitors

House Key, Island settings:

- **Privacy**: Private (only you and your guests), Friends & guild, or Public.
- **Guest list**: invite by name, your target, or your whole party. Guests can always
  visit, whatever the privacy setting. They're told when you invite them.
- **Greeting**: a message every visitor sees when they arrive.

Visit someone with House Key, Visit an island. It lists the islands of your party, guild
and friends, the ones you're invited to, and public ones. Only islands you're allowed into
are shown, so every entry works with one click. Visitors can use chairs and stations but
can't change anything. The owner is told when someone arrives.

### Likes and the visitor log

- Visitors can **like** an island from the Home menu (one like per account, so alts don't
  count twice), and take it back. The owner is told.
- Visit an island, **Most liked islands** lists the islands you may enter, most liked
  first, with their likes.
- Island settings, **Visitor log**: the last visitors with the date and time, how many
  came this week, and your likes. Coming home, Krook says how many visits there were
  since you were last there.

### Roommates

Island settings, Guests, click a guest: **Make them a roommate** (or `.house roommate
<name>`). A roommate can decorate your island with you: place their own pieces, and move,
turn, resize or pick up yours, with their own undo. What they can't do: pack up the
island, set out a layout, change your settings, or dress a mannequin that isn't theirs.

Every piece remembers who placed it and goes back to them when picked up: a roommate's
piece you pick up lands in their House Storage (and undo takes it back out), and yours
land in yours. Gear on a roommate's mannequin goes back to them by mail. "Make them a
guest only" (or `.house unroommate <name>`) ends it; their pieces stay where they are.

### Commands

Everything is also in the menus; these are shortcuts. `.krook` works the same as `.house`.

| Command | What it does |
| --- | --- |
| `.house` | Opens the Home menu |
| `.house home`, `leave`, `unstuck`, `key` | Go home, leave the island, back to the landing spot, a new House Key |
| `.house decorate [on\|off]` | Start or stop decorating |
| `.house edit [on\|off]` | Edit mode (the addon's keys): decorating, and a click on a piece only selects it |
| `.house shift <forward> <left> <up> <degrees> [id]` | Move and turn a piece in one go, relative to your facing; quick runs on one piece are one undo step |
| `.house undo`, `redo` | Undo or redo the last change |
| `.house select [id\|next\|previous]` | Select a piece by number, the nearest one, or the next one out by distance |
| `.house list` | The pieces within 40 yards, with their numbers |
| `.house rotate <degrees> [id]` | Turn a piece (positive is left) |
| `.house face [id]`, `here [id]` | Face you, move to where you stand |
| `.house nudge <forward\|back\|left\|right\|up\|down> [yards] [id]` | Nudge a piece, relative to where you're facing |
| `.house up`, `down` | Raise or lower a tenth of a yard |
| `.house move [id]` | Move a piece with a targeting circle (what's on it comes along) |
| `.house size <bigger\|smaller\|normal\|percent> [id]` | Resize a piece, within the server's limits |
| `.house tilt <forward\|back\|left\|right\|straight> [degrees] [id]` | Tilt a piece (5 degrees unless given); left and right are its own |
| `.house another [id]` | One more of this piece, placed with its turn, size and tilt |
| `.house layout [save <name>\|load <name>\|delete <name>\|send <name> <player>\|list]` | Saved layouts; without more, the menu |
| `.house grid <off\|yards>` | Snap to a grid of 0.25 to 4 yards |
| `.house pickup [id] [inside]` | Pick up a piece; `inside` also takes what's in a building |
| `.house packup` | Pick up everything (undoable) |
| `.house collection [search]`, `storage`, `visit [name]` | Open those menus, search the Collection, or visit someone by name |
| `.house get <item> [count]` | A copy (or up to 20) of an unlocked piece, paid for unless FreeMode |
| `.house take <item\|all>` | Take a piece, or everything that fits, out of House Storage |
| `.house weather <name>`, `time <name>`, `music <sound id\|off>` | The island's weather, time of day and music |
| `.house data <kind>`, `addon`, `seen` | Quiet ones for the addon's window: its lists, that it's there, new unlocks seen |
| `.house invite <name\|target\|party>`, `uninvite <name>` | Manage your guest list |
| `.house roommate <name>`, `unroommate <name>` | Let a guest decorate, or stop |
| `.house like`, `visitors` | Like the island you're visiting (or take it back); your visitor log |
| `.house privacy <private\|friends\|public>` | Who can visit |
| `.house greeting <text\|clear>` | The message visitors see |
| `.house adjust <all\|buildings\|off>` | When a piece's menu opens by itself after placing |

Without an id, commands act on the selected piece (the one you last clicked).

GMs also have `.house unlock <item|name|all> [player]`, `.house relock ...`,
`.house unlocks [player]` and `.house add` (Krook next to you for ten minutes).

### Moderation

Players report an island from its Home menu (Report this island to a GM...) or with
`.house report <what's wrong>`: once per account per island while the report is open.
Online GMs are told at once. GM commands:

| Command | What it does |
| --- | --- |
| `.house reports [all]` | Open reports (or the last 20 of all), newest first |
| `.house close <id>` | Close a report |
| `.house inspect <player>` | Go to anyone's island, whatever its privacy |
| `.house hide <player>`, `unhide <player>` | Close an island to all but its guest list, and take it off the public and most liked lists |
| `.house cleargreeting <player>` | Clear an island's greeting |
| `.house gmpackup <player>` | Pack up an island: every piece goes to the House Storage of whoever placed it, and mannequin gear is mailed back |

GM actions are logged to the server log (module logger).

## Optional client addon

`client-addon/PlayerHousing` is a housing window for players who'd rather click than use
menus, plus an edit mode that moves pieces with the keyboard and mouse. It's optional: the
House Key menus do everything without it. With the addon, the House Key opens the window
(`/housing key` switches back to the menu, which the window's Menu button also opens).

The window has a toolbar (go home or leave, Edit, Undo and Redo, whose tooltips say what
they'd undo, and the full menu) and eight tabs:

- **Bags**: your furnishings and buildings as icons, with filters and search. Click one,
  then click where it goes. Drag one to an action bar to keep it handy. Hover an icon to
  preview the piece next to the window: its model, slowly turning and centered in the
  frame, and its size. Buildings made of world models can't be drawn in a window, so they
  show a floor plan to scale instead, with you next to it.
- **Collection**: every piece there is, unlocked ones in color and locked ones grey, by
  category, favorites, unlocked only, or by name. The tooltip says how to unlock a piece,
  what a copy costs, and how many you have in your bags, House Storage and on the island.
  Click a piece to keep it in the preview with its details and buttons: Place (click, then
  click the spot; with none in your bags the first click gets one), Get 1, Get 5 and Take
  from storage. Hovering other pieces shows them for a moment. Right-click a piece to star
  it as a favorite. New unlocks are marked until you leave the tab.
- **Storage**: House Storage, one piece or everything at once back to the bags.
- **Placed**: the pieces on the island, nearest first: select one, bring it to where you
  stand, or pick it up.
- **Layouts**: save the island under a name, set a layout out again, send it to someone,
  delete it.
- **Guests**: invite by name, your target or your party; make a guest a roommate (who
  can decorate) or remove them.
- **Visit**: the islands of your party, guild and friends, the ones you're invited to,
  public ones and the most liked; or visit someone by name. Like the island you're on.
- **Island**: who can visit, the greeting, and the island's weather, time of day and
  music (with a Music Box placed).

In the preview, drag the model to turn it, use the mouse wheel to zoom and right-drag to move
it up or down. If previews sit too high or too low on your client, `/housing framing`
tries the other way of centering them.

For the selected piece, a panel below the tabs has turn (Shift-click for 5 degrees,
Ctrl-click for 90, or the mouse wheel over the window), face me, move here, nudge, bigger
and smaller, tilt, pick up, Move and Another. Move and Another bring up a button that uses
the right item for you: click it, then click the spot.

**Edit mode** (the Edit button, a key binding, or `/housing edit`): right-click a piece (or
press Tab for the next one nearby), then:

| Key | Does |
| --- | --- |
| Arrow keys | Slide it, the way you face (hold to keep going) |
| Mouse wheel | Turn it (after R: raise and lower it) |
| Page Up, Page Down, Ctrl+wheel | Raise, lower |
| R | Switch what the plain mouse wheel does |
| Shift with any of those | Finer steps |
| Tab, Shift+Tab | Next or previous piece nearby |
| G | The targeting circle, at once: click the new spot |
| Delete | Pick it up |
| Ctrl+Z, Ctrl+Y | Undo, redo |
| Alt+wheel | Zoom the camera |
| Escape | Cancel the circle, or leave edit mode |

A banner at the top of the screen names the selected piece, lists the keys, says what the
last key did (so a key that does the wrong thing shows itself) and has Grid, Wheel, Undo,
Redo and Done buttons. The selected piece has a ring under it. While a piece is selected,
edit mode keeps a Move a Piece item in your bags for G, and takes it away afterwards. With
the grid on, arrows move a square at a time. A quick run of key presses on one piece is a single
undo step. The keys are only bound in edit mode, so the usual ones come back afterwards;
bindings can't change in combat, so they wait for it to end. The client can't slide a game
object, so each step redraws the piece.

The window opens by itself when you arrive home (`/housing auto` turns that off).
`/housing` shows or hides it, and `/housing <command>` runs any `.house` command. A button
on the minimap's edge opens the window (right-click: edit mode); drag it around the edge,
or `/housing minimap` to hide it. Key bindings: Key Bindings, Player Housing.

Install: copy the `PlayerHousing` folder into `World of Warcraft/Interface/AddOns/`. The
window can't open or close, or change tabs, in combat (a WoW rule for windows with item
buttons).

The addon talks to the server with the same `.house` commands, and reads what the server
whispers to the player with the addon prefix `HOUSING`: the island state
(`PlayerHousingMgr::SendAddonState`) and the tabs' lists (`src/HousingAddon.cpp`: `begin`,
a `row` a message, `end`). The piece list with names, icons and unlock hints
(`PieceInfo.lua`) comes from the content builder. `client-addon/test/harness.lua` runs the
addon outside the game against stubbed WoW functions:

```
lua5.1 client-addon/test/harness.lua client-addon/PlayerHousing/PieceModels.lua \
    client-addon/PlayerHousing/PieceInfo.lua client-addon/PlayerHousing/PlayerHousing.lua \
    client-addon/PlayerHousing/EditMode.lua client-addon/PlayerHousing/Window.lua \
    client-addon/PlayerHousing/Minimap.lua
```

## Install

1. Put the module in your AzerothCore `modules` folder and rebuild the server.
2. Apply the SQL, in this order (all files can be re-applied safely):
   - world: `sql/db_world/base/mod_playerhousing_world.sql`, then
     `sql/db_world/base/mod_playerhousing_world_catalog.sql` (optional: the catalog of
     every object, used with `PlayerHousing.Catalog = everything`), then
     `sql/db_world/base/mod_playerhousing_world_content.sql`
   - characters: `sql/db_characters/base/mod_playerhousing_characters.sql`, then
     `sql/db_characters/base/mod_playerhousing_characters_hotfix.sql`

   The core's auto-updater only reads `data/sql`, so these are applied by hand (the test
   server scripts do it for you).
3. Copy `conf/mod_playerhousing.conf.dist` to your server's `modules` config folder as
   `mod_playerhousing.conf`.
4. The island: the default layout, `cleared`, is GM Island with its guild hall removed.
   It needs two things from [tools/gm-island-cleared](tools/gm-island-cleared/README.md):
   the server data (collision and pathing without the hall, one script) and a client patch
   every player installs once. The same patch gives the housing items their bag icons
   (without it they show as question marks). To keep the hall instead, set
   `PlayerHousing.Layout = "guildhouse"`; players then build the patch with `--icons-only`.
5. Restart the worldserver.

## Configuration

| Setting | Default | What it does |
| --- | --- | --- |
| `PlayerHousing.Enable` | 1 | The whole module |
| `PlayerHousing.FreeMode` | 0 | Test servers: copies from the Collection are free and the House Key is instant |
| `PlayerHousing.UnlockAll` | 0 | Test servers: the whole Collection is unlocked, plus "one of everything" |
| `PlayerHousing.Layout` | cleared | `cleared` (no guild hall) or `guildhouse` |
| `PlayerHousing.DefaultPrivacy` | private | Privacy of new islands: `private`, `friends` or `public` |
| `PlayerHousing.GmBypassPrivate` | 0 | GMs in GM mode can visit any island |
| `PlayerHousing.MaxFurnishings` | 200 | Furnishings per island |
| `PlayerHousing.MaxBuildings` | 10 | Buildings per island |
| `PlayerHousing.Size.Min`, `Size.Max` | 0.5, 2 | How small and big pieces can be made (times normal size); 1 and 1 turn resizing off |
| `PlayerHousing.Tilt.Max` | 45 | How far pieces tilt each way, in degrees; 0 turns tilting off |
| `PlayerHousing.SavedLayouts` | 5 | Layouts each character can save (0 turns them off, 20 at most) |
| `PlayerHousing.Catalog` | curated | `curated`: the pieces earned through progression. `everything`: also every other object model in the game (about 2,000), in the Collection's Catalog |
| `PlayerHousing.HouseKey.DelaySeconds` | 5 | How long "Go home" takes; moving or combat cancels |
| `PlayerHousing.StewardEntry` | 900200 | Krook's creature entry |
| `PlayerHousing.StewardDisplayId` | 25384 | Krook's model (a Wolvar orphan) |

Every setting can also come from an environment variable, for example
`AC_PLAYER_HOUSING_FREE_MODE=1` (the core's usual `AC_` naming).

## Content

The pieces, their models and what unlocks them are written as a Python list in
`tools/content/pieces.py`. `tools/content/build_content.py` turns it into
`sql/db_world/base/mod_playerhousing_world_content.sql` (items, objects, pieces, rules and
the targeting circle spells), `docs/UNLOCKS.md`, the addon's model list
(`client-addon/PlayerHousing/PieceModels.lua`) and the items the client patch adds
(`tools/gm-island-cleared/client_items.tsv`). Each item's bag icon is picked from its name
by `tools/content/icons.py` (a piece's `icon` field overrides it). It reads the world
database (to copy models and behavior from existing objects) and the client data's `dbc`
folder (for model sizes, names and icons):

```
python3 tools/content/build_content.py --dbc /path/to/data/dbc \
    --mysql "mysql -uacore -pacore acore_world"
```

The builder also writes `sql/db_world/base/mod_playerhousing_world_catalog.sql`: every
other object model in the game (about 2,000), one piece each, named after its object (or
its model when the object's name is an internal one), sized from the game data, and
marked as fitting on tables when small. Big models (whole areas, ships), collision shapes
and markers are left out. These pieces are used only with `PlayerHousing.Catalog =
everything`: then everyone has them from the start, in the Collection's Catalog (search it
by name). The file uses its own item and object ranges (items 940000 and up), so the
curated content and the catalog never step on each other, and applying it with `curated`
does nothing visible.

Rules in `mod_playerhousing_piece_rule`: level, achievement, reputation rank, quest, kill,
exploring an area, skill level, or never (GM and UnlockAll only). Rules in the same group
must all be met; any complete group unlocks the piece. Achievements come first wherever one
fits, since players already see and track those.

## How it works

- **One island per character.** GM Island (Kalimdor) is shared by everyone, and phasing
  gives each owner a private copy. A house phase has bit 31 set and bit 0 clear, and the
  module turns off the core's "any shared bit" phase matching for it
  (`GLOBALHOOK_ON_BEFORE_WORLDOBJECT_SET_PHASEMASK`), so each phase value is its own ID.
  The owner's guests share the owner's phase.
- An island's pieces are spawned when the first person arrives and removed when the last
  one leaves. Anyone found on the island without going through the House Key (other than
  GMs) is sent back where they came from, and logging out on an island brings you back
  where you came from.
- **Pieces.** Each piece is an item (901100 to 901199 and 902001 to 902999; the House Key
  is 902000) and a gameobject: `910000 + (item - 900000)` normally, and
  `920000 + (item - 900000)` while decorating, a clickable copy of chairs and stations that
  opens the piece menu instead of working. Buildings stay visible from farther away.
  Housing objects have server-side collision turned off.
- **Sizes and outlines** come from the game data: small models from
  `GameObjectDisplayInfo.dbc`, buildings made of world models from the server's collision
  data (`vmaps/GameObjectModels.dtree`). A building's outline, turned the way it faces, is
  what counts as inside it when picking it up with what's inside.
- **Placement** uses a targeting circle: each piece's item carries a ground-target spell
  whose circle matches the piece's size (nine spells, 1 to 20 yards, listed in
  `tools/content/build_content.py`). They're unused creature and quest spells with no
  description, so the item's tooltip has no misleading "Use:" line. The spell is caught
  before it casts, so there's no cast bar, sound or cooldown. The core has already checked
  range and line of sight to the clicked spot by then. Players who have old copies of the items cached see the old
  circle size until they clear their `WDB` folder.
- **Undo** keeps each change as the before and after of the pieces it touched, so undo and
  redo replay them exactly, handing items back or taking them as needed. Each player has
  their own list, in memory, cleared when they leave the island. A step only applies to
  the very pieces it was written for (see Safety and limits).
- **Mannequins** are creatures (entry 900201) with the mirror image flag, the way the
  Mirror Image spell works: the client asks what the figure wears and the module answers
  (a `ServerScript` catching `CMSG_GET_MIRRORIMAGE_DATA`), while weapons are virtual items.
  Gear on a stand leaves the inventory but stays in `item_instance`, the way mail keeps
  items, with a row in `mod_playerhousing_placement_gear`; so enchants, gems and the item's
  guid survive, and undo returns the same item. Deleting a character for good deletes
  its own stand gear; gear roommates left on its island is mailed back to them first.
  The characters rollback mails any gear still on stands back to its owners.

### Safety and limits

Nothing on an island can be duplicated or lost:

- Every piece placed takes its item, and every piece picked up gives one back. The item a
  piece is placed with is taken before anything else the player sends is handled, so
  moving it to the bank, the mail or a trade in the same moment doesn't keep it.
- An undo or redo step only applies to the pieces it was written for. If someone else has
  since picked one up or replaced it, the step is dropped with a message. Placement ids
  are never handed out twice while anyone is on the island, and a GM packing up an island
  or its owner being deleted clears every undo list that points at it.
- Gear on a mannequin is only ever changed by the player it belongs to; anyone else's undo
  moves the stand and leaves what it wears alone.
- When a character is deleted, pieces roommates placed on its island go to the roommates'
  House Storage and their mannequin gear comes by mail. The island itself stays until the
  character is gone for good, so a GM can still restore it; it goes when the core removes
  the character (also when the core purges old deleted characters), and anything left by
  characters removed while the module was off is cleaned up at the next start. Deleted
  characters' islands drop off the visit lists and can't be visited.

Limits, per player (GMs are exempt from the first):

- 15 housing commands or menu clicks in any 3 seconds.
- 3 seconds between heavy actions: pack up, set out a layout, get the missing pieces, one of
  everything, and undoing or redoing a step of more than 20 pieces.
- 1 second between weather, time of day and music changes, which everyone on the island
  receives.
- A like every 10 seconds, and one message of each kind a minute from one player to the
  same other player (an invite, roommate news, a like). Five reports an hour per account.

Player text (greetings, layout names, reports) is escaped for SQL, stripped of control
characters and link codes, and cut to length without splitting a character. `nan` and `inf`
aren't accepted as numbers. Players who aren't on an island cost the module one check per
update, without taking its lock, and the visit lists look up guests and friends once per
list rather than once per island.

### Load

`tools/test-server/testclient/load_test.py` puts many players on their islands at once:
each goes home, opens the island, places pieces, turns, nudges, undoes and redoes, and
visits a neighbor, while a GM samples the server's update times. On the development
container (server and all the clients on one machine):

| Players | Pieces placed | Place, p95 | Undo, p95 | Visit, p95 | Go home, p95 | Server update: mean, p99, max |
| --- | --- | --- | --- | --- | --- | --- |
| 40 | 320 in 37 s | 127 ms | 129 ms | 547 ms | 78 ms | 11 ms, 105 ms, 1244 ms |
| 99 | 792 in 42 s | 168 ms | 134 ms | 549 ms | 92 ms | 14 ms, 150 ms, 223 ms |

Going home is a teleport to another continent, every player at the same moment; a visit
is a short hop on the island. "Go home" is measured by one more player in a process of its
own, going home and back all through the test: the load test's own players share one
Python process, and reading all their arrivals at once takes it seconds (their own
figure, 5.6 s at 99 players, is the test client, not the server). The 1.2 s update at 40
players came in the first run after a restart. The worldserver used about 1.8 GB.

No island showed another island's pieces, and no action failed.

### Upgrading from the house levels version

Older versions had house styles, stages, a vendor catalog and furniture unlocks. Applying
the world SQL removes those tables. At the next startup the module converts what players
had: placed furniture gets its item (so it picks up into your bags), catalog unlocks become
Collection unlocks plus one copy in House Storage, and with the cleared island, anything
that stood inside the old guild hall goes to its owner's House Storage with a message on
their next visit. Gold spent on stages isn't refunded.

## Testing

[tools/test-server](tools/test-server/README.md) has a prebuilt server image, a fast
development container and an end-to-end test that plays the whole thing through with
headless clients (241 checks: placing, undo, decorating, edit mode, the addon's window,
storage, the Collection, buildings, mannequins, layouts, ambience, visitors, roommates,
moderation, working furniture, addon messages, relogging, and the safety rules above), and
a load test.

## Rollback

`sql/db_world/base/mod_playerhousing_world_rollback.sql` and
`sql/db_characters/base/mod_playerhousing_characters_rollback.sql` remove everything the
module added.
