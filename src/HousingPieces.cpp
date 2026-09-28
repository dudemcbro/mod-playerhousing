#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>

using namespace Housing;

namespace
{
    constexpr float PI_F = 3.14159265358979323846f;
    constexpr float TWO_PI_F = 6.28318530717958647692f;
    constexpr size_t JOURNAL_SIZE = 30;
    constexpr float NO_HEIGHT = -50000.0f;

    float NormalizeAngle(float angle)
    {
        angle = std::fmod(angle, TWO_PI_F);
        return angle < 0.0f ? angle + TWO_PI_F : angle;
    }

    float GroundZ(Map* map, uint32 phaseMask, float x, float y, float zHint)
    {
        float height = map->GetHeight(phaseMask, x, y, zHint + 5.0f, true, 50.0f);
        if (height <= NO_HEIGHT)
            height = map->GetHeight(phaseMask, x, y, zHint + 50.0f, true, 200.0f);
        return height > NO_HEIGHT ? height : zHint;
    }
}

PieceDefinition const* PlayerHousingMgr::GetPiece(uint32 itemEntry) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _pieces.find(itemEntry);
    return itr != _pieces.end() ? &itr->second : nullptr;
}

std::vector<PieceDefinition const*> PlayerHousingMgr::GetPiecesInCategory(uint8 category) const
{
    std::vector<PieceDefinition const*> pieces;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        for (auto const& [itemEntry, piece] : _pieces)
            if (piece.category == category)
                pieces.push_back(&piece);
    }

    std::sort(pieces.begin(), pieces.end(), [](PieceDefinition const* left, PieceDefinition const* right)
    {
        if (left->sortOrder != right->sortOrder)
            return left->sortOrder < right->sortOrder;
        return left->name < right->name;
    });
    return pieces;
}

std::string PlayerHousingMgr::PieceName(uint32 itemEntry) const
{
    PieceDefinition const* piece = GetPiece(itemEntry);
    return piece ? piece->name : "furniture";
}

PlayerHousingMgr::Session const* PlayerHousingMgr::FindSessionOf(Player const* player) const
{
    auto playerItr = _playerOwnerByGuid.find(player->GetGUID());
    if (playerItr == _playerOwnerByGuid.end())
        return nullptr;

    auto sessionItr = _sessionsByOwner.find(playerItr->second);
    return sessionItr != _sessionsByOwner.end() ? &sessionItr->second : nullptr;
}

PlayerHousingMgr::Session* PlayerHousingMgr::GetOwnerSession(Player* player, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return nullptr;
    }

    ObjectGuid::LowType owner = GetIslandOwner(player);
    if (owner == 0)
    {
        reason = "Go home first: House Key, Go home.";
        return nullptr;
    }

    if (owner != player->GetGUID().GetCounter())
    {
        reason = "Only the owner can change things on this island.";
        return nullptr;
    }

    auto sessionItr = _sessionsByOwner.find(owner);
    if (sessionItr == _sessionsByOwner.end())
    {
        reason = "Your island is still loading. Try again in a moment.";
        return nullptr;
    }

    if (!sessionItr->second.initialized)
    {
        std::string initReason;
        if (!InitializeSession(owner, initReason))
        {
            reason = initReason;
            return nullptr;
        }
    }

    return &sessionItr->second;
}

void PlayerHousingMgr::CountPlaced(ObjectGuid::LowType ownerGuid, uint32& furnishings, uint32& buildings) const
{
    furnishings = 0;
    buildings = 0;

    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    if (sessionItr != _sessionsByOwner.end() && sessionItr->second.initialized)
    {
        for (auto const& [id, placement] : sessionItr->second.placements)
        {
            auto pieceItr = _pieces.find(placement.itemEntry);
            if (pieceItr != _pieces.end() && pieceItr->second.IsBuilding())
                ++buildings;
            else
                ++furnishings;
        }
        return;
    }

    if (QueryResult result = CharacterDatabase.Query(
            "SELECT source_item_entry FROM mod_playerhousing_placement WHERE owner_guid={} AND map_id={}", ownerGuid, _layout.mapId))
    {
        do
        {
            auto pieceItr = _pieces.find((*result)[0].Get<uint32>());
            if (pieceItr == _pieces.end())
                continue;
            if (pieceItr->second.IsBuilding())
                ++buildings;
            else
                ++furnishings;
        } while (result->NextRow());
    }
}

std::string PlayerHousingMgr::CountsText(ObjectGuid::LowType ownerGuid) const
{
    uint32 furnishings = 0;
    uint32 buildings = 0;
    CountPlaced(ownerGuid, furnishings, buildings);
    return Acore::StringFormat("{}/{} furnishings, {}/{} buildings", furnishings, _maxFurnishings, buildings, _maxBuildings);
}

bool PlayerHousingMgr::CheckLimit(Session const& session, PieceDefinition const& piece, std::string& reason) const
{
    uint32 furnishings = 0;
    uint32 buildings = 0;
    for (auto const& [id, placement] : session.placements)
    {
        auto pieceItr = _pieces.find(placement.itemEntry);
        if (pieceItr != _pieces.end() && pieceItr->second.IsBuilding())
            ++buildings;
        else
            ++furnishings;
    }

    if (piece.IsBuilding() && buildings >= _maxBuildings)
    {
        reason = Acore::StringFormat("Your island has {}/{} buildings. Pick one up first.", buildings, _maxBuildings);
        return false;
    }

    if (!piece.IsBuilding() && furnishings >= _maxFurnishings)
    {
        reason = Acore::StringFormat("Your island is full ({}/{} furnishings). Pick something up first.", furnishings, _maxFurnishings);
        return false;
    }

    return true;
}

bool PlayerHousingMgr::IsSpotOnIsland(float x, float y, float z) const
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;

    float bottom = _layout.landing.GetPositionZ() - 40.0f;
    float top = _layout.landing.GetPositionZ() + 250.0f;
    return IsOnIslandGround(x, y) && z > bottom && z < top;
}

void PlayerHousingMgr::AddToStorage(ObjectGuid::LowType ownerGuid, uint32 itemEntry, int32 delta) const
{
    if (delta > 0)
    {
        CharacterDatabase.DirectExecute(
            "INSERT INTO mod_playerhousing_storage (owner_guid, item_entry, count) VALUES ({}, {}, {}) "
            "ON DUPLICATE KEY UPDATE count = count + {}", ownerGuid, itemEntry, delta, delta);
        return;
    }

    CharacterDatabase.DirectExecute(
        "UPDATE mod_playerhousing_storage SET count = GREATEST(0, CAST(count AS SIGNED) + {}) WHERE owner_guid={} AND item_entry={}",
        delta, ownerGuid, itemEntry);
    CharacterDatabase.DirectExecute("DELETE FROM mod_playerhousing_storage WHERE owner_guid={} AND count=0", ownerGuid);
}

std::map<uint32, uint32> PlayerHousingMgr::GetStorage(ObjectGuid::LowType ownerGuid) const
{
    std::map<uint32, uint32> storage;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT item_entry, count FROM mod_playerhousing_storage WHERE owner_guid={} AND count > 0", ownerGuid))
    {
        do
        {
            storage[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
        } while (result->NextRow());
    }
    return storage;
}

bool PlayerHousingMgr::ReturnItem(Player* player, uint32 itemEntry, bool& toStorage)
{
    toStorage = false;
    if (player->AddItem(itemEntry, 1))
        return true;

    AddToStorage(player->GetGUID().GetCounter(), itemEntry, 1);
    toStorage = true;
    Tip(player, TIP_FIRST_STORAGE, "Your bags were full, so it went to House Storage. House Key, Storage, to take it out.");
    return true;
}

bool PlayerHousingMgr::TakeItem(Player* player, uint32 itemEntry)
{
    uint32 reserved = 0;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto pendingItr = _pendingConsumes.find(player->GetGUID());
        if (pendingItr != _pendingConsumes.end())
        {
            auto entryItr = pendingItr->second.find(itemEntry);
            if (entryItr != pendingItr->second.end())
                reserved = entryItr->second;
        }
    }

    if (player->GetItemCount(itemEntry) > reserved)
    {
        player->DestroyItemCount(itemEntry, 1, true);
        return true;
    }

    std::map<uint32, uint32> storage = GetStorage(player->GetGUID().GetCounter());
    auto itr = storage.find(itemEntry);
    if (itr == storage.end() || itr->second == 0)
        return false;

    AddToStorage(player->GetGUID().GetCounter(), itemEntry, -1);
    return true;
}

void PlayerHousingMgr::ProcessPendingConsumes(Player* player)
{
    std::map<uint32, uint32> consumes;
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        auto itr = _pendingConsumes.find(player->GetGUID());
        if (itr == _pendingConsumes.end())
            return;
        consumes.swap(itr->second);
        _pendingConsumes.erase(itr);
    }

    for (auto const& [itemEntry, count] : consumes)
    {
        uint32 inBags = player->GetItemCount(itemEntry);
        uint32 fromBags = std::min(inBags, count);
        if (fromBags)
            player->DestroyItemCount(itemEntry, fromBags, true);
        // Used up meanwhile (sold, destroyed): take the rest from storage so nothing is free.
        for (uint32 i = fromBags; i < count; ++i)
            AddToStorage(player->GetGUID().GetCounter(), itemEntry, -1);
    }
}

void PlayerHousingMgr::SavePlacement(ObjectGuid::LowType ownerGuid, Placement const& placement, uint32 mapId) const
{
    // The gear on a stand is saved as it moves (see HousingStands.cpp), never from here.
    CharacterDatabase.DirectExecute(
        "REPLACE INTO mod_playerhousing_placement "
        "(owner_guid, placement_id, source_item_entry, map_id, scale, pos_x, pos_y, pos_z, orientation, look, parent_id, pitch, roll) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        ownerGuid, placement.id, placement.itemEntry, mapId, placement.scale, placement.x, placement.y, placement.z, placement.o, placement.look,
        placement.parent, placement.pitch, placement.roll);
}

void PlayerHousingMgr::DeletePlacement(ObjectGuid::LowType ownerGuid, uint32 placementId) const
{
    CharacterDatabase.DirectExecute("DELETE FROM mod_playerhousing_placement WHERE owner_guid={} AND placement_id={}", ownerGuid, placementId);
}

bool PlayerHousingMgr::SpawnPlacement(Session& session, Map* map, Placement const& placement)
{
    auto pieceItr = _pieces.find(placement.itemEntry);
    if (!map || pieceItr == _pieces.end())
        return false;

    PieceDefinition const& piece = pieceItr->second;
    if (piece.HasFlag(PIECE_FLAG_STAND))
        return SpawnStand(session, map, placement);

    bool editCopy = session.decorating && piece.editGoEntry != 0;
    uint32 entry = editCopy ? piece.editGoEntry : piece.goEntry;
    GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(entry);
    if (!goInfo)
        return false;

    GameObject* object = new GameObject();
    if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), entry, map, PHASEMASK_NORMAL,
            placement.x, placement.y, placement.z, placement.o, G3D::Quat(0.0f, 0.0f, 0.0f, 0.0f), 100, GO_STATE_READY))
    {
        delete object;
        return false;
    }

    object->SetRespawnTime(0);
    object->SetSpawnedByDefault(false);
    object->SetObjectScale(goInfo->size * (placement.scale / piece.scale));
    if (placement.pitch != 0.0f || placement.roll != 0.0f)
        object->SetLocalRotationAngles(placement.o, placement.pitch, placement.roll);
    // Buildings are seen from across the island instead of popping in at the normal range.
    if (piece.IsBuilding())
        object->SetVisibilityDistanceOverride(VisibilityDistanceType::Large);

    if (!map->AddToMap(object))
    {
        delete object;
        return false;
    }

    object->SetPhaseMask(session.phaseMask, true);
    // Server-side collision compares phases bit by bit, so every island would collide with
    // every other island's pieces; clients collide with what they see anyway.
    object->EnableCollision(false);

    SpawnedPiece& spawned = session.spawned[placement.id];
    spawned.guid = object->GetGUID();
    spawned.editCopy = editCopy;
    return true;
}

void PlayerHousingMgr::DespawnPlacement(Session& session, Map* map, uint32 placementId)
{
    auto itr = session.spawned.find(placementId);
    if (itr != session.spawned.end())
    {
        RemoveSpawned(map, itr->second.guid);
        session.spawned.erase(itr);
    }

    auto markerItr = session.markers.find(placementId);
    if (markerItr != session.markers.end())
    {
        if (map)
            if (GameObject* marker = map->GetGameObject(markerItr->second))
                marker->AddObjectToRemoveList();
        session.markers.erase(markerItr);
    }
}

void PlayerHousingMgr::RespawnPlacement(Session& session, Map* map, uint32 placementId)
{
    DespawnPlacement(session, map, placementId);
    auto itr = session.placements.find(placementId);
    if (itr == session.placements.end())
        return;

    SpawnPlacement(session, map, itr->second);
    if (session.decorating)
        SpawnMarkers(session, map);
}

void PlayerHousingMgr::SpawnMarkers(Session& session, Map* map)
{
    if (!map)
        return;

    for (auto const& [id, placement] : session.placements)
    {
        if (session.markers.count(id))
            continue;

        auto pieceItr = _pieces.find(placement.itemEntry);
        if (pieceItr == _pieces.end() || !pieceItr->second.HasFlag(PIECE_FLAG_SURFACE))
            continue;

        float top = placement.z + pieceItr->second.height * (placement.scale / pieceItr->second.scale);
        GameObject* marker = new GameObject();
        if (!marker->Create(map->GenerateLowGuid<HighGuid::GameObject>(), HOOK_MARKER_GO, map, PHASEMASK_NORMAL,
                placement.x, placement.y, top + 0.05f, placement.o, G3D::Quat(0.0f, 0.0f, 0.0f, 0.0f), 100, GO_STATE_READY))
        {
            delete marker;
            continue;
        }

        marker->SetRespawnTime(0);
        marker->SetSpawnedByDefault(false);
        if (!map->AddToMap(marker))
        {
            delete marker;
            continue;
        }

        marker->SetPhaseMask(session.phaseMask, true);
        marker->EnableCollision(false);
        session.markers[id] = marker->GetGUID();
    }
}

void PlayerHousingMgr::DespawnMarkers(Session& session, Map* map)
{
    for (auto const& [id, guid] : session.markers)
        if (map)
            if (GameObject* marker = map->GetGameObject(guid))
                marker->AddObjectToRemoveList();
    session.markers.clear();
}

void PlayerHousingMgr::SpawnSteward(Session& session, Map* map)
{
    Position const& landing = _layout.landing;
    float forwardX = std::cos(landing.GetOrientation());
    float forwardY = std::sin(landing.GetOrientation());
    float leftX = -forwardY;
    float leftY = forwardX;

    float x = landing.GetPositionX() + forwardX * _layout.stewardOffsetX + leftX * _layout.stewardOffsetY;
    float y = landing.GetPositionY() + forwardY * _layout.stewardOffsetX + leftY * _layout.stewardOffsetY;
    float z = GroundZ(map, PHASEMASK_NORMAL, x, y, landing.GetPositionZ());
    float o = NormalizeAngle(std::atan2(landing.GetPositionY() - y, landing.GetPositionX() - x));

    Position position;
    position.Relocate(x, y, z + 0.1f, o);
    if (TempSummon* steward = map->SummonCreature(_stewardEntry, position))
    {
        steward->SetPhaseMask(session.phaseMask, true);
        steward->SetDisplayId(_stewardDisplayId);
        steward->SetNativeDisplayId(_stewardDisplayId);
        session.stewardGuid = steward->GetGUID();
    }
    else
        LOG_WARN("module", "mod-playerhousing: Failed to summon steward entry {} for owner {}.", _stewardEntry, session.ownerGuid);
}

void PlayerHousingMgr::PlaceStarterWreckage(Session& session, Map* map)
{
    // The fallen cart and the shredded tent stand a few yards up the island from the landing
    // spot, one to each side, facing the new arrival.
    Position const& landing = _layout.landing;
    float offsets[][2] = { { 9.0f, 5.0f }, { 9.0f, -6.0f }, { 14.0f, 0.0f }, { 5.0f, 10.0f } };
    uint32 index = 0;
    for (auto const& [itemEntry, piece] : _pieces)
    {
        if (!piece.HasFlag(PIECE_FLAG_WRECKAGE) || index >= std::size(offsets))
            continue;

        float forward = offsets[index][0];
        float left = offsets[index][1];
        ++index;

        float o = landing.GetOrientation();
        Placement placement;
        placement.id = session.nextPlacementId++;
        placement.itemEntry = itemEntry;
        placement.x = landing.GetPositionX() + std::cos(o) * forward - std::sin(o) * left;
        placement.y = landing.GetPositionY() + std::sin(o) * forward + std::cos(o) * left;
        placement.z = GroundZ(map, PHASEMASK_NORMAL, placement.x, placement.y, landing.GetPositionZ());
        placement.o = NormalizeAngle(std::atan2(landing.GetPositionY() - placement.y, landing.GetPositionX() - placement.x));
        placement.scale = piece.scale;

        session.placements[placement.id] = placement;
        SavePlacement(session.ownerGuid, placement, session.mapId);
        SpawnPlacement(session, map, placement);
    }
}

bool PlayerHousingMgr::HandlePlacementCast(Player* player, Item* castItem, Position const& target, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);

    PieceDefinition const* piece = castItem ? GetPiece(castItem->GetEntry()) : nullptr;
    if (!piece)
    {
        reason = "That isn't a furnishing.";
        return false;
    }

    Session* session = GetOwnerSession(player, reason);
    if (!session)
    {
        if (!GetIslandOwner(player))
            reason = "Furnishings go on your own island. House Key, Go home.";
        return false;
    }

    if (!IsSpotOnIsland(target.GetPositionX(), target.GetPositionY(), target.GetPositionZ()))
    {
        reason = "That spot is off your island.";
        return false;
    }

    if (!CheckLimit(*session, *piece, reason))
    {
        Tip(player, TIP_LIMIT, "Pick up pieces you don't need: they go back to your bags.");
        return false;
    }

    uint32& reserved = _pendingConsumes[player->GetGUID()][piece->itemEntry];
    if (player->GetItemCount(piece->itemEntry) <= reserved)
    {
        reason = Acore::StringFormat("You don't have another {}.", piece->name);
        return false;
    }

    Placement placement;
    placement.id = session->nextPlacementId++;
    placement.itemEntry = piece->itemEntry;
    placement.x = target.GetPositionX();
    placement.y = target.GetPositionY();
    placement.z = target.GetPositionZ();
    placement.o = NormalizeAngle(std::atan2(player->GetPositionY() - placement.y, player->GetPositionX() - placement.x));
    placement.scale = piece->scale;
    // A new mannequin takes after its owner.
    if (piece->HasFlag(PIECE_FLAG_STAND))
        placement.look = uint32(player->getRace()) | (uint32(player->getGender()) << 8);
    // Clicked right onto a table top: it stands on the table and moves with it.
    if (!piece->IsBuilding())
        placement.parent = FindSurfaceUnder(*session, placement.x, placement.y, placement.z);

    // A copy of a piece takes after it; otherwise the grid (when on) squares it up.
    float grid = GetGridSize(player->GetGUID().GetCounter());
    auto copyItr = _pendingCopies.find(player->GetGUID());
    bool copying = copyItr != _pendingCopies.end() && copyItr->second.itemEntry == piece->itemEntry;
    if (copying)
    {
        placement.o = copyItr->second.o;
        placement.scale = copyItr->second.scale;
        placement.pitch = copyItr->second.pitch;
        placement.roll = copyItr->second.roll;
    }
    else if (grid > 0.0f)
    {
        float step = PI_F / 4.0f;
        placement.o = NormalizeAngle(std::round(placement.o / step) * step);
    }
    if (!placement.parent)
        SnapToGrid(player->GetGUID().GetCounter(), placement.x, placement.y);

    Map* map = player->GetMap();
    if (!SpawnPlacement(*session, map, placement))
    {
        reason = "Couldn't place that here.";
        return false;
    }

    // The item is still in use by the cast that brought us here; it is removed on the
    // player's next update (see ProcessPendingConsumes).
    ++reserved;

    session->placements[placement.id] = placement;
    session->selected = placement.id;
    SavePlacement(session->ownerGuid, placement, session->mapId);
    if (session->decorating)
        SpawnMarkers(*session, map);
    if (copying)
        _pendingCopies.erase(player->GetGUID());

    Record(player, "placed " + piece->name, { Change{ placement.id, std::nullopt, placement } });
    reason = Acore::StringFormat("Placed {} ({}).", piece->name, CountsText(session->ownerGuid));
    Tip(player, TIP_FIRST_PLACE, "In decorate mode, click a piece to turn, move or pick it up. Mistake? House Key, Undo.");
    return true;
}

bool PlayerHousingMgr::ApplyState(Player* player, Session& session, Map* map, uint32 placementId, std::optional<Placement> const& target, std::string& reason)
{
    auto currentItr = session.placements.find(placementId);
    bool exists = currentItr != session.placements.end();

    if (exists && !target)
    {
        Placement current = currentItr->second;
        for (auto const& [slot, gear] : current.gear)
            ReturnGear(player, session.ownerGuid, placementId, slot, gear);
        DespawnPlacement(session, map, placementId);
        session.placements.erase(currentItr);
        DeletePlacement(session.ownerGuid, placementId);
        if (session.selected == placementId)
            session.selected = 0;

        bool toStorage = false;
        ReturnItem(player, current.itemEntry, toStorage);
        if (toStorage)
            ++_report.toStorage;
        else
            ++_report.toBags;
        return true;
    }

    if (!exists && target)
    {
        auto pieceItr = _pieces.find(target->itemEntry);
        if (pieceItr == _pieces.end())
        {
            reason = "That piece no longer exists.";
            return false;
        }

        if (!CheckLimit(session, pieceItr->second, reason))
            return false;

        if (!TakeItem(player, target->itemEntry))
        {
            reason = Acore::StringFormat("You no longer have {}.", pieceItr->second.name);
            return false;
        }

        // A stand comes back with the gear it had, as far as that gear is still in the bags.
        Placement placed = *target;
        placed.gear.clear();
        SavePlacement(session.ownerGuid, placed, session.mapId);
        for (auto const& [slot, gear] : target->gear)
        {
            std::string ignored;
            if (MoveGearToStand(player, session.ownerGuid, placementId, slot, gear.itemGuid, ignored))
                placed.gear[slot] = gear;
            else
                _report.gearMissing.push_back(StandItemName(gear.itemEntry));
        }

        session.placements[placementId] = placed;
        session.nextPlacementId = std::max(session.nextPlacementId, placementId + 1);
        SpawnPlacement(session, map, placed);
        session.selected = placementId;
        ++_report.placed;
        return true;
    }

    if (exists && target)
    {
        Placement current = currentItr->second;
        Placement applied = *target;
        auto sameItem = [](std::map<uint8, GearItem> const& gear, uint8 slot, uint32 itemGuid)
        {
            auto itr = gear.find(slot);
            return itr != gear.end() && itr->second.itemGuid == itemGuid;
        };

        // Gear comes off first, so a swap frees the slot, then the new gear goes on.
        for (auto const& [slot, gear] : current.gear)
            if (!sameItem(target->gear, slot, gear.itemGuid))
                ReturnGear(player, session.ownerGuid, placementId, slot, gear);
        for (auto const& [slot, gear] : target->gear)
        {
            if (sameItem(current.gear, slot, gear.itemGuid))
                continue;
            std::string ignored;
            if (!MoveGearToStand(player, session.ownerGuid, placementId, slot, gear.itemGuid, ignored))
            {
                applied.gear.erase(slot);
                _report.gearMissing.push_back(StandItemName(gear.itemEntry));
            }
        }

        currentItr->second = applied;
        SavePlacement(session.ownerGuid, applied, session.mapId);
        RespawnPlacement(session, map, placementId);
        session.selected = placementId;
        return true;
    }

    return true;
}

bool PlayerHousingMgr::ApplyChanges(Player* player, Session& session, std::vector<Change> const& changes, bool towardsAfter, std::string& reason)
{
    Map* map = player->GetMap();
    bool allOk = true;
    auto apply = [&](Change const& change)
    {
        std::string failure;
        if (!ApplyState(player, session, map, change.placementId, towardsAfter ? change.after : change.before, failure))
        {
            allOk = false;
            reason = failure;
        }
    };

    // Undo walks the changes backwards, so a building comes back before what stood in it.
    if (towardsAfter)
        for (Change const& change : changes)
            apply(change);
    else
        for (auto itr = changes.rbegin(); itr != changes.rend(); ++itr)
            apply(*itr);

    if (session.decorating)
        SpawnMarkers(session, map);
    return allOk;
}

void PlayerHousingMgr::Record(Player* player, std::string const& label, std::vector<Change> changes)
{
    Journal& journal = _journals[player->GetGUID().GetCounter()];
    journal.undo.push_back(JournalEntry{ label, std::move(changes) });
    while (journal.undo.size() > JOURNAL_SIZE)
        journal.undo.pop_front();
    journal.redo.clear();
}

std::string PlayerHousingMgr::UndoLabel(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _journals.find(player->GetGUID().GetCounter());
    return itr != _journals.end() && !itr->second.undo.empty() ? itr->second.undo.back().label : "";
}

std::string PlayerHousingMgr::RedoLabel(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _journals.find(player->GetGUID().GetCounter());
    return itr != _journals.end() && !itr->second.redo.empty() ? itr->second.redo.back().label : "";
}

std::string PlayerHousingMgr::DescribeReturns() const
{
    std::string gear;
    // Gear taken off a stand is the subject of the sentence before; gear coming back with
    // a picked-up stand is extra news.
    bool piecesReturned = _report.toBags || _report.toStorage;
    if (_report.gearToBags && !piecesReturned)
        gear += _report.gearToBags == 1 ? " It's back in your bags." : " They're back in your bags.";
    else if (_report.gearToBags)
        gear += _report.gearToBags == 1 ? " A piece of gear went back to your bags too." : Acore::StringFormat(" {} pieces of gear went back to your bags too.", _report.gearToBags);
    if (_report.gearMailed)
        gear += Acore::StringFormat(" Your bags were full, so Krook mailed you {} (check your mailbox).",
            _report.gearMailed == 1 ? "a piece of gear" : Acore::StringFormat("{} pieces of gear", _report.gearMailed));
    if (!_report.gearMissing.empty())
    {
        std::string names;
        for (std::string const& name : _report.gearMissing)
            names += (names.empty() ? "" : ", ") + name;
        gear += " Not in your bags any more, so not put back: " + names + ".";
    }
    return DescribeItemReturns() + gear;
}

std::string PlayerHousingMgr::DescribeItemReturns() const
{
    if (_report.toStorage && _report.toBags)
        return Acore::StringFormat(" {} went back to your bags and {} to House Storage (bags full).", _report.toBags, _report.toStorage);
    if (_report.toStorage)
        return _report.toStorage == 1 ? " It's in your House Storage (bags full)." : " They're in your House Storage (bags full).";
    if (_report.toBags)
        return _report.toBags == 1 ? " It's back in your bags." : " They're back in your bags.";
    return "";
}

bool PlayerHousingMgr::Undo(Player* player, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Journal& journal = _journals[session->ownerGuid];
    if (journal.undo.empty())
    {
        reason = "Nothing to undo.";
        return false;
    }

    JournalEntry entry = journal.undo.back();
    journal.undo.pop_back();

    _report = {};
    std::string failure;
    bool ok = ApplyChanges(player, *session, entry.changes, false, failure);
    journal.redo.push_back(entry);

    reason = Acore::StringFormat("Undid: {}.{} ({})", entry.label, DescribeReturns(), CountsText(session->ownerGuid));
    if (!ok)
        reason += " Not everything could be undone: " + failure;
    SendAddonState(player);
    return ok;
}

bool PlayerHousingMgr::Redo(Player* player, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Journal& journal = _journals[session->ownerGuid];
    if (journal.redo.empty())
    {
        reason = "Nothing to redo.";
        return false;
    }

    JournalEntry entry = journal.redo.back();
    journal.redo.pop_back();

    _report = {};
    std::string failure;
    bool ok = ApplyChanges(player, *session, entry.changes, true, failure);
    journal.undo.push_back(entry);

    reason = Acore::StringFormat("Redid: {}.{} ({})", entry.label, DescribeReturns(), CountsText(session->ownerGuid));
    if (!ok)
        reason += " Not everything could be redone: " + failure;
    SendAddonState(player);
    return ok;
}

uint32 PlayerHousingMgr::GetSelectedPlacement(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _sessionsByOwner.find(player->GetGUID().GetCounter());
    return itr != _sessionsByOwner.end() ? itr->second.selected : 0;
}

uint32 PlayerHousingMgr::ResolvePlacementArgument(Player* player, uint32 placementId) const
{
    if (placementId)
        return placementId;

    if (uint32 selected = GetSelectedPlacement(player))
        return selected;

    // Nothing selected yet: the nearest piece within reach.
    std::vector<std::pair<Placement, float>> nearby = GetNearbyPlacements(player, 10.0f);
    return nearby.empty() ? 0 : nearby.front().first.id;
}

std::optional<Placement> PlayerHousingMgr::GetPlacement(Player const* player, uint32 placementId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session)
        return std::nullopt;

    auto itr = session->placements.find(placementId);
    if (itr == session->placements.end())
        return std::nullopt;
    return itr->second;
}

uint32 PlayerHousingMgr::GetPlacementForObject(Player const* player, ObjectGuid const& guid) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session)
        return 0;

    for (auto const& [id, spawned] : session->spawned)
        if (spawned.guid == guid)
            return id;
    return 0;
}

uint32 PlayerHousingMgr::GetSurfaceForMarker(Player const* player, ObjectGuid const& guid) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session)
        return 0;

    for (auto const& [id, markerGuid] : session->markers)
        if (markerGuid == guid)
            return id;
    return 0;
}

std::vector<std::pair<Placement, float>> PlayerHousingMgr::GetNearbyPlacements(Player const* player, float range) const
{
    std::vector<std::pair<Placement, float>> nearby;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session)
        return nearby;

    for (auto const& [id, placement] : session->placements)
    {
        float distance = player->GetExactDist(placement.x, placement.y, placement.z);
        auto pieceItr = _pieces.find(placement.itemEntry);
        // Buildings count from their edge, so standing inside one finds it.
        if (pieceItr != _pieces.end() && pieceItr->second.IsBuilding())
            distance = std::max(0.0f, player->GetExactDist2d(placement.x, placement.y) - pieceItr->second.footprint);
        if (distance <= range)
            nearby.emplace_back(placement, distance);
    }

    std::sort(nearby.begin(), nearby.end(), [](auto const& left, auto const& right) { return left.second < right.second; });
    return nearby;
}

std::vector<Placement> PlayerHousingMgr::GetPiecesInside(ObjectGuid::LowType ownerGuid, uint32 buildingPlacementId) const
{
    std::vector<Placement> inside;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
        return inside;

    Session const& session = sessionItr->second;
    auto buildingItr = session.placements.find(buildingPlacementId);
    if (buildingItr == session.placements.end())
        return inside;

    auto buildingPieceItr = _pieces.find(buildingItr->second.itemEntry);
    if (buildingPieceItr == _pieces.end() || !buildingPieceItr->second.IsBuilding())
        return inside;

    Placement const& building = buildingItr->second;
    PieceDefinition const& buildingPiece = buildingPieceItr->second;
    float radius = buildingPiece.footprint;
    float top = building.z + std::max(4.0f, buildingPiece.height);
    // Into the building's own frame (x forward), where its outline is a plain rectangle.
    float cosO = std::cos(building.o);
    float sinO = std::sin(building.o);
    float scale = buildingPiece.scale > 0.0f ? building.scale / buildingPiece.scale : 1.0f;
    for (auto const& [id, placement] : session.placements)
    {
        if (id == buildingPlacementId)
            continue;

        auto pieceItr = _pieces.find(placement.itemEntry);
        if (pieceItr == _pieces.end() || pieceItr->second.IsBuilding())
            continue;

        float dx = placement.x - building.x;
        float dy = placement.y - building.y;
        bool within;
        if (buildingPiece.HasOutline())
        {
            float localX = (cosO * dx + sinO * dy) / scale;
            float localY = (-sinO * dx + cosO * dy) / scale;
            within = localX >= buildingPiece.outlineMinX && localX <= buildingPiece.outlineMaxX &&
                     localY >= buildingPiece.outlineMinY && localY <= buildingPiece.outlineMaxY;
        }
        else
            within = dx * dx + dy * dy <= radius * radius;
        if (within && placement.z >= building.z - 1.5f && placement.z <= top)
            inside.push_back(placement);
    }
    return inside;
}

bool PlayerHousingMgr::PickUp(Player* player, uint32 placementId, bool withInside, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    if (itr == session->placements.end())
    {
        reason = "Nothing here to pick up. Stand next to a piece, or click it while decorating.";
        return false;
    }

    Placement placement = itr->second;
    PieceDefinition const* piece = GetPiece(placement.itemEntry);
    std::string name = piece ? piece->name : "furniture";

    // What stands on it comes along; a building brings what's inside only when asked.
    std::vector<Change> changes;
    changes.push_back(Change{ placementId, placement, std::nullopt });
    uint32 insideCount = 0;
    for (uint32 carriedId : CarriedBy(*session, placementId, withInside))
    {
        changes.push_back(Change{ carriedId, session->placements[carriedId], std::nullopt });
        ++insideCount;
    }

    _report = {};
    std::string failure;
    ApplyChanges(player, *session, changes, true, failure);

    std::string pieces = Acore::StringFormat("{} {}", insideCount, insideCount == 1 ? "piece" : "pieces");
    char const* where = piece && piece->IsBuilding() ? "inside" : "on it";
    std::string label = insideCount ? Acore::StringFormat("picked up {} and {} {}", name, pieces, where) : "picked up " + name;
    Record(player, label, std::move(changes));

    if (insideCount)
        reason = Acore::StringFormat("Picked up {} and the {} {}.{} ({})", name, pieces, piece && piece->IsBuilding() ? "inside it" : "on it",
            DescribeReturns(), CountsText(session->ownerGuid));
    else
        reason = Acore::StringFormat("Picked up {}.{} ({})", name, DescribeReturns(), CountsText(session->ownerGuid));
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::Transform(Player* player, uint32 placementId, std::string const& label, float dx, float dy, float dz, float dO, bool absoluteO, float o, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    if (itr == session->placements.end())
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    Placement before = itr->second;
    float turn = absoluteO ? o - before.o : dO;
    Placement after = before;
    after.x += dx;
    after.y += dy;
    after.z += dz;
    after.o = NormalizeAngle(before.o + turn);

    // What stands on it (and, for a building, what's inside) moves and turns with it, as if
    // it were one piece.
    std::vector<uint32> carriedIds = CarriedBy(*session, placementId, true);
    PieceDefinition const* movedPiece = GetPiece(before.itemEntry);
    if ((dx != 0.0f || dy != 0.0f || dz != 0.0f) && movedPiece && !movedPiece->IsBuilding())
    {
        // Moved onto a table, or off one.
        std::set<uint32> exclude(carriedIds.begin(), carriedIds.end());
        exclude.insert(placementId);
        after.parent = FindSurfaceUnder(*session, after.x, after.y, after.z, exclude);
    }

    std::vector<Change> changes{ Change{ placementId, before, after } };
    float cosTurn = std::cos(turn);
    float sinTurn = std::sin(turn);
    for (uint32 carriedId : carriedIds)
    {
        Placement const& carried = session->placements[carriedId];
        Placement moved = carried;
        float relX = carried.x - before.x;
        float relY = carried.y - before.y;
        moved.x = after.x + relX * cosTurn - relY * sinTurn;
        moved.y = after.y + relX * sinTurn + relY * cosTurn;
        moved.z = carried.z + dz;
        moved.o = NormalizeAngle(carried.o + turn);
        changes.push_back(Change{ carriedId, carried, moved });
    }

    return Commit(player, *session, label, std::move(changes), reason);
}

bool PlayerHousingMgr::Commit(Player* player, Session& session, std::string const& label, std::vector<Change> changes, std::string& reason)
{
    for (Change const& change : changes)
    {
        if (change.after && !IsSpotOnIsland(change.after->x, change.after->y, change.after->z))
        {
            reason = "That would take it off your island.";
            return false;
        }
    }

    _report = {};
    std::string failure;
    if (!ApplyChanges(player, session, changes, true, failure))
    {
        reason = failure;
        return false;
    }

    // "turned 45° left" becomes "turned Westfall Chair 45° left".
    Placement const& before = *changes.front().before;
    std::string name = PieceName(before.itemEntry);
    size_t space = label.find(' ');
    std::string verb = label.substr(0, space);
    std::string rest = space == std::string::npos ? "" : label.substr(space);
    std::string entryLabel = verb + " " + name + rest;
    if (changes.size() == 2)
        entryLabel += Acore::StringFormat(" with the {}", PieceName(changes[1].before->itemEntry));
    else if (changes.size() > 2)
        entryLabel += Acore::StringFormat(" with {} pieces", changes.size() - 1);
    Record(player, entryLabel, std::move(changes));

    reason = entryLabel + ".";
    reason[0] = char(std::toupper(static_cast<unsigned char>(reason[0])));
    SendAddonState(player);
    return true;
}

std::vector<uint32> PlayerHousingMgr::CarriedBy(Session const& session, uint32 placementId, bool includeInside) const
{
    std::vector<uint32> carried;
    std::set<uint32> seen{ placementId };
    auto add = [&](uint32 id)
    {
        if (seen.insert(id).second)
            carried.push_back(id);
    };

    auto root = session.placements.find(placementId);
    auto rootPiece = root != session.placements.end() ? _pieces.find(root->second.itemEntry) : _pieces.end();
    if (includeInside && rootPiece != _pieces.end() && rootPiece->second.IsBuilding())
        for (Placement const& inside : GetPiecesInside(session.ownerGuid, placementId))
            add(inside.id);

    // Then whatever stands on anything carried so far, level by level.
    for (size_t index = 0; index <= carried.size(); ++index)
    {
        uint32 holder = index == 0 ? placementId : carried[index - 1];
        for (auto const& [id, placement] : session.placements)
            if (placement.parent == holder)
                add(id);
    }
    return carried;
}

uint32 PlayerHousingMgr::FindSurfaceUnder(Session const& session, float x, float y, float z, std::set<uint32> const& exclude) const
{
    for (auto const& [id, surface] : session.placements)
    {
        if (exclude.count(id))
            continue;

        auto pieceItr = _pieces.find(surface.itemEntry);
        if (pieceItr == _pieces.end() || !pieceItr->second.HasFlag(PIECE_FLAG_SURFACE))
            continue;

        PieceDefinition const& piece = pieceItr->second;
        float scale = piece.scale > 0.0f ? surface.scale / piece.scale : 1.0f;
        float top = surface.z + piece.height * scale;
        if (std::fabs(z - top) > 0.35f)
            continue;

        float dx = x - surface.x;
        float dy = y - surface.y;
        bool within;
        if (piece.HasOutline())
        {
            float localX = (std::cos(surface.o) * dx + std::sin(surface.o) * dy) / scale;
            float localY = (-std::sin(surface.o) * dx + std::cos(surface.o) * dy) / scale;
            within = localX >= piece.outlineMinX && localX <= piece.outlineMaxX && localY >= piece.outlineMinY && localY <= piece.outlineMaxY;
        }
        else
            within = dx * dx + dy * dy <= piece.footprint * piece.footprint;
        if (within)
            return id;
    }
    return 0;
}

bool PlayerHousingMgr::StartMove(Player* player, uint32 placementId, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    PieceDefinition const* piece = itr != session->placements.end() ? GetPiece(itr->second.itemEntry) : nullptr;
    if (!piece)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    // The mover with the same circle as the piece, so the circle shows its size again.
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(piece->itemEntry);
    auto moverItr = proto ? _moverBySpell.find(proto->Spells[0].SpellId) : _moverBySpell.end();
    if (moverItr == _moverBySpell.end())
    {
        reason = "That can't be moved with the circle. Nudge it, or move it to where you're standing.";
        return false;
    }

    CancelMove(player);
    if (!player->AddItem(moverItr->second, 1))
    {
        reason = "Your bags are full: make room for a Move a Piece item first.";
        return false;
    }

    _pendingMoves[player->GetGUID()] = PendingMove{ placementId, moverItr->second };
    session->selected = placementId;
    reason = Acore::StringFormat("Right-click Move a Piece in your bags, then click where the {} should go.", piece->name);
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::HandleMoveCast(Player* player, Item* castItem, Position const& target, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto pending = _pendingMoves.find(player->GetGUID());
    if (pending == _pendingMoves.end())
    {
        reason = "Nothing to move: click a piece while decorating and choose Move.";
        return false;
    }

    uint32 placementId = pending->second.placementId;
    std::optional<Placement> placement = GetPlacement(player, placementId);
    if (!placement || !IsOnOwnIsland(player))
    {
        CancelMove(player);
        reason = "That piece isn't there any more.";
        return false;
    }

    if (!IsSpotOnIsland(target.GetPositionX(), target.GetPositionY(), target.GetPositionZ()))
    {
        reason = "That spot is off your island.";
        return false;
    }

    float tx = target.GetPositionX();
    float ty = target.GetPositionY();
    if (Session const* session = FindSessionOf(player))
    {
        // On the grid, unless it's going onto a table top (the grid could put it off the edge).
        std::vector<uint32> carried = CarriedBy(*session, placementId, true);
        std::set<uint32> exclude(carried.begin(), carried.end());
        exclude.insert(placementId);
        if (!FindSurfaceUnder(*session, tx, ty, target.GetPositionZ(), exclude))
            SnapToGrid(player->GetGUID().GetCounter(), tx, ty);
    }

    if (!Transform(player, placementId, "moved", tx - placement->x, ty - placement->y,
            target.GetPositionZ() - placement->z, 0.0f, false, 0.0f, reason))
        return false;

    // The item is still in use by the cast; it goes on the player's next update.
    _pendingMoves.erase(player->GetGUID());
    ++_pendingConsumes[player->GetGUID()][castItem->GetEntry()];
    SendAddonState(player);
    return true;
}

void PlayerHousingMgr::CancelMove(Player* player)
{
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        _pendingMoves.erase(player->GetGUID());
        _pendingCopies.erase(player->GetGUID());
    }
    for (uint32 entry = MOVER_ITEM_FIRST; entry <= MOVER_ITEM_LAST; ++entry)
        if (uint32 count = player->GetItemCount(entry))
            player->DestroyItemCount(entry, count, true);
}

uint32 PlayerHousingMgr::GetPendingMover(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _pendingMoves.find(player->GetGUID());
    return itr != _pendingMoves.end() ? itr->second.moverItem : 0;
}

void PlayerHousingMgr::SelectPlacement(Player const* player, uint32 placementId)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _sessionsByOwner.find(player->GetGUID().GetCounter());
    if (itr != _sessionsByOwner.end() && itr->second.placements.count(placementId))
        itr->second.selected = placementId;
}

ObjectGuid PlayerHousingMgr::GetObjectForPlacement(Player const* player, uint32 placementId) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session)
        return ObjectGuid::Empty;

    auto itr = session->spawned.find(placementId);
    return itr != session->spawned.end() ? itr->second.guid : ObjectGuid::Empty;
}

bool PlayerHousingMgr::Rotate(Player* player, uint32 placementId, float degrees, std::string& reason)
{
    std::string label = Acore::StringFormat("turned {:.0f}° {}", std::fabs(degrees), degrees >= 0.0f ? "left" : "right");
    return Transform(player, placementId, label, 0.0f, 0.0f, 0.0f, degrees * PI_F / 180.0f, false, 0.0f, reason);
}

bool PlayerHousingMgr::Nudge(Player* player, uint32 placementId, float forward, float left, float up, std::string& reason)
{
    // Directions are the player's: forward is where the player is facing.
    float po = player->GetOrientation();
    float dx = std::cos(po) * forward - std::sin(po) * left;
    float dy = std::sin(po) * forward + std::cos(po) * left;

    // On the grid: one square along the grid line closest to that direction, onto the grid.
    float grid = GetGridSize(player->GetGUID().GetCounter());
    std::optional<Placement> placement = GetPlacement(player, ResolvePlacementArgument(player, placementId));
    if (grid > 0.0f && placement && (dx != 0.0f || dy != 0.0f))
    {
        float x = placement->x;
        float y = placement->y;
        if (std::fabs(dx) >= std::fabs(dy))
            x += dx > 0.0f ? grid : -grid;
        else
            y += dy > 0.0f ? grid : -grid;
        SnapToGrid(player->GetGUID().GetCounter(), x, y);
        dx = x - placement->x;
        dy = y - placement->y;
    }

    std::string label = "nudged";
    if (up > 0.0f)
        label = "raised";
    else if (up < 0.0f)
        label = "lowered";
    return Transform(player, placementId, label, dx, dy, up, 0.0f, false, 0.0f, reason);
}

bool PlayerHousingMgr::FaceMe(Player* player, uint32 placementId, std::string& reason)
{
    std::optional<Placement> placement = GetPlacement(player, ResolvePlacementArgument(player, placementId));
    if (!placement)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    float o = std::atan2(player->GetPositionY() - placement->y, player->GetPositionX() - placement->x);
    return Transform(player, placement->id, "turned toward you", 0.0f, 0.0f, 0.0f, 0.0f, true, o, reason);
}

bool PlayerHousingMgr::MoveHere(Player* player, uint32 placementId, std::string& reason)
{
    std::optional<Placement> placement = GetPlacement(player, ResolvePlacementArgument(player, placementId));
    if (!placement)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    float x = player->GetPositionX();
    float y = player->GetPositionY();
    SnapToGrid(player->GetGUID().GetCounter(), x, y);
    return Transform(player, placement->id, "moved", x - placement->x, y - placement->y, player->GetPositionZ() - placement->z,
        0.0f, false, 0.0f, reason);
}

bool PlayerHousingMgr::Resize(Player* player, uint32 placementId, float percent, bool relative, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    PieceDefinition const* piece = itr != session->placements.end() ? GetPiece(itr->second.itemEntry) : nullptr;
    if (!piece)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    int32 lowest = int32(std::lround(_sizeMin * 100.0f));
    int32 highest = int32(std::lround(_sizeMax * 100.0f));
    if (lowest >= 100 && highest <= 100)
    {
        reason = "Pieces keep their size on this server.";
        return false;
    }

    // Sizes are whole percents of the piece's normal size, so steps land on round numbers.
    Placement const before = itr->second;
    float normal = piece->scale > 0.0f ? piece->scale : 1.0f;
    int32 current = int32(std::lround(before.scale / normal * 100.0f));
    int32 wanted = std::clamp(relative ? current + int32(std::lround(percent)) : int32(std::lround(percent)), lowest, highest);
    if (wanted == current)
    {
        if (!relative && wanted == 100)
            reason = "It's already its normal size.";
        else if (wanted == highest && (relative ? percent > 0.0f : percent >= highest))
            reason = Acore::StringFormat("That's as big as it gets ({}%).", highest);
        else if (wanted == lowest)
            reason = Acore::StringFormat("That's as small as it gets ({}%).", lowest);
        else
            reason = Acore::StringFormat("It's already {}%.", wanted);
        return false;
    }

    Placement after = before;
    after.scale = normal * float(wanted) / 100.0f;
    float k = after.scale / before.scale;

    // What stands on it keeps its place on the bigger or smaller top; what's inside a
    // building keeps its place in the rooms. Pieces on those move with what they stand on.
    std::vector<uint32> carriedIds = CarriedBy(*session, placementId, true);
    std::set<uint32> carriedSet(carriedIds.begin(), carriedIds.end());
    std::map<uint32, std::array<float, 3>> shifts;
    std::function<std::array<float, 3>(uint32, uint32)> shiftOf = [&](uint32 id, uint32 depth) -> std::array<float, 3>
    {
        auto known = shifts.find(id);
        if (known != shifts.end())
            return known->second;
        Placement const& carried = session->placements[id];
        std::array<float, 3> shift;
        if (depth < 16 && carried.parent != placementId && carriedSet.count(carried.parent))
            shift = shiftOf(carried.parent, depth + 1);
        else
            shift = { (carried.x - before.x) * (k - 1.0f), (carried.y - before.y) * (k - 1.0f), (carried.z - before.z) * (k - 1.0f) };
        shifts[id] = shift;
        return shift;
    };

    std::vector<Change> changes{ Change{ placementId, before, after } };
    for (uint32 carriedId : carriedIds)
    {
        Placement const& carried = session->placements[carriedId];
        std::array<float, 3> shift = shiftOf(carriedId, 0);
        Placement moved = carried;
        moved.x += shift[0];
        moved.y += shift[1];
        moved.z += shift[2];
        changes.push_back(Change{ carriedId, carried, moved });
    }

    std::string label;
    if (wanted == 100)
        label = "brought back to normal size";
    else
        label = Acore::StringFormat("made {} ({}%)", wanted > current ? "bigger" : "smaller", wanted);
    return Commit(player, *session, label, std::move(changes), reason);
}

bool PlayerHousingMgr::Tilt(Player* player, uint32 placementId, float forwardDegrees, float rightDegrees, bool straighten, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    PieceDefinition const* piece = itr != session->placements.end() ? GetPiece(itr->second.itemEntry) : nullptr;
    if (!piece)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    if (piece->HasFlag(PIECE_FLAG_STAND))
    {
        reason = "Mannequins always stand upright.";
        return false;
    }

    if (_tiltMax <= 0.0f && !straighten)
    {
        reason = "Pieces stand upright on this server.";
        return false;
    }

    // Tenths of a degree, so steps back and forth land back on level.
    auto toTenths = [](float radians) { return int32(std::lround(radians * 1800.0f / PI_F)); };
    auto toRadians = [](int32 tenths) { return float(tenths) * PI_F / 1800.0f; };
    int32 limit = int32(std::lround(_tiltMax * 10.0f));

    Placement const before = itr->second;
    int32 pitch = straighten ? 0 : std::clamp(toTenths(before.pitch) + int32(std::lround(forwardDegrees * 10.0f)), -limit, limit);
    int32 roll = straighten ? 0 : std::clamp(toTenths(before.roll) + int32(std::lround(rightDegrees * 10.0f)), -limit, limit);
    if (pitch == toTenths(before.pitch) && roll == toTenths(before.roll))
    {
        reason = straighten ? "It's already standing straight." : Acore::StringFormat("That's as far as it tilts ({:.0f}°).", _tiltMax);
        return false;
    }

    Placement after = before;
    after.pitch = toRadians(pitch);
    after.roll = toRadians(roll);

    // Only the piece tilts: what stands on it stays where it is.
    std::string label;
    if (straighten)
        label = "stood straight";
    else if (forwardDegrees != 0.0f)
        label = Acore::StringFormat("tilted {:.0f}° {}", std::fabs(forwardDegrees), forwardDegrees > 0.0f ? "forward" : "back");
    else
        label = Acore::StringFormat("tilted {:.0f}° to its {}", std::fabs(rightDegrees), rightDegrees > 0.0f ? "right" : "left");
    return Commit(player, *session, label, { Change{ placementId, before, after } }, reason);
}

bool PlayerHousingMgr::PlaceAnother(Player* player, uint32 placementId, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    placementId = ResolvePlacementArgument(player, placementId);
    auto itr = session->placements.find(placementId);
    PieceDefinition const* piece = itr != session->placements.end() ? GetPiece(itr->second.itemEntry) : nullptr;
    if (!piece)
    {
        reason = "Choose a piece first: click it while decorating, or stand next to it.";
        return false;
    }

    if (!CheckLimit(*session, *piece, reason))
        return false;

    // One from the bags, else from House Storage, else a new copy from the Collection.
    std::string got;
    uint32 reserved = 0;
    auto consumes = _pendingConsumes.find(player->GetGUID());
    if (consumes != _pendingConsumes.end())
    {
        auto entry = consumes->second.find(piece->itemEntry);
        if (entry != consumes->second.end())
            reserved = entry->second;
    }
    if (player->GetItemCount(piece->itemEntry) <= reserved)
    {
        std::map<uint32, uint32> storage = GetStorage(session->ownerGuid);
        auto stored = storage.find(piece->itemEntry);
        if (stored != storage.end() && stored->second > 0)
        {
            if (!player->AddItem(piece->itemEntry, 1))
            {
                reason = "Your bags are full.";
                return false;
            }
            AddToStorage(session->ownerGuid, piece->itemEntry, -1);
            got = "Took one out of House Storage. ";
        }
        else
        {
            uint32 cost = _freeMode ? 0 : piece->copyCost;
            if (!GetCopy(player, piece->itemEntry, reason))
                return false;
            got = cost ? Acore::StringFormat("Here's a new one, for {}. ", FormatMoney(cost)) : "Here's a new one from your Collection. ";
        }
    }

    CancelMove(player);
    Placement const& source = itr->second;
    _pendingCopies[player->GetGUID()] = PendingCopy{ piece->itemEntry, source.o, source.scale, source.pitch, source.roll };
    reason = Acore::StringFormat("{}Right-click the {} in your bags, then click where it goes: it gets this one's turn, size and tilt.",
        got, piece->name);
    SendAddonState(player);
    return true;
}

uint32 PlayerHousingMgr::GetPendingCopy(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _pendingCopies.find(player->GetGUID());
    return itr != _pendingCopies.end() ? itr->second.itemEntry : 0;
}

void PlayerHousingMgr::SnapToGrid(ObjectGuid::LowType guid, float& x, float& y) const
{
    float grid = GetGridSize(guid);
    if (grid <= 0.0f)
        return;
    x = std::round(x / grid) * grid;
    y = std::round(y / grid) * grid;
}

bool PlayerHousingMgr::PlaceOnHook(Player* player, uint32 surfacePlacementId, uint32 itemEntry, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    auto surfaceItr = session->placements.find(surfacePlacementId);
    PieceDefinition const* surfacePiece = surfaceItr != session->placements.end() ? GetPiece(surfaceItr->second.itemEntry) : nullptr;
    if (!surfacePiece || !surfacePiece->HasFlag(PIECE_FLAG_SURFACE))
    {
        reason = "Nothing can go on top of that.";
        return false;
    }

    PieceDefinition const* piece = GetPiece(itemEntry);
    if (!piece || piece->IsBuilding())
    {
        reason = "That doesn't fit there.";
        return false;
    }

    Placement const& surface = surfaceItr->second;
    Placement placement;
    placement.id = session->nextPlacementId++;
    placement.itemEntry = itemEntry;
    placement.x = surface.x;
    placement.y = surface.y;
    placement.z = surface.z + surfacePiece->height * (surface.scale / surfacePiece->scale);
    placement.o = surface.o;
    placement.scale = piece->scale;
    placement.parent = surfacePlacementId;

    _report = {};
    std::string failure;
    if (!ApplyState(player, *session, player->GetMap(), placement.id, placement, failure))
    {
        reason = failure;
        return false;
    }

    if (session->decorating)
        SpawnMarkers(*session, player->GetMap());

    Record(player, Acore::StringFormat("put {} on {}", piece->name, surfacePiece->name), { Change{ placement.id, std::nullopt, placement } });
    reason = Acore::StringFormat("Put {} on the {} ({}).", piece->name, surfacePiece->name, CountsText(session->ownerGuid));
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::PackUpEverything(Player* player, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    if (session->placements.empty())
    {
        reason = "There's nothing to pack up.";
        return false;
    }

    std::vector<Change> changes;
    for (auto const& [id, placement] : session->placements)
        changes.push_back(Change{ id, placement, std::nullopt });

    _report = {};
    std::string failure;
    ApplyChanges(player, *session, changes, true, failure);
    uint32 count = uint32(changes.size());
    Record(player, Acore::StringFormat("packed up {} pieces", count), std::move(changes));

    reason = Acore::StringFormat("Packed up {} pieces.{} Changed your mind? House Key, Undo.", count, DescribeReturns());
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::SetDecorating(Player* player, bool on, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    if (session->decorating == on)
    {
        reason = on ? "You're already decorating." : "You're not decorating.";
        return false;
    }

    Map* map = player->GetMap();
    session->decorating = on;
    for (auto const& [id, placement] : session->placements)
    {
        auto pieceItr = _pieces.find(placement.itemEntry);
        if (pieceItr != _pieces.end() && pieceItr->second.editGoEntry)
            RespawnPlacement(*session, map, id);
    }

    if (on)
    {
        SpawnMarkers(*session, map);
        reason = "Decorating. Click any piece to turn, move or pick it up; blue runes on tables take small pieces. House Key, Done decorating, when you're finished.";
    }
    else
    {
        DespawnMarkers(*session, map);
        reason = "Done decorating. Chairs, mailboxes and crafting stations work normally again.";
    }

    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::TakeFromStorage(Player* player, uint32 itemEntry, bool all, std::string& reason)
{
    std::map<uint32, uint32> storage = GetStorage(player->GetGUID().GetCounter());
    if (storage.empty())
    {
        reason = "Your House Storage is empty.";
        return false;
    }

    uint32 taken = 0;
    bool bagsFull = false;
    for (auto const& [entry, count] : storage)
    {
        if (!all && entry != itemEntry)
            continue;

        for (uint32 i = 0; i < count; ++i)
        {
            if (!player->AddItem(entry, 1))
            {
                bagsFull = true;
                break;
            }
            AddToStorage(player->GetGUID().GetCounter(), entry, -1);
            ++taken;
        }

        if (bagsFull)
            break;
    }

    if (!taken)
    {
        reason = "Your bags are full.";
        return false;
    }

    reason = Acore::StringFormat("Took {} {} out of House Storage.{}", taken, taken == 1 ? "piece" : "pieces",
        bagsFull ? " Your bags are full; the rest stays in storage." : "");
    return true;
}
