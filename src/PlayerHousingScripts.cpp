#include "PlayerHousingMgr.h"

#include "AllMapScript.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "ItemScript.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "StringConvert.h"
#include "Tokenize.h"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    bool ParsePositiveInteger(char const* code, uint32& outValue)
    {
        std::string raw = code ? code : "";
        std::size_t first = raw.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return false;

        std::size_t last = raw.find_last_not_of(" \t\r\n");
        std::string_view trimmed(raw.data() + first, last - first + 1);
        auto converted = Acore::StringTo<uint32>(trimmed);
        if (!converted || *converted == 0)
            return false;

        outValue = *converted;
        return true;
    }

    void SendResult(Player* player, std::string const& reason)
    {
        if (!player)
            return;

        ChatHandler(player->GetSession()).PSendSysMessage("Housing: {}", reason);
    }

    void SendLines(Player* player, std::vector<std::string> const& lines)
    {
        if (!player)
            return;

        ChatHandler handler(player->GetSession());
        for (std::string const& line : lines)
            handler.SendSysMessage(line);
    }

    void EnsureStewardAppearance(Creature* creature)
    {
        if (!creature || !sPlayerHousingMgr->IsStewardEntry(creature->GetEntry()))
            return;

        uint32 displayId = sPlayerHousingMgr->GetStewardDisplayId();
        if (!displayId)
            return;

        if (creature->GetDisplayId() != displayId || creature->GetNativeDisplayId() != displayId)
        {
            creature->SetDisplayId(displayId);
            creature->SetNativeDisplayId(displayId);
        }
    }
}

class npc_playerhousing_steward : public CreatureScript
{
public:
    npc_playerhousing_steward() : CreatureScript("npc_playerhousing_steward") { }

    enum StewardAction : uint32
    {
        ACTION_ENTER = 1,
        ACTION_VISIT_CODE = 2,
        ACTION_LEAVE = 3,
        ACTION_STATUS = 4,
        ACTION_UPGRADE = 5,

        ACTION_PRIVACY_MENU = 10,
        ACTION_SET_PRIVATE = 11,
        ACTION_SET_PUBLIC = 12,

        ACTION_STYLE_MENU = 20,
        ACTION_STYLE_HUMAN = 21,
        ACTION_STYLE_GNOME = 22,
        ACTION_STYLE_TAUREN = 23,
        ACTION_STYLE_UNDEAD = 24,

        ACTION_GUEST_MENU = 30,
        ACTION_INVITE_CODE = 31,
        ACTION_UNINVITE_CODE = 32,

        ACTION_FURNITURE_MENU = 40,
        ACTION_CATALOG_LIST = 41,
        ACTION_UNLOCK_CODE = 42,
        ACTION_PLACE_SELECT_MENU = 43,
        ACTION_MOVE_CODE = 44,
        ACTION_REMOVE_CODE = 45,
        ACTION_FURNITURE_LIST = 46,
        ACTION_PLACE_CODE = 47,
        ACTION_CANCEL_PENDING_PLACE = 48,
        ACTION_OPEN_VENDOR = 49,

        ACTION_HELP = 60,
        ACTION_BACK_MAIN = 99
    };

    static constexpr uint32 ACTION_PLACE_SELECT_BASE = 100000;
    static constexpr uint32 ACTION_PLACE_SELECT_LIMIT = 200000;

    void BuildMainMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Enter my house", GOSSIP_SENDER_MAIN, ACTION_ENTER);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Visit a player's house", GOSSIP_SENDER_MAIN, ACTION_VISIT_CODE, "Enter character name", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Leave current house", GOSSIP_SENDER_MAIN, ACTION_LEAVE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Show house status", GOSSIP_SENDER_MAIN, ACTION_STATUS);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Upgrade my house", GOSSIP_SENDER_MAIN, ACTION_UPGRADE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Privacy options", GOSSIP_SENDER_MAIN, ACTION_PRIVACY_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Change house style", GOSSIP_SENDER_MAIN, ACTION_STYLE_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Guest access list", GOSSIP_SENDER_MAIN, ACTION_GUEST_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Furniture tools", GOSSIP_SENDER_MAIN, ACTION_FURNITURE_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "How housing works", GOSSIP_SENDER_MAIN, ACTION_HELP);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    void BuildPrivacyMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Set house to private", GOSSIP_SENDER_MAIN, ACTION_SET_PRIVATE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Set house to public", GOSSIP_SENDER_MAIN, ACTION_SET_PUBLIC);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, ACTION_BACK_MAIN);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    void BuildStyleMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Human", GOSSIP_SENDER_MAIN, ACTION_STYLE_HUMAN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Gnome", GOSSIP_SENDER_MAIN, ACTION_STYLE_GNOME);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Tauren", GOSSIP_SENDER_MAIN, ACTION_STYLE_TAUREN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Undead", GOSSIP_SENDER_MAIN, ACTION_STYLE_UNDEAD);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, ACTION_BACK_MAIN);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    void BuildGuestMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Invite guest by name", GOSSIP_SENDER_MAIN, ACTION_INVITE_CODE, "Enter character name", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Remove guest by name", GOSSIP_SENDER_MAIN, ACTION_UNINVITE_CODE, "Enter character name", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, ACTION_BACK_MAIN);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    void BuildFurnitureMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Browse Krook's Cranny furniture", GOSSIP_SENDER_MAIN, ACTION_OPEN_VENDOR);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Show available catalog", GOSSIP_SENDER_MAIN, ACTION_CATALOG_LIST);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Unlock catalog item by ID", GOSSIP_SENDER_MAIN, ACTION_UNLOCK_CODE, "Enter catalog ID", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Select unlocked furniture to place", GOSSIP_SENDER_MAIN, ACTION_PLACE_SELECT_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Select furniture by catalog ID (advanced)", GOSSIP_SENDER_MAIN, ACTION_PLACE_CODE, "Enter catalog ID", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cancel pending targeted placement", GOSSIP_SENDER_MAIN, ACTION_CANCEL_PENDING_PLACE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Move furniture placement by ID", GOSSIP_SENDER_MAIN, ACTION_MOVE_CODE, "Enter placement ID", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Remove furniture placement by ID", GOSSIP_SENDER_MAIN, ACTION_REMOVE_CODE, "Enter placement ID", 0, true);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "List placed furniture", GOSSIP_SENDER_MAIN, ACTION_FURNITURE_LIST);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, ACTION_BACK_MAIN);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    void BuildPlacementSelectionMenu(Player* player, Creature* creature) const
    {
        ClearGossipMenuFor(player);

        std::vector<std::pair<uint32, std::string>> choices;
        std::string reason;
        if (!sPlayerHousingMgr->ListPlacementChoices(player, choices, reason))
        {
            SendResult(player, reason);
        }
        else
        {
            for (auto const& [catalogId, displayName] : choices)
            {
                if (catalogId >= (ACTION_PLACE_SELECT_LIMIT - ACTION_PLACE_SELECT_BASE))
                    continue;

                std::string label = displayName + " (id " + std::to_string(catalogId) + ")";
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, label, GOSSIP_SENDER_MAIN, ACTION_PLACE_SELECT_BASE + catalogId);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, ACTION_FURNITURE_MENU);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        EnsureStewardAppearance(creature);
        BuildMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        std::string reason;
        std::vector<std::string> lines;

        if (action >= ACTION_PLACE_SELECT_BASE && action < ACTION_PLACE_SELECT_LIMIT)
        {
            uint32 catalogId = action - ACTION_PLACE_SELECT_BASE;
            if (!sPlayerHousingMgr->BeginSpellPlacement(player, catalogId, reason))
                SendResult(player, reason);
            else
                SendResult(player, reason);

            BuildFurnitureMenu(player, creature);
            return true;
        }

        switch (action)
        {
            case ACTION_ENTER:
                if (!sPlayerHousingMgr->EnterOwnHouse(player, reason))
                    SendResult(player, reason);
                CloseGossipMenuFor(player);
                return true;
            case ACTION_LEAVE:
                if (!sPlayerHousingMgr->LeaveHouse(player, reason))
                    SendResult(player, reason);
                CloseGossipMenuFor(player);
                return true;
            case ACTION_STATUS:
                if (sPlayerHousingMgr->GetHouseStatus(player, lines, reason))
                    SendLines(player, lines);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_UPGRADE:
                if (!sPlayerHousingMgr->UpgradeHouse(player, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_PRIVACY_MENU:
                BuildPrivacyMenu(player, creature);
                return true;
            case ACTION_SET_PRIVATE:
                if (!sPlayerHousingMgr->SetPrivacy(player, true, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_SET_PUBLIC:
                if (!sPlayerHousingMgr->SetPrivacy(player, false, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_STYLE_MENU:
                BuildStyleMenu(player, creature);
                return true;
            case ACTION_STYLE_HUMAN:
                if (!sPlayerHousingMgr->SetStyle(player, "human", reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_STYLE_GNOME:
                if (!sPlayerHousingMgr->SetStyle(player, "gnome", reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_STYLE_TAUREN:
                if (!sPlayerHousingMgr->SetStyle(player, "tauren", reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_STYLE_UNDEAD:
                if (!sPlayerHousingMgr->SetStyle(player, "undead", reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildMainMenu(player, creature);
                return true;
            case ACTION_GUEST_MENU:
                BuildGuestMenu(player, creature);
                return true;
            case ACTION_FURNITURE_MENU:
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_OPEN_VENDOR:
                if (creature->IsVendor())
                {
                    player->GetSession()->SendListInventory(creature->GetGUID());
                    CloseGossipMenuFor(player);
                }
                else
                {
                    SendResult(player, "Vendor inventory is not available on this steward.");
                    BuildFurnitureMenu(player, creature);
                }
                return true;
            case ACTION_PLACE_SELECT_MENU:
                BuildPlacementSelectionMenu(player, creature);
                return true;
            case ACTION_CANCEL_PENDING_PLACE:
                if (!sPlayerHousingMgr->CancelSpellPlacement(player, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_CATALOG_LIST:
                if (sPlayerHousingMgr->ListCatalog(player, lines, reason))
                    SendLines(player, lines);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_FURNITURE_LIST:
                if (sPlayerHousingMgr->ListFurniture(player, lines, reason))
                    SendLines(player, lines);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_HELP:
                ChatHandler(player->GetSession()).SendSysMessage("Housing quickstart:");
                ChatHandler(player->GetSession()).SendSysMessage("- Starter house is free at stage 0.");
                ChatHandler(player->GetSession()).SendSysMessage("- Upgrade cost grows x3 each stage (up to stage 6).");
                ChatHandler(player->GetSession()).SendSysMessage("- Buy furniture from Krook's Cranny, then right click the item to place.");
                ChatHandler(player->GetSession()).SendSysMessage("- Guests can visit, but only owner can edit.");
                BuildMainMenu(player, creature);
                return true;
            case ACTION_BACK_MAIN:
                BuildMainMenu(player, creature);
                return true;
            default:
                CloseGossipMenuFor(player);
                return true;
        }
    }

    bool OnGossipSelectCode(Player* player, Creature* creature, uint32 /*sender*/, uint32 action, const char* code) override
    {
        std::string reason;

        if (action == ACTION_VISIT_CODE)
        {
            std::string ownerName = code ? code : "";
            if (!sPlayerHousingMgr->VisitHouse(player, ownerName, reason))
                SendResult(player, reason);
            CloseGossipMenuFor(player);
            return true;
        }

        if (action == ACTION_INVITE_CODE)
        {
            std::string guestName = code ? code : "";
            if (!sPlayerHousingMgr->InviteGuest(player, guestName, reason))
                SendResult(player, reason);
            else
                SendResult(player, reason);
            BuildGuestMenu(player, creature);
            return true;
        }

        if (action == ACTION_UNINVITE_CODE)
        {
            std::string guestName = code ? code : "";
            if (!sPlayerHousingMgr->RemoveGuest(player, guestName, reason))
                SendResult(player, reason);
            else
                SendResult(player, reason);
            BuildGuestMenu(player, creature);
            return true;
        }

        uint32 numericId = 0;
        if (!ParsePositiveInteger(code, numericId))
        {
            SendResult(player, "Value must be a positive number.");
            BuildFurnitureMenu(player, creature);
            return true;
        }

        switch (action)
        {
            case ACTION_UNLOCK_CODE:
                if (!sPlayerHousingMgr->UnlockCatalog(player, numericId, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_PLACE_CODE:
                if (!sPlayerHousingMgr->BeginSpellPlacement(player, numericId, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_MOVE_CODE:
                if (!sPlayerHousingMgr->MoveFurniture(player, numericId, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            case ACTION_REMOVE_CODE:
                if (!sPlayerHousingMgr->RemoveFurniture(player, numericId, reason))
                    SendResult(player, reason);
                else
                    SendResult(player, reason);
                BuildFurnitureMenu(player, creature);
                return true;
            default:
                BuildMainMenu(player, creature);
                return true;
        }
    }
};

class mod_playerhousing_worldscript : public WorldScript
{
public:
    mod_playerhousing_worldscript() : WorldScript("mod_playerhousing_worldscript") { }

    void OnStartup() override
    {
        sPlayerHousingMgr->OnStartup();
    }
};

class item_playerhousing_furniture : public ItemScript
{
public:
    item_playerhousing_furniture() : ItemScript("item_playerhousing_furniture") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return true;

        std::string reason;
        if (!sPlayerHousingMgr->BeginItemPlacement(player, item, reason))
        {
            if (!reason.empty())
                ChatHandler(player->GetSession()).PSendSysMessage("Housing: {}", reason);
            return true;
        }

        if (!reason.empty())
            ChatHandler(player->GetSession()).PSendSysMessage("Housing: {}", reason);

        // Allow default item spell cast to proceed; furniture items cast Flare for point selection.
        return false;
    }
};

class mod_playerhousing_commandscript : public CommandScript
{
public:
    mod_playerhousing_commandscript() : CommandScript("mod_playerhousing_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rootTable =
        {
            { "krook", HandleKrookCommand, SEC_PLAYER, Console::No }
        };

        return rootTable;
    }

    static void SendUsage(ChatHandler* handler)
    {
        if (!handler)
            return;

        handler->SendSysMessage("Krook commands:");
        handler->SendSysMessage(".krook add - spawn a housing steward near you");
        handler->SendSysMessage(".krook add <catalogId> - select furniture for Flare placement");
        handler->SendSysMessage(".krook leave - leave your current house");
        handler->SendSysMessage(".krook status - show current house status");
    }

    static bool HandleKrookCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler ? handler->GetPlayer() : nullptr;
        if (!player)
        {
            if (handler)
            {
                handler->SendSysMessage("Krook: this command can only be used in-game.");
                handler->SetSentErrorMessage(true);
            }
            return false;
        }

        std::vector<std::string_view> tokens = Acore::Tokenize(args ? args : "", ' ', false);
        if (tokens.empty())
        {
            SendUsage(handler);
            return true;
        }

        std::string subcommand(tokens[0]);
        std::string reason;

        if (subcommand == "add")
        {
            if (tokens.size() == 1)
            {
                float spawnDistance = 2.5f;
                float spawnAngle = player->GetOrientation();
                float spawnX = player->GetPositionX() + (std::cos(spawnAngle) * spawnDistance);
                float spawnY = player->GetPositionY() + (std::sin(spawnAngle) * spawnDistance);
                float spawnZ = player->GetMap()->GetHeight(PHASEMASK_NORMAL, spawnX, spawnY, player->GetPositionZ() + 5.0f, true, 50.0f);
                if (!std::isfinite(spawnZ) || spawnZ < -50000.0f)
                    spawnZ = player->GetPositionZ();

                uint32 stewardEntry = sPlayerHousingMgr->GetStewardEntry();
                if (Creature* steward = player->SummonCreature(stewardEntry, spawnX, spawnY, spawnZ + 0.35f, spawnAngle, TEMPSUMMON_MANUAL_DESPAWN, 0))
                {
                    EnsureStewardAppearance(steward);
                    handler->SendSysMessage("Housing: spawned a steward near you.");
                    return true;
                }

                handler->SendSysMessage("Housing: could not spawn steward.");
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (tokens.size() > 2)
            {
                handler->SendSysMessage("Usage: .krook add <catalogId>");
                handler->SetSentErrorMessage(true);
                return false;
            }

            auto catalogId = Acore::StringTo<uint32>(tokens[1]);
            if (!catalogId || *catalogId == 0)
            {
                handler->PSendSysMessage("Krook: invalid catalog id '{}'.", tokens[1]);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (!sPlayerHousingMgr->BeginSpellPlacement(player, *catalogId, reason))
            {
                handler->PSendSysMessage("Housing: {}", reason);
                handler->SetSentErrorMessage(true);
                return false;
            }

            handler->PSendSysMessage("Housing: {}", reason);
            return true;
        }

        if (subcommand == "leave")
        {
            if (!sPlayerHousingMgr->LeaveHouse(player, reason))
            {
                handler->PSendSysMessage("Housing: {}", reason);
                handler->SetSentErrorMessage(true);
                return false;
            }

            handler->SendSysMessage("Housing: Leaving house.");
            return true;
        }

        if (subcommand == "status")
        {
            std::vector<std::string> lines;
            if (!sPlayerHousingMgr->GetHouseStatus(player, lines, reason))
            {
                handler->PSendSysMessage("Housing: {}", reason);
                handler->SetSentErrorMessage(true);
                return false;
            }

            for (std::string const& line : lines)
                handler->SendSysMessage(line);
            return true;
        }

        handler->PSendSysMessage("Krook: unknown subcommand '{}'.", subcommand);
        SendUsage(handler);
        handler->SetSentErrorMessage(true);
        return false;
    }
};

class mod_playerhousing_playerscript : public PlayerScript
{
public:
    mod_playerhousing_playerscript() : PlayerScript("mod_playerhousing_playerscript") { }

    void OnPlayerLogin(Player* player) override
    {
        sPlayerHousingMgr->OnPlayerLogin(player);
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        sPlayerHousingMgr->OnPlayerLogout(player);
    }

    void OnPlayerUpdate(Player* player, uint32 diffMs) override
    {
        sPlayerHousingMgr->OnPlayerUpdate(player, diffMs);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        sPlayerHousingMgr->OnPlayerMapChanged(player);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        std::string reason;
        if (sPlayerHousingMgr->HandlePlacementSpellCast(player, spell, reason) && !reason.empty())
            ChatHandler(player->GetSession()).PSendSysMessage("Housing: {}", reason);
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        sPlayerHousingMgr->OnPlayerDelete(guid);
    }

};

class mod_playerhousing_allmapscript : public AllMapScript
{
public:
    mod_playerhousing_allmapscript() : AllMapScript("mod_playerhousing_allmapscript") { }

    void OnDestroyMap(Map* map) override
    {
        sPlayerHousingMgr->OnDestroyMap(map);
    }
};

void Addmod_playerhousingScripts()
{
    new item_playerhousing_furniture();
    new mod_playerhousing_commandscript();
    new mod_playerhousing_worldscript();
    new mod_playerhousing_playerscript();
    new mod_playerhousing_allmapscript();
    new npc_playerhousing_steward();
}
