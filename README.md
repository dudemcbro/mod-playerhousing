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
"Buildings (6/49)". An unlocked piece hands you a copy when clicked (free with FreeMode,
a small gold cost otherwise). A locked piece tells you how to earn it, with your progress
so far ("Reach level 20 (you're level 15)", "Exalted with Stormwind (you're Revered)").

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

### Commands

Everything is also in the menus; these are shortcuts. `.krook` works the same as `.house`.

| Command | What it does |
| --- | --- |
| `.house` | Opens the Home menu |
| `.house home`, `leave`, `unstuck`, `key` | Go home, leave the island, back to the landing spot, a new House Key |
| `.house decorate [on\|off]` | Start or stop decorating |
| `.house undo`, `redo` | Undo or redo the last change |
| `.house select [id]` | Select a piece by number, or the nearest one |
| `.house list` | The pieces within 40 yards, with their numbers |
| `.house rotate <degrees> [id]` | Turn a piece (positive is left) |
| `.house face [id]`, `here [id]` | Face you, move to where you stand |
| `.house nudge <forward\|back\|left\|right\|up\|down> [yards] [id]` | Nudge a piece, relative to where you're facing |
| `.house up`, `down` | Raise or lower a tenth of a yard |
| `.house pickup [id] [inside]` | Pick up a piece; `inside` also takes what's in a building |
| `.house packup` | Pick up everything (undoable) |
| `.house collection`, `storage`, `visit [name]` | Open those menus, or visit someone by name |
| `.house invite <name\|target\|party>`, `uninvite <name>` | Manage your guest list |
| `.house privacy <private\|friends\|public>` | Who can visit |
| `.house greeting <text\|clear>` | The message visitors see |
| `.house adjust <all\|buildings\|off>` | When a piece's menu opens by itself after placing |

Without an id, commands act on the selected piece (the one you last clicked).

GMs also have `.house unlock <item|name|all> [player]`, `.house relock ...`,
`.house unlocks [player]` and `.house add` (Krook next to you for ten minutes).

## Optional client addon

`client-addon/PlayerHousing` is a window for players who'd rather click icons than use
menus. It's optional: the House Key menus do everything without it.

- Your furnishings and buildings as icons, with filters and search. Click one, then click
  where it goes. Drag one to an action bar to keep it handy.
- Hover an icon to preview the piece: its model, slowly turning, and its size. Buildings
  made of world models can't be drawn in a window, so they show a floor plan to scale
  instead, with you next to it. Their outline is the most room they take, from the
  server's collision data (`vmaps/GameObjectModels.dtree`).
- Go home or leave, Decorate, Undo and Redo (the tooltip says what they'd undo),
  Collection, Storage, Visit and the full menu, one click each.
- For the selected piece: turn left or right (or use the mouse wheel over the window:
  Shift for small steps, Ctrl to raise or lower), face me, move here, nudge, and pick up.
- Opens by itself when you arrive home (`/housing auto` turns that off). `/housing`
  shows or hides it, and `/housing <command>` runs any `.house` command.
- Key bindings for the window, undo, redo, decorate, turning and selecting the nearest
  piece (Key Bindings, Player Housing).

Install: copy the `PlayerHousing` folder into `World of Warcraft/Interface/AddOns/`. The
window can't open or close in combat (a WoW rule for windows with item buttons).

The addon talks to the server with the same `.house` commands and reads a state message
the server whispers to the player (addon prefix `HOUSING`, fields in
`PlayerHousingMgr::SendAddonState`). `client-addon/test/harness.lua` runs it outside the
game against stubbed WoW functions: `lua5.1 client-addon/test/harness.lua
client-addon/PlayerHousing/PieceModels.lua client-addon/PlayerHousing/PlayerHousing.lua`.

## Install

1. Put the module in your AzerothCore `modules` folder and rebuild the server.
2. Apply the SQL, in this order (all files can be re-applied safely):
   - world: `sql/db_world/base/mod_playerhousing_world.sql`, then
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
   every player installs once. To keep the hall instead, set
   `PlayerHousing.Layout = "guildhouse"`; no patch is needed then.
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
| `PlayerHousing.HouseKey.DelaySeconds` | 5 | How long "Go home" takes; moving or combat cancels |
| `PlayerHousing.StewardEntry` | 900200 | Krook's creature entry |
| `PlayerHousing.StewardDisplayId` | 25384 | Krook's model (a Wolvar orphan) |

Every setting can also come from an environment variable, for example
`AC_PLAYER_HOUSING_FREE_MODE=1` (the core's usual `AC_` naming).

## Content

The pieces, their models and what unlocks them are written as a Python list in
`tools/content/pieces.py`. `tools/content/build_content.py` turns it into
`sql/db_world/base/mod_playerhousing_world_content.sql` (items, objects, pieces, rules and
the targeting circle spells), `docs/UNLOCKS.md` and the addon's model list
(`client-addon/PlayerHousing/PieceModels.lua`). It reads the world database (to copy models and behavior from
existing objects) and the client data's `dbc` folder (for model sizes and names):

```
python3 tools/content/build_content.py --dbc /path/to/data/dbc \
    --mysql "mysql -uacore -pacore acore_world"
```

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
- **Placement** uses a targeting circle: each piece's item carries a ground-target spell
  whose circle matches the piece's size (ten spells, 1 to 20 yards, listed in
  `tools/content/build_content.py`). The spell is caught before it casts, so there's no
  cast bar, sound or cooldown. The core has already checked range and line of sight to
  the clicked spot by then. Players who have old copies of the items cached see the old
  circle size until they clear their `WDB` folder.
- **Undo** keeps each change as the before and after of the pieces it touched, so undo and
  redo replay them exactly, handing items back or taking them as needed. The list lives in
  memory and is cleared when the owner leaves the island.
- **Mannequins** are creatures (entry 900201) with the mirror image flag, the way the
  Mirror Image spell works: the client asks what the figure wears and the module answers
  (a `ServerScript` catching `CMSG_GET_MIRRORIMAGE_DATA`), while weapons are virtual items.
  Gear on a stand leaves the inventory but stays in `item_instance`, the way mail keeps
  items, with a row in `mod_playerhousing_placement_gear`; so enchants, gems and the item's
  guid survive, and undo returns the same item. Deleting a character deletes its stand
  gear; the characters rollback mails any gear still on stands back to its owners.

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
headless clients (103 checks: placing, undo, decorating, storage, the Collection,
buildings, mannequins, visitors, working furniture, addon messages, relogging).

## Rollback

`sql/db_world/base/mod_playerhousing_world_rollback.sql` and
`sql/db_characters/base/mod_playerhousing_characters_rollback.sql` remove everything the
module added.
