#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"

using namespace Housing;

// Moderation: players report islands; GMs read the reports, look for themselves, and can
// clear a greeting, hide an island from strangers, or pack it up. Packing up never loses
// anything: every piece goes to the House Storage of whoever placed it, and gear on
// mannequins is mailed back.

namespace
{
    std::string CleanText(std::string const& text, size_t maxLength)
    {
        std::string clean;
        for (char c : text)
            if (static_cast<unsigned char>(c) >= 32 && c != '|' && c != 127)
                clean += c;
        size_t begin = clean.find_first_not_of(' ');
        if (begin == std::string::npos)
            return "";
        clean = clean.substr(begin, clean.find_last_not_of(' ') - begin + 1);
        return clean.size() > maxLength ? clean.substr(0, maxLength) : clean;
    }
}

bool PlayerHousingMgr::ReportIsland(Player* reporter, std::string const& text, std::string& reason)
{
    ObjectGuid::LowType owner = GetIslandOwner(reporter);
    if (!owner || owner == reporter->GetGUID().GetCounter())
    {
        reason = "Visit the island you want to report, then report it from there.";
        return false;
    }

    std::string why = CleanText(text, 200);
    if (why.empty())
    {
        reason = "Say what's wrong, so a GM knows what to look for.";
        return false;
    }

    // One open report per account per island.
    uint32 account = reporter->GetSession()->GetAccountId();
    if (CharacterDatabase.Query("SELECT 1 FROM mod_playerhousing_report WHERE owner_guid={} AND reporter_account={} AND closed_at IS NULL",
            owner, account))
    {
        reason = "You've already reported this island. A GM will look at it.";
        return false;
    }

    std::string escaped = why;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.DirectExecute(
        "INSERT INTO mod_playerhousing_report (owner_guid, reporter_guid, reporter_account, reason) VALUES ({}, {}, {}, '{}')",
        owner, reporter->GetGUID().GetCounter(), account, escaped);

    std::string ownerName = NameOf(owner);
    ChatHandler(reporter->GetSession()).SendGlobalGMSysMessage(Acore::StringFormat(
        "Housing: {} reported {}'s island: {} (.house reports)", reporter->GetName(), ownerName, why).c_str());
    reason = Acore::StringFormat("Thanks. {}'s island has been reported to the GMs.", ownerName);
    return true;
}

std::vector<IslandReport> PlayerHousingMgr::GetReports(bool includeClosed, uint32 limit) const
{
    std::vector<IslandReport> reports;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT id, owner_guid, reporter_guid, reason, DATE_FORMAT(created_at, '%Y-%m-%d %H:%i'), closed_at IS NOT NULL "
            "FROM mod_playerhousing_report {} ORDER BY id DESC LIMIT {}", includeClosed ? "" : "WHERE closed_at IS NULL", limit))
    {
        do
        {
            Field* fields = result->Fetch();
            IslandReport report;
            report.id = fields[0].Get<uint32>();
            report.ownerGuid = fields[1].Get<uint32>();
            report.reporterGuid = fields[2].Get<uint32>();
            report.reason = fields[3].Get<std::string>();
            report.when = fields[4].Get<std::string>();
            report.closed = fields[5].Get<int64>() != 0;
            reports.push_back(report);
        } while (result->NextRow());
    }
    return reports;
}

bool PlayerHousingMgr::CloseReport(Player* gm, uint32 reportId, std::string& reason)
{
    if (!CharacterDatabase.Query("SELECT 1 FROM mod_playerhousing_report WHERE id={} AND closed_at IS NULL", reportId))
    {
        reason = Acore::StringFormat("There's no open report #{}.", reportId);
        return false;
    }

    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_report SET closed_at=NOW(), closed_by={} WHERE id={}", gm->GetGUID().GetCounter(), reportId);
    LOG_INFO("module", "mod-playerhousing: {} closed island report #{}.", gm->GetName(), reportId);
    reason = Acore::StringFormat("Closed report #{}.", reportId);
    return true;
}

bool PlayerHousingMgr::GmInspect(Player* gm, ObjectGuid::LowType ownerGuid, std::string& reason)
{
    LOG_INFO("module", "mod-playerhousing: {} inspects the island of {}.", gm->GetName(), NameOf(ownerGuid));
    return EnterHouse(gm, ownerGuid, reason, true);
}

bool PlayerHousingMgr::GmClearGreeting(Player* gm, ObjectGuid::LowType ownerGuid, std::string& reason)
{
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET greeting='' WHERE owner_guid={}", ownerGuid);
    LOG_INFO("module", "mod-playerhousing: {} cleared the greeting of {}'s island.", gm->GetName(), NameOf(ownerGuid));
    reason = Acore::StringFormat("Cleared the greeting of {}'s island.", NameOf(ownerGuid));
    return true;
}

bool PlayerHousingMgr::GmSetHidden(Player* gm, ObjectGuid::LowType ownerGuid, bool hidden, std::string& reason)
{
    if (!EnsureHouse(ownerGuid))
        return false;

    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET flags = (flags & ~{}) | {} WHERE owner_guid={}",
        uint32(HOUSE_FLAG_HIDDEN), hidden ? uint32(HOUSE_FLAG_HIDDEN) : 0, ownerGuid);
    LOG_INFO("module", "mod-playerhousing: {} {} the island of {}.", gm->GetName(), hidden ? "hid" : "unhid", NameOf(ownerGuid));
    if (Player* owner = ObjectAccessor::FindPlayerByLowGUID(ownerGuid))
        Say(owner, hidden ? "A GM closed your island to everyone but your guest list, and took it off the visit lists."
                          : "A GM opened your island again: your privacy setting applies as before.");
    reason = Acore::StringFormat("{}'s island is {}.", NameOf(ownerGuid),
        hidden ? "hidden: only its guests can visit, and it's off the public and most liked lists" : "no longer hidden");
    return true;
}

bool PlayerHousingMgr::GmPackUp(Player* gm, ObjectGuid::LowType ownerGuid, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    // The island as it is: from its session when someone is there, otherwise from the
    // database.
    std::map<uint32, Placement> placements;
    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    Session* session = sessionItr != _sessionsByOwner.end() && sessionItr->second.initialized ? &sessionItr->second : nullptr;
    if (session)
        placements = session->placements;
    else if (QueryResult result = CharacterDatabase.Query(
                 "SELECT placement_id, source_item_entry, placed_by FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={}",
                 ownerGuid, _layout.mapId))
    {
        do
        {
            Placement placement;
            placement.id = (*result)[0].Get<uint32>();
            placement.itemEntry = (*result)[1].Get<uint32>();
            placement.placedBy = (*result)[2].Get<uint32>();
            placements[placement.id] = placement;
        } while (result->NextRow());
        LoadGear(ownerGuid, placements);
    }

    if (placements.empty())
    {
        reason = Acore::StringFormat("{}'s island is already empty.", NameOf(ownerGuid));
        return false;
    }

    Map* map = session ? GetHousingMap() : nullptr;
    _report = {};
    for (auto const& [id, placement] : placements)
    {
        ObjectGuid::LowType itemOwner = placement.placedBy ? placement.placedBy : ownerGuid;
        for (auto const& [slot, gear] : placement.gear)
            ReturnGear(nullptr, ownerGuid, itemOwner, id, slot, gear);
        if (session)
            DespawnPlacement(*session, map, id);
        DeletePlacement(ownerGuid, id);
        AddToStorage(itemOwner, placement.itemEntry, 1);
    }

    if (session)
    {
        session->placements.clear();
        session->selected.clear();
        // Undo lists on this island point at pieces that are gone.
        for (ObjectGuid const& occupant : session->occupants)
            _journals.erase(occupant.GetCounter());
    }

    LOG_INFO("module", "mod-playerhousing: {} packed up the island of {} ({} pieces).", gm->GetName(), NameOf(ownerGuid), placements.size());
    if (Player* owner = ObjectAccessor::FindPlayerByLowGUID(ownerGuid))
        Say(owner, "A GM packed up your island. Everything is in your House Storage (House Key, Storage); mannequin gear came by mail.");
    reason = Acore::StringFormat("Packed up {}'s island: {} pieces went to the House Storage of whoever placed them.", NameOf(ownerGuid),
        placements.size());
    return true;
}
