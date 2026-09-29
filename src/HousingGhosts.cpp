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

// Ghosts: a piece that follows its player until it's set down. The client can't say where the
// mouse points in the world, so the piece floats ahead of the player instead: they walk it
// where it goes, push it farther or nearer, raise it and turn it, and see it there before
// setting it down. Furniture is a see-through copy of itself (a creature with its model, which
// can glide); buildings (world models, which creatures can't be) and pieces without a ghost
// model are carried as they are, redrawn a few times a second.

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
    if (!_ghosts || !piece || piece->IsBuilding() || itemEntry < 900000)
        return 0;
    uint32 display = GHOST_DISPLAY_BASE + (itemEntry - 900000);
    return sCreatureDisplayInfoStore.LookupEntry(display) ? display : 0;
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

void PlayerHousingMgr::PoseGhost(Player* player, Session const& session, Ghost& ghost, float& x, float& y, float& z, uint32& parent)
{
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

    PieceDefinition const* lead = GetPiece(ghost.pieces.front().itemEntry);
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
    float cosO = std::cos(ghost.o);
    float sinO = std::sin(ghost.o);
    for (GhostPiece& piece : ghost.pieces)
    {
        // Its spot: the lead's, and its own place around the lead, turned with it.
        float gx = x + piece.dx * cosO - piece.dy * sinO;
        float gy = y + piece.dx * sinO + piece.dy * cosO;
        float gz = z + piece.dz;
        float go = NormalizeAngle(ghost.o + piece.dO);
        bool moved = force || !piece.shown || std::fabs(gx - piece.x) > 0.02f || std::fabs(gy - piece.y) > 0.02f
            || std::fabs(gz - piece.z) > 0.02f || AngleBetween(go, piece.o) > 0.005f;
        if (!moved)
            continue;

        PieceDefinition const* definition = GetPiece(piece.itemEntry);
        if (!definition)
            continue;
        float size = definition->scale > 0.0f ? piece.scale / definition->scale : 1.0f;

        if (uint32 display = GhostDisplayFor(piece.itemEntry))
        {
            // See-through: a creature with the piece's model, gliding to the new spot.
            Creature* ghostCreature = piece.shown ? map->GetCreature(piece.shown) : nullptr;
            if (!ghostCreature)
            {
                Position position;
                position.Relocate(gx, gy, gz, go);
                TempSummon* summon = map->SummonCreature(GHOST_ENTRY, position);
                if (!summon)
                    continue;
                summon->SetDisplayId(display);
                summon->SetNativeDisplayId(display);
                float scale = 1.0f;
                if (definition->HasFlag(PIECE_FLAG_FIGURE))
                    scale = piece.scale;
                else if (!definition->IsCreature())
                    if (GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(definition->goEntry))
                        scale = goInfo->size * size;
                summon->SetObjectScale(scale);
                summon->SetReactState(REACT_PASSIVE);
                summon->SetDisableGravity(true);
                summon->SetCanFly(true);
                summon->SetPhaseMask(session.phaseMask, true);
                piece.shown = summon->GetGUID();
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
            // Carried as it is: the object itself, put down again in the new spot.
            if (!force && piece.shown && getMSTimeDiff(piece.drawnMs, now) < CARRY_REDRAW_MS)
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
                object->SetLocalRotationAngles(go, piece.pitch, piece.roll);
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
    }
    ghost.posed = true;
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
    DespawnGhost(itr->second, player->IsInWorld() ? player->GetMap() : GetHousingMap());
    _carrying.erase(itr);
}

void PlayerHousingMgr::CancelGhost(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (!_carrying.count(player->GetGUID()))
        return;
    EndGhost(player);
    SendAddonState(player);
}

bool PlayerHousingMgr::StartGhostNew(Player* player, uint32 itemEntry, uint32 copyOf, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
    {
        if (!GetIslandOwner(player))
            reason = "Furnishings go on your own island. House Key, Go home.";
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
        Tip(player, TIP_LIMIT, "Pick up pieces you don't need: they go back to your bags.");
        return false;
    }

    // One to set down: from the bags or House Storage, else a new copy from the Collection
    // (FreeMode's into House Storage; otherwise bought, into the bags).
    std::string got;
    std::string missing;
    if (!EnsurePieces(player, { { itemEntry, 1 } }, missing))
    {
        if (_freeMode || !IsUnlocked(player, *piece))
        {
            reason = missing;
            return false;
        }
        uint32 cost = piece->copyCost;
        if (!GetCopy(player, itemEntry, reason))
            return false;
        got = cost ? Acore::StringFormat("Bought a new one for {}. ", FormatMoney(cost)) : "";
    }

    CancelMove(player);
    EndGhost(player);

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
        lead.look = uint32(player->getRace()) | (uint32(player->getGender()) << 8);  // it takes after its owner
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

    Ghost& carried = _carrying[player->GetGUID()] = ghost;
    DrawGhost(player, *session, carried, true);
    reason = got + Acore::StringFormat("The {} follows you: walk it where it goes, then G sets it down (Escape: never mind).", piece->name);
    SendAddonState(player);
    return true;
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

    CancelMove(player);
    EndGhost(player);

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

    Ghost& shown = _carrying[player->GetGUID()] = ghost;
    DrawGhost(player, *session, shown, true);
    size_t count = moving.size();
    reason = Acore::StringFormat("{} {} you: walk {} where {} go, then G sets {} down (Escape: never mind).",
        count == 1 ? "The " + PieceName(lead.itemEntry) : Pieces(count), count == 1 ? "follows" : "follow",
        count == 1 ? "it" : "them", count == 1 ? "it should" : "they should", count == 1 ? "it" : "them");
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::AdjustGhost(Player* player, float forward, float left, float up, float degrees, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
    {
        reason = "Nothing is following you: choose a piece to place or move first.";
        return false;
    }
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Ghost& ghost = itr->second;
    ghost.forward = std::clamp(ghost.forward + std::clamp(forward, -5.0f, 5.0f), MIN_FORWARD, MAX_FORWARD);
    ghost.side = std::clamp(ghost.side + std::clamp(left, -5.0f, 5.0f), -MAX_SIDE, MAX_SIDE);
    ghost.lift = std::clamp(ghost.lift + std::clamp(up, -2.0f, 2.0f), MIN_LIFT, MAX_LIFT);
    ghost.o = NormalizeAngle(ghost.o + std::clamp(degrees, -360.0f, 360.0f) * PI_F / 180.0f);
    DrawGhost(player, *session, ghost, true);
    return true;
}

bool PlayerHousingMgr::PlaceGhost(Player* player, bool another, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _carrying.find(player->GetGUID());
    if (itr == _carrying.end())
    {
        reason = "Nothing is following you: choose a piece to place or move first.";
        return false;
    }
    Session* session = GetOwnerSession(player, reason);
    if (!session)
    {
        EndGhost(player);
        return false;
    }

    // Where it's shown now.
    Ghost& ghost = itr->second;
    float x;
    float y;
    float z;
    uint32 parent;
    PoseGhost(player, *session, ghost, x, y, z, parent);
    float cosO = std::cos(ghost.o);
    float sinO = std::sin(ghost.o);
    auto spotOf = [&](GhostPiece const& piece, Placement& placement)
    {
        placement.x = x + piece.dx * cosO - piece.dy * sinO;
        placement.y = y + piece.dx * sinO + piece.dy * cosO;
        placement.z = z + piece.dz;
        placement.o = NormalizeAngle(ghost.o + piece.dO);
    };

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
        if (!TakeItem(player, lead.itemEntry))
        {
            reason = Acore::StringFormat("You don't have another {}.", piece->name);
            EndGhost(player);
            SendAddonState(player);
            return false;
        }

        placement.id = session->nextPlacementId++;
        placement.itemEntry = lead.itemEntry;
        placement.scale = lead.scale;
        placement.pitch = piece->IsCreature() ? 0.0f : lead.pitch;
        placement.roll = piece->IsCreature() ? 0.0f : lead.roll;
        placement.look = lead.look;
        placement.parent = piece->IsBuilding() ? 0 : parent;
        // A roommate's piece stays theirs: picking it up returns it to them.
        if (player->GetGUID().GetCounter() != session->ownerGuid)
            placement.placedBy = player->GetGUID().GetCounter();
        if (!AddNewPlacement(player, *session, placement, reason))
        {
            bool toStorage;
            ReturnItem(player, lead.itemEntry, toStorage);
            return false;
        }

        // Another of the same, while there are more (and room for them).
        std::string more;
        if (another && CheckLimit(*session, *piece, more) && EnsurePieces(player, { { lead.itemEntry, 1 } }, more))
        {
            DrawGhost(player, *session, ghost, true);
            reason += " Another follows you.";
        }
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
    uint32 now = getMSTime();
    if (getMSTimeDiff(ghost.updatedMs, now) < GHOST_UPDATE_MS)
        return;
    ghost.updatedMs = now;
    DrawGhost(player, sessionItr->second, ghost, false);
}
