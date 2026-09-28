#include "HousingMenus.h"
#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "GameObject.h"
#include "GlobalScript.h"
#include "Item.h"
#include "ItemScript.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "ServerScript.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;
using namespace Housing;

namespace
{
    MenuSource FromPlayer(Player* player) { return MenuSource{ SOURCE_PLAYER, player->GetGUID() }; }

    void Reply(Player* player, std::string const& reason)
    {
        if (!reason.empty())
            sPlayerHousingMgr->Say(player, reason);
    }

    void EnsureStewardAppearance(Creature* creature)
    {
        uint32 displayId = sPlayerHousingMgr->GetStewardDisplayId();
        if (creature && displayId && (creature->GetDisplayId() != displayId || creature->GetNativeDisplayId() != displayId))
        {
            creature->SetDisplayId(displayId);
            creature->SetNativeDisplayId(displayId);
        }
    }

    std::string Lower(std::string_view text)
    {
        std::string value(text);
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value;
    }
}

// Krook, the housing steward: in the capital cities and on every island.
class npc_playerhousing_steward : public CreatureScript
{
public:
    npc_playerhousing_steward() : CreatureScript("npc_playerhousing_steward") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        EnsureStewardAppearance(creature);
        HousingMenus::ShowHome(player, MenuSource{ SOURCE_CREATURE, creature->GetGUID() });
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_CREATURE, creature->GetGUID() }, sender, action, nullptr);
        return true;
    }

    bool OnGossipSelectCode(Player* player, Creature* creature, uint32 sender, uint32 action, char const* code) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_CREATURE, creature->GetGUID() }, sender, action, code);
        return true;
    }
};

// A stand's figure. The owner gets the stand's menu (dress it, turn it, pick it up); guests
// see what it's wearing.
class npc_playerhousing_mannequin : public CreatureScript
{
public:
    npc_playerhousing_mannequin() : CreatureScript("npc_playerhousing_mannequin") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        MenuSource source{ SOURCE_CREATURE, creature->GetGUID() };
        uint32 placementId = sPlayerHousingMgr->GetPlacementForObject(player, creature->GetGUID());
        if (!placementId)
            return true;

        if (sPlayerHousingMgr->IsOnOwnIsland(player))
            HousingMenus::ShowPiece(player, source, placementId);
        else
            HousingMenus::ShowStandToGuest(player, source, placementId);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_CREATURE, creature->GetGUID() }, sender, action, nullptr);
        return true;
    }
};

// A mannequin's armor reaches the client as mirror image data, which the client asks for
// when the figure comes into view. The core only answers for real mirror images (spells), so
// the module answers for mannequins.
class mod_playerhousing_serverscript : public ServerScript
{
public:
    mod_playerhousing_serverscript() : ServerScript("mod_playerhousing_serverscript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (packet.GetOpcode() != CMSG_GET_MIRRORIMAGE_DATA || packet.size() < sizeof(uint64))
            return true;

        return !sPlayerHousingMgr->SendMannequinLook(session, ObjectGuid(packet.read<uint64>(0)));
    }
};

// The House Key: its spell is caught in spell_playerhousing_key, which opens the Home menu.
class item_playerhousing_key : public ItemScript
{
public:
    item_playerhousing_key() : ItemScript("item_playerhousing_key") { }

    bool OnUse(Player* /*player*/, Item* /*item*/, SpellCastTargets const& /*targets*/) override
    {
        return false;
    }

    void OnGossipSelect(Player* player, Item* item, uint32 sender, uint32 action) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_ITEM, item->GetGUID() }, sender, action, nullptr);
    }

    void OnGossipSelectCode(Player* player, Item* item, uint32 sender, uint32 action, char const* code) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_ITEM, item->GetGUID() }, sender, action, code);
    }
};

// Furnishings and buildings: using one brings up the targeting circle (Flare); the chosen
// spot is caught in spell_playerhousing_place before anything is cast.
class item_playerhousing_piece : public ItemScript
{
public:
    item_playerhousing_piece() : ItemScript("item_playerhousing_piece") { }

    bool OnUse(Player* /*player*/, Item* /*item*/, SpellCastTargets const& /*targets*/) override
    {
        return false;
    }
};

// Places the piece where the targeting circle was clicked, then cancels the cast silently:
// no flare, no global cooldown, no stealth reveal. Hunters' own Flare is left alone. The core
// has already checked range and line of sight to the spot by the time this runs.
class spell_playerhousing_place : public SpellScript
{
    PrepareSpellScript(spell_playerhousing_place);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        Item* item = GetCastItem();
        bool mover = item && PlayerHousingMgr::IsMoverItem(item->GetEntry());
        if (!player || !item || !sPlayerHousingMgr->IsEnabled() || (!mover && !sPlayerHousingMgr->GetPiece(item->GetEntry())))
            return SPELL_CAST_OK;

        std::string reason;
        if (WorldLocation const* destination = GetExplTargetDest())
        {
            Position target;
            target.Relocate(destination->GetPositionX(), destination->GetPositionY(), destination->GetPositionZ());
            uint32 itemEntry = item->GetEntry();
            if (mover)
            {
                sPlayerHousingMgr->HandleMoveCast(player, item, target, reason);
                Reply(player, reason);
                return SPELL_FAILED_DONT_REPORT;
            }
            bool placed = sPlayerHousingMgr->HandlePlacementCast(player, item, target, reason);
            sPlayerHousingMgr->SendAddonState(player);
            // The preview: the piece stands where it'll be, and its menu offers to keep it,
            // adjust it or take it back.
            if (placed && sPlayerHousingMgr->ShouldAdjustAfterPlacing(player, itemEntry))
                if (uint32 placementId = sPlayerHousingMgr->GetSelectedPlacement(player))
                    HousingMenus::ShowPiece(player, FromPlayer(player), placementId, true);
        }
        else
            reason = "Click a spot with the targeting circle to place it.";

        Reply(player, reason);
        return SPELL_FAILED_DONT_REPORT;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_playerhousing_place::CheckCast);
    }
};

class spell_playerhousing_key : public SpellScript
{
    PrepareSpellScript(spell_playerhousing_key);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        Item* item = GetCastItem();
        if (!player || !item || item->GetEntry() != HOUSE_KEY_ITEM)
            return SPELL_CAST_OK;

        if (!sPlayerHousingMgr->IsEnabled())
        {
            Reply(player, "Housing is disabled on this server.");
            return SPELL_FAILED_DONT_REPORT;
        }

        HousingMenus::ShowHome(player, MenuSource{ SOURCE_ITEM, item->GetGUID() });
        return SPELL_FAILED_DONT_REPORT;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_playerhousing_key::CheckCast);
    }
};

// Every placed piece and snap marker. While the owner decorates, clicking a piece opens its
// menu; otherwise pieces behave like the real thing (chairs seat, mailboxes open).
class go_playerhousing_piece : public GameObjectScript
{
public:
    go_playerhousing_piece() : GameObjectScript("go_playerhousing_piece") { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (go->GetEntry() == HOOK_MARKER_GO)
        {
            if (sPlayerHousingMgr->IsDecorating(player))
                if (uint32 surface = sPlayerHousingMgr->GetSurfaceForMarker(player, go->GetGUID()))
                    HousingMenus::ShowHook(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, surface);
            return true;
        }

        uint32 placementId = sPlayerHousingMgr->GetPlacementForObject(player, go->GetGUID());
        if (!placementId)
            return false;

        if (sPlayerHousingMgr->IsDecorating(player))
        {
            HousingMenus::ShowPiece(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, placementId);
            return true;
        }

        if (sPlayerHousingMgr->IsOnOwnIsland(player) && go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
        {
            sPlayerHousingMgr->SelectPlacement(player, placementId);
            Reply(player, "To change this, start decorating: House Key, Start decorating.");
            return true;
        }

        return false;
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 sender, uint32 action) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, sender, action, nullptr);
        return true;
    }

    bool OnGossipSelectCode(Player* player, GameObject* go, uint32 sender, uint32 action, char const* code) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, sender, action, code);
        return true;
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

class mod_playerhousing_commandscript : public CommandScript
{
public:
    mod_playerhousing_commandscript() : CommandScript("mod_playerhousing_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rootTable =
        {
            { "house", HandleHouseCommand, SEC_PLAYER, Console::No },
            { "krook", HandleHouseCommand, SEC_PLAYER, Console::No }
        };
        return rootTable;
    }

    static void SendUsage(ChatHandler* handler, bool gm)
    {
        handler->SendSysMessage("Housing commands (.house alone opens the Home menu):");
        handler->SendSysMessage(".house home | leave | unstuck | key");
        handler->SendSysMessage(".house decorate [on|off] | undo | redo | packup");
        handler->SendSysMessage(".house pickup [id] [inside] | rotate <degrees> [id] | face [id] | here [id] | move [id]");
        handler->SendSysMessage(".house nudge <forward|back|left|right|up|down> [yards] [id] | select <id|nearest> | list");
        handler->SendSysMessage(".house size <bigger|smaller|normal|percent> [id] | tilt <forward|back|left|right|straight> [degrees] [id]");
        handler->SendSysMessage(".house another [id] | grid <off|yards>");
        handler->SendSysMessage(".house collection [search] | storage | visit [name] | invite <name|target|party> | uninvite <name>");
        handler->SendSysMessage(".house privacy <private|friends|public> | greeting <text|clear> | adjust <all|buildings|off>");
        if (gm)
            handler->SendSysMessage("GM: .house unlock|relock <item|name|all> [player] | unlocks [player] | add (steward)");
    }

    static Player* GmTarget(ChatHandler* handler, Player* self, std::vector<std::string_view> const& tokens, size_t index)
    {
        if (tokens.size() > index)
            if (Player* named = ObjectAccessor::FindPlayerByName(std::string(tokens[index]), true))
                return named;
        if (Player* selected = handler->getSelectedPlayer())
            return selected;
        return self;
    }

    static bool HandleHouseCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler ? handler->GetPlayer() : nullptr;
        if (!player)
        {
            if (handler)
            {
                handler->SendSysMessage("This command can only be used in-game.");
                handler->SetSentErrorMessage(true);
            }
            return false;
        }

        bool gm = player->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
        std::vector<std::string_view> tokens = Acore::Tokenize(args ? args : "", ' ', false);
        if (tokens.empty())
        {
            HousingMenus::ShowHome(player, FromPlayer(player));
            return true;
        }

        auto number = [&](size_t index) -> uint32
        {
            if (tokens.size() <= index)
                return 0;
            return Acore::StringTo<uint32>(tokens[index]).value_or(0);
        };
        auto decimal = [&](size_t index, float fallback) -> float
        {
            if (tokens.size() <= index)
                return fallback;
            return Acore::StringTo<float>(tokens[index]).value_or(fallback);
        };
        auto restFrom = [&](size_t index) -> std::string
        {
            std::string text;
            for (size_t i = index; i < tokens.size(); ++i)
                text += (text.empty() ? "" : " ") + std::string(tokens[i]);
            return text;
        };

        std::string sub = Lower(tokens[0]);
        std::string reason;
        PlayerHousingMgr* mgr = sPlayerHousingMgr;

        if (sub == "state")
        {
            // Quiet: for the client addon.
            mgr->SendAddonState(player);
            return true;
        }
        else if (sub == "home" || sub == "go")
            mgr->RequestGoHome(player, reason);
        else if (sub == "leave")
            mgr->LeaveHouse(player, reason);
        else if (sub == "unstuck")
            mgr->Unstuck(player, reason);
        else if (sub == "key")
            mgr->GiveHouseKey(player, reason);
        else if (sub == "decorate")
        {
            std::string mode = tokens.size() > 1 ? Lower(tokens[1]) : "";
            bool on = mode == "on" ? true : (mode == "off" ? false : !mgr->IsDecorating(player));
            mgr->SetDecorating(player, on, reason);
        }
        else if (sub == "undo")
            mgr->Undo(player, reason);
        else if (sub == "redo")
            mgr->Redo(player, reason);
        else if (sub == "packup")
            mgr->PackUpEverything(player, reason);
        else if (sub == "pickup")
        {
            bool inside = tokens.size() > 1 && Lower(tokens.back()) == "inside";
            mgr->PickUp(player, number(1), inside, reason);
        }
        else if (sub == "rotate" || sub == "turn")
            mgr->Rotate(player, number(2), decimal(1, 45.0f), reason);
        else if (sub == "move")
            mgr->StartMove(player, number(1), reason);
        else if (sub == "face")
            mgr->FaceMe(player, number(1), reason);
        else if (sub == "here")
            mgr->MoveHere(player, number(1), reason);
        else if (sub == "nudge" || sub == "up" || sub == "down")
        {
            std::string direction = sub == "nudge" ? (tokens.size() > 1 ? Lower(tokens[1]) : "") : sub;
            size_t distanceIndex = sub == "nudge" ? 2 : 1;
            bool vertical = direction == "up" || direction == "down";
            float distance = decimal(distanceIndex, vertical ? 0.1f : 0.25f);
            uint32 id = number(distanceIndex + 1);
            float forward = 0.0f, left = 0.0f, up = 0.0f;
            if (direction == "forward") forward = distance;
            else if (direction == "back") forward = -distance;
            else if (direction == "left") left = distance;
            else if (direction == "right") left = -distance;
            else if (direction == "up") up = distance;
            else if (direction == "down") up = -distance;
            else
                reason = "Nudge which way? forward, back, left, right, up or down.";

            if (reason.empty())
                mgr->Nudge(player, id, forward, left, up, reason);
        }
        else if (sub == "size" || sub == "resize")
        {
            std::string how = tokens.size() > 1 ? Lower(tokens[1]) : "";
            uint32 id = number(2);
            if (how == "bigger" || how == "up")
                mgr->Resize(player, id, 10.0f, true, reason);
            else if (how == "smaller" || how == "down")
                mgr->Resize(player, id, -10.0f, true, reason);
            else if (how == "normal" || how == "reset")
                mgr->Resize(player, id, 100.0f, false, reason);
            else if (float percent = decimal(1, 0.0f); percent > 0.0f)
                mgr->Resize(player, id, percent, false, reason);
            else
                reason = Acore::StringFormat("Usage: .house size <bigger|smaller|normal|percent> [id]. Sizes go from {:.0f}% to {:.0f}%.",
                    mgr->GetMinSize() * 100.0f, mgr->GetMaxSize() * 100.0f);
        }
        else if (sub == "tilt")
        {
            std::string direction = tokens.size() > 1 ? Lower(tokens[1]) : "";
            float degrees = decimal(2, 5.0f);
            uint32 id = number(3);
            if (direction == "forward")
                mgr->Tilt(player, id, degrees, 0.0f, false, reason);
            else if (direction == "back")
                mgr->Tilt(player, id, -degrees, 0.0f, false, reason);
            else if (direction == "right")
                mgr->Tilt(player, id, 0.0f, degrees, false, reason);
            else if (direction == "left")
                mgr->Tilt(player, id, 0.0f, -degrees, false, reason);
            else if (direction == "straight" || direction == "level")
                mgr->Tilt(player, number(2), 0.0f, 0.0f, true, reason);
            else
                reason = "Usage: .house tilt <forward|back|left|right|straight> [degrees] [id]. Forward tips its front down; left and right are its own.";
        }
        else if (sub == "another" || sub == "copy")
            mgr->PlaceAnother(player, number(1), reason);
        else if (sub == "grid")
        {
            std::string size = tokens.size() > 1 ? Lower(tokens[1]) : "";
            if (size == "off" || size == "0")
                mgr->SetGridSize(player, 0.0f, reason);
            else if (float yards = decimal(1, 0.0f); yards > 0.0f)
                mgr->SetGridSize(player, yards, reason);
            else if (float current = mgr->GetGridSize(player->GetGUID().GetCounter()); current > 0.0f)
                reason = Acore::StringFormat("The grid is {} yards. Usage: .house grid <off|yards> (0.25 to 4).", PlayerHousingMgr::FormatYards(current));
            else
                reason = "The grid is off. Usage: .house grid <off|yards> (0.25 to 4).";
        }
        else if (sub == "select")
        {
            uint32 id = number(1);
            if (!id)
            {
                auto nearby = mgr->GetNearbyPlacements(player, 25.0f);
                id = nearby.empty() ? 0 : nearby.front().first.id;
            }
            if (id && mgr->GetPlacement(player, id))
            {
                mgr->SelectPlacement(player, id);
                reason = "Selected " + mgr->GetPiece(mgr->GetPlacement(player, id)->itemEntry)->name + ".";
                mgr->SendAddonState(player);
            }
            else
                reason = "No piece with that number nearby.";
        }
        else if (sub == "list")
        {
            auto nearby = mgr->GetNearbyPlacements(player, 40.0f);
            if (nearby.empty())
                reason = "No pieces within 40 yards.";
            for (auto const& [placement, distance] : nearby)
                handler->PSendSysMessage("#{} {} ({:.0f} yd)", placement.id, mgr->GetPiece(placement.itemEntry)->name, distance);
        }
        else if (sub == "collection")
        {
            if (tokens.size() > 1)
                HousingMenus::ShowCollectionSearch(player, FromPlayer(player), restFrom(1));
            else
                HousingMenus::ShowCollection(player, FromPlayer(player));
        }
        else if (sub == "storage")
            HousingMenus::ShowStorage(player, FromPlayer(player));
        else if (sub == "visit")
        {
            if (tokens.size() > 1)
                mgr->VisitHouseByName(player, std::string(tokens[1]), reason);
            else
                HousingMenus::ShowVisit(player, FromPlayer(player));
        }
        else if (sub == "invite")
        {
            std::string who = tokens.size() > 1 ? std::string(tokens[1]) : "target";
            if (Lower(who) == "target")
                mgr->InviteTarget(player, reason);
            else if (Lower(who) == "party")
                mgr->InviteParty(player, reason);
            else
                mgr->InviteGuestByName(player, who, reason);
        }
        else if (sub == "uninvite")
        {
            if (tokens.size() > 1)
                mgr->RemoveGuestByName(player, std::string(tokens[1]), reason);
            else
                reason = "Usage: .house uninvite <name>";
        }
        else if (sub == "privacy")
        {
            std::string mode = tokens.size() > 1 ? Lower(tokens[1]) : "";
            if (mode == "private")
                mgr->SetPrivacy(player, PRIVACY_PRIVATE, reason);
            else if (mode == "friends" || mode == "guild")
                mgr->SetPrivacy(player, PRIVACY_FRIENDS, reason);
            else if (mode == "public")
                mgr->SetPrivacy(player, PRIVACY_PUBLIC, reason);
            else
                mgr->CyclePrivacy(player, reason);
        }
        else if (sub == "adjust")
        {
            std::string mode = tokens.size() > 1 ? Lower(tokens[1]) : "";
            if (mode == "all" || mode == "everything")
                mgr->SetAdjustMode(player, ADJUST_ALL, reason);
            else if (mode == "buildings")
                mgr->SetAdjustMode(player, ADJUST_BUILDINGS, reason);
            else if (mode == "off" || mode == "never")
                mgr->SetAdjustMode(player, ADJUST_NEVER, reason);
            else
                reason = Acore::StringFormat("The adjust menu opens {}. Usage: .house adjust <all|buildings|off>",
                    PlayerHousingMgr::AdjustModeName(mgr->GetAdjustMode(player->GetGUID().GetCounter())));
        }
        else if (sub == "greeting")
        {
            std::string text = restFrom(1);
            mgr->SetGreeting(player, Lower(text) == "clear" ? "" : text, reason);
        }
        else if (gm && (sub == "unlock" || sub == "relock"))
        {
            if (tokens.size() < 2)
                reason = "Usage: .house unlock|relock <item|name|all> [player]";
            else
                mgr->GmUnlock(GmTarget(handler, player, tokens, 2), std::string(tokens[1]), sub == "unlock", reason);
        }
        else if (gm && sub == "unlocks")
        {
            Player* target = GmTarget(handler, player, tokens, 1);
            uint32 unlocked = 0;
            uint32 total = 0;
            mgr->CollectionCounts(target, -1, unlocked, total);
            reason = Acore::StringFormat("{} has {} of {} pieces unlocked.", target->GetName(), unlocked, total);
        }
        else if (gm && sub == "add")
        {
            float angle = player->GetOrientation();
            float x = player->GetPositionX() + std::cos(angle) * 2.5f;
            float y = player->GetPositionY() + std::sin(angle) * 2.5f;
            if (Creature* steward = player->SummonCreature(mgr->GetStewardEntry(), x, y, player->GetPositionZ(), angle + float(M_PI), TEMPSUMMON_TIMED_DESPAWN, 10 * MINUTE * IN_MILLISECONDS))
            {
                EnsureStewardAppearance(steward);
                reason = "Krook will wait here for ten minutes.";
            }
            else
                reason = "Could not summon Krook.";
        }
        else
        {
            SendUsage(handler, gm);
            return true;
        }

        Reply(player, reason);
        return true;
    }
};

class mod_playerhousing_playerscript : public PlayerScript
{
public:
    mod_playerhousing_playerscript() : PlayerScript("mod_playerhousing_playerscript") { }

    void OnPlayerLogin(Player* player) override { sPlayerHousingMgr->OnPlayerLogin(player); }
    void OnPlayerBeforeLogout(Player* player) override { sPlayerHousingMgr->OnPlayerLogout(player); }
    void OnPlayerUpdate(Player* player, uint32 diffMs) override { sPlayerHousingMgr->OnPlayerUpdate(player, diffMs); }
    void OnPlayerMapChanged(Player* player) override { sPlayerHousingMgr->OnPlayerMapChanged(player); }
    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override { sPlayerHousingMgr->OnPlayerDelete(guid); }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32 sender, uint32 action) override
    {
        if (menuId == PLAYER_MENU_ID)
            HousingMenus::HandleSelect(player, FromPlayer(player), sender, action, nullptr);
    }

    void OnPlayerGossipSelectCode(Player* player, uint32 menuId, uint32 sender, uint32 action, char const* code) override
    {
        if (menuId == PLAYER_MENU_ID)
            HousingMenus::HandleSelect(player, FromPlayer(player), sender, action, code);
    }

    // Progress that unlocks pieces for the Collection.
    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        sPlayerHousingMgr->EvaluateUnlocks(player, RULE_ACHIEVEMENT, achievement->ID, true);
    }

    void OnPlayerReputationRankChange(Player* player, uint32 factionId, ReputationRank newRank, ReputationRank /*oldRank*/, bool increased) override
    {
        if (increased)
            sPlayerHousingMgr->EvaluateUnlocks(player, RULE_REPUTATION, factionId, true, uint32(newRank));
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        if (quest)
            sPlayerHousingMgr->EvaluateUnlocks(player, RULE_QUEST, quest->GetQuestId(), true);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (killed)
            sPlayerHousingMgr->OnCreatureKilled(killer, killed->GetEntry());
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        if (killed)
            sPlayerHousingMgr->OnCreatureKilled(owner, killed->GetEntry());
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        sPlayerHousingMgr->EvaluateUnlocks(player, RULE_EXPLORE, newZone, true);
    }

    void OnPlayerUpdateArea(Player* player, uint32 /*oldArea*/, uint32 newArea) override
    {
        sPlayerHousingMgr->EvaluateUnlocks(player, RULE_EXPLORE, newArea, true);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        sPlayerHousingMgr->EvaluateUnlocks(player, RULE_LEVEL, 0, true, player->GetLevel());
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/, uint32 newValue) override
    {
        sPlayerHousingMgr->EvaluateUnlocks(player, RULE_SKILL, skillId, true, newValue);
    }
};

class mod_playerhousing_globalscript : public GlobalScript
{
public:
    mod_playerhousing_globalscript() : GlobalScript("mod_playerhousing_globalscript", { GLOBALHOOK_ON_BEFORE_WORLDOBJECT_SET_PHASEMASK }) { }

    void OnBeforeWorldObjectSetPhaseMask(WorldObject const* /*worldObject*/, uint32& oldPhaseMask, uint32& newPhaseMask, bool& useCombinedPhases, bool& /*update*/) override
    {
        sPlayerHousingMgr->OnBeforeSetPhaseMask(oldPhaseMask, newPhaseMask, useCombinedPhases);
    }
};

void Addmod_playerhousingScripts()
{
    new npc_playerhousing_steward();
    new npc_playerhousing_mannequin();
    new mod_playerhousing_serverscript();
    new item_playerhousing_key();
    new item_playerhousing_piece();
    RegisterSpellScript(spell_playerhousing_place);
    RegisterSpellScript(spell_playerhousing_key);
    new go_playerhousing_piece();
    new mod_playerhousing_worldscript();
    new mod_playerhousing_commandscript();
    new mod_playerhousing_playerscript();
    new mod_playerhousing_globalscript();
}
