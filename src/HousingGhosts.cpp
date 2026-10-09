#include "PlayerHousingMgr.h"

#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Map.h"
#include "MoveSplineInit.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

using namespace Housing;

// Ghosts: a piece that follows its player until it's set down. With PlayerHousing.dll the
// addon says where the mouse points in the world (the game alone can't) and the piece shows
// there: on the floor, on a table, or on a wall facing out; a click sets it down. Without it
// the piece floats ahead of the player instead: they walk it where it goes, push it farther
// or nearer, raise it and turn it, and see it there before setting it down. Furniture is a
// see-through copy of itself (a creature with its model, which can glide). An M2 building uses
// that exact same path. A world-model building cannot be a creature, so it is carried as its
// real collisionless game object, redrawn a few times a second. Pieces whose ghost the server
// doesn't have are carried the same way. (An older client patch may show no translucent ghost.)
//
// With a DLL that moves units (version 2 and up) the player's own client moves the ghost every
// frame, under the mouse: the server only follows along (so others and the click see the same
// spot) and tells the client where it put the ghost after its own rules (the grid, a table's
// top, a wall, the ground under a building), which the client then shows.

namespace
{
    constexpr float PI_F = 3.14159265358979323846f;
    constexpr float TWO_PI_F = 6.28318530717958647692f;
    constexpr uint32 GHOST_UPDATE_MS = 100;  // how often a ghost catches up with its player
    constexpr uint32 CARRY_REDRAW_MS = 250;  // a carried object: redrawn at most this often
    constexpr float MIN_FORWARD = 0.5f;
    constexpr float MAX_FORWARD = 40.0f;
    constexpr float MAX_SIDE = 20.0f;
    constexpr float MIN_LIFT = -3.0f;
    constexpr float MAX_LIFT = 30.0f;
    constexpr float ON_TOP = 0.35f;          // this close to a table's top, it stands on the table
    constexpr float POINT_ON_TOP = 0.5f;     // the mouse on a table: its top this close to the point
    constexpr float MAX_POINT_DISTANCE = 120.0f;  // the mouse on something farther off: not taken
    constexpr float MAX_POINT_HEIGHT = 40.0f;     // ... or this far above the ground (a tall roof)
    constexpr float MAX_POINT_DEPTH = 3.0f;       // ... or this far under it
    constexpr uint32 POINT_DRAW_MS = 60;     // points coming faster than this are drawn by the update
    constexpr float WALL_STEEPNESS = 0.6f;   // a surface facing up less than this is a wall

    float NormalizeAngle(float angle)
    {
        angle = std::fmod(angle, TWO_PI_F);
        return angle < 0.0f ? angle + TWO_PI_F : angle;
    }

    float AngleBetween(float a, float b)
    {
        return std::fabs(std::remainder(a - b, TWO_PI_F));
    }

    std::string Pieces(size_t count)
    {
        return Acore::StringFormat("{} {}", count, count == 1 ? "piece" : "pieces");
    }
}

uint32 PlayerHousingMgr::GhostDisplayFor(uint32 itemEntry) const
{
    PieceDefinition const* piece = GetPiece(itemEntry);
    if (!_ghosts || !piece || itemEntry < 900000 || !sObjectMgr->GetCreatureTemplate(GHOST_ENTRY))
        return 0;

    // M2 buildings have an exact translucent creature model, like furnishings. A WMO cannot
    // be a creature model in this client; show its real collisionless game object instead of
    // an ambiguous rectangular block. Generated WMO building templates use this type.
    if (piece->IsBuilding())
    {
        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(piece->goEntry);
        if (goInfo && goInfo->type == GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING)
            return 0;
    }
    // Its display and its model both: a display without a model would stop the server.
    uint32 display = GHOST_DISPLAY_BASE + (itemEntry - 900000);
    CreatureDisplayInfoEntry const* info = sCreatureDisplayInfoStore.LookupEntry(display);
    return info && sCreatureModelDataStore.LookupEntry(info->ModelId) ? display : 0;
}

uint32 PlayerHousingMgr::SolidDisplayFor(PieceDefinition const& piece) const
{
    // A figurine or a mannequin without a ghost model: its own creature's, solid.
    if (!piece.IsCreature() || !sObjectMgr->GetCreatureTemplate(GHOST_ENTRY))
        return 0;
    CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(piece.HasFlag(PIECE_FLAG_STAND) ? MANNEQUIN_ENTRY : piece.creatureEntry);
    CreatureModel const* model = creature ? creature->GetFirstValidModel() : nullptr;
    return model ? model->CreatureDisplayID : 0;
}

bool PlayerHousingMgr::HasOneToPlace(Player* player, uint32 itemEntry) const
{
    std::map<uint32, uint32> storage = GetStorage(HomeOf(player));
    auto stored = storage.find(itemEntry);
    return stored != storage.end() && stored->second > 0;
}

uint32 PlayerHousingMgr::GetGhostItem(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    return itr != _carrying.end() && !itr->second.pieces.empty() ? itr->second.pieces.front().itemEntry : 0;
}

bool PlayerHousingMgr::IsGhostMove(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    return itr != _carrying.end() && !itr->second.isNew;
}

bool PlayerHousingMgr::IsCarrying(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _carrying.count(player->GetGUID()) > 0;
}

void PlayerHousingMgr::PredictPlayer(Player* player, Ghost& ghost, float& x, float& y, float& z, float& o) const
{
    uint32 now = getMSTime();
    if (!ghost.seenMs || std::fabs(player->GetPositionX() - ghost.seenX) > 0.001f || std::fabs(player->GetPositionY() - ghost.seenY) > 0.001f
            || std::fabs(player->GetPositionZ() - ghost.seenZ) > 0.001f || AngleBetween(player->GetOrientation(), ghost.seenO) > 0.0001f)
    {
        ghost.seenX = player->GetPositionX();
        ghost.seenY = player->GetPositionY();
        ghost.seenZ = player->GetPositionZ();
        ghost.seenO = player->GetOrientation();
        ghost.seenMs = now;
    }
    x = ghost.seenX;
    y = ghost.seenY;
    z = ghost.seenZ;
    o = ghost.seenO;

    // Running in a straight line, the client only reports every half second or so: carry the
    // player on at their speed since then, so the ghost keeps up instead of jumping.
    MovementInfo const& movement = player->m_movementInfo;
    if (movement.HasMovementFlag(MOVEMENTFLAG_FALLING))
        return;
    float seconds = std::min(getMSTimeDiff(ghost.seenMs, now), 600u) / 1000.0f;
    float ahead = (movement.HasMovementFlag(MOVEMENTFLAG_FORWARD) ? 1.0f : 0.0f) - (movement.HasMovementFlag(MOVEMENTFLAG_BACKWARD) ? 1.0f : 0.0f);
    float left = (movement.HasMovementFlag(MOVEMENTFLAG_STRAFE_LEFT) ? 1.0f : 0.0f) - (movement.HasMovementFlag(MOVEMENTFLAG_STRAFE_RIGHT) ? 1.0f : 0.0f);
    float length = std::sqrt(ahead * ahead + left * left);
    if (length > 0.0f)
    {
        float speed = player->GetSpeed(movement.HasMovementFlag(MOVEMENTFLAG_WALKING) ? MOVE_WALK : ahead < 0.0f ? MOVE_RUN_BACK : MOVE_RUN);
        float step = speed * seconds / length;
        x += (std::cos(o) * ahead - std::sin(o) * left) * step;
        y += (std::sin(o) * ahead + std::cos(o) * left) * step;
    }
    float turn = (movement.HasMovementFlag(MOVEMENTFLAG_LEFT) ? 1.0f : 0.0f) - (movement.HasMovementFlag(MOVEMENTFLAG_RIGHT) ? 1.0f : 0.0f);
    o = NormalizeAngle(o + turn * player->GetSpeed(MOVE_TURN_RATE) * seconds);
}

float PlayerHousingMgr::GhostFloor(Player* player, Session const& session, Ghost const& ghost, float x, float y, float playerZ, bool building,
    uint32& parent) const
{
    parent = 0;
    if (!building)
    {
        // The highest table top under it, not far above or below the player (the pieces
        // being moved don't count: they can't stand on themselves).
        std::set<uint32> moving;
        for (GhostPiece const& piece : ghost.pieces)
            if (piece.placementId)
                moving.insert(piece.placementId);
        float best = -std::numeric_limits<float>::max();
        for (auto const& [id, surface] : session.placements)
        {
            if (moving.count(id))
                continue;
            PieceDefinition const* piece = GetPiece(surface.itemEntry);
            if (!piece || !piece->HasFlag(PIECE_FLAG_SURFACE))
                continue;
            float scale = piece->scale > 0.0f ? surface.scale / piece->scale : 1.0f;
            float top = surface.z + piece->height * scale;
            if (top > playerZ + 3.0f || top < playerZ - 3.0f || top <= best || !IsOverSurface(*piece, surface, x, y))
                continue;
            best = top;
            parent = id;
        }
        if (parent)
            return best;
    }

    // The floor the player stands on when it's raised (a building's, which the server can't
    // see: pieces have no collision here), else the ground there.
    return ghost.floorRaised ? ghost.floorZ : GroundHeightNear(player, x, y, playerZ);
}

float PlayerHousingMgr::GhostFacing(Ghost const& ghost)
{
    return ghost.atPoint && ghost.onWall ? NormalizeAngle(ghost.wallO + ghost.wallTurn) : ghost.o;
}

void PlayerHousingMgr::PoseGhost(Player* player, Session const& session, Ghost& ghost, float& x, float& y, float& z, uint32& parent)
{
    PieceDefinition const* lead = GetPiece(ghost.pieces.front().itemEntry);
    parent = 0;
    if (ghost.atPoint)
    {
        // Where the mouse points: on the grid (not on a wall), and on the table there when the
        // mouse is on its top.
        x = ghost.pointX;
        y = ghost.pointY;
        float floor = ghost.pointZ;
        if (!ghost.onWall)
        {
            SnapToGrid(player->GetGUID().GetCounter(), x, y);
            if (ghost.onCeiling && lead)
            {
                // Under a ceiling: it hangs, its top at the point.
                float scale = lead->scale > 0.0f ? ghost.pieces.front().scale / lead->scale : 1.0f;
                floor -= lead->height * scale;
            }
            else if (!lead || !lead->IsBuilding())
            {
                uint32 table = 0;
                float top = GhostFloor(player, session, ghost, x, y, ghost.pointZ, false, table);
                if (table && std::fabs(top - ghost.pointZ) <= POINT_ON_TOP)
                {
                    floor = top;
                    parent = table;
                }
            }
        }
        z = floor + ghost.lift;
        if (parent && std::fabs(ghost.lift) > ON_TOP)
            parent = 0;
        return;
    }

    float px;
    float py;
    float pz;
    float po;
    PredictPlayer(player, ghost, px, py, pz, po);

    // What the player stands on, noted while they're on it (not mid-jump).
    if (!player->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING) || !ghost.posed)
    {
        ghost.floorZ = pz;
        ghost.floorRaised = pz - GroundHeightNear(player, px, py, pz) > 0.5f;
    }

    x = px + std::cos(po) * ghost.forward - std::sin(po) * ghost.side;
    y = py + std::sin(po) * ghost.forward + std::cos(po) * ghost.side;
    SnapToGrid(player->GetGUID().GetCounter(), x, y);

    float floor = GhostFloor(player, session, ghost, x, y, ghost.floorRaised ? ghost.floorZ : pz, lead && lead->IsBuilding(), parent);
    z = floor + ghost.lift;
    // Held above a table, it isn't standing on it.
    if (parent && std::fabs(ghost.lift) > ON_TOP)
        parent = 0;
}

void PlayerHousingMgr::DrawGhost(Player* player, Session& session, Ghost& ghost, bool force)
{
    Map* map = player->GetMap();
    if (!map || ghost.pieces.empty())
        return;

    float x;
    float y;
    float z;
    uint32 parent;
    PoseGhost(player, session, ghost, x, y, z, parent);
    ghost.parent = parent;

    uint32 now = getMSTime();
    float facing = GhostFacing(ghost);
    float cosO = std::cos(facing);
    float sinO = std::sin(facing);
    bool anyMoved = false;
    for (GhostPiece& piece : ghost.pieces)
    {
        // Its spot: the lead's, and its own place around the lead, turned with it.
        float gx = x + piece.dx * cosO - piece.dy * sinO;
        float gy = y + piece.dx * sinO + piece.dy * cosO;
        float gz = z + piece.dz;
        float go = NormalizeAngle(facing + piece.dO);
        bool moved = force || piece.reshaped || !piece.shown || std::fabs(gx - piece.x) > 0.02f || std::fabs(gy - piece.y) > 0.02f
            || std::fabs(gz - piece.z) > 0.02f || AngleBetween(go, piece.o) > 0.005f;
        if (!moved)
            continue;
        anyMoved = true;

        PieceDefinition const* definition = GetPiece(piece.itemEntry);
        if (!definition)
            continue;
        float size = definition->scale > 0.0f ? piece.scale / definition->scale : 1.0f;
        // Tilted: the object itself, which can lean (a creature, the see-through ghost, can't),
        // so the tilt shows while it's held.
        bool tilted = piece.pitch != 0.0f || piece.roll != 0.0f;
        uint32 display = tilted ? 0 : GhostDisplayFor(piece.itemEntry);
        if (!display)
            display = SolidDisplayFor(*definition);
        // Shown the other way before (the setting changed): that one goes.
        auto ghostScale = [&]()
        {
            if (definition->IsCreature())
                return piece.scale;  // figurines and mannequins are sized like the piece
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(definition->goEntry);
            return goInfo ? goInfo->size * size : 1.0f;
        };
        if (piece.shown && piece.shown.IsCreature() != (display != 0))
        {
            RemoveSpawned(map, piece.shown);
            piece.shown.Clear();
        }

        if (display)
        {
            // See-through: a creature with the piece's model, gliding to the new spot.
            Creature* ghostCreature = piece.shown ? map->GetCreature(piece.shown) : nullptr;
            // Sized: the same ghost grows or shrinks (a new one would show both for a moment).
            if (ghostCreature && piece.reshaped)
                ghostCreature->SetObjectScale(ghostScale());
            if (!ghostCreature)
            {
                Position position;
                position.Relocate(gx, gy, gz, go);
                TempSummon* summon = map->SummonCreature(GHOST_ENTRY, position);
                if (!summon)
                    continue;
                summon->SetDisplayId(display);
                summon->SetNativeDisplayId(display);
                summon->SetObjectScale(ghostScale());
                if (definition->IsBuilding())
                    summon->SetVisibilityDistanceOverride(VisibilityDistanceType::Large);
                summon->SetReactState(REACT_PASSIVE);
                summon->SetDisableGravity(true);
                summon->SetCanFly(true);
                summon->SetPhaseMask(session.phaseMask, true);
                piece.shown = summon->GetGUID();
            }
            else if (ghost.local && ghost.atPoint)
            {
                // The player's own client moves it under the mouse: here it only keeps up, with
                // no move sent to anyone (one would fight the client over where it is).
                map->CreatureRelocation(ghostCreature, gx, gy, gz, go);
            }
            else
            {
                float distance = std::sqrt((gx - piece.x) * (gx - piece.x) + (gy - piece.y) * (gy - piece.y) + (gz - piece.z) * (gz - piece.z));
                Movement::MoveSplineInit init(ghostCreature);
                init.MoveTo(gx, gy, gz, false, true);
                init.SetFly();
                init.SetOrientationFixed(true);  // no turning to face the way it glides
                init.SetFacing(go);
                init.SetVelocity(std::max(distance / 0.15f, 1.0f));
                init.Launch();
            }
        }
        else
        {
            // Carried as it is: the object itself, put down again in the new spot, a few
            // times a second at most (a key held down can't make it more; each is a new object).
            if (piece.shown && !piece.reshaped && getMSTimeDiff(piece.drawnMs, now) < CARRY_REDRAW_MS)
                continue;  // soon: the next update redraws it
            GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(definition->goEntry);
            if (!goInfo)
                continue;
            GameObject* object = new GameObject();
            if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), definition->goEntry, map, PHASEMASK_NORMAL, gx, gy, gz, go,
                    G3D::Quat(0.0f, 0.0f, 0.0f, 0.0f), 100, GO_STATE_READY))
            {
                delete object;
                continue;
            }
            object->SetRespawnTime(0);
            object->SetSpawnedByDefault(false);
            object->SetObjectScale(goInfo->size * size);
            if (piece.pitch != 0.0f || piece.roll != 0.0f)
                SetRotationAngles(object, go, piece.pitch, piece.roll);
            if (definition->IsBuilding())
                object->SetVisibilityDistanceOverride(VisibilityDistanceType::Large);
            if (!map->AddToMap(object))
            {
                delete object;
                continue;
            }
            object->SetPhaseMask(session.phaseMask, true);
            object->EnableCollision(false);
            // The new one first, then the old goes: the client never shows the spot empty.
            if (piece.shown)
                if (GameObject* old = map->GetGameObject(piece.shown))
                    old->AddObjectToRemoveList();
            piece.shown = object->GetGUID();
            piece.drawnMs = now;
        }
        piece.x = gx;
        piece.y = gy;
        piece.z = gz;
        piece.o = go;
        piece.reshaped = false;
    }
    ghost.posed = true;

    // A client moving it itself hears which creatures to move, and where the server's rules
    // put it (the grid, a table top, the ground under a building).
    if (ghost.local)
    {
        SendGhostPieces(player, ghost);
        if (anyMoved || force)
            SendGhostPose(player, ghost, x, y, z);
    }
}

void PlayerHousingMgr::SendGhostPieces(Player* player, Ghost& ghost) const
{
    // One row a piece: its creature (0 when it's carried as an object, which only the server
    // can move) and where it sits from the lead, in the lead's own frame.
    std::vector<std::string> rows;
    for (size_t index = 0; index < ghost.pieces.size(); ++index)
    {
        GhostPiece const& piece = ghost.pieces[index];
        uint64 raw = piece.shown.IsCreature() ? piece.shown.GetRawValue() : 0;
        rows.push_back(Acore::StringFormat("gpiece\t{}\t{}\t{:08X}\t{:08X}\t{:.3f}\t{:.3f}\t{:.3f}\t{:.4f}", index + 1, ghost.pieces.size(),
            uint32(raw >> 32), uint32(raw & 0xFFFFFFFF), piece.dx, piece.dy, piece.dz, piece.dO));
    }
    std::string all;
    for (std::string const& row : rows)
        all += row + "\n";
    if (all == ghost.piecesSent)
        return;
    ghost.piecesSent = all;
    for (std::string const& row : rows)
        SendAddon(player, row);
}

void PlayerHousingMgr::SendGhostPose(Player* player, Ghost const& ghost, float x, float y, float z) const
{
    // The lead's spot and facing; then what the client needs to place it the same way itself:
    // the lift, the turn on the floor, the turn on a wall, and how far the server's rules moved
    // it up or down from the point it was given (the ground under a building, a table's top).
    float zfix = ghost.atPoint ? z - (ghost.rawZ + ghost.lift) : 0.0f;
    SendAddon(player, Acore::StringFormat("gpose\t{:.3f}\t{:.3f}\t{:.3f}\t{:.4f}\t{:.3f}\t{:.4f}\t{:.4f}\t{:.3f}\t{}", x, y, z,
        GhostFacing(ghost), ghost.lift, ghost.o, ghost.wallTurn, zfix, ghost.onWall ? 1 : 0));
}

void PlayerHousingMgr::DespawnGhost(Ghost& ghost, Map* map)
{
    for (GhostPiece& piece : ghost.pieces)
    {
        if (!piece.shown)
            continue;
        if (map)
            RemoveSpawned(map, piece.shown);
        piece.shown.Clear();
    }
}

void PlayerHousingMgr::EndGhost(Player* player)
{
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
        return;
    DespawnGhost(itr->second, GetHousingMap());
    _carrying.erase(itr);
}

void PlayerHousingMgr::CancelGhost(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    EndGhost(player);
    SendAddonState(player);  // even with nothing following: the addon may think otherwise
}

bool PlayerHousingMgr::StartGhostNew(Player* player, uint32 itemEntry, uint32 copyOf, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
    {
        if (!GetIslandOwner(player))
            reason = "Furnishings go on your own island: Go home first.";
        return false;
    }

    PieceDefinition const* piece = GetPiece(itemEntry);
    if (!piece)
    {
        reason = "That isn't a furnishing.";
        return false;
    }
    Placement const* source = nullptr;
    if (copyOf)
    {
        auto itr = session->placements.find(copyOf);
        if (itr == session->placements.end() || itr->second.itemEntry != itemEntry)
        {
            reason = "That piece isn't there any more.";
            return false;
        }
        source = &itr->second;
    }
    if (!CheckLimit(*session, *piece, reason))
    {
        Tip(player, TIP_LIMIT, "Pick up pieces you don't need: they go back to your Collection.");
        return false;
    }

    // One to set down: one the player owns, or else a new copy from the Collection, paid for
    // (outside FreeMode) only when it's set down: Never mind costs nothing.
    if (!HasOneToPlace(player, itemEntry))
    {
        if (!IsUnlocked(player, *piece))
        {
            reason = Acore::StringFormat("{} is still locked: {}.", piece->name, DescribeProgress(player, *piece));
            return false;
        }
        if (!_freeMode && piece->copyCost && player->GetMoney() < piece->copyCost)
        {
            reason = Acore::StringFormat("A {} costs {}.", piece->name, FormatMoney(piece->copyCost));
            return false;
        }
    }

    Ghost ghost;
    ghost.isNew = true;
    ghost.copyOf = copyOf;
    GhostPiece lead;
    lead.itemEntry = itemEntry;
    lead.scale = source ? source->scale : piece->scale;
    lead.pitch = source ? source->pitch : 0.0f;
    lead.roll = source ? source->roll : 0.0f;
    if (source)
        lead.look = source->look;
    else if (piece->HasFlag(PIECE_FLAG_STAND))
        lead.look = uint64(player->getRace()) | (uint64(player->getGender()) << 8);  // it takes after its owner
    ghost.pieces.push_back(lead);

    // Far enough ahead to see all of it, facing the player (a copy: turned like the original).
    float size = piece->scale > 0.0f ? lead.scale / piece->scale : 1.0f;
    ghost.forward = std::clamp(piece->footprint * size + (piece->IsBuilding() ? 3.0f : 1.5f), 2.0f, MAX_FORWARD);
    ghost.o = source ? source->o : NormalizeAngle(player->GetOrientation() + PI_F);
    if (!source && GetGridSize(player->GetGUID().GetCounter()) > 0.0f)
    {
        float step = PI_F / 4.0f;
        ghost.o = NormalizeAngle(std::round(ghost.o / step) * step);
    }

    BeginGhost(player, *session, std::move(ghost));
    reason.clear();
    return true;
}

void PlayerHousingMgr::BeginGhost(Player* player, Session& session, Ghost ghost)
{
    EndGhost(player);
    ghost.local = HasLocalGhosts(player);
    Ghost& carried = _carrying[player->GetGUID()] = std::move(ghost);
    DrawGhost(player, session, carried, true);
    // The addon's banner says what to do; typed, once.
    if (!HasAddon(player))
        Tip(player, TIP_GHOST, "It follows you: walk it where it goes, then .house ghost place (or .house ghost cancel).");
    SendAddonState(player);
}

bool PlayerHousingMgr::StartGhostMove(Player* player, uint32 placementId, std::string& reason)
{
    std::vector<uint32> members;
    if (!placementId)
        members = GetGroup(player);
    if (members.empty())
    {
        placementId = ResolvePlacementArgument(player, placementId);
        if (placementId)
            members.push_back(placementId);
    }

    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;
    members.erase(std::remove_if(members.begin(), members.end(), [&](uint32 id) { return !session->placements.count(id); }), members.end());
    if (members.empty())
    {
        reason = "Choose a piece first: click it in edit mode, or press Tab.";
        return false;
    }

    // What moves: each piece and what stands on it (a building: what's inside, too). The lead
    // is the selected piece, unless it stands on another selected one.
    std::vector<uint32> roots;
    std::vector<uint32> carried;
    GroupRoots(*session, members, roots, carried);
    if (roots.empty())
    {
        reason = "Choose a piece first: click it in edit mode, or press Tab.";
        return false;
    }
    Placement const& lead = session->placements[roots.front()];

    Ghost ghost;
    ghost.isNew = false;
    float cosL = std::cos(-lead.o);
    float sinL = std::sin(-lead.o);
    std::vector<uint32> moving = roots;
    moving.insert(moving.end(), carried.begin(), carried.end());
    for (uint32 id : moving)
    {
        Placement const& placement = session->placements[id];
        GhostPiece piece;
        piece.placementId = id;
        piece.itemEntry = placement.itemEntry;
        float relX = placement.x - lead.x;
        float relY = placement.y - lead.y;
        piece.dx = relX * cosL - relY * sinL;
        piece.dy = relX * sinL + relY * cosL;
        piece.dz = placement.z - lead.z;
        piece.dO = placement.o - lead.o;
        piece.scale = placement.scale;
        piece.pitch = placement.pitch;
        piece.roll = placement.roll;
        piece.look = placement.look;
        ghost.pieces.push_back(piece);
    }

    // It starts where it is: that far ahead of the player and to the side, at its height
    // over what it stands on.
    float po = player->GetOrientation();
    float relX = lead.x - player->GetPositionX();
    float relY = lead.y - player->GetPositionY();
    ghost.forward = std::clamp(std::cos(po) * relX + std::sin(po) * relY, MIN_FORWARD, MAX_FORWARD);
    ghost.side = std::clamp(-std::sin(po) * relX + std::cos(po) * relY, -MAX_SIDE, MAX_SIDE);
    ghost.o = lead.o;
    ghost.floorZ = player->GetPositionZ();
    ghost.floorRaised = player->GetPositionZ() - GroundHeightNear(player, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()) > 0.5f;
    PieceDefinition const* leadPiece = GetPiece(lead.itemEntry);
    uint32 under = 0;
    float floor = GhostFloor(player, *session, ghost, lead.x, lead.y, ghost.floorRaised ? ghost.floorZ : player->GetPositionZ(),
        leadPiece && leadPiece->IsBuilding(), under);
    ghost.lift = std::clamp(lead.z - floor, MIN_LIFT, MAX_LIFT);

    BeginGhost(player, *session, std::move(ghost));
    reason.clear();
    return true;
}

bool PlayerHousingMgr::AdjustGhost(Player* player, float forward, float left, float up, float degrees, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
    {
        reason = "Nothing is following you: choose a piece to place or move first.";
        SendAddonState(player);  // the addon thought otherwise
        return false;
    }
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Ghost& ghost = itr->second;
    forward = std::clamp(forward, -5.0f, 5.0f);
    left = std::clamp(left, -5.0f, 5.0f);
    float turn = std::clamp(degrees, -360.0f, 360.0f) * PI_F / 180.0f;
    if (ghost.atPoint)
    {
        // Following the mouse: nudged from the point the player's way (until the mouse moves on).
        float po = player->GetOrientation();
        float x = ghost.pointX + std::cos(po) * forward - std::sin(po) * left;
        float y = ghost.pointY + std::sin(po) * forward + std::cos(po) * left;
        if (player->GetExactDist2d(x, y) <= MAX_POINT_DISTANCE && IsSpotOnIsland(x, y, ghost.pointZ))
        {
            ghost.pointX = x;
            ghost.pointY = y;
        }
        if (ghost.onWall)
            ghost.wallTurn = NormalizeAngle(ghost.wallTurn + turn);
        else
            ghost.o = NormalizeAngle(ghost.o + turn);
    }
    else
    {
        ghost.forward = std::clamp(ghost.forward + forward, MIN_FORWARD, MAX_FORWARD);
        ghost.side = std::clamp(ghost.side + left, -MAX_SIDE, MAX_SIDE);
        ghost.o = NormalizeAngle(ghost.o + turn);
    }
    ghost.lift = std::clamp(ghost.lift + std::clamp(up, -2.0f, 2.0f), MIN_LIFT, MAX_LIFT);
    DrawGhost(player, *session, ghost, true);
    // The window names which way the front points. Keep that current while the wheel turns
    // the held piece.
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::ResizeGhost(Player* player, float percent, bool relative, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    Session* session = itr != _carrying.end() ? GetOwnerSession(player, reason) : nullptr;
    if (!session)
        return false;
    Ghost& ghost = itr->second;
    if (ghost.setId)
    {
        reason = "A set keeps its pieces' sizes.";
        return false;
    }
    PieceDefinition const* lead = GetPiece(ghost.pieces.front().itemEntry);
    if (!lead)
        return false;

    int32 lowest = int32(std::lround(_sizeMin * 100.0f));
    int32 highest = int32(std::lround(_sizeMax * 100.0f));
    if (lowest >= 100 && highest <= 100)
    {
        reason = "Pieces keep their size on this server.";
        return false;
    }

    // Whole percents of the lead's normal size; whatever comes along with it (what stands on
    // it, what's inside) grows or shrinks with it, and keeps its place on it.
    float normal = lead->scale > 0.0f ? lead->scale : 1.0f;
    int32 current = int32(std::lround(ghost.pieces.front().scale / normal * 100.0f));
    int32 wanted = std::clamp(relative ? current + int32(std::lround(percent)) : int32(std::lround(percent)), lowest, highest);
    if (wanted == current)
    {
        if (!relative && wanted == 100)
            reason = "It's already its normal size.";
        else if (wanted == highest)
            reason = Acore::StringFormat("That's as big as it gets ({}%).", highest);
        else
            reason = Acore::StringFormat("That's as small as it gets ({}%).", lowest);
        return false;
    }
    float k = float(wanted) / float(current);
    for (GhostPiece& piece : ghost.pieces)
    {
        piece.scale *= k;
        piece.dx *= k;
        piece.dy *= k;
        piece.dz *= k;
    }
    ghost.pieces.front().scale = normal * float(wanted) / 100.0f;

    // Shown at the new size, in place.
    for (GhostPiece& piece : ghost.pieces)
        piece.reshaped = true;
    DrawGhost(player, *session, ghost, true);
    reason = HasAddon(player) ? "" : Acore::StringFormat("Size {}%.", wanted);  // the window shows it
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::TiltGhost(Player* player, float forwardDegrees, float rightDegrees, bool straighten, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    Session* session = itr != _carrying.end() ? GetOwnerSession(player, reason) : nullptr;
    if (!session)
        return false;
    Ghost& ghost = itr->second;
    if (ghost.setId || ghost.pieces.size() > 1)
    {
        reason = "Tilting is one piece at a time: this one has others with it.";
        return false;
    }
    GhostPiece& lead = ghost.pieces.front();
    PieceDefinition const* piece = GetPiece(lead.itemEntry);
    if (!piece)
        return false;
    if (piece->IsCreature())
    {
        reason = piece->HasFlag(PIECE_FLAG_STAND) ? "Mannequins always stand upright." : "Figurines always stand upright.";
        return false;
    }
    if (_tiltMax <= 0.0f && !straighten)
    {
        reason = "Pieces stand upright on this server.";
        return false;
    }

    // As Tilt: tenths of a degree, up to the server's limit, or right round at 180.
    auto toTenths = [](float radians) { return int32(std::lround(radians * 1800.0f / PI_F)); };
    int32 limit = int32(std::lround(_tiltMax * 10.0f));
    auto tilted = [&](float radians, float degrees)
    {
        int32 tenths = toTenths(radians) + int32(std::lround(degrees * 10.0f));
        if (_tiltMax < 180.0f)
            return std::clamp(tenths, -limit, limit);
        tenths %= 3600;
        if (tenths > 1800)
            tenths -= 3600;
        else if (tenths <= -1800)
            tenths += 3600;
        return tenths;
    };
    int32 pitch = straighten ? 0 : tilted(lead.pitch, forwardDegrees);
    int32 roll = straighten ? 0 : tilted(lead.roll, rightDegrees);
    if (pitch == toTenths(lead.pitch) && roll == toTenths(lead.roll))
    {
        reason = straighten ? "It's already standing straight." : Acore::StringFormat("That's as far as it tilts ({:.0f}°).", _tiltMax);
        return false;
    }
    lead.pitch = float(pitch) * PI_F / 1800.0f;
    lead.roll = float(roll) * PI_F / 1800.0f;
    // Shown again, leaning: tilted, it's the object itself (see DrawGhost).
    lead.reshaped = true;
    DrawGhost(player, *session, ghost, true);
    reason.clear();
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::GhostAt(Player* player, float x, float y, float z, float const* facing, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
    {
        reason = "Nothing is following you: choose a piece to place or move first.";
        SendAddonState(player);  // the addon thought otherwise
        return false;
    }
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Ghost& ghost = itr->second;
    std::string note;
    float rawZ = z;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || player->GetExactDist(x, y, z) > MAX_POINT_DISTANCE)
        note = "That's too far away.";
    else if (!IsSpotOnIsland(x, y, z))
        note = "That spot is off your island.";
    else
    {
        // Nothing the mouse can point at is under the ground or high above it (a client that
        // says otherwise doesn't get to bury pieces or hang them in the sky).
        float terrain = player->GetMap()->GetGridHeight(x, y);
        if (terrain > INVALID_HEIGHT + 1.0f && z < terrain - MAX_POINT_DEPTH)
            note = "That's under the ground.";
        else if (terrain > INVALID_HEIGHT + 1.0f && z > terrain + MAX_POINT_HEIGHT)
            note = "That's too high up.";
    }
    if (!note.empty())
    {
        // It stays where it was; the addon hears why (once, not for every point).
        reason = note;
        if (ghost.note != note)
        {
            ghost.note = note;
            SendAddonState(player);
        }
        return false;
    }

    // A building stands on the ground, whatever the mouse is on (a roof, a cliff's side): the
    // land itself (a height search would find buildings' roofs, the one being moved too).
    PieceDefinition const* lead = GetPiece(ghost.pieces.front().itemEntry);
    bool building = lead && lead->IsBuilding();
    if (building)
    {
        float land = player->GetMap()->GetGridHeight(x, y);
        z = land > INVALID_HEIGHT + 1.0f ? land : GroundHeightNear(player, x, y, z);
    }

    // The mouse on a piece being moved (still standing where it was): where the lead stood, as
    // if it were on what that piece stands on.
    bool onOriginal = false;
    for (GhostPiece const& piece : ghost.pieces)
    {
        if (building)
            break;
        auto placementItr = piece.placementId ? session->placements.find(piece.placementId) : session->placements.end();
        if (placementItr == session->placements.end())
            continue;
        Placement const& original = placementItr->second;
        PieceDefinition const* definition = GetPiece(original.itemEntry);
        if (!definition)
            continue;
        float scale = definition->scale > 0.0f ? original.scale / definition->scale : 1.0f;
        if (z < original.z - 0.2f || z > original.z + definition->height * scale + 0.2f || !IsOverSurface(*definition, original, x, y))
            continue;
        z = original.z - piece.dz;
        onOriginal = true;
        break;
    }

    // From the first point on it sits on what the mouse points at: its height over what was
    // under it until now doesn't carry over (the wheel still raises it from there).
    if (!ghost.atPoint)
        ghost.lift = 0.0f;
    ghost.atPoint = true;
    ghost.pointX = x;
    ghost.pointY = y;
    ghost.pointZ = z;
    ghost.rawZ = rawZ;
    // A wall (the surface facing more sideways than up): the piece faces out from it.
    // A ceiling (facing down as much): it hangs from it.
    bool wall = false;
    bool ceiling = false;
    if (facing && !onOriginal && !building && std::isfinite(facing[0]) && std::isfinite(facing[1]) && std::isfinite(facing[2]))
    {
        float length = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
        if (length > 0.5f && length < 1.5f && facing[2] / length < -WALL_STEEPNESS)
            ceiling = true;
        else if (length > 0.5f && length < 1.5f && std::fabs(facing[2] / length) < WALL_STEEPNESS)
        {
            wall = true;
            if (!ghost.onWall)
                ghost.wallTurn = 0.0f;
            ghost.wallO = NormalizeAngle(std::atan2(facing[1], facing[0]));
        }
    }
    ghost.onWall = wall;
    ghost.onCeiling = ceiling;
    if (!ghost.note.empty())
    {
        ghost.note.clear();
        SendAddonState(player);
    }

    // Drawn now, unless points are coming quickly: then the next update draws the latest.
    uint32 now = getMSTime();
    if (getMSTimeDiff(ghost.updatedMs, now) >= POINT_DRAW_MS)
    {
        ghost.updatedMs = now;
        DrawGhost(player, *session, ghost, false);
    }
    return true;
}

std::string PlayerHousingMgr::GetGhostNote(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    return itr != _carrying.end() ? itr->second.note : "";
}

bool PlayerHousingMgr::PlaceGhost(Player* player, bool another, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
    {
        reason = "Nothing is following you: choose a piece to place or move first.";
        SendAddonState(player);  // the addon thought otherwise
        return false;
    }
    Session* session = GetOwnerSession(player, reason);
    if (!session)
    {
        EndGhost(player);
        SendAddonState(player);
        return false;
    }

    // Where it's shown now.
    Ghost& ghost = itr->second;
    float x;
    float y;
    float z;
    uint32 parent;
    PoseGhost(player, *session, ghost, x, y, z, parent);
    float facing = GhostFacing(ghost);
    float cosO = std::cos(facing);
    float sinO = std::sin(facing);
    auto spotOf = [&](GhostPiece const& piece, Placement& placement)
    {
        placement.x = x + piece.dx * cosO - piece.dy * sinO;
        placement.y = y + piece.dx * sinO + piece.dy * cosO;
        placement.z = z + piece.dz;
        placement.o = NormalizeAngle(facing + piece.dO);
    };

    if (ghost.setId)
    {
        // A saved set: every piece, its first one where the ghost's lead is (pieces it doesn't
        // own come from the Collection, paid for).
        Position target;
        target.Relocate(x, y, z);
        if (!IsSpotOnIsland(x, y, z))
        {
            reason = "That spot is off your island.";
            return false;
        }
        if (!StampSet(player, *session, ghost.setId, target, facing, reason))
            return false;
        if (another)
            DrawGhost(player, *session, ghost, true);  // another of the set follows
        else
            EndGhost(player);
        SendAddonState(player);
        return true;
    }

    if (ghost.isNew)
    {
        GhostPiece const& lead = ghost.pieces.front();
        PieceDefinition const* piece = GetPiece(lead.itemEntry);
        if (!piece || !CheckLimit(*session, *piece, reason))
            return false;
        Placement placement;
        spotOf(lead, placement);
        if (!IsSpotOnIsland(placement.x, placement.y, placement.z))
        {
            reason = "That spot is off your island.";
            return false;
        }
        // One the player owns, or a new copy from the Collection (bought now, outside FreeMode).
        bool handedOver = false;
        uint32 paid = 0;
        if (!TakePiece(player, lead.itemEntry))
        {
            if (!IsUnlocked(player, *piece))
            {
                reason = Acore::StringFormat("You don't have another {}.", piece->name);
                EndGhost(player);
                SendAddonState(player);
                return false;
            }
            if (!_freeMode && piece->copyCost)
            {
                if (player->GetMoney() < piece->copyCost)
                {
                    reason = Acore::StringFormat("A new {} costs {}.", piece->name, FormatMoney(piece->copyCost));
                    return false;
                }
                paid = piece->copyCost;
                player->ModifyMoney(-int64(paid));
            }
            handedOver = true;
        }

        placement.id = session->nextPlacementId++;
        placement.itemEntry = lead.itemEntry;
        placement.scale = lead.scale;
        placement.pitch = piece->IsCreature() ? 0.0f : lead.pitch;
        placement.roll = piece->IsCreature() ? 0.0f : lead.roll;
        placement.look = lead.look;
        placement.parent = piece->IsBuilding() ? 0 : parent;
        // A roommate's piece stays theirs: picking it up returns it to them.
        if (HomeOf(player) != session->ownerGuid)
            placement.placedBy = HomeOf(player);
        if (!AddNewPlacement(player, *session, placement, reason))
        {
            if (!handedOver)
                ReturnPiece(player, lead.itemEntry);
            if (paid)
                player->ModifyMoney(int64(paid));
            return false;
        }
        // Money spent is news (the piece itself is there to see).
        if (paid)
            reason = Acore::StringFormat("Bought a new {} for {}.{}", piece->name, FormatMoney(paid), reason.empty() ? "" : " " + reason);

        // Another of the same, while there are more (and room, and money, for them).
        std::string more;
        bool anotherOne = HasOneToPlace(player, lead.itemEntry)
            || (IsUnlocked(player, *piece) && (_freeMode || player->GetMoney() >= piece->copyCost));
        if (another && CheckLimit(*session, *piece, more) && anotherOne)
            DrawGhost(player, *session, ghost, true);  // another follows (the ghost shows it)
        else
        {
            if (another)
                reason += " " + (more.empty() ? std::string("That was the last one.") : more);
            EndGhost(player);
        }
        SendAddonState(player);
        return true;
    }

    // Moving: every piece to its new spot at once, one undo step.
    std::set<uint32> moving;
    for (GhostPiece const& piece : ghost.pieces)
        moving.insert(piece.placementId);
    std::vector<Change> changes;
    for (size_t index = 0; index < ghost.pieces.size(); ++index)
    {
        GhostPiece const& piece = ghost.pieces[index];
        auto placementItr = session->placements.find(piece.placementId);
        if (placementItr == session->placements.end())
        {
            reason = "Those pieces have changed since (picked up, or moved by someone else).";
            EndGhost(player);
            SendAddonState(player);
            return false;
        }
        Placement const& before = placementItr->second;
        Placement after = before;
        spotOf(piece, after);
        // Sized and tilted while it followed.
        after.scale = piece.scale;
        after.pitch = piece.pitch;
        after.roll = piece.roll;
        PieceDefinition const* definition = GetPiece(before.itemEntry);
        bool standsOnMoving = before.parent && moving.count(before.parent);
        if (definition && definition->IsBuilding())
            after.parent = 0;
        else if (index == 0)
            after.parent = parent;
        else if (!standsOnMoving)
            after.parent = FindSurfaceUnder(*session, after.x, after.y, after.z, moving);
        changes.push_back(Change{ piece.placementId, before, after });
    }

    size_t count = changes.size();
    std::string label = count == 1 ? "moved" : "moved " + Pieces(count);
    bool ok = Commit(player, *session, label, std::move(changes), reason, false, count > 1);
    if (ok)
        EndGhost(player);
    SendAddonState(player);
    return ok;
}

void PlayerHousingMgr::UpdateGhost(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
        return;

    // No longer allowed to decorate here (the owner left, a roommate was sent home): it goes.
    auto sessionItr = _sessionsByOwner.find(GetIslandOwner(player));
    if (sessionItr == _sessionsByOwner.end() || !CanDecorate(player))
    {
        EndGhost(player);
        SendAddonState(player);
        return;
    }

    Ghost& ghost = itr->second;
    // The player turning turns the piece by as much (on a wall, about the wall): a finer turn
    // than the wheel's, by turning on the spot.
    float playerO = player->GetOrientation();
    if (ghost.playerOKnown)
    {
        float delta = std::remainder(playerO - ghost.playerO, TWO_PI_F);
        if (std::fabs(delta) > 0.0005f)
        {
            if (ghost.atPoint && ghost.onWall)
                ghost.wallTurn = NormalizeAngle(ghost.wallTurn + delta);
            else
                ghost.o = NormalizeAngle(ghost.o + delta);
        }
    }
    ghost.playerO = playerO;
    ghost.playerOKnown = true;

    uint32 now = getMSTime();
    if (getMSTimeDiff(ghost.updatedMs, now) < GHOST_UPDATE_MS)
        return;
    ghost.updatedMs = now;
    DrawGhost(player, sessionItr->second, ghost, false);
}
