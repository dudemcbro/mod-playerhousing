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
#include "QuestDef.h"
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
#include <limits>
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

    // Taking "Home Sweet Island" from Krook on the island itself: already there.
    bool OnQuestAccept(Player* player, Creature* /*creature*/, Quest const* quest) override
    {
        if (quest->GetQuestId() == QUEST_TOUR_HOME && sPlayerHousingMgr->IsOnOwnIsland(player))
            PlayerHousingMgr::QuestEvent(player, QUEST_TOUR_HOME);
        return false;
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

        if (sPlayerHousingMgr->CanDecorate(player))
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

// A figurine: its owner (and roommates) get its piece menu; visitors hear what it is.
class npc_playerhousing_figurine : public CreatureScript
{
public:
    npc_playerhousing_figurine() : CreatureScript("npc_playerhousing_figurine") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        MenuSource source{ SOURCE_CREATURE, creature->GetGUID() };
        uint32 placementId = sPlayerHousingMgr->GetPlacementForObject(player, creature->GetGUID());
        if (!placementId)
            return true;

        if (sPlayerHousingMgr->CanDecorate(player))
            HousingMenus::ShowPiece(player, source, placementId);
        else
            Reply(player, Acore::StringFormat("{}: a trophy of {}'s adventures.", creature->GetName(),
                sPlayerHousingMgr->NameOf(sPlayerHousingMgr->GetIslandOwner(player))));
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        HousingMenus::HandleSelect(player, MenuSource{ SOURCE_CREATURE, creature->GetGUID() }, sender, action, nullptr);
        return true;
    }
};

// Incoming packets, for two things. Items owed for placed pieces are settled first. And a
// mannequin's armor reaches the client as mirror image data, which the client asks for when
// the figure comes into view: the core only answers for real mirror images (spells), so the
// module answers for mannequins.
class mod_playerhousing_serverscript : public ServerScript
{
public:
    mod_playerhousing_serverscript() : ServerScript("mod_playerhousing_serverscript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        // An item used to place a piece is taken before anything else the player sends is
        // handled: moving it to the bank or the mail in the same breath doesn't keep it.
        if (sPlayerHousingMgr->HasPendingConsumes())
            if (Player* player = session->GetPlayer(); player && player->IsInWorld())
                sPlayerHousingMgr->ProcessPendingConsumes(player);

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

// Furnishings and buildings: using one brings up the targeting circle of the item's spell;
// the chosen spot is caught in spell_playerhousing_place before anything is cast.
class item_playerhousing_piece : public ItemScript
{
public:
    item_playerhousing_piece() : ItemScript("item_playerhousing_piece") { }

    bool OnUse(Player* /*player*/, Item* /*item*/, SpellCastTargets const& /*targets*/) override
    {
        return false;
    }
};

// Places the piece where the targeting circle was clicked, then cancels the cast silently, so
// the borrowed spell never does anything. Only casts from housing items are caught. The core
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

        // With the client addon, its window; the menu is a button away there.
        if (sPlayerHousingMgr->KeyOpensWindow(player))
            sPlayerHousingMgr->SendAddon(player, "open");
        else
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

        // Ctrl held (the addon says so) while decorating: the piece joins the group, or leaves it.
        if (sPlayerHousingMgr->IsDecorating(player) && sPlayerHousingMgr->IsGroupHold(player))
        {
            std::string reason;
            sPlayerHousingMgr->ToggleGroupMember(player, placementId, reason);
            return true;
        }

        // Edit mode: a click only picks the piece; the keys do the rest.
        if (sPlayerHousingMgr->IsInEditMode(player))
        {
            sPlayerHousingMgr->SelectPlacement(player, placementId);
            sPlayerHousingMgr->SendAddonState(player);
            return true;
        }

        if (sPlayerHousingMgr->IsDecorating(player))
        {
            HousingMenus::ShowPiece(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, placementId);
            return true;
        }

        // A Bank Chest opens for its owner only (it's their bank).
        if (std::optional<Placement> placement = sPlayerHousingMgr->GetPlacement(player, placementId))
        {
            PieceDefinition const* piece = sPlayerHousingMgr->GetPiece(placement->itemEntry);
            if (piece && piece->HasFlag(PIECE_FLAG_CHEST))
            {
                if (sPlayerHousingMgr->IsOnOwnIsland(player))
                    HousingMenus::ShowChest(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() }, placementId);
                else
                    Reply(player, "The chest is locked: it holds its owner's bank.");
                return true;
            }
            if (piece && piece->HasFlag(PIECE_FLAG_MUSIC))
            {
                if (sPlayerHousingMgr->IsOnOwnIsland(player))
                    HousingMenus::ShowMusicBox(player, MenuSource{ SOURCE_GAMEOBJECT, go->GetGUID() });
                else
                {
                    HouseRecord house;
                    char const* track = sPlayerHousingMgr->GetHouseRecord(sPlayerHousingMgr->GetIslandOwner(player), house) && house.music
                        ? PlayerHousingMgr::MusicName(house.music) : nullptr;
                    Reply(player, track ? Acore::StringFormat("The music box is playing {}.", track) : std::string("The music box is quiet."));
                }
                return true;
            }
        }

        if (sPlayerHousingMgr->CanDecorate(player) && go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
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
        handler->SendSysMessage(".house decorate [on|off] | edit [on|off] | undo | redo | packup");
        handler->SendSysMessage(".house shift <forward> <left> <up> <degrees> [id] | select next|previous");
        handler->SendSysMessage(".house pickup [id] [inside] | rotate <degrees> [id] | face [id] | here [id] | move [id]");
        handler->SendSysMessage(".house nudge <forward|back|left|right|up|down> [yards] [id] | select <id|nearest> | list");
        handler->SendSysMessage(".house size <bigger|smaller|normal|percent> [id] | tilt <forward|back|left|right|straight> [degrees] [id]");
        handler->SendSysMessage(".house ghost <item> | ghost move [id] | ghost adjust <forward> <left> <up> <degrees> | ghost at <x> <y> <z>");
        handler->SendSysMessage(".house ghost place [another] | ghost cancel");
        handler->SendSysMessage(".house another [id] | grid <off|yards> | roommate <name> | unroommate <name> | like | visitors");
        handler->SendSysMessage(".house layout [save <name> | load <name> | delete <name> | send <name> <player> | list]");
        handler->SendSysMessage(".house collection [search] | storage | visit [name] | invite <name|target|party> | uninvite <name>");
        handler->SendSysMessage(".house get <item> [count] | take <item|all> | weather <name> | time <name> | music <sound id|off>");
        handler->SendSysMessage(".house privacy <private|friends|public> | greeting <text|clear> | adjust <all|buildings|off>");
        handler->SendSysMessage(".house group [add|remove <id> | clear] | match <height|turn|line|space> | row <count> [yards] [right|left|forward|back]");
        handler->SendSysMessage(".house set [save <name> | place <name> | delete <name> | list] | undo [steps] | goto <id>");
        handler->SendSysMessage(".house sign <note> (while visiting) | guestbook [delete <id>] | door [here|reset]");
        handler->SendSysMessage(".house report <what's wrong> (while visiting)");
        if (gm)
        {
            handler->SendSysMessage("GM: .house unlock|relock <item|name|all> [player] | unlocks [player] | add (steward)");
            handler->SendSysMessage("GM: .house reports [all] | close <id> | inspect <player> | hide|unhide <player> | cleargreeting <player> | gmpackup <player>");
            handler->SendSysMessage("GM: .house phototour <start|next|stop|item> (the addon's /housing phototour takes the pictures)");
        }
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
            // nan and inf read as numbers too; they aren't turns or distances.
            std::optional<float> value = Acore::StringTo<float>(tokens[index]);
            return value && std::isfinite(*value) ? *value : fallback;
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

        // The addon's state request is quiet and cheap; edit mode's moves have their own
        // window; everything else counts.
        bool holdMessage = sub == "group" && tokens.size() > 1 && Lower(tokens[1]) == "hold";
        bool ghostAdjust = sub == "ghost" && tokens.size() > 1 && Lower(tokens[1]) == "adjust";
        bool ghostAt = sub == "ghost" && tokens.size() > 1 && Lower(tokens[1]) == "at";
        if (ghostAt)
        {
            if (!gm && mgr->PointFlood(player))
                return true;
        }
        else if (sub == "shift" || ghostAdjust)
        {
            if (!gm && mgr->ShiftFlood(player))
                return true;
        }
        else if (sub != "state" && !holdMessage && !gm && mgr->CommandFlood(player))
        {
            Reply(player, "Too many housing commands at once: give it a moment.");
            return true;
        }

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
        else if (sub == "addon")
        {
            // Quiet: the addon says it's there, whether the House Key opens its window, and
            // whether PlayerHousing.dll is there (mouse).
            mgr->SetAddonClient(player, !(tokens.size() > 2 && tokens[2] == "0"), tokens.size() > 3 && Lower(tokens[3]) == "mouse");
            mgr->SendAddonState(player);
            return true;
        }
        else if (sub == "data")
        {
            // Quiet: lists for the addon's window.
            if (tokens.size() < 2 || !mgr->SendAddonData(player, Lower(tokens[1]), tokens.size() > 2 ? std::string(tokens[2]) : "", reason))
                Reply(player, reason.empty() ? "Usage: .house data <collection|placed|layouts|guests|visits <list>|island>" : reason);
            return true;
        }
        else if (sub == "seen")
        {
            mgr->MarkAllSeen(player);
            return true;
        }
        else if (sub == "get")
        {
            uint32 item = number(1);
            uint32 count = std::clamp<uint32>(tokens.size() > 2 ? number(2) : 1, 1, 20);
            if (!item)
                reason = "Usage: .house get <item entry> [count]";
            else if (count == 1)
                mgr->GetCopy(player, item, reason);
            else
                mgr->GetCopies(player, item, count, reason);
        }
        else if (sub == "take")
            mgr->TakeFromStorageCommand(player, tokens.size() > 1 ? Lower(tokens[1]) : "", number(2), reason);
        else if (sub == "weather" || sub == "time")
        {
            // By number, or by name: .house weather light rain
            bool weather = sub == "weather";
            uint8 count = weather ? PlayerHousingMgr::WeatherCount() : PlayerHousingMgr::TimeOfDayCount();
            std::string wanted = Lower(restFrom(1));
            int32 choice = -1;
            for (uint8 i = 0; i < count; ++i)
                if (wanted == std::to_string(i) || wanted == Lower(weather ? PlayerHousingMgr::WeatherName(i) : PlayerHousingMgr::TimeOfDayName(i)))
                    choice = i;
            if (choice < 0)
            {
                std::string names;
                for (uint8 i = 0; i < count; ++i)
                    names += (names.empty() ? "" : ", ") + std::string(weather ? PlayerHousingMgr::WeatherName(i) : PlayerHousingMgr::TimeOfDayName(i));
                reason = Acore::StringFormat("Usage: .house {} <{}>", sub, names);
            }
            else if (weather)
                mgr->SetWeather(player, uint8(choice), reason);
            else
                mgr->SetTimeOfDay(player, uint8(choice), reason);
        }
        else if (sub == "music")
        {
            std::string wanted = tokens.size() > 1 ? Lower(tokens[1]) : "";
            mgr->SetMusic(player, wanted == "off" || wanted == "none" ? 0 : number(1), reason);
        }
        else if (sub == "edit")
        {
            std::string mode = tokens.size() > 1 ? Lower(tokens[1]) : "";
            bool on = mode == "on" ? true : (mode == "off" ? false : !mgr->IsInEditMode(player));
            mgr->SetEditMode(player, on, reason);
        }
        else if (sub == "shift")
        {
            // Edit mode: forward, left and up in yards, a turn in degrees, then the piece.
            // Quiet when it works; the addon shows the result.
            if (mgr->Shift(player, number(5), decimal(1, 0.0f), decimal(2, 0.0f), decimal(3, 0.0f), decimal(4, 0.0f), reason))
                reason.clear();
        }
        else if (sub == "undo")
            mgr->UndoSteps(player, std::max<uint32>(1, number(1)), reason);
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
        else if (sub == "report")
            mgr->ReportIsland(player, restFrom(1), reason);
        else if (gm && sub == "reports")
        {
            bool all = tokens.size() > 1 && Lower(tokens[1]) == "all";
            auto reports = mgr->GetReports(all, 20);
            if (reports.empty())
                reason = all ? "No reports yet." : "No open reports.";
            for (IslandReport const& report : reports)
                handler->PSendSysMessage("#{} {}: {}'s island, reported by {}: {}{}", report.id, report.when, mgr->NameOf(report.ownerGuid),
                    mgr->NameOf(report.reporterGuid), report.reason, report.closed ? " (closed)" : "");
        }
        else if (gm && sub == "close")
            mgr->CloseReport(player, number(1), reason);
        else if (gm && (sub == "inspect" || sub == "hide" || sub == "unhide" || sub == "cleargreeting" || sub == "gmpackup"))
        {
            ObjectGuid::LowType target = 0;
            std::string name;
            if (tokens.size() < 2 || !mgr->ResolvePlayerGuid(std::string(tokens[1]), target, name))
                reason = Acore::StringFormat("Usage: .house {} <player>", sub);
            else if (sub == "inspect")
                mgr->GmInspect(player, target, reason);
            else if (sub == "hide" || sub == "unhide")
                mgr->GmSetHidden(player, target, sub == "hide", reason);
            else if (sub == "cleargreeting")
                mgr->GmClearGreeting(player, target, reason);
            else
                mgr->GmPackUp(player, target, reason);
        }
        else if (sub == "like")
            mgr->ToggleLike(player, reason);
        else if (sub == "visitors")
        {
            ObjectGuid::LowType self = player->GetGUID().GetCounter();
            auto log = mgr->GetVisitorLog(self, 20);
            reason = Acore::StringFormat("{} visitors this week, {} likes.{}", mgr->CountVisitorsThisWeek(self), mgr->CountLikes(self),
                log.empty() ? " Nobody has visited yet." : "");
            for (auto const& [name, when] : log)
                handler->PSendSysMessage("{}, {}", name, when);
        }
        else if (sub == "roommate" || sub == "unroommate")
        {
            ObjectGuid::LowType guestGuid = 0;
            std::string name;
            if (tokens.size() < 2)
                reason = Acore::StringFormat("Usage: .house {} <name>", sub);
            else if (!mgr->ResolvePlayerGuid(std::string(tokens[1]), guestGuid, name))
                reason = "No character with that name.";
            else
                mgr->SetRoommate(player, guestGuid, sub == "roommate", reason);
        }
        else if (sub == "layout" || sub == "layouts")
        {
            std::string what = tokens.size() > 1 ? Lower(tokens[1]) : "";
            ObjectGuid::LowType self = player->GetGUID().GetCounter();
            auto find = [&](size_t index) -> uint32
            {
                std::string name = tokens.size() > index ? std::string(tokens[index]) : "";
                if (what == "send" && tokens.size() > 3)
                    name = std::string(tokens[2]);
                else
                    name = restFrom(index);
                std::optional<SavedLayout> layout = mgr->FindSavedLayout(self, name);
                if (!layout)
                    reason = name.empty() ? "Which layout? .house layout list shows them." : "You have no layout called " + name + ".";
                return layout ? layout->id : 0;
            };
            if (what.empty())
            {
                HousingMenus::ShowSavedLayouts(player, FromPlayer(player));
                return true;
            }
            else if (what == "save")
                mgr->SaveLayout(player, 0, restFrom(2), reason);
            else if (what == "load" || what == "switch" || what == "set")
            {
                if (uint32 id = find(2))
                    mgr->SwitchLayout(player, id, reason);
            }
            else if (what == "delete")
            {
                if (uint32 id = find(2))
                    mgr->DeleteLayout(player, id, reason);
            }
            else if (what == "send")
            {
                if (tokens.size() < 4)
                    reason = "Usage: .house layout send <layout> <player>";
                else if (uint32 id = find(2))
                    mgr->SendLayout(player, id, std::string(tokens[3]), reason);
            }
            else if (what == "list")
            {
                std::vector<SavedLayout> layouts = mgr->GetSavedLayouts(self);
                if (layouts.empty())
                    reason = "No saved layouts yet: .house layout save <name>.";
                for (SavedLayout const& layout : layouts)
                    handler->PSendSysMessage("#{} {} ({} pieces, {})", layout.id, layout.name, layout.pieces, layout.savedAt);
            }
            else
                reason = "Usage: .house layout [save <name> | load <name> | delete <name> | send <name> <player> | list]";
        }
        else if (sub == "another" || sub == "copy")
            mgr->PlaceAnother(player, number(1), reason);
        else if (sub == "ghost")
        {
            // A piece following the player until it's set down (the addon's keys drive it).
            std::string what = tokens.size() > 1 ? Lower(tokens[1]) : "";
            if (what == "move")
                mgr->StartGhostMove(player, number(2), reason);
            else if (what == "adjust")
            {
                // Quiet when it works: the ghost itself shows it.
                if (mgr->AdjustGhost(player, decimal(2, 0.0f), decimal(3, 0.0f), decimal(4, 0.0f), decimal(5, 0.0f), reason))
                    reason.clear();
            }
            else if (what == "at")
            {
                // Quiet: where the mouse points, a few times a second (the addon, with
                // PlayerHousing.dll); x y z, then which way the surface there faces.
                float nan = std::numeric_limits<float>::quiet_NaN();
                float facing[3] = { decimal(5, nan), decimal(6, nan), decimal(7, nan) };
                mgr->GhostAt(player, decimal(2, nan), decimal(3, nan), decimal(4, nan), tokens.size() > 7 ? facing : nullptr, reason);
                return true;
            }
            else if (what == "place")
                mgr->PlaceGhost(player, tokens.size() > 2 && Lower(tokens[2]) == "another", reason);
            else if (what == "cancel")
                mgr->CancelGhost(player);
            else if (what == "new" && tokens.size() > 2)
                mgr->StartGhostNew(player, number(2), 0, reason);
            else if (uint32 item = number(1))
                mgr->StartGhostNew(player, item, 0, reason);
            else
                reason = "Usage: .house ghost <item> | move [id] | adjust <forward> <left> <up> <degrees> | at <x> <y> <z> | place [another] | cancel";
        }
        else if (sub == "group")
        {
            std::string what = tokens.size() > 1 ? Lower(tokens[1]) : "";
            if (what == "hold")
            {
                // Quiet: the addon says Ctrl went down or up.
                mgr->SetGroupHold(player, tokens.size() > 2 && Lower(tokens[2]) == "on");
                return true;
            }
            if (what == "add" || what == "remove")
                mgr->SetGroupMember(player, number(2), what == "add", reason);
            else if (what == "clear")
            {
                mgr->ClearGroup(player);
                reason = "One piece selected again.";
            }
            else
            {
                size_t count = mgr->GetGroup(player).size();
                reason = count > 1 ? Acore::StringFormat("{} pieces selected. Ctrl-click a piece (in edit mode or while decorating) to add or remove it.", count)
                                   : "One piece selected. Ctrl-click others (in edit mode or while decorating) to move them together.";
            }
        }
        else if (sub == "match")
            mgr->MatchGroup(player, tokens.size() > 1 ? Lower(tokens[1]) : "", reason);
        else if (sub == "row")
        {
            // .house row <count> [yards] [right|left|forward|back] [id]
            uint32 count = number(1);
            float spacing = 0.0f;
            std::string direction = "right";
            bool sawDirection = false;
            uint32 id = 0;
            for (size_t i = 2; i < tokens.size(); ++i)
            {
                std::string word = Lower(tokens[i]);
                if (word == "right" || word == "left" || word == "forward" || word == "back")
                {
                    direction = word;
                    sawDirection = true;
                }
                else if (!sawDirection && spacing == 0.0f)
                    spacing = decimal(i, 0.0f);
                else
                    id = number(i);
            }
            if (!count)
                reason = "Usage: .house row <count> [yards apart] [right|left|forward|back] [id]";
            else
                mgr->PlaceRow(player, id, count, spacing, direction, reason);
        }
        else if (sub == "set" || sub == "sets")
        {
            std::string what = tokens.size() > 1 ? Lower(tokens[1]) : "list";
            ObjectGuid::LowType self = player->GetGUID().GetCounter();
            std::optional<SavedSet> set = tokens.size() > 2 ? mgr->FindSavedSet(self, restFrom(2)) : std::nullopt;
            if (what == "save")
                mgr->SaveSet(player, restFrom(2), reason);
            else if ((what == "place" || what == "delete") && !set)
                reason = tokens.size() > 2 ? "You have no set called " + restFrom(2) + "." : "Which set? .house set list shows them.";
            else if (what == "place")
                mgr->StartSetPlacement(player, set->id, reason);
            else if (what == "delete")
                mgr->DeleteSet(player, set->id, reason);
            else
            {
                std::vector<SavedSet> sets = mgr->GetSavedSets(self);
                if (sets.empty())
                    reason = "No saved sets yet: select pieces (Ctrl-click), then .house set save <name>.";
                for (SavedSet const& saved : sets)
                    handler->PSendSysMessage("#{} {} ({} pieces, {})", saved.id, saved.name, saved.pieces, saved.savedAt);
            }
        }
        else if (sub == "goto")
            mgr->GoTo(player, number(1), reason);
        else if (sub == "sign")
            mgr->SignGuestbook(player, restFrom(1), reason);
        else if (sub == "guestbook")
        {
            ObjectGuid::LowType self = player->GetGUID().GetCounter();
            if (tokens.size() > 2 && Lower(tokens[1]) == "delete")
                mgr->DeleteNote(player, number(2), reason);
            else
            {
                std::vector<GuestbookNote> notes = mgr->GetGuestbook(self, 10);
                if (notes.empty())
                    reason = "Your guestbook is empty: visitors sign it with .house sign <note>.";
                for (GuestbookNote const& note : notes)
                    handler->PSendSysMessage("#{} {}, {}{}: {}", note.id, note.author, note.when, note.fresh ? " (new)" : "", note.text);
                mgr->MarkGuestbookRead(self);
            }
        }
        else if (sub == "door")
        {
            std::string what = tokens.size() > 1 ? Lower(tokens[1]) : "here";
            mgr->SetDoor(player, what == "reset" || what == "off" || what == "landing", reason);
        }
        else if (gm && sub == "phototour")
            mgr->PhotoTour(player, tokens.size() > 1 ? Lower(tokens[1]) : "start", reason);
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
            mgr->SendAddonState(player);  // the edit mode banner shows it
        }
        else if (sub == "select" && tokens.size() > 1 && (Lower(tokens[1]) == "next" || Lower(tokens[1]) == "previous" || Lower(tokens[1]) == "prev"))
        {
            // Quiet too: Tab in edit mode.
            mgr->SelectNext(player, Lower(tokens[1]) != "next", reason);
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
    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction /*trans*/, uint32 guid) override { sPlayerHousingMgr->OnPlayerDeleteFromDB(guid); }

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
    new npc_playerhousing_figurine();
    new mod_playerhousing_worldscript();
    new mod_playerhousing_commandscript();
    new mod_playerhousing_playerscript();
    new mod_playerhousing_globalscript();
}
