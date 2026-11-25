#include "HousingEventsInternal.h"

#include "Log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <vector>

// The listeners of HousingEvents.h. Registering happens at startup, the events come from
// map threads: the list has a lock of its own, never held while a listener runs. Each event
// goes to a copy of the list, and a listener taken off meanwhile (by itself or another) is
// skipped. A listener being called is counted as in flight, and UnregisterListener waits
// until no other thread is still in one of its callbacks, so once it returns the listener is
// not called again and may be destroyed. Calls on the unregistering thread itself (a
// listener taking itself or another off from inside a callback) are not waited for.

namespace
{
    struct ListenerState
    {
        std::mutex lock;
        std::condition_variable idle;
        std::vector<Housing::HousingListener*> listeners;
        std::unordered_map<Housing::HousingListener*, uint32> inFlight;   // calls running now, all threads
    };

    ListenerState& State()
    {
        static ListenerState state;
        return state;
    }

    // The listeners this thread is inside the callbacks of, innermost last.
    thread_local std::vector<Housing::HousingListener*> tCalling;

    std::atomic<bool> debugLog{false};

    bool IsRegisteredLocked(ListenerState& state, Housing::HousingListener* listener)
    {
        return std::find(state.listeners.begin(), state.listeners.end(), listener) != state.listeners.end();
    }

    // Marks one call of a listener as running, for as long as it lives.
    class InFlight
    {
    public:
        explicit InFlight(Housing::HousingListener* listener) : _listener(listener)
        {
            tCalling.push_back(listener);
        }

        ~InFlight()
        {
            tCalling.pop_back();
            ListenerState& state = State();
            {
                std::lock_guard<std::mutex> guard(state.lock);
                auto it = state.inFlight.find(_listener);
                if (it != state.inFlight.end() && --it->second == 0)
                    state.inFlight.erase(it);
            }
            state.idle.notify_all();
        }

        InFlight(InFlight const&) = delete;
        InFlight& operator=(InFlight const&) = delete;

    private:
        Housing::HousingListener* _listener;
    };

    template <typename Call>
    void Notify(Call const& call)
    {
        ListenerState& state = State();
        std::vector<Housing::HousingListener*> listeners;
        {
            std::lock_guard<std::mutex> guard(state.lock);
            if (state.listeners.empty())
                return;
            listeners = state.listeners;
        }
        for (Housing::HousingListener* listener : listeners)
        {
            {
                std::lock_guard<std::mutex> guard(state.lock);
                if (!IsRegisteredLocked(state, listener))
                    continue;
                ++state.inFlight[listener];
            }
            InFlight running(listener);
            call(listener);
        }
    }
}

namespace Housing
{
    void RegisterListener(HousingListener* listener)
    {
        if (!listener)
            return;
        ListenerState& state = State();
        std::lock_guard<std::mutex> guard(state.lock);
        if (!IsRegisteredLocked(state, listener))
            state.listeners.push_back(listener);
    }

    void UnregisterListener(HousingListener* listener)
    {
        if (!listener)
            return;
        ListenerState& state = State();
        std::unique_lock<std::mutex> guard(state.lock);
        state.listeners.erase(std::remove(state.listeners.begin(), state.listeners.end(), listener), state.listeners.end());
        uint32 const own = uint32(std::count(tCalling.begin(), tCalling.end(), listener));
        state.idle.wait(guard, [&]
        {
            auto it = state.inFlight.find(listener);
            return it == state.inFlight.end() || it->second <= own;
        });
    }
}

namespace Housing::Events
{
    void SetDebug(bool debug)
    {
        debugLog = debug;
    }

    bool Wanted()
    {
        if (debugLog)
            return true;
        ListenerState& state = State();
        std::lock_guard<std::mutex> guard(state.lock);
        return !state.listeners.empty();
    }

    void IslandOpened(IslandInfo const& island)
    {
        if (debugLog)
            LOG_INFO("module", "HousingEvents: opened owner={} phase={} map={}", island.owner, island.phaseMask, island.mapId);
        Notify([&](HousingListener* listener) { listener->OnIslandOpened(island); });
    }

    void IslandClosed(IslandInfo const& island)
    {
        if (debugLog)
            LOG_INFO("module", "HousingEvents: closed owner={} phase={} map={}", island.owner, island.phaseMask, island.mapId);
        Notify([&](HousingListener* listener) { listener->OnIslandClosed(island); });
    }

    void PiecePlaced(ObjectGuid::LowType owner, Placement const& placement)
    {
        if (debugLog)
            LOG_INFO("module", "HousingEvents: placed owner={} placement={} item={}", owner, placement.id, placement.itemEntry);
        Notify([&](HousingListener* listener) { listener->OnPiecePlaced(owner, placement); });
    }

    void PieceRemoved(ObjectGuid::LowType owner, uint32 placementId)
    {
        if (debugLog)
            LOG_INFO("module", "HousingEvents: removed owner={} placement={}", owner, placementId);
        Notify([&](HousingListener* listener) { listener->OnPieceRemoved(owner, placementId); });
    }
}
