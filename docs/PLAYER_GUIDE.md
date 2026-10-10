# Player guide

Everything a player does with housing, from getting a House Key to sharing an island. Getting the game ready first: [PLAYER_SETUP.md](PLAYER_SETUP.md).

## Getting started

- Find **Krook** beside the innkeeper in any capital (Stormwind, Ironforge, Darnassus, the
  Exodar, Orgrimmar, Thunder Bluff, the Undercity, Silvermoon, Shattrath and Dalaran) and
  ask for a house: he gives you a **House Key**. The first key on an account comes with a
  chair, table and lantern in the shared Collection; your other characters ask him for
  their own key and share the same island.
- Right-click the House Key (or type `.house`, or `/housing`) for the housing window. **Go
  home** takes you to your island (it needs the key). A fallen cart and a shredded tent are
  waiting.
- Lost the key? Krook has another, at any innkeeper or on your island.

## Krook's welcome tour

Krook (in every capital beside the innkeeper, and on your island) offers five short
quests that walk you through housing, each done the moment you do the thing:

1. **Home Sweet Island**: use your House Key to go home.
2. **Making It Yours**: place a furnishing.
3. **A Fresh Look**: right-click a piece and set it down somewhere new.
4. **Nothing Is Ever Lost**: undo a change.
5. **Open House**: invite a guest, or open your island to friends or everyone.

Each gives a little silver; the last unlocks **Krook's Picnic Basket**. Skipping them costs
nothing. Once the tour is done, Krook leaves your island; **Call Krook** (Island tab) brings
him back.

## Placing things

Click a piece in the Collection tab and it goes on your mouse: a see-through copy of its
model (world-model buildings show as their real, solid object). With
[PlayerHousing.dll](../client-dll/README.md) it follows the mouse over the world: on the
floor, on a table top, on a wall facing out, or hanging under a ceiling. A left-click sets
it down (Shift-click: then another of the same), a right-click or Escape puts it back.
Without the DLL the game can't say where the mouse points, so it floats a couple of yards
ahead of you: walk it where it goes, the arrow keys push it about, and G sets it down. While
you hold a piece the housing window steps aside; it comes back when you set the piece down
or put it back.

Everything about the held piece is the mouse wheel with a modifier (the plain wheel still
zooms the camera): see [the table below](ADDON.md). Nothing is used until it is set
down: it takes a copy you own, or buys a new unlocked copy at that moment.

You can place anywhere on your island, indoors or out, up to the beach. The only rules: it
has to be on your island, and each island holds up to 200 furnishings and 10 buildings
(both set in the config). Swim too far out and you're brought back to the beach.

## Changing things

- With the housing window open, **right-click any furnishing or building** to put it back
  on the mouse, where it can be moved, turned, tilted and resized like a new piece. What
  stands on it (a lantern on a table, the furniture in a building) comes along, and one
  undo puts it all back.
- **Several at once**: Ctrl-right-click pieces to select them, then right-click one of them
  to pick them all up together. **Save as a set** keeps them, to set down anywhere later.
- **The Placed tab** lists every piece, nearest first. Click one and it gets a green ring
  on the island, so you know which of several alike it is; Move, Go (walk to it) and Put
  away are beside it.
- **Grid** (Shift+middle-click while holding, or `.house grid 1`): pieces land on a grid.
- **Undo** and **Redo** are at the top of the window; their tooltips say what they'd undo,
  and right-clicking Undo lists the recent changes to undo back to any of them.
- Shift-right-click a held piece to put it away in the Collection.
- **Pack up all** (Island tab) returns every piece at once, and can be undone too.
- Closing the window puts everything back to normal: chairs can be sat on, mailboxes and
  crafting stations work.

## Figurines

The Collection's **Figurines**: 21 trophies, from a Kobold and Hogger to Onyxia, Illidan
and the Lich King. Each is the creature's own model, shrunk to fit on a table (about a
yard long) and frozen mid-pose. Defeat the boss to unlock it, or hold its dungeon or raid
achievement, so past victories count. Figurines go on tables like other small pieces,
resize like anything else, and don't tilt. Visitors who click one learn whose trophy it
is.

## The Bank Chest

Unlocked at level 20 (or by buying 7 bank slots). Place it anywhere, and click it when
you're not decorating: your own bank opens, right there. It works the way a banker does:
only near the chest, for a few minutes after you open it. Visitors find it locked.

## Weather, time of day and music

The Island tab. Each island keeps its own:

- **Weather**: clear, fog, light rain, rain, thunderstorm, light snow, snow, blizzard or
  sandstorm.
- **Time of day**: the server's clock, or always dawn, midday, dusk or night.
- **Music**: place a **Music Box** (unlocked at level 10) and click it to pick a tune:
  the capital cities, taverns, Dalaran, Nagrand, Grizzly Hills and more.

Everyone on the island sees and hears them, visitors included, and nobody else does.
Leaving puts back the real clock and the weather where you land.

## Saved layouts

The Layouts tab (or `.house layout`). Save your island as it is, try something new, and set
the old one back out whenever you like. Each island keeps up to 5 (set in the config).

- **Set it out** packs up the island and places the layout: every piece where it stood,
  turned, sized and tilted the same, lanterns back on their tables. It uses your own
  pieces; ones you don't have are left out, and the message says which. One undo puts the
  island back as it was.
- A layout counts what's missing and gets the ones you've unlocked in one click, and says
  how many are still locked.
- **Send a copy** to someone in your party or guild, or a friend who has you on their list.
  With **Visitors may copy my layout** ticked, anyone visiting can save a copy (Visit tab,
  Copy). Layouts hold no items: whoever sets one out places their own.
- Mannequins come back bare: their gear goes to your bags when the island is packed up.

## Mannequins: show off your gear

The Mannequin (in everyone's starter set) is a stand for armor and weapons. Place it like
any piece, then right-click it for its character sheet:

- **Drag gear** from your bags onto it (or onto a slot) to put it on; the item leaves your
  bags while it's on display, enchants and gems included. Putting something on an occupied
  slot swaps them.
- **Drag it off**, or right-click a slot, to take it back to your bags (Krook mails it to
  you if your bags are full). **Take all off** empties it.
- **Trade gear**: what the mannequin wears goes on you, and what you wear goes on it, in
  one go. Anything you can't wear goes to your bags.
- **Race**, **Man** or **Woman**, and **New look**: every playable race, with a random face,
  skin and hair a character could be made with.
- **Move** puts it on the mouse like any piece.
- Mannequins stand still in a plain standing pose (poses are planned, see
  [Future features](../README.md#future-features)).
- Visitors can click it to see what it's wearing, but can't change anything.

Rings, necklaces, trinkets and relics don't show on a body, so they aren't offered.

## The Collection

The Collection tab. It lists every piece by category with your progress, for example
"Buildings (6/49, 2 new)". Click an unlocked piece and it goes on your mouse at once (a new
copy is paid for only when you set it down, so putting it back costs nothing); its details
show how many you own and have placed, with Get 1 and Get 5 (free with FreeMode, a small
gold cost otherwise). A locked piece
tells you how to earn it, with your progress so far
("Reach level 20 (you're level 15)", "Exalted with Stormwind (you're Revered)").

- **Search by name** (or `.house collection lamp`) finds pieces in every category,
  locked ones included, so you can see what's out there.
- Pieces you've unlocked but not looked at yet are marked **(new)**, and the Collection
  counts them. Leaving the tab clears the marks.
- **Sort** by the Collection's order, name, cost, how many you own, or what unlocks them.
- **Showing all pieces / unlocked only**: hide what you haven't earned yet, for a shorter
  list of what you can place right now.
- Unlocks happen the moment you earn them, with a message.
- Things you did before the module was installed count: they unlock at your next login, one
  copy each in the Collection. That covers level, achievements, reputation, finished quests,
  explored areas and profession skill. Boss kills on their own don't, because the server keeps
  no record of past kills; every boss figurine also unlocks from that dungeon's or raid's
  achievement, so earlier victories still count that way.
- Unlocks are shared by all your characters, except faction buildings, which need Exalted
  with their faction on the character that places them.

## Visitors

The Island and Guests tabs:

- **Privacy**: Private (only you and your guests), Friends & guild, or Public.
- **Guest list**: invite by name, your target, or your whole party. Guests can always
  visit, whatever the privacy setting. They're told when you invite them.
- **Greeting**: a message every visitor sees when they arrive.

The Visit tab lists the islands of your party, guild and friends, the ones you're invited
to, public ones and the most liked; or visit someone by name. Only islands you're allowed
into are shown, so every entry works with one click. Visitors can use chairs and stations
but can't change anything. The owner is told when someone arrives.

## Likes and the visitor log

- Visitors can **like** an island from the Visit tab (one like per account, so alts don't
  count twice), and take it back. The owner is told.
- The Visit tab's **Most liked** list shows the islands you may enter, most liked first.
- The Island tab's **Visitor log**: the last visitors with the date and time, how many
  came this week, and your likes. Coming home, Krook says how many visits there were
  since you were last there.
- **The guestbook**: visitors sign it from the Visit tab or with `.house sign <note>`, once
  a day per island (and five notes an hour per account). Coming home, you're told how many
  new notes there are; the Guests tab's second page lists them, and you can throw one out.
  The last 100 notes are kept.
- **The door**: Island tab, Door here: visitors (roommates too)
  arrive there, facing the way you faced, instead of at the landing spot, until you set it
  back.

## Roommates

The Guests tab: make a guest a **roommate** (or `.house roommate <name>`). A roommate can decorate your island with you: place their own pieces, and move,
turn, resize or pick up yours, with their own undo. What they can't do: pack up the
island, set out a layout, change your settings, or dress a mannequin that isn't theirs.

Every piece remembers who placed it and goes back to them when picked up: a roommate's
piece you pick up goes back to their Collection (and undo takes it back out), and yours
to yours. Gear on a roommate's mannequin goes back to them by mail. "Make them a
guest only" (or `.house unroommate <name>`) ends it; their pieces stay where they are.
