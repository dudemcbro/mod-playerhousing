#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>

using namespace Housing;

namespace
{
    constexpr uint32 SOCIAL_FLAG_FRIEND = 0x01;
    constexpr size_t MAX_GREETING = 200;

    char const* PrivacyDescription(uint8 privacy)
    {
        switch (privacy)
        {
            case PRIVACY_PUBLIC: return "anyone can visit";
            case PRIVACY_FRIENDS: return "your friends, your guild and your guests can visit";
            default: return "only you and your guests can visit";
        }
    }
}

char const* PlayerHousingMgr::PrivacyName(uint8 privacy)
{
    switch (privacy)
    {
        case PRIVACY_PUBLIC: return "Public";
        case PRIVACY_FRIENDS: return "Friends & guild";
        default: return "Private";
    }
}

bool PlayerHousingMgr::GetHouseRecord(ObjectGuid::LowType ownerGuid, HouseRecord& outRecord) const
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT owner_guid, is_private, flags, greeting, weather, time_of_day, music FROM mod_playerhousing_house WHERE owner_guid={}", ownerGuid);
    if (!result)
        return false;

    Field* fields = result->Fetch();
    outRecord.ownerGuid = fields[0].Get<uint32>();
    outRecord.privacy = std::min<uint8>(fields[1].Get<uint8>(), PRIVACY_FRIENDS);
    outRecord.flags = fields[2].Get<uint32>();
    outRecord.greeting = fields[3].Get<std::string>();
    outRecord.weather = fields[4].Get<uint8>();
    outRecord.timeOfDay = fields[5].Get<uint8>();
    outRecord.music = fields[6].Get<uint32>();
    return true;
}

bool PlayerHousingMgr::EnsureHouse(ObjectGuid::LowType ownerGuid) const
{
    // Synchronous, so a read right after sees the row.
    CharacterDatabase.DirectExecute(
        "INSERT IGNORE INTO mod_playerhousing_house (owner_guid, style_id, stage, is_private) VALUES ({}, 1, 0, {})",
        ownerGuid, uint32(_defaultPrivacy));
    return true;
}

bool PlayerHousingMgr::CanVisit(Player const* visitor, HouseRecord const& house, std::string& reason) const
{
    ObjectGuid::LowType visitorGuid = visitor->GetGUID().GetCounter();
    if (visitorGuid == house.ownerGuid)
        return true;

    if (_gmVisitBypass && visitor->IsGameMaster())
        return true;

    if (house.privacy == PRIVACY_PUBLIC)
        return true;

    if (CharacterDatabase.Query("SELECT 1 FROM mod_playerhousing_acl WHERE owner_guid={} AND guest_guid={}", house.ownerGuid, visitorGuid))
        return true;

    if (house.privacy == PRIVACY_FRIENDS)
    {
        if (CharacterDatabase.Query("SELECT 1 FROM character_social WHERE guid={} AND friend={} AND (flags & {})",
                house.ownerGuid, visitorGuid, SOCIAL_FLAG_FRIEND))
            return true;

        if (uint32 guildId = visitor->GetGuildId())
            if (CharacterDatabase.Query("SELECT 1 FROM guild_member WHERE guid={} AND guildid={}", house.ownerGuid, guildId))
                return true;

        reason = "That island is open to its owner's friends and guild only.";
        return false;
    }

    reason = "That island is private, and you're not on its guest list.";
    return false;
}

bool PlayerHousingMgr::SetPrivacy(Player* player, uint8 privacy, std::string& reason)
{
    if (!player || privacy > PRIVACY_FRIENDS)
    {
        reason = "Unknown privacy setting.";
        return false;
    }

    ObjectGuid::LowType owner = player->GetGUID().GetCounter();
    EnsureHouse(owner);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET is_private={} WHERE owner_guid={}", uint32(privacy), owner);
    reason = Acore::StringFormat("Your island is now {}: {}.", PrivacyName(privacy), PrivacyDescription(privacy));
    return true;
}

bool PlayerHousingMgr::CyclePrivacy(Player* player, std::string& reason)
{
    HouseRecord house;
    uint8 current = _defaultPrivacy;
    if (GetHouseRecord(player->GetGUID().GetCounter(), house))
        current = house.privacy;

    uint8 next = current == PRIVACY_PRIVATE ? PRIVACY_FRIENDS : (current == PRIVACY_FRIENDS ? PRIVACY_PUBLIC : PRIVACY_PRIVATE);
    return SetPrivacy(player, next, reason);
}

bool PlayerHousingMgr::InviteGuest(Player* player, ObjectGuid::LowType guestGuid, std::string& reason)
{
    ObjectGuid::LowType owner = player->GetGUID().GetCounter();
    if (guestGuid == owner)
    {
        reason = "You can always visit your own island.";
        return false;
    }

    EnsureHouse(owner);
    CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_playerhousing_acl (owner_guid, guest_guid) VALUES ({}, {})", owner, guestGuid);

    std::string guestName = NameOf(guestGuid);
    if (Player* guest = ObjectAccessor::FindPlayerByLowGUID(guestGuid))
        Say(guest, Acore::StringFormat("{} invited you to their island. House Key, Visit an island, Islands you're invited to.", player->GetName()));

    reason = Acore::StringFormat("Invited {}. They can visit any time.", guestName);
    return true;
}

bool PlayerHousingMgr::InviteGuestByName(Player* player, std::string const& name, std::string& reason)
{
    ObjectGuid::LowType guestGuid = 0;
    std::string normalized;
    if (!ResolvePlayerGuid(name, guestGuid, normalized))
    {
        reason = "No character with that name.";
        return false;
    }

    return InviteGuest(player, guestGuid, reason);
}

bool PlayerHousingMgr::InviteTarget(Player* player, std::string& reason)
{
    Player* target = player->GetSelectedPlayer();
    if (!target || target == player)
    {
        reason = "Target the player you want to invite first.";
        return false;
    }

    return InviteGuest(player, target->GetGUID().GetCounter(), reason);
}

bool PlayerHousingMgr::InviteParty(Player* player, std::string& reason)
{
    Group* group = player->GetGroup();
    if (!group)
    {
        reason = "You're not in a party.";
        return false;
    }

    uint32 invited = 0;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == player->GetGUID())
            continue;

        std::string ignored;
        if (InviteGuest(player, slot.guid.GetCounter(), ignored))
            ++invited;
    }

    reason = Acore::StringFormat("Invited {} party {}.", invited, invited == 1 ? "member" : "members");
    return invited > 0;
}

bool PlayerHousingMgr::RemoveGuest(Player* player, ObjectGuid::LowType guestGuid, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    CharacterDatabase.DirectExecute("DELETE FROM mod_playerhousing_acl WHERE owner_guid={} AND guest_guid={}", self, guestGuid);
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto sessionItr = _sessionsByOwner.find(self);
        if (sessionItr != _sessionsByOwner.end())
            sessionItr->second.roommates.erase(guestGuid);
    }
    reason = Acore::StringFormat("Removed {} from your guest list.", NameOf(guestGuid));
    return true;
}

bool PlayerHousingMgr::IsRoommate(ObjectGuid::LowType ownerGuid, ObjectGuid::LowType guid) const
{
    return bool(CharacterDatabase.Query("SELECT 1 FROM mod_playerhousing_acl WHERE owner_guid={} AND guest_guid={} AND roommate=1", ownerGuid, guid));
}

bool PlayerHousingMgr::SetRoommate(Player* owner, ObjectGuid::LowType guestGuid, bool roommate, std::string& reason)
{
    ObjectGuid::LowType self = owner->GetGUID().GetCounter();
    if (guestGuid == self)
    {
        reason = "That's you.";
        return false;
    }

    // A roommate is a guest who may decorate: making one puts them on the guest list too.
    if (roommate)
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_playerhousing_acl (owner_guid, guest_guid, roommate) VALUES ({}, {}, 1) ON DUPLICATE KEY UPDATE roommate=1", self, guestGuid);
    else
        CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_acl SET roommate=0 WHERE owner_guid={} AND guest_guid={}", self, guestGuid);

    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto sessionItr = _sessionsByOwner.find(self);
        if (sessionItr != _sessionsByOwner.end())
        {
            if (roommate)
                sessionItr->second.roommates.insert(guestGuid);
            else
                sessionItr->second.roommates.erase(guestGuid);
        }
    }

    std::string name = NameOf(guestGuid);
    if (Player* guest = ObjectAccessor::FindPlayerByLowGUID(guestGuid))
        Say(guest, roommate ? Acore::StringFormat("{} made you a roommate: you can decorate their island (House Key, Start decorating there).", owner->GetName())
                            : Acore::StringFormat("You're no longer a roommate on {}'s island (still a guest).", owner->GetName()));
    reason = roommate ? Acore::StringFormat("{} is now a roommate: they can place their own pieces and change yours. Their pieces stay theirs.", name)
                      : Acore::StringFormat("{} is a guest again, no longer decorating.", name);
    return true;
}

bool PlayerHousingMgr::RemoveGuestByName(Player* player, std::string const& name, std::string& reason)
{
    ObjectGuid::LowType guestGuid = 0;
    std::string normalized;
    if (!ResolvePlayerGuid(name, guestGuid, normalized))
    {
        reason = "No character with that name.";
        return false;
    }

    return RemoveGuest(player, guestGuid, reason);
}

std::vector<VisitEntry> PlayerHousingMgr::GetGuests(ObjectGuid::LowType ownerGuid) const
{
    std::vector<VisitEntry> guests;
    if (QueryResult result = CharacterDatabase.Query("SELECT guest_guid, roommate FROM mod_playerhousing_acl WHERE owner_guid={}", ownerGuid))
    {
        do
        {
            VisitEntry entry;
            entry.ownerGuid = (*result)[0].Get<uint32>();
            entry.ownerName = NameOf(entry.ownerGuid);
            entry.roommate = (*result)[1].Get<uint8>() != 0;
            guests.push_back(entry);
        } while (result->NextRow());
    }

    std::sort(guests.begin(), guests.end(), [](VisitEntry const& left, VisitEntry const& right) { return left.ownerName < right.ownerName; });
    return guests;
}

std::vector<VisitEntry> PlayerHousingMgr::GetVisitList(Player const* player, uint8 list) const
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::vector<ObjectGuid::LowType> candidates;

    auto collect = [&](QueryResult result)
    {
        if (!result)
            return;
        do
        {
            candidates.push_back((*result)[0].Get<uint32>());
        } while (result->NextRow());
    };

    switch (list)
    {
        case 0:  // party
            if (Group const* group = player->GetGroup())
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                    if (slot.guid != player->GetGUID())
                        candidates.push_back(slot.guid.GetCounter());
            break;
        case 1:  // guild
            if (uint32 guildId = player->GetGuildId())
                collect(CharacterDatabase.Query(
                    "SELECT gm.guid FROM guild_member gm JOIN mod_playerhousing_house h ON h.owner_guid = gm.guid "
                    "WHERE gm.guildid={} AND gm.guid<>{} LIMIT 100", guildId, self));
            break;
        case 2:  // friends
            collect(CharacterDatabase.Query(
                "SELECT s.friend FROM character_social s JOIN mod_playerhousing_house h ON h.owner_guid = s.friend "
                "WHERE s.guid={} AND (s.flags & {}) LIMIT 100", self, SOCIAL_FLAG_FRIEND));
            break;
        case 3:  // invited
            collect(CharacterDatabase.Query("SELECT owner_guid FROM mod_playerhousing_acl WHERE guest_guid={} LIMIT 100", self));
            break;
        default:  // public
            collect(CharacterDatabase.Query(
                "SELECT owner_guid FROM mod_playerhousing_house WHERE is_private={} AND owner_guid<>{} ORDER BY updated_at DESC LIMIT 100",
                uint32(PRIVACY_PUBLIC), self));
            break;
    }

    // Only islands this player may enter, so every entry works when clicked.
    std::vector<VisitEntry> entries;
    std::set<ObjectGuid::LowType> seen;
    for (ObjectGuid::LowType owner : candidates)
    {
        if (!seen.insert(owner).second)
            continue;

        HouseRecord house;
        std::string ignored;
        if (!GetHouseRecord(owner, house) || !CanVisit(player, house, ignored))
            continue;

        VisitEntry entry;
        entry.ownerGuid = owner;
        entry.ownerName = NameOf(owner);
        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), [](VisitEntry const& left, VisitEntry const& right) { return left.ownerName < right.ownerName; });
    return entries;
}

bool PlayerHousingMgr::SetGreeting(Player* player, std::string const& greeting, std::string& reason)
{
    std::string text = greeting;
    // No color or link codes from players.
    text.erase(std::remove(text.begin(), text.end(), '|'), text.end());
    text.erase(0, text.find_first_not_of(" \t"));
    text.erase(text.find_last_not_of(" \t") + 1);
    if (text.size() > MAX_GREETING)
        text.resize(MAX_GREETING);

    ObjectGuid::LowType owner = player->GetGUID().GetCounter();
    EnsureHouse(owner);
    CharacterDatabase.EscapeString(text);
    CharacterDatabase.DirectExecute("UPDATE mod_playerhousing_house SET greeting='{}' WHERE owner_guid={}", text, owner);
    reason = text.empty() ? "Greeting cleared." : "Visitors will now be greeted with your message.";
    return true;
}
