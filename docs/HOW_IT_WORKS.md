# How it works

How the module is built: islands and phases, pieces, ghosts, undo, mannequins, the safety rules, and measured load.

- **One island per account.** Every character on an account shares it
  (`mod_playerhousing_account` names the character it's kept under). GM Island (Kalimdor) is
  shared by everyone, and phasing gives each account a private copy. A house phase has bit 31 set and bit 0 clear, and the
  module turns off the core's "any shared bit" phase matching for it
  (`GLOBALHOOK_ON_BEFORE_WORLDOBJECT_SET_PHASEMASK`), so each phase value is its own ID.
  The owner's guests share the owner's phase.
- An island's pieces are spawned when the first person arrives and removed when the last
  one leaves. Anyone found on the island without going through the House Key (other than
  GMs) is sent back where they came from. Logging out on your own island logs you back in
  there; logging out on someone else's brings you back where you came from.
- **Pieces.** Each piece has an item entry (901100 to 901199 and 902001 to 902999; the House
  Key is 902000) for its name and icon, but what a player owns is a count in the Collection
  (`mod_playerhousing_storage`), not a bag item. Its object is `910000 + (item - 900000)`
  normally, and `920000 + (item - 900000)` while decorating: a clickable copy of chairs and
  stations, so a right-click picks the piece up instead of using it (world-model buildings
  have none and are picked up from the Placed tab). Buildings stay visible from farther away.
  Housing objects have server-side collision turned off.
- **Sizes and outlines** come from the game data: small models from
  `GameObjectDisplayInfo.dbc`, buildings made of world models from the server's collision
  data (`vmaps/GameObjectModels.dtree`). A building's outline, turned the way it faces, is
  what counts as inside it when picking it up with what's inside.
- **Placement** follows the mouse: with PlayerHousing.dll the addon asks the game where the
  mouse points and which way the surface there faces, and sends it to the server
  (`.house ghost at x y z [nx ny nz]`). The server decides where the piece goes (a table
  top, a wall, a ceiling, the ground under a building) and tells the addon, which moves the
  ghost itself between the server's updates.
- **Ghosts** (`src/HousingGhosts.cpp`): a held piece is shown as a see-through copy of
  itself. Furniture's ghost is a creature (entry 900203) with a see-through model of the
  piece: a `CreatureModelData` and `CreatureDisplayInfo` row per piece model (display 60000
  plus the item's offset from 900000, opacity 150), which the content builder writes for the
  server (`creaturemodeldata_dbc`, `creaturedisplayinfo_dbc`) and the client patch adds to
  the client. Figurines and the mannequin get a see-through copy of their creature's display.
  Buildings with an M2 model use their own see-through model the same way; world-model
  (`.wmo`) buildings can't be a creature's model, so they're carried as their real,
  collisionless object, and so is a tilted piece (a creature can't lean). With
  PlayerHousing.dll the addon sends where the mouse points, and which way the surface there
  faces (`.house ghost at x y z [nx ny nz]`), up to ten times a second over AzerothCore's
  addon command channel (prefix `AzerothCore`), which chat's flood limit doesn't count; the
  server keeps its own limit of 25 a second. The server works out where the piece goes (the
  grid, a table top, a wall, a ceiling, the ground under a building) and tells the addon,
  which moves the ghost on the client between the server's answers, so it keeps up with the
  mouse. Without the DLL the ghost follows the player, guessed ahead of their last movement
  packet and redrawn every tenth of a second.
- **Undo** keeps each change as the before and after of the pieces it touched, so undo and
  redo replay them exactly, handing items back or taking them as needed. Each player has
  their own list, in memory, cleared when they leave the island. A step only applies to
  the very pieces it was written for (see Safety and limits).
- **Mannequins** are creatures (entry 900201) with the mirror image flag, frozen still, the way the
  Mirror Image spell works: the client asks what the figure wears and the module answers
  (a `ServerScript` catching `CMSG_GET_MIRRORIMAGE_DATA`), while weapons are virtual items.
  Gear on a stand leaves the inventory but stays in `item_instance`, the way mail keeps
  items, with a row in `mod_playerhousing_placement_gear`; so enchants, gems and the item's
  guid survive, and undo returns the same item. Race, gender and features (skin, face,
  hair, hair color, facial hair) are packed into the placement's `look`. Deleting a character for good deletes
  its own stand gear; gear roommates left on its island is mailed back to them first.
  The characters rollback mails any gear still on stands back to its owners.

## Safety and limits

Nothing on an island can be duplicated or lost:

- Every piece placed takes one from the Collection's count, and every piece picked up gives
  one back.
- An undo or redo step only applies to the pieces it was written for. If someone else has
  since picked one up or replaced it, the step is dropped with a message. Placement ids
  are never handed out twice while anyone is on the island, and a GM packing up an island
  or its owner being deleted clears every undo list that points at it.
- Gear on a mannequin is only ever changed by the player it belongs to; anyone else's undo
  moves the stand and leaves what it wears alone.
- When a character is deleted, pieces roommates placed on its island go back to the
  roommates' Collections and their mannequin gear comes by mail. If it was the character
  the account's island was kept under, the island moves to another character on the account. The island itself stays until the
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
  same other player (an invite, roommate news, a like, a guestbook note). Five reports an
  hour per account; guestbook notes as above.

Player text (greetings, layout names, set names, guestbook notes, reports) is escaped for SQL, stripped of control
characters and link codes, and cut to length without splitting a character. `nan` and `inf`
aren't accepted as numbers. Players who aren't on an island cost the module one check per
update, without taking its lock, and the visit lists look up guests and friends once per
list rather than once per island.

## Load

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

Then with everyone at home and their pieces out (`--hold 60`; `--walk` has them run in
circles, as players moving about do):

| Islands open | Pieces out | Players | Server update: mean, p99 |
| --- | --- | --- | --- |
| none | 0 | | 8 ms, 39 ms |
| 100 | 2000 | standing | 8 ms, 53 ms |
| 100 | 2000 | running about | 9 ms, 56 ms |
| 50 | 3000 | running about | 9 ms, 51 ms |
| 100 | 6000 | standing | 8 ms, 51 ms |
| 100 | 6000 | running about | 19 ms, 195 ms |

Placing those 6000 pieces (100 players at once, as fast as the test can click) ran at
29 ms mean, 424 ms p99.

## Thousands of players

Every island is at the same spot on the same map (GM Island, on Kalimdor), told apart
only by phase. That is cheap at the sizes above, and what limits it past them:

- All open islands share one map thread, the one that also runs the rest of Kalimdor.
  `MapUpdate.Threads` can't spread them.
- Pieces left out cost next to nothing while their owners stand still. But whenever a
  player moves, the server looks again at everything within sight to decide what to show,
  and that includes every other island's pieces: hidden by phase, but still looked at. In a
  profile of the 100 island, 6000 piece run, about half the map thread was there
  (`PlayerRelocationNotifier`). The cost grows with the players moving about at home
  times the pieces out on all islands.
- Housing commands run on the world thread, one after another, and most do a few
  database reads and writes while they wait.

A rough extrapolation from the one cost that grows: 300 islands of 60 pieces each (a few
thousand players online, 10% of them at home) is about 9 times the 6000 piece run, around
100 ms more a tick for everyone on Kalimdor; 1000 islands open is past what one map
thread can do. The fix is islands as instances: a map of their own (a Map.dbc row and a
WDT listing GM Island's tiles, added to the client patch, with the server's maps, vmaps
and mmaps extracted for it), so each island only looks at its own pieces and islands
update in parallel. Until then, a lower `PlayerHousing.MaxFurnishings` keeps the cost
down on busy servers.
