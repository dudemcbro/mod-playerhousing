# API for other modules

Since 1.1.0, other AzerothCore modules can follow what happens on housing islands: when an
island opens and closes, and when a piece is placed on it or taken off it. They can also ask
for an island's phase, its bounds and the pieces on it. Everything here is in
`src/HousingEvents.h` (which includes `src/PlayerHousingMgr.h`).

## Compatibility

1.1.0 is a minor version: the API is new and nothing that was there before changed (no
setting, table, command or addon message). What this page lists is kept, with the same
meaning, through every 1.x release; later additions are only ever new members, new
virtual methods with empty defaults, or new fields at the end of `IslandInfo`. A module that
needs the API checks for the header at build time (for example
`if(EXISTS "${CMAKE_SOURCE_DIR}/modules/mod-playerhousing/src/HousingEvents.h")` in its
CMake, defining a macro, or `#if __has_include("HousingEvents.h")` in the source), so it
still builds without the housing module.

## Listening

```cpp
#include "HousingEvents.h"

class StableListener : public Housing::HousingListener
{
public:
    void OnIslandOpened(Housing::IslandInfo const& island) override;
    void OnIslandClosed(Housing::IslandInfo const& island) override;
    void OnPiecePlaced(ObjectGuid::LowType owner, Housing::Placement const& placement) override;
    void OnPieceRemoved(ObjectGuid::LowType owner, uint32 placementId) override;
};

// In a script's constructor or WorldScript::OnStartup:
Housing::RegisterListener(&listener);
```

- `RegisterListener` is idempotent: a listener registered twice is called once.
  `UnregisterListener` may be called at any time, also from inside a callback; a listener
  taken off is not called again, not even for the event being delivered. When a callback of
  it is running on another thread, `UnregisterListener` waits for that call to return, so
  once it has returned the listener may be destroyed. A call running on the unregistering
  thread itself (taking a listener off from inside a callback) is not waited for. Don't call
  it while holding a lock that the listener's callbacks wait for.
- Callbacks run on the thread that made the change: the island map's update thread for what
  players and GMs do there, the world thread at startup and when a character is deleted for
  good, and the thread that logs the last players out at shutdown. The housing lock is held
  during the call: a listener may call the getters below, but must not wait on another
  thread that might need the housing lock, and must not throw.
- `owner` is always the house owner, the account's home character (an island belongs to an
  account and is kept under one character; `PlayerHousingMgr::HomeOfCharacter` finds it).

### The events

| Event | When |
| --- | --- |
| `OnIslandOpened` | The island's session spawned its objects for the first time: the first person arrived. A reload of an island that is already open (the owner stops decorating, the grid comes back) is not a new opening. |
| `OnIslandClosed` | The last person left, or the owner was deleted, and the session's objects were despawned. Only for an island that was opened. At server shutdown each island still open closes as its last players are logged out, which happens after `WorldScript::OnShutdown`: a listener that doesn't want those unregisters in its `OnShutdown`. |
| `OnPiecePlaced` | A piece was placed: from the Collection, a ghost set down, Place another, a row, a saved set or layout, undo of a pick-up or redo of a placement, the starter wreckage of a new island, and the pieces of an island that moves to another of the account's characters (placed under the new owner). |
| `OnPieceRemoved` | A piece was taken away: picked up (with what's inside), pack up, undo of a placement or redo of a pick-up, loading a layout that replaces it, a GM's pack up, pieces going back to roommates, a character deleted for good, two islands of an account merged, an island moving to another character (removed from the old owner), and the one-time clearing of GM Island's guild hall. |

Each real change is told exactly once. Moving, turning, tilting, resizing, re-parenting,
changing a mannequin's looks or gear, and the decorating copies of pieces are not placements
or removals and send nothing; read the pieces again with `GetPlacements` if they matter.
Placed and removed events also come for islands that aren't open (a GM's pack up, a deleted
character, accounts merged): a listener that keeps state per open island should ignore
those, and read `GetPlacements` when the island opens. A new island's starter wreckage is
placed just before its first `OnIslandOpened`. Only pieces the island would show are told
about: on the layout's map, with a piece still defined. Converting pre-1.0 data at startup
sends nothing, except the one-time clearing of GM Island's guild hall, which sends
`OnPieceRemoved` for each piece it takes away (on the world thread, to listeners already
registered then, such as from a script constructor).

## Getters (`sPlayerHousingMgr`)

| Member | Returns |
| --- | --- |
| `uint32 GetHousePhase(owner) const` | The owner's island phase: `0x80000000 \| (owner & 0x3FFFFFFF) << 1`. Each value is an ID of its own, not a set of bits (see How it works). |
| `bool IsIslandOpen(owner) const` | Between its `OnIslandOpened` and `OnIslandClosed`. |
| `std::vector<ObjectGuid::LowType> GetOpenIslands() const` | Every open island's owner. |
| `bool GetIslandInfo(owner, Housing::IslandInfo& out) const` | Phase, map, centre and radius (yards) of the island; false if the owner has no house. The layout has no height: `centerZ` is the landing spot's. Reads the database when the island isn't open. |
| `std::vector<Housing::Placement> GetPlacements(owner) const` | The pieces on the island now, with their position, size, tilt, parent and mannequin gear: from its session while it's loaded, otherwise read from the database. |

## Testing

`PlayerHousing.Debug.Events = 1` logs every event as it is sent (to the `module` log), for
headless tests:

```
HousingEvents: opened owner=<n> phase=<p> map=<m>
HousingEvents: closed owner=<n> phase=<p> map=<m>
HousingEvents: placed owner=<n> placement=<id> item=<entry>
HousingEvents: removed owner=<n> placement=<id>
```
