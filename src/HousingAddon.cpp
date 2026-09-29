#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>

using namespace Housing;

// The client addon's housing window. Lists go to it as addon whispers with the prefix
// HOUSING, one row a message, so none gets near the client's 255 character limit:
//   begin <kind>, then row <kind> <fields...> for each row, then end <kind>
// Fields are tab separated; text fields have their tabs taken out.

namespace
{
    constexpr size_t ROW_LIMIT = 200;   // characters of list data in one row
    constexpr size_t PLACED_ROWS = 250;
    constexpr size_t RECENT_PIECES = 12;
    constexpr uint32 HISTORY_ROWS = 15;
    constexpr uint32 GUESTBOOK_ROWS = 30;

    std::string Clean(std::string text)
    {
        std::replace(text.begin(), text.end(), '\t', ' ');
        std::replace(text.begin(), text.end(), '\n', ' ');
        return text;
    }

    // Runs of item entries: 901100-901110,902001
    std::vector<std::string> Ranges(std::vector<uint32> entries)
    {
        std::sort(entries.begin(), entries.end());
        std::vector<std::string> parts;
        for (size_t i = 0; i < entries.size();)
        {
            size_t j = i;
            while (j + 1 < entries.size() && entries[j + 1] == entries[j] + 1)
                ++j;
            parts.push_back(j > i ? Acore::StringFormat("{}-{}", entries[i], entries[j]) : std::to_string(entries[i]));
            i = j + 1;
        }
        return parts;
    }
}

void PlayerHousingMgr::SendAddon(Player* player, std::string const& text) const
{
    if (!player || !player->GetSession() || player->GetSession()->IsBot())
        return;

    // A whisper to themselves, always the plain kind: for a GM in GM mode the core would send
    // the GM variant, which the test client (and, it may be, some addons) doesn't read.
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player->GetGUID(), player->GetGUID(), "HOUSING\t" + text, 0);
    player->SendDirectMessage(&data);
}

void PlayerHousingMgr::SendAddonRows(Player* player, std::string const& kind, std::string const& label, std::vector<std::string> const& parts) const
{
    // Comma lists over several rows when long.
    std::string row;
    for (std::string const& part : parts)
    {
        if (!row.empty() && row.size() + part.size() + 1 > ROW_LIMIT)
        {
            SendAddon(player, "row\t" + kind + "\t" + label + "\t" + row);
            row.clear();
        }
        row += (row.empty() ? "" : ",") + part;
    }
    if (!row.empty())
        SendAddon(player, "row\t" + kind + "\t" + label + "\t" + row);
}

void PlayerHousingMgr::SetAddonClient(Player* player, bool keyOpensWindow)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    _addonClients[player->GetGUID()] = keyOpensWindow;
}

bool PlayerHousingMgr::KeyOpensWindow(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _addonClients.find(player->GetGUID());
    return itr != _addonClients.end() && itr->second;
}

bool PlayerHousingMgr::SendAddonData(Player* player, std::string const& kind, std::string const& argument, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();

    if (kind == "collection")
    {
        SendAddon(player, "begin\tcollection");
        SendAddon(player, Acore::StringFormat("row\tcollection\tsettings\t{}\t{}\t{}\t{}\t{}", _freeMode ? 1 : 0, _catalogEverything ? 1 : 0,
            _maxFurnishings, _maxBuildings, _unlockAll ? 1 : 0));

        std::set<uint32> known = LoadUnlocks(player);
        std::vector<uint32> unlocked;
        {
            std::lock_guard<std::recursive_mutex> guard(_lock);
            for (auto const& [itemEntry, piece] : _pieces)
                if (IsUnlocked(player, piece, &known))
                    unlocked.push_back(itemEntry);
        }
        SendAddonRows(player, "collection", "unlocked", Ranges(unlocked));

        std::set<uint32> fresh = LoadNewUnlocks(player);
        SendAddonRows(player, "collection", "new", Ranges(std::vector<uint32>(fresh.begin(), fresh.end())));

        std::vector<std::string> stored;
        for (auto const& [itemEntry, count] : GetStorage(self))
            stored.push_back(Acore::StringFormat("{}:{}", itemEntry, count));
        SendAddonRows(player, "collection", "storage", stored);

        std::vector<std::string> placed;
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT source_item_entry, COUNT(*) FROM mod_playerhousing_placement WHERE owner_guid={} GROUP BY source_item_entry", self))
        {
            do
            {
                placed.push_back(Acore::StringFormat("{}:{}", (*result)[0].Get<uint32>(), (*result)[1].Get<uint64>()));
            } while (result->NextRow());
        }
        SendAddonRows(player, "collection", "placed", placed);

        // The last pieces placed on the player's island, newest first.
        std::vector<std::string> recent;
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT source_item_entry FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={} ORDER BY placement_id DESC LIMIT 80",
                self, _layout.mapId))
        {
            std::set<uint32> seen;
            do
            {
                uint32 itemEntry = (*result)[0].Get<uint32>();
                if (itemEntry && seen.insert(itemEntry).second)
                    recent.push_back(std::to_string(itemEntry));
            } while (result->NextRow() && recent.size() < RECENT_PIECES);
        }
        SendAddonRows(player, "collection", "recent", recent);
        SendAddon(player, "end\tcollection");
        return true;
    }

    if (kind == "placed")
    {
        // The island the player is on, nearest first.
        if (!CanDecorate(player))
        {
            reason = "Go home (or to an island where you're a roommate) to list its pieces.";
            return false;
        }
        std::vector<std::pair<Placement, float>> nearby = GetNearbyPlacements(player, 100000.0f);
        SendAddon(player, "begin\tplaced");
        for (size_t i = 0; i < nearby.size() && i < PLACED_ROWS; ++i)
            SendAddon(player, Acore::StringFormat("row\tplaced\t{}\t{}\t{:.1f}", nearby[i].first.id, nearby[i].first.itemEntry, nearby[i].second));
        SendAddon(player, Acore::StringFormat("end\tplaced\t{}", nearby.size()));
        return true;
    }

    if (kind == "layouts")
    {
        SendAddon(player, "begin\tlayouts");
        SendAddon(player, Acore::StringFormat("row\tlayouts\tlimit\t{}", _maxSavedLayouts));
        for (SavedLayout const& layout : GetSavedLayouts(self))
            SendAddon(player, Acore::StringFormat("row\tlayouts\tlayout\t{}\t{}\t{}\t{}\t{}", layout.id, Clean(layout.name), layout.pieces,
                layout.savedAt, Clean(layout.source)));
        SendAddon(player, "end\tlayouts");
        return true;
    }

    if (kind == "guests")
    {
        SendAddon(player, "begin\tguests");
        for (VisitEntry const& guest : GetGuests(self))
            SendAddon(player, Acore::StringFormat("row\tguests\tguest\t{}\t{}", Clean(guest.ownerName), guest.roommate ? 1 : 0));
        SendAddon(player, "end\tguests");
        return true;
    }

    if (kind == "visits")
    {
        uint8 list = uint8(std::min<uint32>(Acore::StringTo<uint32>(argument).value_or(0), 5));
        SendAddon(player, Acore::StringFormat("begin\tvisits\t{}", list));
        for (VisitEntry const& entry : GetVisitList(player, list))
            SendAddon(player, Acore::StringFormat("row\tvisits\tisland\t{}\t{}", Clean(entry.ownerName), entry.likes));
        SendAddon(player, Acore::StringFormat("end\tvisits\t{}", list));
        return true;
    }

    if (kind == "island")
    {
        EnsureHouse(self);
        HouseRecord house;
        GetHouseRecord(self, house);
        SendAddon(player, "begin\tisland");
        SendAddon(player, Acore::StringFormat("row\tisland\tsettings\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", house.privacy, house.weather,
            house.timeOfDay, house.music, HasMusicBox(self) ? 1 : 0, CountLikes(self), CountVisitorsThisWeek(self), house.hasDoor ? 1 : 0,
            CountNewNotes(self)));
        SendAddon(player, "row\tisland\tgreeting\t" + Clean(house.greeting));
        for (uint8 i = 0; i < WeatherCount(); ++i)
            SendAddon(player, Acore::StringFormat("row\tisland\tweather\t{}\t{}", i, WeatherName(i)));
        for (uint8 i = 0; i < TimeOfDayCount(); ++i)
            SendAddon(player, Acore::StringFormat("row\tisland\ttime\t{}\t{}", i, TimeOfDayName(i)));
        for (auto const& [soundId, name] : MusicTracks())
            SendAddon(player, Acore::StringFormat("row\tisland\tmusic\t{}\t{}", soundId, name));
        SendAddon(player, "end\tisland");
        return true;
    }

    if (kind == "history")
    {
        // Undo and redo, newest first: the addon's list under its Undo button.
        SendAddon(player, "begin\thistory");
        for (std::string const& label : JournalLabels(player, false, HISTORY_ROWS))
            SendAddon(player, "row\thistory\tundo\t" + Clean(label));
        for (std::string const& label : JournalLabels(player, true, HISTORY_ROWS))
            SendAddon(player, "row\thistory\tredo\t" + Clean(label));
        SendAddon(player, "end\thistory");
        return true;
    }

    if (kind == "sets")
    {
        SendAddon(player, "begin\tsets");
        SendAddon(player, Acore::StringFormat("row\tsets\tlimit\t{}", GetMaxSavedSets()));
        for (SavedSet const& set : GetSavedSets(self))
            SendAddon(player, Acore::StringFormat("row\tsets\tset\t{}\t{}\t{}\t{}", set.id, Clean(set.name), set.pieces, set.savedAt));
        SendAddon(player, "end\tsets");
        return true;
    }

    if (kind == "guestbook")
    {
        // The owner reading it marks the notes read.
        SendAddon(player, "begin\tguestbook");
        for (GuestbookNote const& note : GetGuestbook(self, GUESTBOOK_ROWS))
            SendAddon(player, Acore::StringFormat("row\tguestbook\tnote\t{}\t{}\t{}\t{}\t{}", note.id, Clean(note.author), note.when,
                note.fresh ? 1 : 0, Clean(note.text)));
        SendAddon(player, "end\tguestbook");
        MarkGuestbookRead(self);
        return true;
    }

    reason = "Usage: .house data <collection|placed|layouts|guests|visits <list>|island|history|sets|guestbook>";
    return false;
}

bool PlayerHousingMgr::TakeFromStorageCommand(Player* player, std::string const& what, uint32 count, std::string& reason)
{
    if (what == "all")
        return TakeFromStorage(player, 0, true, reason);
    uint32 itemEntry = Acore::StringTo<uint32>(what).value_or(0);
    if (!itemEntry)
    {
        reason = "Usage: .house take <item entry|all> [count]";
        return false;
    }
    return TakeFromStorage(player, itemEntry, false, reason, count);
}

void PlayerHousingMgr::MarkAllSeen(Player* player) const
{
    std::set<uint32> fresh = LoadNewUnlocks(player);
    MarkSeen(player, std::vector<uint32>(fresh.begin(), fresh.end()));
}
