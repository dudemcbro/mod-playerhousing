#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SocialMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>

using namespace Housing;

// Saved layouts: snapshots of an island's pieces (where each stands, its turn, size, tilt,
// what it stands on), to set out again later, send to a friend, or copy from an island
// whose owner allows it. A layout holds no items: setting one out packs up the island and
// places what the player has, leaving out what they don't.

namespace
{
    constexpr size_t LAYOUT_NAME_MAX = 40;

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    }

    // Printable, no chat escapes, trimmed, not too long.
    std::string CleanName(std::string const& name)
    {
        std::string clean;
        for (char c : name)
        {
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 32 || c == '|' || c == 127)
                continue;
            clean += c;
        }
        size_t begin = clean.find_first_not_of(' ');
        if (begin == std::string::npos)
            return "";
        clean = clean.substr(begin, clean.find_last_not_of(' ') - begin + 1);
        PlayerHousingMgr::TruncateUtf8(clean, LAYOUT_NAME_MAX);
        return clean;
    }

    std::string Plural(uint32 count, char const* one, char const* many)
    {
        return Acore::StringFormat("{} {}", count, count == 1 ? one : many);
    }
}

std::vector<SavedLayout> PlayerHousingMgr::GetSavedLayouts(ObjectGuid::LowType ownerGuid) const
{
    std::vector<SavedLayout> layouts;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT l.layout_id, l.name, l.source, DATE_FORMAT(l.saved_at, '%Y-%m-%d'), "
            "(SELECT COUNT(*) FROM mod_playerhousing_saved_piece p WHERE p.owner_guid = l.owner_guid AND p.layout_id = l.layout_id) "
            "FROM mod_playerhousing_saved_layout l WHERE l.owner_guid={} ORDER BY l.layout_id", ownerGuid))
    {
        do
        {
            Field* fields = result->Fetch();
            SavedLayout layout;
            layout.id = fields[0].Get<uint32>();
            layout.name = fields[1].Get<std::string>();
            layout.source = fields[2].Get<std::string>();
            layout.savedAt = fields[3].Get<std::string>();
            layout.pieces = uint32(fields[4].Get<uint64>());
            layouts.push_back(layout);
        } while (result->NextRow());
    }
    return layouts;
}

std::optional<SavedLayout> PlayerHousingMgr::FindSavedLayout(ObjectGuid::LowType ownerGuid, std::string const& nameOrNumber) const
{
    std::vector<SavedLayout> layouts = GetSavedLayouts(ownerGuid);
    std::string wanted = ToLower(CleanName(nameOrNumber));
    for (SavedLayout const& layout : layouts)
        if (ToLower(layout.name) == wanted || std::to_string(layout.id) == wanted)
            return layout;
    // A unique start of a name will do.
    std::optional<SavedLayout> found;
    for (SavedLayout const& layout : layouts)
    {
        if (!wanted.empty() && ToLower(layout.name).rfind(wanted, 0) == 0)
        {
            if (found)
                return std::nullopt;
            found = layout;
        }
    }
    return found;
}

std::optional<SavedLayout> PlayerHousingMgr::GetSavedLayout(ObjectGuid::LowType ownerGuid, uint32 layoutId) const
{
    for (SavedLayout const& layout : GetSavedLayouts(ownerGuid))
        if (layout.id == layoutId)
            return layout;
    return std::nullopt;
}

std::vector<Placement> PlayerHousingMgr::LoadSavedPieces(ObjectGuid::LowType ownerGuid, uint32 layoutId) const
{
    std::vector<Placement> pieces;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT placement_id, item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_id "
            "FROM mod_playerhousing_saved_piece WHERE owner_guid={} AND layout_id={} ORDER BY placement_id", ownerGuid, layoutId))
    {
        do
        {
            Field* fields = result->Fetch();
            Placement placement;
            placement.id = fields[0].Get<uint32>();
            placement.itemEntry = fields[1].Get<uint32>();
            placement.x = fields[2].Get<float>();
            placement.y = fields[3].Get<float>();
            placement.z = fields[4].Get<float>();
            placement.o = fields[5].Get<float>();
            placement.scale = std::max(0.05f, fields[6].Get<float>());
            placement.pitch = fields[7].Get<float>();
            placement.roll = fields[8].Get<float>();
            placement.look = fields[9].Get<uint32>();
            placement.parent = fields[10].Get<uint32>();
            pieces.push_back(placement);
        } while (result->NextRow());
    }
    return pieces;
}

void PlayerHousingMgr::WriteLayout(ObjectGuid::LowType ownerGuid, uint32 layoutId, std::string const& name, std::string const& source,
    ObjectGuid::LowType fromOwner, uint32 fromLayout) const
{
    std::string escapedName = name;
    std::string escapedSource = source;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.EscapeString(escapedSource);

    // fromLayout 0: the island as it is now; otherwise another saved layout.
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("DELETE FROM mod_playerhousing_saved_piece WHERE owner_guid={} AND layout_id={}", ownerGuid, layoutId);
    trans->Append("REPLACE INTO mod_playerhousing_saved_layout (owner_guid, layout_id, name, source, saved_at) VALUES ({}, {}, '{}', '{}', NOW())",
        ownerGuid, layoutId, escapedName, escapedSource);
    if (fromLayout)
        trans->Append(
            "INSERT INTO mod_playerhousing_saved_piece "
            "(owner_guid, layout_id, placement_id, item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_id) "
            "SELECT {}, {}, placement_id, item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_id "
            "FROM mod_playerhousing_saved_piece WHERE owner_guid={} AND layout_id={}", ownerGuid, layoutId, fromOwner, fromLayout);
    else
        trans->Append(
            "INSERT INTO mod_playerhousing_saved_piece "
            "(owner_guid, layout_id, placement_id, item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_id) "
            "SELECT {}, {}, placement_id, source_item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_id "
            "FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={}", ownerGuid, layoutId, fromOwner, _layout.mapId);
    CharacterDatabase.DirectCommitTransaction(trans);
}

uint32 PlayerHousingMgr::NextLayoutId(ObjectGuid::LowType ownerGuid) const
{
    if (QueryResult result = CharacterDatabase.Query("SELECT IFNULL(MAX(layout_id), 0) FROM mod_playerhousing_saved_layout WHERE owner_guid={}", ownerGuid))
        return (*result)[0].Get<uint32>() + 1;
    return 1;
}

bool PlayerHousingMgr::SaveLayout(Player* player, uint32 layoutId, std::string const& name, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    if (!_maxSavedLayouts)
    {
        reason = "Saved layouts are turned off on this server.";
        return false;
    }

    uint32 placed = 0;
    if (QueryResult result = CharacterDatabase.Query("SELECT COUNT(*) FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={}",
            self, _layout.mapId))
        placed = uint32((*result)[0].Get<uint64>());
    if (!placed)
    {
        reason = "Your island is empty: place some pieces first.";
        return false;
    }

    std::string layoutName;
    if (layoutId)
    {
        std::optional<SavedLayout> existing = GetSavedLayout(self, layoutId);
        if (!existing)
        {
            reason = "That layout is gone.";
            return false;
        }
        layoutName = existing->name;
    }
    else
    {
        layoutName = CleanName(name);
        if (layoutName.empty())
        {
            reason = "Give the layout a name.";
            return false;
        }
        std::vector<SavedLayout> layouts = GetSavedLayouts(self);
        if (layouts.size() >= _maxSavedLayouts)
        {
            reason = Acore::StringFormat("You have {} saved layouts, the most there can be. Save over one, or delete one.", layouts.size());
            return false;
        }
        for (SavedLayout const& layout : layouts)
            if (ToLower(layout.name) == ToLower(layoutName))
            {
                reason = Acore::StringFormat("You already have a layout called {}. Save over it from its page, or pick another name.", layout.name);
                return false;
            }
        layoutId = NextLayoutId(self);
    }

    WriteLayout(self, layoutId, layoutName, "", self, 0);
    reason = Acore::StringFormat("Saved your island as {} ({}).", layoutName, Plural(placed, "piece", "pieces"));
    return true;
}

bool PlayerHousingMgr::RenameLayout(Player* player, uint32 layoutId, std::string const& name, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::string clean = CleanName(name);
    if (clean.empty())
    {
        reason = "Give the layout a name.";
        return false;
    }
    if (!GetSavedLayout(self, layoutId))
    {
        reason = "That layout is gone.";
        return false;
    }

    std::string escaped = clean;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_saved_layout SET name='{}' WHERE owner_guid={} AND layout_id={}", escaped, self, layoutId);
    reason = "Renamed it " + clean + ".";
    return true;
}

bool PlayerHousingMgr::DeleteLayout(Player* player, uint32 layoutId, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::optional<SavedLayout> layout = GetSavedLayout(self, layoutId);
    if (!layout)
    {
        reason = "That layout is gone.";
        return false;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("DELETE FROM mod_playerhousing_saved_piece WHERE owner_guid={} AND layout_id={}", self, layoutId);
    trans->Append("DELETE FROM mod_playerhousing_saved_layout WHERE owner_guid={} AND layout_id={}", self, layoutId);
    CharacterDatabase.DirectCommitTransaction(trans);
    reason = "Deleted the layout " + layout->name + ". Your island is as it was.";
    return true;
}

std::map<uint32, uint32> PlayerHousingMgr::LayoutShortfall(Player* player, uint32 layoutId) const
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::map<uint32, uint32> needed;
    for (Placement const& placement : LoadSavedPieces(self, layoutId))
        if (_pieces.count(placement.itemEntry))
            ++needed[placement.itemEntry];

    // Pieces out on the island now count: setting out a layout packs them up first.
    std::map<uint32, uint32> have = GetStorage(self);
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT source_item_entry, COUNT(*) FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={} GROUP BY source_item_entry",
            self, _layout.mapId))
    {
        do
        {
            have[(*result)[0].Get<uint32>()] += uint32((*result)[1].Get<uint64>());
        } while (result->NextRow());
    }

    std::map<uint32, uint32> missing;
    for (auto const& [itemEntry, count] : needed)
    {
        uint32 owned = have[itemEntry] + player->GetItemCount(itemEntry);
        if (owned < count)
            missing[itemEntry] = count - owned;
    }
    return missing;
}

void PlayerHousingMgr::DescribeShortfall(Player* player, std::map<uint32, uint32> const& missing, uint32& gettable, uint64& cost, uint32& locked) const
{
    gettable = 0;
    cost = 0;
    locked = 0;
    std::set<uint32> known = LoadUnlocks(player);
    for (auto const& [itemEntry, count] : missing)
    {
        PieceDefinition const* piece = GetPiece(itemEntry);
        if (!piece)
            continue;
        if (IsUnlocked(player, *piece, &known))
        {
            gettable += count;
            cost += uint64(_freeMode ? 0 : piece->copyCost) * count;
        }
        else
            locked += count;
    }
}

bool PlayerHousingMgr::GetMissingForLayout(Player* player, uint32 layoutId, std::string& reason)
{
    if (OnCooldown(player, COOLDOWN_HEAVY, 3000, reason))
        return false;
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::map<uint32, uint32> missing = LayoutShortfall(player, layoutId);
    uint32 gettable = 0;
    uint64 cost = 0;
    uint32 locked = 0;
    DescribeShortfall(player, missing, gettable, cost, locked);
    if (!gettable)
    {
        reason = locked ? Acore::StringFormat("The {} still missing {} locked: earn them first.", Plural(locked, "piece", "pieces"), locked == 1 ? "is" : "are")
                        : "You have everything this layout needs.";
        return false;
    }
    if (cost > player->GetMoney())
    {
        reason = Acore::StringFormat("The missing pieces cost {}.", FormatMoney(cost));
        return false;
    }

    // Into the bags while they fit, then House Storage.
    std::set<uint32> known = LoadUnlocks(player);
    uint32 toBags = 0;
    uint32 toStorage = 0;
    for (auto const& [itemEntry, count] : missing)
    {
        PieceDefinition const* piece = GetPiece(itemEntry);
        if (!piece || !IsUnlocked(player, *piece, &known))
            continue;
        for (uint32 i = 0; i < count; ++i)
        {
            if (player->AddItem(itemEntry, 1))
                ++toBags;
            else
            {
                AddToStorage(self, itemEntry, 1);
                ++toStorage;
            }
        }
    }
    if (cost)
        player->ModifyMoney(-int64(cost));

    reason = Acore::StringFormat("Got {}{}{}.", Plural(toBags + toStorage, "piece", "pieces"),
        toStorage ? Acore::StringFormat(" ({} in House Storage)", toStorage) : std::string(),
        cost ? " for " + FormatMoney(cost) : std::string());
    if (locked)
        reason += Acore::StringFormat(" {} still locked.", Plural(locked, "piece is", "pieces are"));
    return true;
}

bool PlayerHousingMgr::SwitchLayout(Player* player, uint32 layoutId, std::string& reason)
{
    if (OnCooldown(player, COOLDOWN_HEAVY, 3000, reason))
        return false;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason, true);
    if (!session)
        return false;

    ObjectGuid::LowType self = session->ownerGuid;
    std::optional<SavedLayout> layout = GetSavedLayout(self, layoutId);
    if (!layout)
    {
        reason = "That layout is gone.";
        return false;
    }

    std::vector<Placement> saved = LoadSavedPieces(self, layoutId);
    if (saved.empty())
    {
        reason = "That layout is empty.";
        return false;
    }

    // What there is to set it out with: the pieces out now (packed up first), the bags, and
    // House Storage.
    std::map<uint32, uint32> have = GetStorage(self);
    for (auto const& [id, placement] : session->placements)
        ++have[placement.itemEntry];
    std::set<uint32> wanted;
    for (Placement const& placement : saved)
        wanted.insert(placement.itemEntry);
    auto consumes = _pendingConsumes.find(player->GetGUID());
    for (uint32 itemEntry : wanted)
    {
        uint32 reserved = 0;
        if (consumes != _pendingConsumes.end() && consumes->second.count(itemEntry))
            reserved = consumes->second.at(itemEntry);
        uint32 inBags = player->GetItemCount(itemEntry);
        have[itemEntry] += inBags > reserved ? inBags - reserved : 0;
    }

    uint32 furnishings = 0;
    uint32 buildings = 0;
    std::map<uint32, uint32> missing;
    std::map<uint32, uint32> newIds;
    std::vector<Placement> kept;
    uint32 nextId = session->nextPlacementId;
    for (Placement const& placement : saved)
    {
        PieceDefinition const* piece = GetPiece(placement.itemEntry);
        if (!piece)
            continue;
        uint32& left = have[placement.itemEntry];
        bool fits = piece->IsBuilding() ? buildings < _maxBuildings : furnishings < _maxFurnishings;
        if (!left || !fits || !IsSpotOnIsland(placement.x, placement.y, placement.z))
        {
            ++missing[placement.itemEntry];
            continue;
        }
        --left;
        ++(piece->IsBuilding() ? buildings : furnishings);

        Placement copy = placement;
        copy.id = nextId++;
        copy.gear.clear();
        if (piece->IsCreature())
            copy.pitch = copy.roll = 0.0f;
        newIds[placement.id] = copy.id;
        kept.push_back(copy);
    }
    if (kept.empty())
    {
        reason = Acore::StringFormat("You have none of the pieces {} needs. Its page can get them for you.", layout->name);
        return false;
    }
    for (Placement& placement : kept)
    {
        auto parent = newIds.find(placement.parent);
        placement.parent = parent != newIds.end() ? parent->second : 0;
    }
    session->nextPlacementId = nextId;

    std::vector<Change> changes;
    for (auto const& [id, placement] : session->placements)
        changes.push_back(Change{ id, placement, std::nullopt });
    for (Placement const& placement : kept)
        changes.push_back(Change{ placement.id, std::nullopt, placement });

    CancelMove(player);
    _report = {};
    std::string failure;
    ApplyChanges(player, *session, changes, true, failure);
    Record(player, "set out " + layout->name, std::move(changes));

    std::string leftOut;
    if (!missing.empty())
    {
        uint32 total = 0;
        std::string names;
        uint32 listed = 0;
        for (auto const& [itemEntry, count] : missing)
        {
            total += count;
            if (++listed <= 4)
                names += (names.empty() ? "" : ", ") + (count > 1 ? Acore::StringFormat("{} {}", count, PieceName(itemEntry)) : PieceName(itemEntry));
        }
        if (listed > 4)
            names += Acore::StringFormat(" and {} more kinds", listed - 4);
        leftOut = Acore::StringFormat(" Left out {} you don't have: {}.", Plural(total, "piece", "pieces"), names);
    }

    reason = Acore::StringFormat("Set out {}: {}.{}{} Changed your mind? House Key, Undo.", layout->name, Plural(uint32(kept.size()), "piece", "pieces"),
        leftOut, DescribeReturns());
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::IsLayoutCopyable(ObjectGuid::LowType ownerGuid) const
{
    HouseRecord house;
    return GetHouseRecord(ownerGuid, house) && (house.flags & HOUSE_FLAG_LAYOUT_COPYABLE);
}

void PlayerHousingMgr::SetLayoutCopyable(Player* player, bool copyable, std::string& reason) const
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    EnsureHouse(self);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET flags = (flags & ~{}) | {} WHERE owner_guid={}",
        uint32(HOUSE_FLAG_LAYOUT_COPYABLE), copyable ? uint32(HOUSE_FLAG_LAYOUT_COPYABLE) : 0, self);
    reason = copyable ? "Visitors can now save a copy of your island's layout (not your pieces: they place their own)."
                      : "Visitors can no longer copy your island's layout.";
}

bool PlayerHousingMgr::CopyIslandLayout(Player* visitor, std::string& reason)
{
    ObjectGuid::LowType self = visitor->GetGUID().GetCounter();
    ObjectGuid::LowType owner = GetIslandOwner(visitor);
    if (!owner || owner == self)
    {
        reason = "Visit someone's island to copy its layout.";
        return false;
    }
    if (!IsLayoutCopyable(owner))
    {
        reason = "This island's owner doesn't share its layout.";
        return false;
    }
    if (!_maxSavedLayouts)
    {
        reason = "Saved layouts are turned off on this server.";
        return false;
    }
    if (GetSavedLayouts(self).size() >= _maxSavedLayouts)
    {
        reason = "Your saved layouts are full: delete one first (Island settings, Saved layouts).";
        return false;
    }

    std::string ownerName = NameOf(owner);
    std::string name = CleanName(ownerName + "'s island");
    WriteLayout(self, NextLayoutId(self), name, ownerName, owner, 0);
    reason = Acore::StringFormat("Saved a copy of {}'s layout as {}. At home: Island settings, Saved layouts.", ownerName, name);
    return true;
}

bool PlayerHousingMgr::SendLayout(Player* player, uint32 layoutId, std::string const& recipientName, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::optional<SavedLayout> layout = GetSavedLayout(self, layoutId);
    if (!layout)
    {
        reason = "That layout is gone.";
        return false;
    }

    ObjectGuid::LowType recipient = 0;
    std::string name;
    if (!ResolvePlayerGuid(recipientName, recipient, name))
    {
        reason = "There's no character called " + CleanName(recipientName) + ".";
        return false;
    }
    if (recipient == self)
    {
        reason = "That's you.";
        return false;
    }

    // Only to people who know you: your party, your guild, or anyone with you on their
    // friends list.
    bool known = false;
    if (Group* group = player->GetGroup())
        known = group->IsMember(ObjectGuid::Create<HighGuid::Player>(recipient));
    if (!known && player->GetGuildId())
        known = bool(CharacterDatabase.Query("SELECT 1 FROM guild_member WHERE guid={} AND guildid={}", recipient, player->GetGuildId()));
    if (!known)
        known = bool(CharacterDatabase.Query("SELECT 1 FROM character_social WHERE guid={} AND friend={} AND (flags & {})",
            recipient, self, SOCIAL_FLAG_FRIEND));
    if (!known)
    {
        reason = Acore::StringFormat("Layouts go to your party, your guild, or friends who have you on their list; {} is none of those.", name);
        return false;
    }

    if (GetSavedLayouts(recipient).size() >= _maxSavedLayouts)
    {
        reason = Acore::StringFormat("{}'s saved layouts are full.", name);
        return false;
    }

    std::string senderName = player->GetName();
    std::string copyName = CleanName(layout->name + " (from " + senderName + ")");
    WriteLayout(recipient, NextLayoutId(recipient), copyName, senderName, self, layoutId);
    if (Player* online = ObjectAccessor::FindPlayerByLowGUID(recipient))
        Say(online, Acore::StringFormat("{} sent you a layout: {}. Island settings, Saved layouts.", senderName, copyName));
    reason = Acore::StringFormat("Sent {} a copy of {}.", name, layout->name);
    return true;
}
