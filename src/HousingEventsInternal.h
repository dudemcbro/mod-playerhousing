#ifndef MOD_PLAYERHOUSING_EVENTS_INTERNAL_H
#define MOD_PLAYERHOUSING_EVENTS_INTERNAL_H

// Inside mod-playerhousing only: what the module calls to tell the listeners of
// HousingEvents.h. Other modules include HousingEvents.h, never this.

#include "HousingEvents.h"

namespace Housing::Events
{
    // PlayerHousing.Debug.Events: a log line for every event (for tests).
    void SetDebug(bool debug);
    // Someone listens, or the debug log is on: worth reading what a database-only change
    // removes before it goes.
    bool Wanted();

    void IslandOpened(IslandInfo const& island);
    void IslandClosed(IslandInfo const& island);
    void PiecePlaced(ObjectGuid::LowType owner, Placement const& placement);
    void PieceRemoved(ObjectGuid::LowType owner, uint32 placementId);
}

#endif
