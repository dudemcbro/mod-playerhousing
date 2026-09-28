#include "HousingMenus.h"

#include "Chat.h"
#include "GossipDef.h"
#include "Item.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "StringFormat.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>
#include <unordered_map>

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
        CMD_STAND_DRESS_MENU, // action: placement id | page << 24
        CMD_STAND_PUT,        // action: index into the list the dress menu showed
        CMD_STAND_TAKE_OFF,   // action: placement id | slot << 24 (0xFF: everything)
        CMD_STAND_FIGURE,     // action: placement id
        CMD_ADJUST_MODE,
        CMD_SHAPE_MENU,       // action: placement id
        CMD_GRID,
        CMD_COLLECTION_FILTER,
        CMD_COLLECTION_SEARCH, // coded
        CMD_SEARCH_PAGE,      // action: page << 8
        CMD_SEARCH_LOCKED,    // action: page << 8 (shows the hint again)
        CMD_PIECE_INFO,       // action: item entry | origin << 24
        CMD_GET_COPIES,       // action: item entry | count << 20 | origin << 24
        CMD_LAYOUTS,
        CMD_LAYOUT_SAVE_NEW,  // coded: the name
        CMD_LAYOUT_PAGE,      // action: layout id
        CMD_LAYOUT_SWITCH,    // action: layout id
        CMD_LAYOUT_OVERWRITE, // action: layout id
        CMD_LAYOUT_RENAME,    // coded, action: layout id
        CMD_LAYOUT_SEND,      // coded, action: layout id
        CMD_LAYOUT_DELETE,    // action: layout id
        CMD_LAYOUT_GET_MISSING, // action: layout id
        CMD_LAYOUT_COPYABLE,
        CMD_LAYOUT_COPY_ISLAND,
        CMD_CHEST_BANK,       // action: placement id
        CMD_CLOSE
    };

    constexpr uint32 SLOT_EVERYTHING = 0xFF;

    // Item guids don't fit in a gossip action next to the stand, so the dress menu remembers
    // what it listed and each option carries its index.
    struct DressChoice
    {
        uint32 placementId{0};
        std::vector<uint32> itemGuids;
    };
    std::mutex dressChoicesLock;
    std::unordered_map<ObjectGuid, DressChoice> dressChoices;

    // What the player last searched the Collection for, for its pages.
    std::mutex searchesLock;
    std::unordered_map<ObjectGuid, std::string> searches;

    // Where a piece's Collection page goes back to.
    enum PieceOrigin : uint32
    {
        ORIGIN_CATEGORY = 0,
        ORIGIN_SEARCH = 1
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
        OP_UNDO,
        OP_MOVE_CIRCLE,
        OP_TURN_LEFT_90,
        OP_TURN_RIGHT_90,
        OP_TURN_LEFT_5,
        OP_TURN_RIGHT_5,
        OP_TILT_FORWARD,
        OP_TILT_BACK,
        OP_TILT_LEFT,
        OP_TILT_RIGHT,
        OP_STRAIGHTEN,
        OP_BIGGER,
        OP_SMALLER,
        OP_NORMAL_SIZE,
        OP_ANOTHER
    };

    // Ops of the "More turns, tilt and size" menu, which comes back after each.
    bool IsShapeOp(uint32 op)
    {
        return op == OP_TURN_LEFT_15 || op == OP_TURN_RIGHT_15 || (op >= OP_TURN_LEFT_90 && op <= OP_NORMAL_SIZE);
    }

    constexpr uint32 PAGE_SIZE = 18;
    constexpr uint32 ITEM_BASE = 900000;
    constexpr float NUDGE_STEP = 0.25f;
    constexpr float RAISE_STEP = 0.1f;
    constexpr float TILT_STEP = 5.0f;
    constexpr float SIZE_STEP = 10.0f;
    constexpr float GRID_STEPS[] = { 0.0f, 0.5f, 1.0f, 2.0f };  // what the settings menu cycles through

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

    // One page of a Collection list: unlocked pieces open their page, locked ones say how to
    // earn them. Pieces shown as new are new no more.
    void AddPieceLines(Player* player, std::vector<PieceDefinition const*> const& pieces, uint32 page, std::set<uint32> const& known,
        std::set<uint32> const& fresh, uint32 origin, uint32 lockedCommand, uint32 lockedAction)
    {
        std::vector<uint32> seen;
        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < pieces.size() && i < first + PAGE_SIZE; ++i)
        {
            PieceDefinition const* piece = pieces[i];
            if (sPlayerHousingMgr->IsUnlocked(player, *piece, &known))
            {
                bool isNew = fresh.count(piece->itemEntry) > 0;
                if (isNew)
                    seen.push_back(piece->itemEntry);
                Add(player, GOSSIP_ICON_VENDOR, piece->name + (isNew ? " (new)" : ""), CMD_PIECE_INFO, piece->itemEntry | (origin << 24));
            }
            else
                Add(player, GOSSIP_ICON_DOT, piece->name + ": " + sPlayerHousingMgr->DescribeProgress(player, *piece), lockedCommand, lockedAction);
        }
        sPlayerHousingMgr->MarkSeen(player, seen);
    }

    // With "unlocked only" on, the locked ones are left out.
    std::vector<PieceDefinition const*> Listed(Player* player, std::vector<PieceDefinition const*> const& pieces, std::set<uint32> const& known,
        uint32& unlocked)
    {
        bool unlockedOnly = sPlayerHousingMgr->IsCollectionUnlockedOnly(player->GetGUID().GetCounter());
        std::vector<PieceDefinition const*> listed;
        unlocked = 0;
        for (PieceDefinition const* piece : pieces)
        {
            bool open = sPlayerHousingMgr->IsUnlocked(player, *piece, &known);
            if (open)
                ++unlocked;
            if (open || !unlockedOnly)
                listed.push_back(piece);
        }
        return listed;
    }

    void ShowCategory(Player* player, MenuSource const& source, uint8 category, uint32 page)
    {
        std::vector<PieceDefinition const*> all = sPlayerHousingMgr->GetPiecesInCategory(category);
        std::set<uint32> known = sPlayerHousingMgr->LoadUnlocks(player);
        std::set<uint32> fresh = sPlayerHousingMgr->LoadNewUnlocks(player);
        uint32 unlocked = 0;
        std::vector<PieceDefinition const*> pieces = Listed(player, all, known, unlocked);

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {} of {} unlocked", PlayerHousingMgr::CategoryName(category), unlocked, all.size()),
            CMD_CATEGORY, category | (page << 8));
        AddPieceLines(player, pieces, page, known, fresh, ORIGIN_CATEGORY, CMD_LOCKED, category | (page << 8));
        Paging(player, CMD_CATEGORY, category, page, uint32(pieces.size()));
        Add(player, GOSSIP_ICON_CHAT, "Back to the Collection", CMD_COLLECTION);
        Send(player, source, TEXT_COLLECTION);
    }

    std::string GetSearch(Player* player)
    {
        std::lock_guard<std::mutex> guard(searchesLock);
        auto itr = searches.find(player->GetGUID());
        return itr != searches.end() ? itr->second : "";
    }

    void ShowSearch(Player* player, MenuSource const& source, uint32 page)
    {
        std::string text = GetSearch(player);
        if (text.empty())
        {
            HousingMenus::ShowCollection(player, source);
            return;
        }

        std::set<uint32> known = sPlayerHousingMgr->LoadUnlocks(player);
        std::set<uint32> fresh = sPlayerHousingMgr->LoadNewUnlocks(player);
        uint32 unlocked = 0;
        std::vector<PieceDefinition const*> pieces = Listed(player, sPlayerHousingMgr->SearchPieces(text), known, unlocked);

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("\"{}\": {} found, {} unlocked", text, pieces.size(), unlocked), CMD_SEARCH_PAGE, page << 8);
        AddPieceLines(player, pieces, page, known, fresh, ORIGIN_SEARCH, CMD_SEARCH_LOCKED, page << 8);
        Paging(player, CMD_SEARCH_PAGE, 0, page, uint32(pieces.size()));
        Ask(player, GOSSIP_ICON_CHAT, pieces.empty() ? "Nothing matches. Search again..." : "Search again...", CMD_COLLECTION_SEARCH);
        Add(player, GOSSIP_ICON_CHAT, "Back to the Collection", CMD_COLLECTION);
        Send(player, source, TEXT_COLLECTION);
    }

    std::string Have(Player* player, PieceDefinition const& piece)
    {
        ObjectGuid::LowType self = player->GetGUID().GetCounter();
        std::map<uint32, uint32> storage = sPlayerHousingMgr->GetStorage(self);
        uint32 bags = player->GetItemCount(piece.itemEntry);
        uint32 stored = storage.count(piece.itemEntry) ? storage[piece.itemEntry] : 0;
        uint32 placed = sPlayerHousingMgr->CountPlacedOf(self, piece.itemEntry);

        std::vector<std::string> parts;
        if (bags)
            parts.push_back(Acore::StringFormat("{} in your bags", bags));
        if (stored)
            parts.push_back(Acore::StringFormat("{} in storage", stored));
        if (placed)
            parts.push_back(Acore::StringFormat("{} placed", placed));
        if (parts.empty())
            return "you have none yet";
        std::string text;
        for (size_t i = 0; i < parts.size(); ++i)
            text += (i == 0 ? "" : (i + 1 == parts.size() ? " and " : ", ")) + parts[i];
        return text;
    }

    void ShowPieceInfo(Player* player, MenuSource const& source, uint32 itemEntry, uint32 origin)
    {
        PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(itemEntry);
        if (!piece)
        {
            HousingMenus::ShowCollection(player, source);
            return;
        }

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {}", piece->name, Have(player, *piece)), CMD_PIECE_INFO, itemEntry | (origin << 24));
        if (sPlayerHousingMgr->IsUnlocked(player, *piece))
        {
            uint32 cost = sPlayerHousingMgr->IsFreeMode() ? 0 : piece->copyCost;
            for (uint32 count : { 1u, 5u })
            {
                std::string label = count == 1 ? "Get one" : Acore::StringFormat("Get {}", count);
                uint32 action = itemEntry | (count << 20) | (origin << 24);
                if (cost)
                    Confirm(player, GOSSIP_ICON_VENDOR, label + " (" + PlayerHousingMgr::FormatMoney(uint64(cost) * count) + ")", CMD_GET_COPIES, action,
                        count == 1 ? "Get a " + piece->name + "?" : Acore::StringFormat("Get {} of the {}?", count, piece->name), cost * count);
                else
                    Add(player, GOSSIP_ICON_VENDOR, label, CMD_GET_COPIES, action);
            }
        }
        else
            Add(player, GOSSIP_ICON_DOT, "Locked: " + sPlayerHousingMgr->DescribeProgress(player, *piece), CMD_PIECE_INFO, itemEntry | (origin << 24));

        if (origin == ORIGIN_SEARCH)
            Add(player, GOSSIP_ICON_CHAT, "Back to the search", CMD_SEARCH_PAGE, 0);
        else
            Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("Back to {}", PlayerHousingMgr::CategoryName(piece->category)), CMD_CATEGORY,
                piece->category);
        Send(player, source, TEXT_COLLECTION);
    }

    void ShowLayouts(Player* player, MenuSource const& source)
    {
        ObjectGuid::LowType self = player->GetGUID().GetCounter();
        std::vector<SavedLayout> layouts = sPlayerHousingMgr->GetSavedLayouts(self);
        uint32 most = sPlayerHousingMgr->GetMaxSavedLayouts();

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("Saved layouts: {} of {}", layouts.size(), most), CMD_LAYOUTS);
        if (layouts.size() < most)
            Ask(player, GOSSIP_ICON_INTERACT_1, "Save my island as a new layout...", CMD_LAYOUT_SAVE_NEW);
        else
            Add(player, GOSSIP_ICON_DOT, "All full: save over one, or delete one", CMD_LAYOUTS);
        for (SavedLayout const& layout : layouts)
            Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("{} ({} pieces, {})", layout.name, layout.pieces, layout.savedAt), CMD_LAYOUT_PAGE, layout.id);
        Add(player, GOSSIP_ICON_CHAT, "Back to Island settings", CMD_SETTINGS);
        Send(player, source, TEXT_SETTINGS);
    }

    void ShowLayoutPage(Player* player, MenuSource const& source, uint32 layoutId)
    {
        ObjectGuid::LowType self = player->GetGUID().GetCounter();
        std::optional<SavedLayout> layout = sPlayerHousingMgr->GetSavedLayout(self, layoutId);
        if (!layout)
        {
            ShowLayouts(player, source);
            return;
        }

        std::map<uint32, uint32> missing = sPlayerHousingMgr->LayoutShortfall(player, layoutId);
        uint32 gettable = 0;
        uint64 cost = 0;
        uint32 locked = 0;
        sPlayerHousingMgr->DescribeShortfall(player, missing, gettable, cost, locked);

        ClearGossipMenuFor(player);
        std::string from = layout->source.empty() ? "" : ", from " + layout->source;
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {} pieces, saved {}{}", layout->name, layout->pieces, layout->savedAt, from),
            CMD_LAYOUT_PAGE, layoutId);
        if (sPlayerHousingMgr->IsOnOwnIsland(player))
            Confirm(player, GOSSIP_ICON_INTERACT_1, "Set it out on my island", CMD_LAYOUT_SWITCH, layoutId,
                "Pack up your island and set out " + layout->name + "? Gear on mannequins goes back to your bags. Pieces you don't have are "
                "left out. You can undo this.");
        else
            Add(player, GOSSIP_ICON_DOT, "Go home to set it out", CMD_LAYOUT_PAGE, layoutId);

        if (gettable)
        {
            std::string price = cost ? " (" + PlayerHousingMgr::FormatMoney(cost) + ")" : "";
            std::string text = Acore::StringFormat("Get the {} missing {}{}", gettable, gettable == 1 ? "piece" : "pieces", price);
            if (cost)
                Confirm(player, GOSSIP_ICON_MONEY_BAG, text, CMD_LAYOUT_GET_MISSING, layoutId, "Get the missing pieces?", uint32(std::min<uint64>(cost, 0xFFFFFFFF)));
            else
                Add(player, GOSSIP_ICON_MONEY_BAG, text, CMD_LAYOUT_GET_MISSING, layoutId);
        }
        if (locked)
            Add(player, GOSSIP_ICON_DOT, Acore::StringFormat("{} {} still locked for you", locked, locked == 1 ? "piece is" : "pieces are"),
                CMD_LAYOUT_PAGE, layoutId);

        Confirm(player, GOSSIP_ICON_INTERACT_1, "Save my island over it", CMD_LAYOUT_OVERWRITE, layoutId,
            "Replace " + layout->name + " with your island as it is now?");
        Ask(player, GOSSIP_ICON_INTERACT_1, "Rename it...", CMD_LAYOUT_RENAME, layoutId);
        Ask(player, GOSSIP_ICON_TALK, "Send a copy to a player...", CMD_LAYOUT_SEND, layoutId);
        Confirm(player, GOSSIP_ICON_INTERACT_1, "Delete it", CMD_LAYOUT_DELETE, layoutId,
            "Delete the layout " + layout->name + "? Your island stays as it is.");
        Add(player, GOSSIP_ICON_CHAT, "Back to saved layouts", CMD_LAYOUTS);
        Send(player, source, TEXT_SETTINGS);
    }

    std::string CollectionLabel(Player* player, uint32 unlocked)
    {
        size_t fresh = sPlayerHousingMgr->LoadNewUnlocks(player).size();
        if (fresh)
            return Acore::StringFormat("Collection ({} unlocked, {} new)", unlocked, fresh);
        return Acore::StringFormat("Collection ({} unlocked)", unlocked);
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
        Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Adjust menu opens: {} (click to change)",
            PlayerHousingMgr::AdjustModeName(sPlayerHousingMgr->GetAdjustMode(self))), CMD_ADJUST_MODE);
        if (sPlayerHousingMgr->GetMaxSavedLayouts())
        {
            Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("Saved layouts ({} of {})", sPlayerHousingMgr->GetSavedLayouts(self).size(),
                sPlayerHousingMgr->GetMaxSavedLayouts()), CMD_LAYOUTS);
            Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Visitors may copy my layout: {} (click to change)",
                sPlayerHousingMgr->IsLayoutCopyable(self) ? "yes" : "no"), CMD_LAYOUT_COPYABLE);
        }
        float grid = sPlayerHousingMgr->GetGridSize(self);
        Add(player, GOSSIP_ICON_INTERACT_1, grid > 0.0f
            ? Acore::StringFormat("Grid: {} yd (click to change)", PlayerHousingMgr::FormatYards(grid))
            : std::string("Grid: off (click to change)"), CMD_GRID);
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

    std::string DescribeShape(Placement const& placement, PieceDefinition const& piece)
    {
        float normal = piece.scale > 0.0f ? piece.scale : 1.0f;
        std::string text = Acore::StringFormat("Size {}%", int32(std::lround(placement.scale / normal * 100.0f)));
        int32 forward = int32(std::lround(placement.pitch * 180.0f / 3.14159265f));
        int32 right = int32(std::lround(placement.roll * 180.0f / 3.14159265f));
        if (forward)
            text += Acore::StringFormat(", tilted {}° {}", std::abs(forward), forward > 0 ? "forward" : "back");
        if (right)
            text += Acore::StringFormat("{} {}° to its {}", forward ? " and" : ", tilted", std::abs(right), right > 0 ? "right" : "left");
        return text;
    }

    void ShowShape(Player* player, MenuSource const& source, uint32 placementId)
    {
        std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId);
        PieceDefinition const* piece = placement ? sPlayerHousingMgr->GetPiece(placement->itemEntry) : nullptr;
        if (!piece || !sPlayerHousingMgr->IsOnOwnIsland(player))
        {
            CloseGossipMenuFor(player);
            return;
        }

        auto op = [&](uint32 which) { return placementId | (which << 24); };
        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {}", piece->name, DescribeShape(*placement, *piece)), CMD_SHAPE_MENU, placementId);
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 90°", CMD_PIECE_OP, op(OP_TURN_LEFT_90));
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 90°", CMD_PIECE_OP, op(OP_TURN_RIGHT_90));
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 15°", CMD_PIECE_OP, op(OP_TURN_LEFT_15));
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 15°", CMD_PIECE_OP, op(OP_TURN_RIGHT_15));
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 5°", CMD_PIECE_OP, op(OP_TURN_LEFT_5));
        Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 5°", CMD_PIECE_OP, op(OP_TURN_RIGHT_5));
        if (!piece->HasFlag(PIECE_FLAG_STAND) && sPlayerHousingMgr->GetMaxTilt() > 0.0f)
        {
            Add(player, GOSSIP_ICON_INTERACT_1, "Tilt forward 5° (its front down)", CMD_PIECE_OP, op(OP_TILT_FORWARD));
            Add(player, GOSSIP_ICON_INTERACT_1, "Tilt back 5°", CMD_PIECE_OP, op(OP_TILT_BACK));
            Add(player, GOSSIP_ICON_INTERACT_1, "Tilt to its left 5°", CMD_PIECE_OP, op(OP_TILT_LEFT));
            Add(player, GOSSIP_ICON_INTERACT_1, "Tilt to its right 5°", CMD_PIECE_OP, op(OP_TILT_RIGHT));
        }
        if (placement->pitch != 0.0f || placement->roll != 0.0f)
            Add(player, GOSSIP_ICON_INTERACT_1, "Stand it straight", CMD_PIECE_OP, op(OP_STRAIGHTEN));
        if (sPlayerHousingMgr->GetMinSize() < 1.0f || sPlayerHousingMgr->GetMaxSize() > 1.0f)
        {
            Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Bigger (up to {:.0f}%)", sPlayerHousingMgr->GetMaxSize() * 100.0f),
                CMD_PIECE_OP, op(OP_BIGGER));
            Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Smaller (down to {:.0f}%)", sPlayerHousingMgr->GetMinSize() * 100.0f),
                CMD_PIECE_OP, op(OP_SMALLER));
        }
        float normal = piece->scale > 0.0f ? piece->scale : 1.0f;
        if (std::lround(placement->scale / normal * 100.0f) != 100)
            Add(player, GOSSIP_ICON_INTERACT_1, "Normal size", CMD_PIECE_OP, op(OP_NORMAL_SIZE));
        if (!sPlayerHousingMgr->UndoLabel(player).empty())
            Add(player, GOSSIP_ICON_INTERACT_2, "Undo: " + sPlayerHousingMgr->UndoLabel(player), CMD_PIECE_OP, op(OP_UNDO));
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
        std::string pieces = Acore::StringFormat("{} {}", inside, inside == 1 ? "piece" : "pieces");
        ClearGossipMenuFor(player);
        Confirm(player, GOSSIP_ICON_INTERACT_1, "Pick up the building only", CMD_PIECE_OP, placementId | (OP_PICKUP << 24),
            inside ? Acore::StringFormat("Return the {} to your bags? The {} inside {} where {}.", piece->name, pieces,
                                         inside == 1 ? "stays" : "stay", inside == 1 ? "it is" : "they are")
                   : Acore::StringFormat("Return the {} to your bags?", piece->name));
        if (inside)
            Confirm(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Pick up the building and the {} inside it", pieces),
                CMD_PIECE_OP, placementId | (OP_PICKUP_WITH_INSIDE << 24),
                Acore::StringFormat("Return the {} and the {} inside it to your bags?", piece->name, pieces));
        Add(player, GOSSIP_ICON_CHAT, "Back to the building", CMD_PIECE, placementId);
        Send(player, source, TEXT_PIECE);
    }

    // After a change the piece is a new object; a menu opened from the old one would point
    // at nothing, so it follows the piece. `pieceObject` is the piece's object before the change.
    MenuSource FollowPiece(Player* player, MenuSource const& source, uint32 placementId, ObjectGuid const& pieceObject)
    {
        if (source.type == SOURCE_PLAYER || source.type == SOURCE_ITEM || source.guid != pieceObject)
            return source;

        ObjectGuid guid = sPlayerHousingMgr->GetObjectForPlacement(player, placementId);
        if (guid.IsEmpty())
            return MenuSource{ SOURCE_PLAYER, player->GetGUID() };
        return MenuSource{ guid.IsGameObject() ? SOURCE_GAMEOBJECT : SOURCE_CREATURE, guid };
    }

    void ShowDress(Player* player, MenuSource const& source, uint32 placementId, uint32 page)
    {
        std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId);
        PieceDefinition const* piece = placement ? sPlayerHousingMgr->GetPiece(placement->itemEntry) : nullptr;
        if (!piece || !piece->HasFlag(PIECE_FLAG_STAND))
        {
            CloseGossipMenuFor(player);
            return;
        }

        std::vector<Item*> items = sPlayerHousingMgr->GetWearableItems(player);
        DressChoice choice;
        choice.placementId = placementId;
        for (Item* item : items)
            choice.itemGuids.push_back(item->GetGUID().GetCounter());
        {
            std::lock_guard<std::mutex> guard(dressChoicesLock);
            dressChoices[player->GetGUID()] = choice;
        }

        ClearGossipMenuFor(player);
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("What should the {} wear?", piece->name), CMD_STAND_DRESS_MENU, placementId);
        if (items.empty())
            Add(player, GOSSIP_ICON_CHAT, "Nothing in your bags shows on a mannequin: armor you can see, weapons, shields, shirts and tabards do.",
                CMD_PIECE, placementId);

        uint32 first = page * PAGE_SIZE;
        for (uint32 i = first; i < items.size() && i < first + PAGE_SIZE; ++i)
        {
            ItemTemplate const* proto = items[i]->GetTemplate();
            int8 slot = PlayerHousingMgr::StandSlotFor(proto, placement->gear);
            std::string swap;
            auto worn = placement->gear.find(uint8(slot));
            if (worn != placement->gear.end())
                swap = ", instead of " + PlayerHousingMgr::StandItemName(worn->second.itemEntry);
            Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("{} ({}{})", proto->Name1, PlayerHousingMgr::StandSlotName(uint8(slot)), swap),
                CMD_STAND_PUT, i);
        }

        if (page > 0)
            Add(player, GOSSIP_ICON_DOT, "Previous page", CMD_STAND_DRESS_MENU, placementId | ((page - 1) << 24));
        if ((page + 1) * PAGE_SIZE < items.size())
            Add(player, GOSSIP_ICON_DOT, "Next page", CMD_STAND_DRESS_MENU, placementId | ((page + 1) << 24));
        Add(player, GOSSIP_ICON_CHAT, "Back", CMD_PIECE, placementId);
        Send(player, source, TEXT_PIECE);
    }

    // Stand changes respawn the figure, so the menu follows it, like DoPieceOp.
    void AfterStandChange(Player* player, MenuSource const& source, uint32 placementId, ObjectGuid const& pieceObject, std::string const& reason)
    {
        Say(player, reason);
        if (!sPlayerHousingMgr->GetPlacement(player, placementId))
        {
            CloseGossipMenuFor(player);
            return;
        }
        HousingMenus::ShowPiece(player, FollowPiece(player, source, placementId, pieceObject), placementId);
    }

    void DoPieceOp(Player* player, MenuSource const& source, uint32 placementId, uint32 op)
    {
        std::string reason;
        bool keepMenu = true;
        ObjectGuid pieceObject = sPlayerHousingMgr->GetObjectForPlacement(player, placementId);
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
            case OP_TURN_LEFT_90: sPlayerHousingMgr->Rotate(player, placementId, 90.0f, reason); break;
            case OP_TURN_RIGHT_90: sPlayerHousingMgr->Rotate(player, placementId, -90.0f, reason); break;
            case OP_TURN_LEFT_5: sPlayerHousingMgr->Rotate(player, placementId, 5.0f, reason); break;
            case OP_TURN_RIGHT_5: sPlayerHousingMgr->Rotate(player, placementId, -5.0f, reason); break;
            case OP_TILT_FORWARD: sPlayerHousingMgr->Tilt(player, placementId, TILT_STEP, 0.0f, false, reason); break;
            case OP_TILT_BACK: sPlayerHousingMgr->Tilt(player, placementId, -TILT_STEP, 0.0f, false, reason); break;
            case OP_TILT_LEFT: sPlayerHousingMgr->Tilt(player, placementId, 0.0f, -TILT_STEP, false, reason); break;
            case OP_TILT_RIGHT: sPlayerHousingMgr->Tilt(player, placementId, 0.0f, TILT_STEP, false, reason); break;
            case OP_STRAIGHTEN: sPlayerHousingMgr->Tilt(player, placementId, 0.0f, 0.0f, true, reason); break;
            case OP_BIGGER: sPlayerHousingMgr->Resize(player, placementId, SIZE_STEP, true, reason); break;
            case OP_SMALLER: sPlayerHousingMgr->Resize(player, placementId, -SIZE_STEP, true, reason); break;
            case OP_NORMAL_SIZE: sPlayerHousingMgr->Resize(player, placementId, 100.0f, false, reason); break;
            case OP_ANOTHER:
                // The menu closes so the copy can be placed.
                sPlayerHousingMgr->PlaceAnother(player, placementId, reason);
                keepMenu = false;
                break;
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
            case OP_MOVE_CIRCLE:
                // The menu closes so the "Move a Piece" item can be used.
                sPlayerHousingMgr->StartMove(player, placementId, reason);
                keepMenu = false;
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

        MenuSource followed = FollowPiece(player, source, placementId, pieceObject);
        bool nudging = op >= OP_NUDGE_FORWARD && op <= OP_NUDGE_DOWN;
        if (nudging)
            ShowNudge(player, followed, placementId);
        else if (IsShapeOp(op))
            ShowShape(player, followed, placementId);
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
        Add(player, GOSSIP_ICON_VENDOR, CollectionLabel(player, unlocked), CMD_COLLECTION);
        if (stored)
            Add(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("Storage ({})", stored), CMD_STORAGE, 0);
        Add(player, GOSSIP_ICON_TABARD, "Island settings", CMD_SETTINGS);
        if (sPlayerHousingMgr->GetMaxSavedLayouts())
            Add(player, GOSSIP_ICON_VENDOR, "Saved layouts", CMD_LAYOUTS);
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
        if (sPlayerHousingMgr->GetMaxSavedLayouts() && sPlayerHousingMgr->IsLayoutCopyable(islandOwner))
            Add(player, GOSSIP_ICON_VENDOR, "Save a copy of this island's layout", CMD_LAYOUT_COPY_ISLAND);
        Add(player, GOSSIP_ICON_TAXI, "Visit another island", CMD_VISIT);
        Add(player, GOSSIP_ICON_VENDOR, CollectionLabel(player, unlocked), CMD_COLLECTION);
        Add(player, GOSSIP_ICON_CHAT, "Unstuck: back to the landing spot", CMD_UNSTUCK);
        Add(player, GOSSIP_ICON_TAXI, "Leave the island", CMD_LEAVE);
        Add(player, GOSSIP_ICON_CHAT, "How housing works", CMD_HELP);
    }
    else
    {
        Add(player, GOSSIP_ICON_CHAT, StatusLine(player), CMD_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Go home", CMD_GO_HOME);
        Add(player, GOSSIP_ICON_TAXI, "Visit an island", CMD_VISIT);
        Add(player, GOSSIP_ICON_VENDOR, CollectionLabel(player, unlocked), CMD_COLLECTION);
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
    std::set<uint32> fresh = sPlayerHousingMgr->LoadNewUnlocks(player);
    bool unlockedOnly = sPlayerHousingMgr->IsCollectionUnlockedOnly(player->GetGUID().GetCounter());
    uint32 unlocked = 0;
    uint32 total = 0;
    sPlayerHousingMgr->CollectionCounts(player, -1, unlocked, total, &known);

    std::map<uint8, uint32> freshByCategory;
    for (uint32 itemEntry : fresh)
        if (PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(itemEntry))
            ++freshByCategory[piece->category];
    auto newText = [](size_t count) { return count ? Acore::StringFormat(", {} new", count) : std::string(); };

    ClearGossipMenuFor(player);
    Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("Collection: {} of {} unlocked{}", unlocked, total, newText(fresh.size())), CMD_COLLECTION);
    Ask(player, GOSSIP_ICON_CHAT, "Search by name...", CMD_COLLECTION_SEARCH);
    Add(player, GOSSIP_ICON_INTERACT_1, unlockedOnly ? "Showing unlocked pieces only (click to show all)"
                                                     : "Showing all pieces (click to show only unlocked)", CMD_COLLECTION_FILTER);
    for (uint8 category = 0; category < CATEGORY_COUNT; ++category)
    {
        uint32 categoryUnlocked = 0;
        uint32 categoryTotal = 0;
        sPlayerHousingMgr->CollectionCounts(player, category, categoryUnlocked, categoryTotal, &known);
        if (categoryTotal && (categoryUnlocked || !unlockedOnly))
            Add(player, GOSSIP_ICON_VENDOR, Acore::StringFormat("{} ({}/{}{})", PlayerHousingMgr::CategoryName(category), categoryUnlocked, categoryTotal,
                newText(freshByCategory[category])), CMD_CATEGORY, category);
    }

    if (sPlayerHousingMgr->IsFreeMode() || sPlayerHousingMgr->IsUnlockAll())
        Add(player, GOSSIP_ICON_MONEY_BAG, "Give me one of everything (test server)", CMD_GET_ALL);
    Add(player, GOSSIP_ICON_CHAT, "Back", CMD_HOME);
    Send(player, source, TEXT_COLLECTION);
}

void HousingMenus::ShowChest(Player* player, MenuSource const& source, uint32 placementId)
{
    uint32 stored = 0;
    for (auto const& [itemEntry, count] : sPlayerHousingMgr->GetStorage(player->GetGUID().GetCounter()))
        stored += count;

    ClearGossipMenuFor(player);
    Add(player, GOSSIP_ICON_MONEY_BAG, "Open my bank", CMD_CHEST_BANK, placementId);
    Add(player, GOSSIP_ICON_MONEY_BAG, Acore::StringFormat("House Storage ({} {})", stored, stored == 1 ? "piece" : "pieces"), CMD_STORAGE, 0);
    Add(player, GOSSIP_ICON_CHAT, "Done", CMD_CLOSE);
    Send(player, source, TEXT_CHEST);
}

void HousingMenus::ShowSavedLayouts(Player* player, MenuSource const& source)
{
    ShowLayouts(player, source);
}

void HousingMenus::ShowCollectionSearch(Player* player, MenuSource const& source, std::string const& text)
{
    // Trimmed, and short enough for a menu line.
    size_t begin = text.find_first_not_of(" \t");
    size_t end = text.find_last_not_of(" \t");
    std::string wanted = begin == std::string::npos ? "" : text.substr(begin, end - begin + 1).substr(0, 40);
    {
        std::lock_guard<std::mutex> guard(searchesLock);
        if (wanted.empty())
            searches.erase(player->GetGUID());
        else
            searches[player->GetGUID()] = wanted;
    }
    ShowSearch(player, source, 0);
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

void HousingMenus::ShowPiece(Player* player, MenuSource const& source, uint32 placementId, bool justPlaced)
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
    if (justPlaced)
    {
        Add(player, GOSSIP_ICON_CHAT, "Keep it here", CMD_CLOSE);
        Add(player, GOSSIP_ICON_INTERACT_2, "Take it back (back to your bags)", CMD_PIECE_OP, placementId | (OP_PICKUP << 24));
    }
    if (piece->HasFlag(PIECE_FLAG_STAND))
    {
        Add(player, GOSSIP_ICON_VENDOR, "Put gear on...", CMD_STAND_DRESS_MENU, placementId);
        for (auto const& [slot, gear] : placement->gear)
            Add(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("Take off {} ({})", PlayerHousingMgr::StandItemName(gear.itemEntry),
                PlayerHousingMgr::StandSlotName(slot)), CMD_STAND_TAKE_OFF, placementId | (uint32(slot) << 24));
        if (placement->gear.size() > 1)
            Add(player, GOSSIP_ICON_INTERACT_1, "Take everything off", CMD_STAND_TAKE_OFF, placementId | (SLOT_EVERYTHING << 24));
        Add(player, GOSSIP_ICON_TABARD, "Figure: " + PlayerHousingMgr::LookName(placement->look) + " (change)", CMD_STAND_FIGURE, placementId);
    }
    if (piece->IsBuilding())
        Add(player, GOSSIP_ICON_INTERACT_1, "Pick up...", CMD_PICKUP_MENU, placementId);
    else if (!placement->gear.empty())
        Add(player, GOSSIP_ICON_INTERACT_1, "Pick up (it and its gear go back to your bags)", CMD_PIECE_OP, placementId | (OP_PICKUP << 24));
    else
        Add(player, GOSSIP_ICON_INTERACT_1, "Pick up (back to your bags)", CMD_PIECE_OP, placementId | (OP_PICKUP << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn left 45°", CMD_PIECE_OP, placementId | (OP_TURN_LEFT_45 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn right 45°", CMD_PIECE_OP, placementId | (OP_TURN_RIGHT_45 << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "Turn toward me", CMD_PIECE_OP, placementId | (OP_FACE_ME << 24));
    Add(player, GOSSIP_ICON_INTERACT_2, "More turns, tilt and size...", CMD_SHAPE_MENU, placementId);
    Add(player, GOSSIP_ICON_INTERACT_1, "Nudge...", CMD_NUDGE_MENU, placementId);
    Add(player, GOSSIP_ICON_INTERACT_1, "Move with the targeting circle", CMD_PIECE_OP, placementId | (OP_MOVE_CIRCLE << 24));
    Add(player, GOSSIP_ICON_INTERACT_1, "Move to where I'm standing", CMD_PIECE_OP, placementId | (OP_MOVE_HERE << 24));
    if (piece->HasFlag(PIECE_FLAG_SURFACE))
        Add(player, GOSSIP_ICON_VENDOR, "Put something on top", CMD_HOOK_MENU, placementId);
    Add(player, GOSSIP_ICON_VENDOR, "Place another like this", CMD_PIECE_OP, placementId | (OP_ANOTHER << 24));
    if (!justPlaced && !sPlayerHousingMgr->UndoLabel(player).empty())
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
        case CMD_LAYOUTS:
            ShowLayouts(player, source);
            return;
        case CMD_CHEST_BANK:
            CloseGossipMenuFor(player);
            if (!sPlayerHousingMgr->OpenBankAtChest(player, action, reason))
                Say(player, reason);
            return;
        case CMD_LAYOUT_SAVE_NEW:
            sPlayerHousingMgr->SaveLayout(player, 0, text, reason);
            Say(player, reason);
            ShowLayouts(player, source);
            return;
        case CMD_LAYOUT_PAGE:
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_SWITCH:
            if (sPlayerHousingMgr->SwitchLayout(player, action, reason))
            {
                Say(player, reason);
                CloseGossipMenuFor(player);
                return;
            }
            Say(player, reason);
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_OVERWRITE:
            sPlayerHousingMgr->SaveLayout(player, action, "", reason);
            Say(player, reason);
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_RENAME:
            sPlayerHousingMgr->RenameLayout(player, action, text, reason);
            Say(player, reason);
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_SEND:
            sPlayerHousingMgr->SendLayout(player, action, text, reason);
            Say(player, reason);
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_DELETE:
            sPlayerHousingMgr->DeleteLayout(player, action, reason);
            Say(player, reason);
            ShowLayouts(player, source);
            return;
        case CMD_LAYOUT_GET_MISSING:
            sPlayerHousingMgr->GetMissingForLayout(player, action, reason);
            Say(player, reason);
            ShowLayoutPage(player, source, action);
            return;
        case CMD_LAYOUT_COPYABLE:
            sPlayerHousingMgr->SetLayoutCopyable(player, !sPlayerHousingMgr->IsLayoutCopyable(player->GetGUID().GetCounter()), reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        case CMD_LAYOUT_COPY_ISLAND:
            sPlayerHousingMgr->CopyIslandLayout(player, reason);
            Say(player, reason);
            ShowHome(player, source);
            return;
        case CMD_COLLECTION_FILTER:
            sPlayerHousingMgr->SetCollectionUnlockedOnly(player, !sPlayerHousingMgr->IsCollectionUnlockedOnly(player->GetGUID().GetCounter()));
            ShowCollection(player, source);
            return;
        case CMD_COLLECTION_SEARCH:
            ShowCollectionSearch(player, source, text);
            return;
        case CMD_SEARCH_PAGE:
            ShowSearch(player, source, action >> 8);
            return;
        case CMD_SEARCH_LOCKED:
            Say(player, "Locked pieces unlock by themselves when you earn them.");
            ShowSearch(player, source, action >> 8);
            return;
        case CMD_PIECE_INFO:
            ShowPieceInfo(player, source, action & 0xFFFFFF, action >> 24);
            return;
        case CMD_GET_COPIES:
            sPlayerHousingMgr->GetCopies(player, action & 0xFFFFF, (action >> 20) & 0xF, reason);
            Say(player, reason);
            ShowPieceInfo(player, source, action & 0xFFFFF, action >> 24);
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
        case CMD_SHAPE_MENU:
            ShowShape(player, source, action);
            return;
        case CMD_GRID:
        {
            // Off, then each size in turn, then off again.
            float grid = sPlayerHousingMgr->GetGridSize(player->GetGUID().GetCounter());
            float next = GRID_STEPS[0];
            for (size_t i = 0; i + 1 < std::size(GRID_STEPS); ++i)
                if (grid >= GRID_STEPS[i] - 0.01f && grid < GRID_STEPS[i + 1] - 0.01f)
                    next = GRID_STEPS[i + 1];
            sPlayerHousingMgr->SetGridSize(player, next, reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        }
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
        case CMD_ADJUST_MODE:
        {
            uint8 mode = sPlayerHousingMgr->GetAdjustMode(player->GetGUID().GetCounter());
            uint8 next = mode == ADJUST_BUILDINGS ? ADJUST_ALL : (mode == ADJUST_ALL ? ADJUST_NEVER : ADJUST_BUILDINGS);
            sPlayerHousingMgr->SetAdjustMode(player, next, reason);
            Say(player, reason);
            ShowSettings(player, source);
            return;
        }
        case CMD_STAND_DRESS_MENU:
            ShowDress(player, source, action & 0xFFFFFF, action >> 24);
            return;
        case CMD_STAND_PUT:
        {
            DressChoice choice;
            {
                std::lock_guard<std::mutex> guard(dressChoicesLock);
                auto itr = dressChoices.find(player->GetGUID());
                if (itr != dressChoices.end())
                    choice = itr->second;
            }
            if (action >= choice.itemGuids.size())
            {
                CloseGossipMenuFor(player);
                return;
            }
            ObjectGuid pieceObject = sPlayerHousingMgr->GetObjectForPlacement(player, choice.placementId);
            sPlayerHousingMgr->PutOnStand(player, choice.placementId, choice.itemGuids[action], reason);
            AfterStandChange(player, source, choice.placementId, pieceObject, reason);
            return;
        }
        case CMD_STAND_TAKE_OFF:
        {
            uint32 placementId = action & 0xFFFFFF;
            uint32 slot = action >> 24;
            ObjectGuid pieceObject = sPlayerHousingMgr->GetObjectForPlacement(player, placementId);
            sPlayerHousingMgr->TakeOffStand(player, placementId, slot == SLOT_EVERYTHING ? -1 : int32(slot), reason);
            AfterStandChange(player, source, placementId, pieceObject, reason);
            return;
        }
        case CMD_STAND_FIGURE:
        {
            ObjectGuid pieceObject = sPlayerHousingMgr->GetObjectForPlacement(player, action);
            sPlayerHousingMgr->ChangeStandFigure(player, action, reason);
            AfterStandChange(player, source, action, pieceObject, reason);
            return;
        }
        case CMD_CLOSE:
        default:
            CloseGossipMenuFor(player);
            return;
    }
}

void HousingMenus::ShowStandToGuest(Player* player, MenuSource const& source, uint32 placementId)
{
    std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId);
    PieceDefinition const* piece = placement ? sPlayerHousingMgr->GetPiece(placement->itemEntry) : nullptr;
    if (!piece)
    {
        CloseGossipMenuFor(player);
        return;
    }

    ClearGossipMenuFor(player);
    if (placement->gear.empty())
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("The {} isn't wearing anything yet.", piece->name), CMD_CLOSE);
    for (auto const& [slot, gear] : placement->gear)
        Add(player, GOSSIP_ICON_CHAT, Acore::StringFormat("{}: {}", PlayerHousingMgr::StandSlotName(slot), PlayerHousingMgr::StandItemName(gear.itemEntry)), CMD_CLOSE);
    Send(player, source, TEXT_STAND);
}
