#ifndef MOD_PLAYERHOUSING_EVENTS_H
#define MOD_PLAYERHOUSING_EVENTS_H

// The public events of mod-playerhousing, for other modules (added in 1.1.0; see
// docs/API.md). A module that wants to know when an island opens or closes, or when a piece
// is placed on one or taken off it, registers a HousingListener.
//
// Housing::Placement is the module's own placement record (PlayerHousingMgr.h, included
// here), so an includer needs nothing else. The pinned interface is used as it is.
//
// Every callback runs on the thread that made the change: the island map's update thread
// for anything a player or a GM does there, the world thread for startup and for a character
// deleted for good, and the thread that logs the last players out at shutdown (after
// WorldScript::OnShutdown). The housing lock is held during the call, so a listener may call the
// PlayerHousingMgr getters (GetPlacements and the rest) but must not wait on another thread
// that might want that lock. A listener must not throw.

#include "PlayerHousingMgr.h"

namespace Housing
{
    struct IslandInfo
    {
        ObjectGuid::LowType owner = 0;   // house owner (home character) low guid
        uint32 phaseMask = 0;            // the island phase
        uint32 mapId = 0;
        float centerX = 0, centerY = 0, centerZ = 0;
        float radius = 0;                // layout radius in yards
    };

    class HousingListener
    {
    public:
        virtual ~HousingListener() = default;
        virtual void OnIslandOpened(IslandInfo const& /*island*/) {}   // first object spawn of an island session
        virtual void OnIslandClosed(IslandInfo const& /*island*/) {}   // after the session objects despawn
        virtual void OnPiecePlaced(ObjectGuid::LowType /*owner*/, Placement const& /*placement*/) {}
        virtual void OnPieceRemoved(ObjectGuid::LowType /*owner*/, uint32 /*placementId*/) {}
    };

    void RegisterListener(HousingListener* listener);    // idempotent; call from a module's script constructor or OnStartup
    // Safe at any time, also from inside a callback. Waits for a callback of the listener
    // running on another thread, so the listener may be destroyed once this returns; don't
    // call it holding a lock that the listener's callbacks wait for.
    void UnregisterListener(HousingListener* listener);
}

#endif
