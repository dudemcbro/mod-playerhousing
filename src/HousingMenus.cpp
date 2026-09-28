#include "HousingMenus.h"

#include "Chat.h"
#include "GossipDef.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "StringFormat.h"

#include <algorithm>

using namespace Housing;

namespace
{
    // Gossip senders are the command, actions carry its argument.
    enum Command : uint32
    {
        CMD_HOME = 1,
        CMD_GO_HOME,
        CMD_LEAVE,
        CMD_UNSTUCK,
        CMD_DECORATE_ON,
        CMD_DECORATE_OFF,
        CMD_UNDO,
        CMD_REDO,
        CMD_COLLECTION,
        CMD_CATEGORY,         // action: category | page << 8
        CMD_GET_COPY,         // action: item entry
        CMD_GET_ALL,
        CMD_LOCKED,           // action: category | page << 8 (shows the hint again)
        CMD_STORAGE,          // action: page
        CMD_STORAGE_TAKE,     // action: item entry
        CMD_STORAGE_TAKE_ALL,
        CMD_SETTINGS,
        CMD_PRIVACY,
        CMD_GUESTS,           // action: page
        CMD_GUEST_REMOVE,     // action: guest guid
        CMD_INVITE_TARGET,
        CMD_INVITE_PARTY,
        CMD_INVITE_NAME,      // coded
        CMD_GREETING,         // coded
        CMD_GREETING_CLEAR,
        CMD_VISIT,
        CMD_VISIT_LIST,       // action: list | page << 8
        CMD_VISIT_OWNER,      // action: owner guid
        CMD_VISIT_NAME,       // coded
        CMD_HELP,
        CMD_NEARBY,           // action: page
        CMD_PIECE,            // action: placement id
        CMD_PIECE_OP,         // action: placement id | op << 24
        CMD_NUDGE_MENU,       // action: placement id
        CMD_PICKUP_MENU,      // action: placement id (buildings: choose what to take)
        CMD_HOOK_MENU,        // action: surface placement id
        CMD_HOOK_PLACE,       // action: surface id << 12 | (item entry - ITEM_BASE)
        CMD_PACKUP,
        CMD_KEY,
        CMD_CLOSE
    };

    enum PieceOp : uint32
    {
        OP_PICKUP = 1,
        OP_PICKUP_WITH_INSIDE,
        OP_TURN_LEFT_45,
        OP_TURN_RIGHT_45,
        OP_TURN_LEFT_15,
        OP_TURN_RIGHT_15,
        OP_FACE_ME,
        OP_MOVE_HERE,
        OP_NUDGE_FORWARD,
        OP_NUDGE_BACK,
        OP_NUDGE_LEFT,
        OP_NUDGE_RIGHT,
        OP_NUDGE_UP,
        OP_NUDGE_DOWN,
        OP_UNDO
    };

    constexpr uint32 PAGE_SIZE = 18;
    constexpr uint32 ITEM_BASE = 900000;
    constexpr float NUDGE_STEP = 0.25f;
    constexpr float RAISE_STEP = 0.1f;

    void Send(Player* player, MenuSource const& source, uint32 textId)
    {
        if (source.type == SOURCE_PLAYER)
            player->PlayerTalkClass->GetGossipMenu().SetMenuId(PLAYER_MENU_ID);
        SendGossipMenuFor(player, textId, source.guid);
    }

    void Add(Player* player, uint32 icon, std::string const& text, uint32 command, uint32 arg = 0)
    {
        AddGossipItemFor(player, icon, text, command, arg);
    }

    void Confirm(Player* player, uint32 icon, std::string const& text, uint32 command, uint32 arg, std::string const& question, uint32 money = 0)
    {
        AddGossipItemFor(player, icon, text, command, arg, question, money, false);
    }

    void Ask(Player* player, uint32 icon, std::string const& text, uint32 command, uint32 arg = 0)
    {
        AddGossipItemFor(player, icon, text, command, arg, "", 0, true);
    }

    void Say(Player* player, std::string const& text)
    {
        if (!text.empty())
            sPlayerHousingMgr->Say(player, text);
    }

    void Paging(Player* player, uint32 command, uint32 low, uint32 page, uint32 count)
    {
        if (page > 0)
            Add(player, GOSSIP_ICON_DOT, "Previous page", command, low | ((page - 1) << 8));
        if ((page + 1) * PAGE_SIZE < count)
            Add(player, GOSSIP_ICON_DOT, "Next page", command, low | ((page + 1) << 8));
    }

    std::string StatusLine(Player* player)
    {
        ObjectGuid::LowType self = player->GetGUID().GetCounter();
        HouseRecord house;
        uint8 privacy = PRIVACY_PRIVATE;
        if (sPlayerHousingMgr->GetHouseRecord(self, house))
            privacy = house.privacy;
        return Acore::StringFormat("Your island: {}, {}", sPlayerHousingMgr->CountsText(self), PlayerHousingMgr::PrivacyName(privacy));
    }

    void ShowCategory(Player* player, MenuSource const& source, uint8 category, uint32 page)
    {
        std::vector<PieceDefinition const*> pieces = sPlayerHousingMgr->GetPiecesInCategory(category);
        std::set<uint32> known = sPlayerHousingMgr->LoadUnlocks(player);
        uint32 unlocked = 0;
        for (PieceDefinition const* piece : pieces)
            if (sPlayerHousingMgr->IsUnlocked(player, *piece, &known))
                ++unlocked;

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {} of {} unlocked", PlayerHousingMgr::CategoryName(category), unlocked, pieces.size()),
            CMD_CATEGORY, category | (page << 8));

        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < pieces.size() && i < first + PAGE_SIZE; ++i)
        {
            PieceDefinition const* piece = pieces[i];
            if (sPlayerHousingMgr->IsUnlocked(player, *piece, &known))
            {
                uint32 cost = sPlayerHousingMgr->IsFreeMode() ? 0 : piece->copyCost;
                std::string label = piece->name;
                if (cost)
                    Confirm(player, GOSSIP_ICON_VENDOR, label + " (" + PlayerHousingMgr::FormatMoney(cost) + ")", CMD_GET_COPY, piece->itemEntry,
                        "Get a " + piece->name + "?", cost);
                else
                    Add(player, GOSSIP_ICON_VENDOR, label, CMD_GET_COPY, piece->itemEntry);
            }
            else
                Add(player, GOSSIP_ICON_DOT, piece->name + ": " + sPlayerHousingMgr->DescribeProgress(player, *piece), CMD_LOCKED, category | (page << 8));
        }

        Paging(player, CMD_CATEGORY, category, page, uint32(pieces.size()));
        Add(player, GOSSIP_ICON_CHAT, "Back to the Collection", CMD_COLLECTION);
        Send(player, source, TEXT_COLLECTION);
    }

    void ShowSettings(Player* player, MenuSource const& source)
    {
        ObjectGuid::LowType self = player->GetGUID().GetCounter();
        sPlayerHousingMgr->EnsureHouse(self);
        HouseRecord house;
        sPlayerHousingMgr->GetHouseRecord(self, house);
        std::vector<VisitEntry> guests = sPlayerHousingMgr->GetGuests(self);

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Privacy: {} (click to change)", PlayerHousingMgr::PrivacyName(house.privacy)), CMD_PRIVACY);
        Add(player, GOSSIP_ICON_TALK, Acore::StringFormat("Guests ({})", guests.size()), CMD_GUESTS, 0);
        std::string greeting = house.greeting.empty() ? "none" : house.greeting;
        if (greeting.size() > 40)
            greeting = greeting.substr(0, 37) + "...";
        Ask(player, GOSSIP_ICON_CHAT, "Greeting for visitors: " + greeting, CMD_GREETING);
        if (!house.greeting.empty())
            Add(player, GOSSIP_ICON_CHAT, "Clear the greeting", CMD_GREETING_CLEAR);
        Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
        Send(player, source, TEXT_SETTINGS);
    }

    void ShowGuests(Player* player, MenuSource const& source, uint32 page)
    {
        std::vector<VisitEntry> guests = sPlayerHousingMgr->GetGuests(player->GetGUID().GetCounter());

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_TALK, "Invite my target", CMD_INVITE_TARGET);
        Add(player, GOSSIP_ICON_TALK, "Invite my party", CMD_INVITE_PARTY);
        Ask(player, GOSSIP_ICON_TALK, "Invite by name...", CMD_INVITE_NAME);

        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < guests.size() && i < first + PAGE_SIZE; ++i)
            Confirm(player, GOSSIP_ICON_DOT, "Remove " + guests[i].ownerName, CMD_GUEST_REMOVE, guests[i].ownerGuid,
                "Remove " + guests[i].ownerName + " from your guest list?");

        Paging(player, CMD_GUESTS, 0, page, uint32(guests.size()));
        Add(player, GOSSIP_ICON_CHAT, "Back", CMD_SETTINGS);
        Send(player, source, TEXT_GUESTS);
    }

    char const* const VISIT_LISTS[] = { "Party members' islands", "Guild members' islands", "Friends' islands", "Islands you're invited to", "Public islands" };

    void ShowVisitList(Player* player, MenuSource const& source, uint8 list, uint32 page)
    {
        std::vector<VisitEntry> entries = sPlayerHousingMgr->GetVisitList(player, list);

        ClearGossipMenuFor(player);
        if (entries.empty())
            Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: none you can visit right now", VISIT_LISTS[std::min<uint8>(list, 4)]), CMD_VISIT);

        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < entries.size() && i < first + PAGE_SIZE; ++i)
            Add(player, GOSSIP_ICON_TAXI, "Visit " + entries[i].ownerName + "'s island", CMD_VISIT_OWNER, entries[i].ownerGuid);

        Paging(player, CMD_VISIT_LIST, list, page, uint32(entries.size()));
        Add(player, GOSSIP_ICON_CHAT, "Back", CMD_VISIT);
        Send(player, source, TEXT_VISIT);
    }

    void ShowNearby(Player* player, MenuSource const& source, uint32 page)
    {
        std::vector<std::pair<Placement, float>> nearby = sPlayerHousingMgr->GetNearbyPlacements(player, 25.0f);

        ClearGossipMenuFor(player);
        if (nearby.empty())
            Add(player, GOSSIP_ICON_CHAT, "Nothing within 25 yards. Walk closer to what you want to change.", CMD_NEARBY, 0);

        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < nearby.size() && i < first + PAGE_SIZE; ++i)
        {
            PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(nearby[i].first.itemEntry);
            std::string name = piece ? piece->name : "furniture";
            Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("{} ({:.0f} yd)", name, nearby[i].second), CMD_PIECE, nearby[i].first.id);
        }

        Paging(player, CMD_NEARBY, 0, page, uint32(nearby.size()));
        Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
        Send(player, source, TEXT_NEARBY);
    }

    void ShowNudge(Player* player, MenuSource const& source, uint32 placementId)
    {
        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_INTERACT_1, "Forward (away from you)", CMD_PIECE_OP, placementId | (OP_NUDGE_FORWARD << 24));
        Add(player, GOSSIP_ICON_INTERACT_1, "Back (toward you)", CMD_PIECE_OP, placementId | (OP_NUDGE_BACK << 24));
        Add(player, GOSSIP_ICON_INTERACT_1, "Left", CMD_PIECE_OP, placementId | (OP_NUDGE_LEFT << 24));
        Add(player, GOSSIP_ICON_INTERACT_1, "Right", CMD_PIECE_OP, placementId | (OP_NUDGE_RIGHT << 24));
        Add(player, GOSSIP_ICON_INTERACT_1, "Up", CMD_PIECE_OP, placementId | (OP_NUDGE_UP << 24));
        Add(player, GOSSIP_ICON_INTERACT_1, "Down", CMD_PIECE_OP, placementId | (OP_NUDGE_DOWN << 24));
        Add(player, GOSSIP_ICON_CHAT, "Back to the piece", CMD_PIECE, placementId);
        Send(player, source, TEXT_PIECE);
    }

    void ShowPickupChoice(Player* player, MenuSource const& source, uint32 placementId)
    {
        std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId);
        PieceDefinition const* piece = placement ? sPlayerHousingMgr->GetPiece(placement->itemEntry) : nullptr;
        if (!piece)
        {
            HousingMenus::ShowHome(player, source);
            return;
        }

        uint32 inside = uint32(sPlayerHousingMgr->GetPiecesInside(player->GetGUID().GetCounter(), placementId).size());
        ClearGossipMenuFor(player);
        Confirm(player, GOSSIP_ICON_INTERACT_1, "Pick up the building only", CMD_PIECE_OP, placementId | (OP_PICKUP << 24),
            inside ? Acore::StringFormat("Return the {} to your bags? The {} pieces inside stay where they are.", piece->name, inside)
                   : Acore::StringFormat("Return the {} to your bags?", piece->name));
        if (inside)
            Confirm(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Pick up the building and the {} pieces inside it", inside),
                CMD_PIECE_OP, placementId | (OP_PICKUP_WITH_INSIDE << 24),
                Acore::StringFormat("Return the {} and the {} pieces inside it to your bags?", piece->name, inside));
        Add(player, GOSSIP_ICON_CHAT, "Back to the building", CMD_PIECE, placementId);
        Send(player, source, TEXT_PIECE);
    }

    // After a change the piece is a new object; a menu opened from the old one would point
    // at nothing, so it follows the piece.
    MenuSource FollowPiece(Player* player, MenuSource const& source, uint32 placementId)
    {
        if (source.type != SOURCE_GAMEOBJECT)
            return source;

        ObjectGuid guid = sPlayerHousingMgr->GetObjectForPlacement(player, placementId);
        if (guid.IsEmpty())
            return MenuSource{ SOURCE_PLAYER, player->GetGUID() };
        return MenuSource{ SOURCE_GAMEOBJECT, guid };
    }

    void DoPieceOp(Player* player, MenuSource const& source, uint32 placementId, uint32 op)
    {
        std::string reason;
        bool keepMenu = true;
        switch (op)
        {
            case OP_PICKUP:
                sPlayerHousingMgr->PickUp(player, placementId, false, reason);
                keepMenu = false;
                break;
            case OP_PICKUP_WITH_INSIDE:
                sPlayerHousingMgr->PickUp(player, placementId, true, reason);
                keepMenu = false;
                break;
            case OP_TURN_LEFT_45: sPlayerHousingMgr->Rotate(player, placementId, 45.0f, reason); break;
            case OP_TURN_RIGHT_45: sPlayerHousingMgr->Rotate(player, placementId, -45.0f, reason); break;
            case OP_TURN_LEFT_15: sPlayerHousingMgr->Rotate(player, placementId, 15.0f, reason); break;
            case OP_TURN_RIGHT_15: sPlayerHousingMgr->Rotate(player, placementId, -15.0f, reason); break;
            case OP_FACE_ME: sPlayerHousingMgr->FaceMe(player, placementId, reason); break;
            case OP_MOVE_HERE: sPlayerHousingMgr->MoveHere(player, placementId, reason); break;
            case OP_NUDGE_FORWARD: sPlayerHousingMgr->Nudge(player, placementId, NUDGE_STEP, 0.0f, 0.0f, reason); break;
            case OP_NUDGE_BACK: sPlayerHousingMgr->Nudge(player, placementId, -NUDGE_STEP, 0.0f, 0.0f, reason); break;
            case OP_NUDGE_LEFT: sPlayerHousingMgr->Nudge(player, placementId, 0.0f, NUDGE_STEP, 0.0f, reason); break;
            case OP_NUDGE_RIGHT: sPlayerHousingMgr->Nudge(player, placementId, 0.0f, -NUDGE_STEP, 0.0f, reason); break;
            case OP_NUDGE_UP: sPlayerHousingMgr->Nudge(player, placementId, 0.0f, 0.0f, RAISE_STEP, reason); break;
            case OP_NUDGE_DOWN: sPlayerHousingMgr->Nudge(player, placementId, 0.0f, 0.0f, -RAISE_STEP, reason); break;
            case OP_UNDO:
                sPlayerHousingMgr->Undo(player, reason);
                keepMenu = sPlayerHousingMgr->GetPlacement(player, placementId).has_value();
                break;
            default:
                break;
        }

        Say(player, reason);
        if (!keepMenu || !sPlayerHousingMgr->GetPlacement(player, placementId))
        {
            CloseGossipMenuFor(player);
            return;
        }

        MenuSource followed = FollowPiece(player, source, placementId);
        bool nudging = op >= OP_NUDGE_FORWARD && op <= OP_NUDGE_DOWN;
        if (nudging)
            ShowNudge(player, followed, placementId);
        else
            HousingMenus::ShowPiece(player, followed, placementId);
    }
}

void HousingMenus::ShowHome(Player* player, MenuSource const& source)
{
    ClearGossipMenuFor(player);

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    ObjectGuid::LowType islandOwner = sPlayerHousingMgr->GetIslandOwner(player);
    uint32 unlocked = 0;
    uint32 total = 0;
    sPlayerHousingMgr->CollectionCounts(player, -1, unlocked, total);
    uint32 stored = 0;
    for (auto const& [itemEntry, count] : sPlayerHousingMgr->GetStorage(self))
        stored += count;

    if (islandOwner == self)
    {
        bool decorating = sPlayerHousingMgr->IsDecorating(player);
        Add(player, GOSSIP_ICON_CHAT, StatusLine(player), CMD_HOME);
        if (decorating)
            Add(player, GOSSIP_ICON_INTERACT_1, "Done decorating", CMD_DECORATE_OFF);
        else
            Add(player, GOSSIP_ICON_INTERACT_1, "Start decorating", CMD_DECORATE_ON);

        std::string undo = sPlayerHousingMgr->UndoLabel(player);
        if (!undo.empty())
            Add(player, GOSSIP_ICON_INTERACT_2, "Undo: " + undo, CMD_UNDO);
        std::string redo = sPlayerHousingMgr->RedoLabel(player);
        if (!redo.empty())
            Add(player, GOSSIP_ICON_INTERACT_2, "Redo: " + redo, CMD_REDO);

        if (decorating)
            Add(player, GOSSIP_ICON_INTERACT_1, "Change a piece near me", CMD_NEARBY, 0);
        Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("Collection ({} unlocked)", unlocked), CMD_COLLECTION);
        if (stored)
            Add(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Storage ({})", stored), CMD_STORAGE, 0);
        Add(player, GOSSIP_ICON_TABARD, "Island settings", CMD_SETTINGS);
        if (decorating)
            Confirm(player, GOSSIP_ICON_INTERACT_2, "Pack up everything", CMD_PACKUP, 0,
                "Put every piece on your island back in your bags (or House Storage)? You can undo this.");
        Add(player, GOSSIP_ICON_TAXI, "Visit another island", CMD_VISIT);
        Add(player, GOSSIP_ICON_CHAT, "Unstuck: back to the landing spot", CMD_UNSTUCK);
        Add(player, GOSSIP_ICON_TAXI, "Leave the island", CMD_LEAVE);
        Add(player, GOSSIP_ICON_CHAT, "How housing works", CMD_HELP);
    }
    else if (islandOwner)
    {
        Add(player, GOSSIP_ICON_CHAT, "You're visiting " + sPlayerHousingMgr->NameOf(islandOwner) + "'s island", CMD_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Go home", CMD_GO_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Visit another island", CMD_VISIT);
        Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("Collection ({} unlocked)", unlocked), CMD_COLLECTION);
        Add(player, GOSSIP_ICON_CHAT, "Unstuck: back to the landing spot", CMD_UNSTUCK);
        Add(player, GOSSIP_ICON_TAXI, "Leave the island", CMD_LEAVE);
        Add(player, GOSSIP_ICON_CHAT, "How housing works", CMD_HELP);
    }
    else
    {
        Add(player, GOSSIP_ICON_CHAT, StatusLine(player), CMD_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Go home", CMD_GO_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Visit an island", CMD_VISIT);
        Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("Collection ({} unlocked)", unlocked), CMD_COLLECTION);
        if (stored)
            Add(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Storage ({})", stored), CMD_STORAGE, 0);
        Add(player, GOSSIP_ICON_TABARD, "Island settings", CMD_SETTINGS);
        if (!player->HasItemCount(HOUSE_KEY_ITEM, 1, true))
            Add(player, GOSSIP_ICON_CHAT, "I lost my House Key", CMD_KEY);
        Add(player, GOSSIP_ICON_CHAT, "How housing works", CMD_HELP);
    }

    Send(player, source, TEXT_HOME);
}

void HousingMenus::ShowCollection(Player* player, MenuSource const& source)
{
    std::set<uint32> known = sPlayerHousingMgr->LoadUnlocks(player);
    uint32 unlocked = 0;
    uint32 total = 0;
    sPlayerHousingMgr->CollectionCounts(player, -1, unlocked, total, &known);

    ClearGossipMenuFor(player);
    Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("Collection: {} of {} unlocked", unlocked, total), CMD_COLLECTION);
    for (uint8 category = 0; category < CATEGORY_COUNT; ++category)
    {
        uint32 categoryUnlocked = 0;
        uint32 categoryTotal = 0;
        sPlayerHousingMgr->CollectionCounts(player, category, categoryUnlocked, categoryTotal, &known);
        if (categoryTotal)
            Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("{} ({}/{})", PlayerHousingMgr::CategoryName(category), categoryUnlocked, categoryTotal),
                CMD_CATEGORY, category);
    }

    if (sPlayerHousingMgr->IsFreeMode() || sPlayerHousingMgr->IsUnlockAll())
        Add(player, GOSSIP_ICON_MONEY_BAG, "Give me one of everything (test server)", CMD_GET_ALL);
    Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
    Send(player, source, TEXT_COLLECTION);
}

void HousingMenus::ShowStorage(Player* player, MenuSource const& source)
{
    std::map<uint32, uint32> storage = sPlayerHousingMgr->GetStorage(player->GetGUID().GetCounter());

    ClearGossipMenuFor(player);
    uint32 total = 0;
    for (auto const& [itemEntry, count] : storage)
        total += count;

    Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("House Storage: {} pieces", total), CMD_STORAGE, 0);
    if (total)
        Add(player, GOSSIP_ICON_MONEY_BAG, "Take everything", CMD_STORAGE_TAKE_ALL);

    uint32 shown = 0;
    for (auto const& [itemEntry, count] : storage)
    {
        if (++shown > PAGE_SIZE)
            break;
        PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(itemEntry);
        Add(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Take {} (x{})", piece ? piece->name : "unknown piece", count), CMD_STORAGE_TAKE, itemEntry);
    }

    Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
    Send(player, source, TEXT_STORAGE);
}

void HousingMenus::ShowVisit(Player* player, MenuSource const& source)
{
    ClearGossipMenuFor(player);
    for (uint8 list = 0; list < 5; ++list)
    {
        size_t count = sPlayerHousingMgr->GetVisitList(player, list).size();
        Add(player, GOSSIP_ICON_TAXI, Acore::StringFormat("{} ({})", VISIT_LISTS[list], count), CMD_VISIT_LIST, list);
    }
    Ask(player, GOSSIP_ICON_TAXI, "Find by character name...", CMD_VISIT_NAME);
    Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
    Send(player, source, TEXT_VISIT);
}

void HousingMenus::ShowPiece(Player* player, MenuSource const& source, uint32 placementId)
{
    std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId);
    PieceDefinition const* piece = placement ? sPlayerHousingMgr->GetPiece(placement->itemEntry) : nullptr;
    if (!piece || !sPlayerHousingMgr->IsOnOwnIsland(player))
    {
        CloseGossipMenuFor(player);
        return;
    }

    sPlayerHousingMgr->SelectPlacement(player, placementId);
    sPlayerHousingMgr->SendAddonState(player);

    ClearGossipMenuFor(player);
    Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{} ({})", piece->name, sPlayerHousingMgr->CountsText(player->GetGUID().GetCounter())),
        CMD_PIECE, placementId);
    if (piece->IsBuilding())
        Add(player, GOSSIP_ICON_INTERACT_1, "Pick up...", CMD_PICKUP_MENU, placementId);
    else
        Add(player, GOSSIP_ICON_INTERACT_1, "Pick up (back to your bags)", CMD_PIECE_OP, placementId | (OP_PICKUP << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 45°", CMD_PIECE_OP, placementId | (OP_TURN_LEFT_45 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 45°", CMD_PIECE_OP, placementId | (OP_TURN_RIGHT_45 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 15°", CMD_PIECE_OP, placementId | (OP_TURN_LEFT_15 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 15°", CMD_PIECE_OP, placementId | (OP_TURN_RIGHT_15 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn toward me", CMD_PIECE_OP, placementId | (OP_FACE_ME << 24));
    Add(player, GOSSIP_ICON_INTERACT_1, "Nudge...", CMD_NUDGE_MENU, placementId);
    Add(player, GOSSIP_ICON_INTERACT_1, "Move to where I'm standing", CMD_PIECE_OP, placementId | (OP_MOVE_HERE << 24));
    if (piece->HasFlag(PIECE_FLAG_SURFACE))
        Add(player, GOSSIP_ICON_VENDOR, "Put something on top", CMD_HOOK_MENU, placementId);
    if (!sPlayerHousingMgr->UndoLabel(player).empty())
        Add(player, GOSSIP_ICON_INTERACT_2, "Undo: " + sPlayerHousingMgr->UndoLabel(player), CMD_PIECE_OP, placementId | (OP_UNDO << 24));
    Add(player, GOSSIP_ICON_CHAT, "Done", CMD_CLOSE);
    Send(player, source, TEXT_PIECE);
}

void HousingMenus::ShowHook(Player* player, MenuSource const& source, uint32 surfacePlacementId)
{
    std::optional<Placement> surface = sPlayerHousingMgr->GetPlacement(player, surfacePlacementId);
    PieceDefinition const* surfacePiece = surface ? sPlayerHousingMgr->GetPiece(surface->itemEntry) : nullptr;
    if (!surfacePiece || !sPlayerHousingMgr->IsOnOwnIsland(player))
    {
        CloseGossipMenuFor(player);
        return;
    }

    // Small pieces the player carries (bags first, then storage) that can go on top.
    std::map<uint32, uint32> available;
    for (PieceDefinition const* piece : [&]()
        {
            std::vector<PieceDefinition const*> all;
            for (uint8 category = 0; category < CATEGORY_COUNT; ++category)
                for (PieceDefinition const* candidate : sPlayerHousingMgr->GetPiecesInCategory(category))
                    all.push_back(candidate);
            return all;
        }())
    {
        if (!piece->HasFlag(PIECE_FLAG_SMALL) || piece->IsBuilding())
            continue;
        if (uint32 count = player->GetItemCount(piece->itemEntry))
            available[piece->itemEntry] += count;
    }
    for (auto const& [itemEntry, count] : sPlayerHousingMgr->GetStorage(player->GetGUID().GetCounter()))
        if (PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(itemEntry))
            if (piece->HasFlag(PIECE_FLAG_SMALL))
                available[itemEntry] += count;

    ClearGossipMenuFor(player);
    Add(player, GOSSIP_ICON_CHAT, "On top of the " + surfacePiece->name, CMD_HOOK_MENU, surfacePlacementId);
    if (available.empty())
        Add(player, GOSSIP_ICON_VENDOR, "You have no small pieces (candles, books, bottles). Get some from your Collection.", CMD_COLLECTION);

    uint32 shown = 0;
    for (auto const& [itemEntry, count] : available)
    {
        if (++shown > PAGE_SIZE)
            break;
        PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(itemEntry);
        Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("Put {} here (you have {})", piece->name, count),
            CMD_HOOK_PLACE, (surfacePlacementId << 12) | (itemEntry - ITEM_BASE));
    }
    Add(player, GOSSIP_ICON_CHAT, "Done", CMD_CLOSE);
    Send(player, source, TEXT_HOOK);
}

void HousingMenus::HandleSelect(Player* player, MenuSource const& source, uint32 sender, uint32 action, char const* code)
{
    std::string reason;
    std::string text = code ? code : "";

    switch (sender)
    {
        case CMD_HOME:
            ShowHome(player, source);
            return;
        case CMD_GO_HOME:
            CloseGossipMenuFor(player);
            if (!sPlayerHousingMgr->RequestGoHome(player, reason) || !reason.empty())
                Say(player, reason);
            return;
        case CMD_LEAVE:
            CloseGossipMenuFor(player);
            if (!sPlayerHousingMgr->LeaveHouse(player, reason))
                Say(player, reason);
            return;
        case CMD_UNSTUCK:
            CloseGossipMenuFor(player);
            sPlayerHousingMgr->Unstuck(player, reason);
            Say(player, reason);
            return;
        case CMD_DECORATE_ON:
        case CMD_DECORATE_OFF:
            sPlayerHousingMgr->SetDecorating(player, sender == CMD_DECORATE_ON, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_UNDO:
            sPlayerHousingMgr->Undo(player, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_REDO:
            sPlayerHousingMgr->Redo(player, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_COLLECTION:
            ShowCollection(player, source);
            return;
        case CMD_CATEGORY:
            ShowCategory(player, source, uint8(action & 0xFF), action >> 8);
            return;
        case CMD_LOCKED:
            Say(player, "Locked pieces unlock by themselves when you earn them.");
            ShowCategory(player, source, uint8(action & 0xFF), action >> 8);
            return;
        case CMD_GET_COPY:
        {
            sPlayerHousingMgr->GetCopy(player, action, reason);
            Say(player, reason);
            PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(action);
            if (piece)
                ShowCategory(player, source, piece->category, 0);
            else
                ShowCollection(player, source);
            return;
        }
        case CMD_GET_ALL:
            sPlayerHousingMgr->GetOneOfEverything(player, reason);
            Say(player, reason);
            ShowCollection(player, source);
            return;
        case CMD_STORAGE:
            ShowStorage(player, source);
            return;
        case CMD_STORAGE_TAKE:
            sPlayerHousingMgr->TakeFromStorage(player, action, false, reason);
            Say(player, reason);
            ShowStorage(player, source);
            return;
        case CMD_STORAGE_TAKE_ALL:
            sPlayerHousingMgr->TakeFromStorage(player, 0, true, reason);
            Say(player, reason);
            ShowStorage(player, source);
            return;
        case CMD_SETTINGS:
            ShowSettings(player, source);
            return;
        case CMD_PRIVACY:
            sPlayerHousingMgr->CyclePrivacy(player, reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        case CMD_GUESTS:
            ShowGuests(player, source, action >> 8);
            return;
        case CMD_GUEST_REMOVE:
            sPlayerHousingMgr->RemoveGuest(player, action, reason);
            Say(player, reason);
            ShowGuests(player, source, 0);
            return;
        case CMD_INVITE_TARGET:
            sPlayerHousingMgr->InviteTarget(player, reason);
            Say(player, reason);
            ShowGuests(player, source, 0);
            return;
        case CMD_INVITE_PARTY:
            sPlayerHousingMgr->InviteParty(player, reason);
            Say(player, reason);
            ShowGuests(player, source, 0);
            return;
        case CMD_INVITE_NAME:
            sPlayerHousingMgr->InviteGuestByName(player, text, reason);
            Say(player, reason);
            ShowGuests(player, source, 0);
            return;
        case CMD_GREETING:
            sPlayerHousingMgr->SetGreeting(player, text, reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        case CMD_GREETING_CLEAR:
            sPlayerHousingMgr->SetGreeting(player, "", reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        case CMD_VISIT:
            ShowVisit(player, source);
            return;
        case CMD_VISIT_LIST:
            ShowVisitList(player, source, uint8(action & 0xFF), action >> 8);
            return;
        case CMD_VISIT_OWNER:
            CloseGossipMenuFor(player);
            if (!sPlayerHousingMgr->VisitHouse(player, action, reason))
                Say(player, reason);
            return;
        case CMD_VISIT_NAME:
            CloseGossipMenuFor(player);
            if (!sPlayerHousingMgr->VisitHouseByName(player, text, reason))
                Say(player, reason);
            return;
        case CMD_HELP:
            ClearGossipMenuFor(player);
            Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
            Send(player, source, TEXT_HELP);
            return;
        case CMD_NEARBY:
            ShowNearby(player, source, action >> 8);
            return;
        case CMD_PIECE:
            ShowPiece(player, source, action);
            return;
        case CMD_PIECE_OP:
            DoPieceOp(player, source, action & 0xFFFFFF, action >> 24);
            return;
        case CMD_NUDGE_MENU:
            ShowNudge(player, source, action);
            return;
        case CMD_PICKUP_MENU:
            ShowPickupChoice(player, source, action);
            return;
        case CMD_HOOK_MENU:
            ShowHook(player, source, action);
            return;
        case CMD_HOOK_PLACE:
            sPlayerHousingMgr->PlaceOnHook(player, action >> 12, (action & 0xFFF) + ITEM_BASE, reason);
            Say(player, reason);
            CloseGossipMenuFor(player);
            return;
        case CMD_PACKUP:
            sPlayerHousingMgr->PackUpEverything(player, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_KEY:
            sPlayerHousingMgr->GiveHouseKey(player, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_CLOSE:
        default:
            CloseGossipMenuFor(player);
            return;
    }
}
