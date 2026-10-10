# Design

How Player Housing is meant to feel, and why it works the way it does. Modeled on Lord of
the Rings Online and Final Fantasy XIV housing, without the neighborhoods.

## Ground rules

These came first and every feature follows them.

- **Nothing is ever lost.** Every placement can be undone, and picking a piece up gives it
  back. Undo covers everything: placing, moving, turning, tilting, resizing, packing up,
  setting out a layout. Nothing asks "are you sure" before something Undo can put back.
- **The island starts empty.** GM Island's guild hall is removed, so everything on an island
  is the owner's own.
- **No house levels.** What a player can place grows as they play the game: exploring,
  dungeons, raids, reputation, professions, holidays and achievements. Nothing is bought to
  unlock it.
- **Homes are built from pre-made buildings.** Everyone starts with wreckage (a broken cart,
  a shredded tent) and works up to grand halls; a faction's building needs Exalted with it.
- **One way to do each thing.** No second system for the same action: one way to place, one
  way to move, one window.
- **The mouse does the placing.** Everything about a held piece (where, turn, height, tilt,
  size) is the mouse and its wheel with modifiers. The window lists and finds things; it
  doesn't steer pieces.
- **Playerbots never get housing.**

## Key decisions

| Decision | Why |
| --- | --- |
| **One island per account**, shared by all its characters | Players think of "my house", not one per alt. Unlocks are shared too, except faction buildings, which need Exalted on the character placing them. |
| **Owned pieces are counts in the Collection**, not bag items | Bags fill up, items get vendored or deleted by accident, and moving dozens of chairs through the mail is no fun. A count can't be lost, and picking up gives it straight back. |
| **Unlocking gives one copy; extra copies cost a little gold** | Earning a piece means something; a second chair shouldn't need a second dungeon run. |
| **The client addon is required** | The stock 3.3.5a client has no way to show a collection of hundreds of pieces, previews, or a window that updates as you build. The first versions tried gossip menus and chat; they were slow and confusing. |
| **PlayerHousing.dll for the mouse (recommended, not required)** | The 3.3.5a client can't tell an addon where the mouse points in the world. A small DLL adds that one function; without it, a held piece floats ahead of the player instead. |
| **See-through ghosts** while holding a piece | You see exactly what you'll get, where, before setting it down. Ghosts are creatures with see-through models from the client patch; world-model buildings can't be creatures, so they show as their real object. |
| **Islands are phases of GM Island** | Cheap and simple at the sizes tested (100 occupied islands, 6,000 pieces). Past a few hundred busy islands this stops scaling; islands as instances are the fix (see the roadmap). |
| **Krook hands out House Keys** beside every capital's innkeeper | A place to start that players already visit, and an easy answer to "I lost my key". The key is the way home. |
| **Mannequins wear real gear** | The item leaves the bags while on display, with its enchants and gems, and comes back as the same item. Trade gear swaps a whole outfit in one go. |
| **Mannequins stand; poses wait for 2.0** | A frozen pose has to look the same for every viewer. Freezing an animation catches it at a different frame for each client, and many poses only exist for some races. |
| **The Placed list rings the chosen piece** | With several of the same piece, you need to know which one Move or Put away acts on. |

## What a player learns

1. Krook, at any capital's inn, gives a House Key.
2. The key opens the housing window; Go home goes to the island.
3. Click a piece in the Collection: it follows the mouse; a click sets it down.
4. Right-click a placed piece to pick it up again.
5. Undo fixes anything.

Krook's welcome tour walks through the first four and ends with inviting a guest. Everything
else (layouts, sets, mannequins, visitors, roommates, weather and music) is found in the
window's tabs when a player is ready for it.

## Unlocks

Each piece has rules: a level, an achievement, a reputation rank, a quest, a kill, exploring
an area, or a skill level. Rules in a group must all be met; any complete group unlocks the
piece. Achievements come first wherever one fits, because players already see and track
them. Past progress counts: a character that did something before the module was installed
unlocks it at the next login. The full list is in [UNLOCKS.md](UNLOCKS.md).

## Where it's going

The README's Future features list, and [ROADMAP.md](ROADMAP.md) for the details.
