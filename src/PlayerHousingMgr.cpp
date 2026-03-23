#include "PlayerHousingMgr.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCEnums.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "InstanceSaveMgr.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "StringFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace
{
    constexpr float INVALID_HEIGHT_SENTINEL = -50000.0f;
    constexpr uint32 DEFAULT_HOUSING_MAP_ID = 658;
    constexpr uint32 STEWARD_DISPLAY_ID = 25384; // Wolvar orphan
    constexpr uint32 PLACEMENT_TARGET_SPELL_ID = 1543; // Flare, used as placement ground-target spell
    constexpr std::time_t PLACEMENT_PENDING_TIMEOUT_SECONDS = 300;
    constexpr uint32 LEGACY_HOUSING_MAPS[] = { 309, 531, 534, 568 };
    constexpr uint8 FURNITURE_SPAWN_TYPE_GAMEOBJECT = 0;
    constexpr uint8 FURNITURE_SPAWN_TYPE_CREATURE = 1;
    constexpr float PI_F = 3.14159265358979323846f;
    constexpr float TWO_PI_F = 6.28318530717958647692f;

    float NormalizeAngle(float angle)
    {
        while (angle < 0.0f)
            angle += TWO_PI_F;
        while (angle >= TWO_PI_F)
            angle -= TWO_PI_F;
        return angle;
    }
}

PlayerHousingMgr* PlayerHousingMgr::instance()
{
    static PlayerHousingMgr instance;
    return &instance;
}

std::string PlayerHousingMgr::ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

uint32 PlayerHousingMgr::GetStyleMaskFor(uint8 styleId)
{
    if (styleId == 0 || styleId > 31)
        return 0;

    return 1u << (styleId - 1);
}

std::string PlayerHousingMgr::FormatMoney(uint64 copper)
{
    uint64 gold = copper / 10000;
    uint64 silver = (copper % 10000) / 100;
    uint64 copperOnly = copper % 100;

    std::ostringstream out;
    out << gold << "g " << silver << "s " << copperOnly << "c";
    return out.str();
}

void PlayerHousingMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("PlayerHousing.Enable", true);
    _autoProvisionOnLogin = sConfigMgr->GetOption<bool>("PlayerHousing.AutoProvisionOnLogin", true);
    _gmVisitBypass = sConfigMgr->GetOption<bool>("PlayerHousing.GmBypassPrivate", true);
    _defaultPrivate = sConfigMgr->GetOption<bool>("PlayerHousing.DefaultPrivate", true);
    _defaultHousingMapId = sConfigMgr->GetOption<uint32>("PlayerHousing.MapId", DEFAULT_HOUSING_MAP_ID);
    _stewardEntry = sConfigMgr->GetOption<uint32>("PlayerHousing.StewardEntry", 900200);
    _stewardDisplayId = sConfigMgr->GetOption<uint32>("PlayerHousing.StewardDisplayId", STEWARD_DISPLAY_ID);
    if (!sCreatureDisplayInfoStore.LookupEntry(_stewardDisplayId))
    {
        LOG_WARN("module", "mod-playerhousing: Steward display id {} is invalid. Falling back to {}.", _stewardDisplayId, STEWARD_DISPLAY_ID);
        _stewardDisplayId = STEWARD_DISPLAY_ID;
    }
    _placementMaxSlopeDegrees = std::clamp(sConfigMgr->GetOption<float>("PlayerHousing.Placement.MaxSlopeDegrees", 35.0f), 5.0f, 89.0f);
    _placementSlopeSampleDistance = std::clamp(sConfigMgr->GetOption<float>("PlayerHousing.Placement.SlopeSampleDistance", 0.75f), 0.25f, 4.0f);
    _placementDefaultMinDistance = std::clamp(sConfigMgr->GetOption<float>("PlayerHousing.Placement.DefaultMinDistance", 1.5f), 0.25f, 10.0f);
    _placementDefaultCollisionRadius = std::clamp(sConfigMgr->GetOption<float>("PlayerHousing.Placement.DefaultCollisionRadius", 1.0f), 0.1f, 10.0f);
    _defaultStyleCode = ToLower(sConfigMgr->GetOption<std::string>("PlayerHousing.DefaultStyle", "human"));
}

bool PlayerHousingMgr::IsSupportedHousingMap(uint32 mapId) const
{
    MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
    if (!mapEntry)
        return false;

    if (!mapEntry->IsDungeon() || mapEntry->IsBattlegroundOrArena())
        return false;

    return sObjectMgr->GetInstanceTemplate(mapId) != nullptr;
}

bool PlayerHousingMgr::ResolveHousingMaps(std::string& reason)
{
    if (!IsSupportedHousingMap(_defaultHousingMapId))
    {
        reason = Acore::StringFormat("Default housing map {} is invalid or not instanceable.", _defaultHousingMapId);
        return false;
    }

    _housingMapIds.clear();
    for (auto& [styleId, style] : _stylesById)
    {
        uint32 mapId = style.mapId != 0 ? style.mapId : _defaultHousingMapId;
        if (!IsSupportedHousingMap(mapId))
        {
            LOG_WARN("module",
                "mod-playerhousing: Style {} map {} is invalid. Falling back to default map {}.",
                style.styleCode, mapId, _defaultHousingMapId);
            mapId = _defaultHousingMapId;
        }

        style.mapId = mapId;
        _housingMapIds.insert(mapId);
    }

    return true;
}

bool PlayerHousingMgr::LoadDefinitions()
{
    std::lock_guard<std::mutex> guard(_lock);

    _definitionsLoaded = false;
    _stylesById.clear();
    _styleIdByCode.clear();
    _stagesById.clear();
    _catalogById.clear();
    _furnitureItemsByEntry.clear();
    _styleStarterUnlocks.clear();
    _styleObjects.clear();

    QueryResult styleResult = WorldDatabase.Query(
        "SELECT style_id, style_code, display_name, map_id, spawn_x, spawn_y, spawn_z, spawn_o "
        "FROM mod_playerhousing_style ORDER BY style_id");
    if (!styleResult)
    {
        LOG_ERROR("module", "mod-playerhousing: Unable to load styles. Did you apply db_world SQL?");
        return false;
    }

    do
    {
        Field* fields = styleResult->Fetch();
        if (!fields)
            continue;

        StyleDefinition style;
        style.styleId = fields[0].Get<uint8>();
        style.styleCode = ToLower(fields[1].Get<std::string>());
        style.displayName = fields[2].Get<std::string>();
        style.mapId = fields[3].Get<uint32>();
        style.spawnX = fields[4].Get<float>();
        style.spawnY = fields[5].Get<float>();
        style.spawnZ = fields[6].Get<float>();
        style.spawnO = fields[7].Get<float>();

        _styleIdByCode[style.styleCode] = style.styleId;
        _stylesById[style.styleId] = style;
    } while (styleResult->NextRow());

    QueryResult stageResult = WorldDatabase.Query(
        "SELECT stage, upgrade_cost_copper, max_items, place_radius "
        "FROM mod_playerhousing_stage ORDER BY stage");
    if (!stageResult)
    {
        LOG_ERROR("module", "mod-playerhousing: Unable to load stages. Did you apply db_world SQL?");
        return false;
    }

    do
    {
        Field* fields = stageResult->Fetch();
        if (!fields)
            continue;

        StageDefinition stage;
        stage.stage = fields[0].Get<uint8>();
        stage.costCopper = fields[1].Get<uint32>();
        stage.maxItems = fields[2].Get<uint32>();
        stage.placeRadius = fields[3].Get<float>();
        _stagesById[stage.stage] = stage;
    } while (stageResult->NextRow());

    QueryResult catalogResult = WorldDatabase.Query(
        "SELECT catalog_id, display_name, gameobject_entry, unlock_cost_copper, min_stage, style_mask, is_default, active, sort_order "
        "FROM mod_playerhousing_catalog ORDER BY sort_order, catalog_id");
    if (!catalogResult)
    {
        LOG_ERROR("module", "mod-playerhousing: Unable to load furniture catalog. Did you apply db_world SQL?");
        return false;
    }

    do
    {
        Field* fields = catalogResult->Fetch();
        if (!fields)
            continue;

        CatalogDefinition catalog;
        catalog.catalogId = fields[0].Get<uint32>();
        catalog.displayName = fields[1].Get<std::string>();
        catalog.gameobjectEntry = fields[2].Get<uint32>();
        catalog.unlockCostCopper = fields[3].Get<uint32>();
        catalog.minStage = fields[4].Get<uint8>();
        catalog.styleMask = fields[5].Get<uint32>();
        catalog.isDefault = fields[6].Get<uint8>() != 0;
        catalog.active = fields[7].Get<uint8>() != 0;
        catalog.sortOrder = fields[8].Get<uint32>();
        _catalogById[catalog.catalogId] = catalog;
    } while (catalogResult->NextRow());

    QueryResult furnitureItemResult = WorldDatabase.Query(
        "SELECT item_entry, catalog_id, display_name, spawn_type, spawn_entry, display_id, scale, collision_radius, min_distance, orientation_offset, "
        "required_stage, style_mask, consume_on_place, active, sort_order "
        "FROM mod_playerhousing_furniture_item ORDER BY sort_order, item_entry");
    if (furnitureItemResult)
    {
        do
        {
            Field* fields = furnitureItemResult->Fetch();
            if (!fields)
                continue;

            FurnitureItemDefinition itemDef;
            itemDef.itemEntry = fields[0].Get<uint32>();
            itemDef.catalogId = fields[1].Get<uint32>();
            itemDef.displayName = fields[2].Get<std::string>();
            itemDef.spawnType = fields[3].Get<uint8>();
            itemDef.spawnEntry = fields[4].Get<uint32>();
            itemDef.displayId = fields[5].Get<uint32>();
            itemDef.scale = std::max(0.1f, fields[6].Get<float>());
            itemDef.collisionRadius = std::max(0.1f, fields[7].Get<float>());
            itemDef.minDistance = std::max(0.1f, fields[8].Get<float>());
            itemDef.orientationOffset = fields[9].Get<float>();
            itemDef.requiredStage = fields[10].Get<uint8>();
            itemDef.styleMask = fields[11].Get<uint32>();
            itemDef.consumeOnPlace = fields[12].Get<uint8>() != 0;
            itemDef.active = fields[13].Get<uint8>() != 0;
            itemDef.sortOrder = fields[14].Get<uint32>();

            if (itemDef.spawnType > FURNITURE_SPAWN_TYPE_CREATURE || itemDef.spawnEntry == 0)
            {
                LOG_WARN("module", "mod-playerhousing: Ignoring invalid furniture item mapping for item {}.", itemDef.itemEntry);
                continue;
            }

            if (itemDef.spawnType == FURNITURE_SPAWN_TYPE_GAMEOBJECT)
            {
                if (!sObjectMgr->GetGameObjectTemplate(itemDef.spawnEntry))
                {
                    LOG_WARN("module", "mod-playerhousing: Furniture item {} has invalid gameobject entry {}.", itemDef.itemEntry, itemDef.spawnEntry);
                    continue;
                }
            }
            else if (!sObjectMgr->GetCreatureTemplate(itemDef.spawnEntry))
            {
                LOG_WARN("module", "mod-playerhousing: Furniture item {} has invalid creature entry {}.", itemDef.itemEntry, itemDef.spawnEntry);
                continue;
            }

            _furnitureItemsByEntry[itemDef.itemEntry] = itemDef;
        } while (furnitureItemResult->NextRow());
    }
    else
    {
        LOG_WARN("module", "mod-playerhousing: Furniture item mapping table not found or empty. Vendor item placement flow will be unavailable.");
    }

    QueryResult starterResult = WorldDatabase.Query(
        "SELECT style_id, catalog_id FROM mod_playerhousing_style_default_unlock ORDER BY style_id, catalog_id");
    if (starterResult)
    {
        do
        {
            Field* fields = starterResult->Fetch();
            if (!fields)
                continue;

            _styleStarterUnlocks[fields[0].Get<uint8>()].push_back(fields[1].Get<uint32>());
        } while (starterResult->NextRow());
    }

    QueryResult styleObjectResult = WorldDatabase.Query(
        "SELECT style_id, min_stage, object_index, gameobject_entry, offset_x, offset_y, offset_z, orientation_offset "
        "FROM mod_playerhousing_style_object ORDER BY style_id, min_stage, object_index");
    if (styleObjectResult)
    {
        do
        {
            Field* fields = styleObjectResult->Fetch();
            if (!fields)
                continue;

            StyleObjectDefinition objectDef;
            objectDef.styleId = fields[0].Get<uint8>();
            objectDef.minStage = fields[1].Get<uint8>();
            objectDef.objectIndex = fields[2].Get<uint8>();
            objectDef.gameobjectEntry = fields[3].Get<uint32>();
            objectDef.offsetX = fields[4].Get<float>();
            objectDef.offsetY = fields[5].Get<float>();
            objectDef.offsetZ = fields[6].Get<float>();
            objectDef.orientationOffset = fields[7].Get<float>();
            _styleObjects[objectDef.styleId].push_back(objectDef);
        } while (styleObjectResult->NextRow());
    }

    if (_stylesById.empty() || _stagesById.empty() || _catalogById.empty())
    {
        LOG_ERROR("module", "mod-playerhousing: Missing required definitions (style/stage/catalog).");
        return false;
    }

    if (_styleIdByCode.find(_defaultStyleCode) == _styleIdByCode.end())
    {
        _defaultStyleCode = _stylesById.begin()->second.styleCode;
        LOG_WARN("module", "mod-playerhousing: Default style not found, falling back to '{}'.", _defaultStyleCode);
    }

    _definitionsLoaded = true;
    LOG_INFO("server.loading", "mod-playerhousing: Loaded {} styles, {} stages, {} catalog entries, {} furniture items.",
        _stylesById.size(), _stagesById.size(), _catalogById.size(), _furnitureItemsByEntry.size());
    return true;
}

void PlayerHousingMgr::OnStartup()
{
    LoadConfig();

    if (!_enabled)
    {
        LOG_INFO("server.loading", "mod-playerhousing: Disabled by config.");
        return;
    }

    if (!LoadDefinitions())
    {
        LOG_ERROR("module", "mod-playerhousing: Definitions failed to load. Module will remain inactive until SQL is applied.");
        _enabled = false;
        return;
    }

    std::string mapReason;
    if (!ResolveHousingMaps(mapReason))
    {
        LOG_ERROR("module", "mod-playerhousing: {} Module disabled.", mapReason);
        _enabled = false;
        return;
    }
}

void PlayerHousingMgr::OnPlayerLogin(Player* player)
{
    if (!_enabled || !player)
        return;

    if (_autoProvisionOnLogin)
    {
        std::string reason;
        EnsureStarterHouse(player, false, reason);
    }

    bool legacyEvac = false;
    for (uint32 legacyMapId : LEGACY_HOUSING_MAPS)
        if (player->GetMapId() == legacyMapId)
            legacyEvac = true;

    if (IsHousingMap(player->GetMapId()) || legacyEvac)
    {
        player->TeleportToEntryPoint();
    }
}

void PlayerHousingMgr::OnPlayerLogout(Player* player)
{
    if (!_enabled || !player)
        return;

    ClearPendingPlacement(player->GetGUID(), player);
    RemovePlayerTracking(player->GetGUID(), player, true, true);
}

void PlayerHousingMgr::OnPlayerUpdate(Player* player, uint32 /*diffMs*/)
{
    if (!_enabled || !player)
        return;

    CleanupExpiredPendingPlacement(player);

    if (!IsHousingMap(player->GetMapId()))
        return;

    uint8 styleId = 0;
    uint8 stageId = 0;
    uint32 sessionMapId = 0;
    uint32 sessionInstanceId = 0;

    {
        std::lock_guard<std::mutex> guard(_lock);
        auto playerItr = _playerOwnerByGuid.find(player->GetGUID());
        if (playerItr == _playerOwnerByGuid.end())
            return;

        auto sessionItr = _sessionsByOwner.find(playerItr->second);
        if (sessionItr == _sessionsByOwner.end())
            return;

        Session const& session = sessionItr->second;
        styleId = session.styleId;
        stageId = session.stage;
        sessionMapId = session.mapId;
        sessionInstanceId = session.instanceId;
    }

    if (player->GetMapId() != sessionMapId || player->GetInstanceId() != sessionInstanceId)
        return;

    Map* map = player->GetMap();
    if (!map)
        return;

    StageDefinition stageDef;
    if (!GetStageDefinition(stageId, stageDef) || stageDef.placeRadius <= 0.0f)
        return;

    Position center;
    if (!GetHouseCenter(map, styleId, center))
        return;

    if (player->GetPositionZ() < center.GetPositionZ() - 80.0f)
    {
        player->NearTeleportTo(center.GetPositionX(), center.GetPositionY(), center.GetPositionZ(), center.GetOrientation());
        return;
    }

    float dx = player->GetPositionX() - center.GetPositionX();
    float dy = player->GetPositionY() - center.GetPositionY();
    float dist2 = dx * dx + dy * dy;
    float bound2 = stageDef.placeRadius * stageDef.placeRadius;
    if (dist2 <= bound2)
        return;

    float dist = std::sqrt(dist2);
    if (dist <= 0.001f)
        return;

    float snapRadius = std::max(2.0f, stageDef.placeRadius - 2.0f);
    float factor = snapRadius / dist;
    float snapX = center.GetPositionX() + dx * factor;
    float snapY = center.GetPositionY() + dy * factor;
    float snapZ = SnapToGround(map, snapX, snapY, center.GetPositionZ());
    player->NearTeleportTo(snapX, snapY, snapZ, player->GetOrientation());
}

void PlayerHousingMgr::OnPlayerMapChanged(Player* player)
{
    if (!_enabled || !player)
        return;

    if (!IsHousingMap(player->GetMapId()))
        ClearPendingPlacement(player->GetGUID(), player);

    ObjectGuid::LowType ownerGuid = 0;
    uint32 instanceId = 0;
    uint32 mapId = 0;

    {
        std::lock_guard<std::mutex> guard(_lock);
        auto playerItr = _playerOwnerByGuid.find(player->GetGUID());
        if (playerItr == _playerOwnerByGuid.end())
            return;

        ownerGuid = playerItr->second;
        auto sessionItr = _sessionsByOwner.find(ownerGuid);
        if (sessionItr != _sessionsByOwner.end())
        {
            instanceId = sessionItr->second.instanceId;
            mapId = sessionItr->second.mapId;
        }
    }

    if (player->GetMapId() == mapId && player->GetInstanceId() == instanceId)
        return;

    bool eraseReturn = (!IsHousingMap(player->GetMapId()));
    RemovePlayerTracking(player->GetGUID(), player, eraseReturn, true);
}

void PlayerHousingMgr::OnPlayerDelete(ObjectGuid guid)
{
    if (!_enabled)
        return;

    ObjectGuid::LowType guidLow = guid.GetCounter();

    ClearPendingPlacement(guid, nullptr);
    RemovePlayerTracking(guid, nullptr, true, true);

    CharacterDatabase.Execute("DELETE FROM mod_playerhousing_acl WHERE owner_guid={} OR guest_guid={}", guidLow, guidLow);
    CharacterDatabase.Execute("DELETE FROM mod_playerhousing_unlock WHERE owner_guid={}", guidLow);
    CharacterDatabase.Execute("DELETE FROM mod_playerhousing_placement WHERE owner_guid={}", guidLow);
    CharacterDatabase.Execute("DELETE FROM mod_playerhousing_house WHERE owner_guid={}", guidLow);

    std::lock_guard<std::mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(guidLow);
    if (sessionItr != _sessionsByOwner.end())
    {
        _ownerByInstance.erase(sessionItr->second.instanceId);
        _sessionsByOwner.erase(sessionItr);
    }
}

void PlayerHousingMgr::OnDestroyMap(Map* map)
{
    if (!_enabled || !map || !IsHousingMap(map->GetId()))
        return;

    std::lock_guard<std::mutex> guard(_lock);
    auto ownerItr = _ownerByInstance.find(map->GetInstanceId());
    if (ownerItr == _ownerByInstance.end())
        return;

    ObjectGuid::LowType ownerGuid = ownerItr->second;
    _ownerByInstance.erase(ownerItr);

    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    if (sessionItr != _sessionsByOwner.end())
    {
        for (ObjectGuid const& playerGuid : sessionItr->second.occupants)
        {
            _playerOwnerByGuid.erase(playerGuid);
            _returnLocations.erase(playerGuid);
        }

        _sessionsByOwner.erase(sessionItr);
    }
}

bool PlayerHousingMgr::ResolvePlayerGuid(std::string const& playerName, ObjectGuid::LowType& guidLow, std::string& normalizedName) const
{
    normalizedName = playerName;
    if (!normalizePlayerName(normalizedName))
        return false;

    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(normalizedName);
    if (!guid)
        return false;

    guidLow = guid.GetCounter();
    return true;
}

bool PlayerHousingMgr::GetHouseRecord(ObjectGuid::LowType ownerGuid, HouseRecord& outRecord) const
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT owner_guid, style_id, stage, is_private FROM mod_playerhousing_house WHERE owner_guid={}",
        ownerGuid);
    if (!result)
        return false;

    Field* fields = result->Fetch();
    if (!fields)
        return false;

    outRecord.ownerGuid = fields[0].Get<uint32>();
    outRecord.styleId = fields[1].Get<uint8>();
    outRecord.stage = fields[2].Get<uint8>();
    outRecord.isPrivate = fields[3].Get<uint8>() != 0;
    return true;
}

bool PlayerHousingMgr::CreateStarterHouse(ObjectGuid::LowType ownerGuid, std::string& reason)
{
    uint8 styleId = 1;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto styleItr = _styleIdByCode.find(_defaultStyleCode);
        if (styleItr != _styleIdByCode.end())
            styleId = styleItr->second;
        else if (!_stylesById.empty())
            styleId = _stylesById.begin()->first;
    }

    CharacterDatabase.Execute(
        "INSERT IGNORE INTO mod_playerhousing_house (owner_guid, style_id, stage, is_private) VALUES ({}, {}, 0, {})",
        ownerGuid, styleId, _defaultPrivate ? 1 : 0);

    HouseRecord created;
    if (!GetHouseRecord(ownerGuid, created))
    {
        reason = "Could not create house record.";
        return false;
    }

    GrantStarterUnlocks(ownerGuid, created.styleId);
    return true;
}

void PlayerHousingMgr::GrantStarterUnlocks(ObjectGuid::LowType ownerGuid, uint8 styleId)
{
    std::set<uint32> unlockSet;
    uint32 styleMask = GetStyleMaskFor(styleId);

    {
        std::lock_guard<std::mutex> guard(_lock);
        for (auto const& [catalogId, catalog] : _catalogById)
        {
            if (!catalog.isDefault)
                continue;

            if (catalog.styleMask != 0 && (catalog.styleMask & styleMask) == 0)
                continue;

            unlockSet.insert(catalogId);
        }

        auto starterItr = _styleStarterUnlocks.find(styleId);
        if (starterItr != _styleStarterUnlocks.end())
            unlockSet.insert(starterItr->second.begin(), starterItr->second.end());
    }

    for (uint32 catalogId : unlockSet)
        CharacterDatabase.Execute("INSERT IGNORE INTO mod_playerhousing_unlock (owner_guid, catalog_id) VALUES ({}, {})", ownerGuid, catalogId);
}

bool PlayerHousingMgr::EnsureStarterHouse(Player* player, bool announce, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord record;
    if (GetHouseRecord(player->GetGUID().GetCounter(), record))
        return true;

    bool created = CreateStarterHouse(player->GetGUID().GetCounter(), reason);
    if (created && announce)
        ChatHandler(player->GetSession()).SendSysMessage("Housing: starter house created.");

    return created;
}

bool PlayerHousingMgr::CanVisitHouse(Player const* visitor, HouseRecord const& house, std::string& reason) const
{
    if (!visitor)
    {
        reason = "Invalid visitor.";
        return false;
    }

    if (visitor->GetGUID().GetCounter() == house.ownerGuid)
        return true;

    if (_gmVisitBypass && visitor->IsGameMaster())
        return true;

    if (!house.isPrivate)
        return true;

    QueryResult aclResult = CharacterDatabase.Query(
        "SELECT 1 FROM mod_playerhousing_acl WHERE owner_guid={} AND guest_guid={} LIMIT 1",
        house.ownerGuid, visitor->GetGUID().GetCounter());
    if (aclResult)
        return true;

    reason = "This house is private and you are not invited.";
    return false;
}

bool PlayerHousingMgr::GetStyleDefinition(uint8 styleId, StyleDefinition& outStyle) const
{
    std::lock_guard<std::mutex> guard(_lock);
    auto itr = _stylesById.find(styleId);
    if (itr == _stylesById.end())
        return false;

    outStyle = itr->second;
    return true;
}

bool PlayerHousingMgr::GetStageDefinition(uint8 stage, StageDefinition& outStage) const
{
    std::lock_guard<std::mutex> guard(_lock);
    auto itr = _stagesById.find(stage);
    if (itr == _stagesById.end())
        return false;

    outStage = itr->second;
    return true;
}

bool PlayerHousingMgr::GetCatalogDefinition(uint32 catalogId, CatalogDefinition& outCatalog) const
{
    std::lock_guard<std::mutex> guard(_lock);
    auto itr = _catalogById.find(catalogId);
    if (itr == _catalogById.end())
        return false;

    outCatalog = itr->second;
    return true;
}

bool PlayerHousingMgr::GetFurnitureItemDefinition(uint32 itemEntry, FurnitureItemDefinition& outDefinition) const
{
    std::lock_guard<std::mutex> guard(_lock);
    auto itr = _furnitureItemsByEntry.find(itemEntry);
    if (itr == _furnitureItemsByEntry.end())
        return false;

    outDefinition = itr->second;
    return true;
}

bool PlayerHousingMgr::IsPlacementPointValid(Player* player, Map* map, Position const& center, StageDefinition const& stage, float minDistance, float collisionRadius, float& x, float& y, float& z, std::string& reason, uint32 ignorePlacementId) const
{
    if (!player || !map)
    {
        reason = "House map context is unavailable.";
        return false;
    }

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        reason = "Placement position is invalid.";
        return false;
    }

    float allowedRadius = std::max(1.0f, stage.placeRadius - std::max(0.1f, collisionRadius));
    float dx = x - center.GetPositionX();
    float dy = y - center.GetPositionY();
    float dist2 = dx * dx + dy * dy;
    if (dist2 > allowedRadius * allowedRadius)
    {
        reason = "That location is outside your current house bounds.";
        return false;
    }

    auto sampleGround = [&](float sampleX, float sampleY, float zHint, float& outZ) -> bool
    {
        float zTop = map->GetHeight(player->GetPhaseMask(), sampleX, sampleY, zHint + 20.0f, true, 200.0f);
        float zMid = map->GetHeight(player->GetPhaseMask(), sampleX, sampleY, zHint, true, 200.0f);
        float zLow = map->GetHeight(player->GetPhaseMask(), sampleX, sampleY, zHint - 20.0f, true, 200.0f);

        float best = INVALID_HEIGHT_SENTINEL;
        float bestDelta = std::numeric_limits<float>::max();
        for (float candidate : { zTop, zMid, zLow })
        {
            if (!std::isfinite(candidate) || candidate <= INVALID_HEIGHT_SENTINEL)
                continue;

            float delta = std::fabs(candidate - zHint);
            if (delta < bestDelta)
            {
                best = candidate;
                bestDelta = delta;
            }
        }

        if (best <= INVALID_HEIGHT_SENTINEL)
            return false;

        outZ = best;
        return true;
    };

    float groundZ = z;
    if (!sampleGround(x, y, z, groundZ))
    {
        reason = "No valid ground at that location.";
        return false;
    }

    z = groundZ;

    float sampleDistance = std::max(0.25f, _placementSlopeSampleDistance);
    float maxSlope = 0.0f;
    for (uint32 i = 0; i < 4; ++i)
    {
        float angle = (PI_F * 0.5f) * float(i);
        float sampleX = x + std::cos(angle) * sampleDistance;
        float sampleY = y + std::sin(angle) * sampleDistance;
        float sampleZ = z;
        if (!sampleGround(sampleX, sampleY, z, sampleZ))
        {
            reason = "That spot is too close to invalid terrain.";
            return false;
        }

        float slope = std::atan(std::fabs(sampleZ - z) / sampleDistance) * (180.0f / PI_F);
        maxSlope = std::max(maxSlope, slope);
    }

    if (maxSlope > _placementMaxSlopeDegrees)
    {
        reason = Acore::StringFormat("Terrain is too steep there (>{:.0f}°).", _placementMaxSlopeDegrees);
        return false;
    }

    float playerCheckZ = player->GetPositionZ() + std::max(0.5f, player->GetCollisionHeight() * 0.5f);
    if (!map->isInLineOfSight(
            player->GetPositionX(), player->GetPositionY(), playerCheckZ,
            x, y, z + 0.25f,
            player->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
    {
        reason = "That location is blocked by terrain or a wall.";
        return false;
    }

    float reachX = x;
    float reachY = y;
    float reachZ = z;
    bool reachable = map->CanReachPositionAndGetValidCoords(player, reachX, reachY, reachZ, true, true);
    if (!reachable)
        reachable = map->CheckCollisionAndGetValidCoords(player, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), reachX, reachY, reachZ, true);

    if (!reachable)
    {
        reason = "That location is not reachable.";
        return false;
    }

    float reachDelta = std::sqrt((reachX - x) * (reachX - x) + (reachY - y) * (reachY - y));
    if (reachDelta > std::max(0.75f, collisionRadius))
    {
        reason = "That location collides with nearby geometry.";
        return false;
    }

    x = reachX;
    y = reachY;
    z = reachZ;

    float hitX = x;
    float hitY = y;
    float hitZ = z + 0.25f;
    bool blocked = map->GetObjectHitPos(
        player->GetPhaseMask(),
        player->GetPositionX(), player->GetPositionY(), playerCheckZ,
        x, y, z + 0.25f,
        hitX, hitY, hitZ, -0.2f);
    if (blocked)
    {
        float blockedDelta = std::sqrt((hitX - x) * (hitX - x) + (hitY - y) * (hitY - y) + (hitZ - (z + 0.25f)) * (hitZ - (z + 0.25f)));
        if (blockedDelta > std::max(0.35f, collisionRadius * 0.25f))
        {
            reason = "That spot intersects world geometry.";
            return false;
        }
    }

    QueryResult overlapResult = CharacterDatabase.Query(
        "SELECT placement_id, pos_x, pos_y, pos_z, collision_radius, min_distance "
        "FROM mod_playerhousing_placement WHERE owner_guid={} AND (map_id=0 OR map_id={})",
        player->GetGUID().GetCounter(), map->GetId());
    if (overlapResult)
    {
        do
        {
            Field* fields = overlapResult->Fetch();
            if (!fields)
                continue;

            uint32 placementId = fields[0].Get<uint32>();
            if (ignorePlacementId != 0 && placementId == ignorePlacementId)
                continue;

            float otherX = fields[1].Get<float>();
            float otherY = fields[2].Get<float>();
            float otherZ = fields[3].Get<float>();
            float otherCollisionRadius = std::max(0.1f, fields[4].Get<float>());
            float otherMinDistance = std::max(0.0f, fields[5].Get<float>());

            float requiredDistance = std::max(minDistance, otherMinDistance) + collisionRadius + otherCollisionRadius;
            float distance = std::sqrt((otherX - x) * (otherX - x) + (otherY - y) * (otherY - y) + (otherZ - z) * (otherZ - z));
            if (distance < requiredDistance)
            {
                reason = "Too close to another furniture object.";
                return false;
            }
        } while (overlapResult->NextRow());
    }

    z = SnapToGround(map, x, y, z);
    return true;
}

bool PlayerHousingMgr::ResolveSafeGroundPosition(Map* map, float seedX, float seedY, float seedZ, float orientation, Position& outPosition) const
{
    if (!map)
        return false;

    auto tryPoint = [&](float x, float y, float zHint) -> bool
    {
        float height = map->GetHeight(PHASEMASK_NORMAL, x, y, zHint + 25.0f, true, 500.0f);
        if (!std::isfinite(height))
            return false;

        if (height <= INVALID_HEIGHT_SENTINEL)
            return false;

        // Guard against maps that return a bogus "default" height (often near 0 / underworld).
        // This prevents teleporting players below the world when collision data is missing.
        if (height < -1000.0f || height > 10000.0f)
            return false;

        if (std::fabs(height - zHint) > 200.0f)
            return false;

        outPosition.Relocate(x, y, height + 0.35f, orientation);
        return true;
    };

    if (tryPoint(seedX, seedY, seedZ) || tryPoint(seedX, seedY, seedZ + 30.0f) || tryPoint(seedX, seedY, seedZ - 30.0f))
        return true;

    constexpr float pi = 3.14159265358979323846f;
    for (float radius = 2.0f; radius <= 80.0f; radius += 2.0f)
    {
        for (uint32 step = 0; step < 24; ++step)
        {
            float angle = (2.0f * pi * float(step)) / 24.0f;
            float x = seedX + std::cos(angle) * radius;
            float y = seedY + std::sin(angle) * radius;

            if (tryPoint(x, y, seedZ) || tryPoint(x, y, seedZ + 30.0f) || tryPoint(x, y, seedZ - 30.0f))
                return true;
        }
    }

    return false;
}

float PlayerHousingMgr::SnapToGround(Map* map, float x, float y, float z) const
{
    if (!map)
        return z;

    float height = map->GetHeight(PHASEMASK_NORMAL, x, y, z + 25.0f, true, 500.0f);
    return (height > INVALID_HEIGHT_SENTINEL) ? height : z;
}

bool PlayerHousingMgr::GetHouseCenter(Map* map, uint8 styleId, Position& outCenter) const
{
    if (!map)
        return false;

    StyleDefinition style;
    if (!GetStyleDefinition(styleId, style))
        return false;

    if (ResolveSafeGroundPosition(map, style.spawnX, style.spawnY, style.spawnZ, style.spawnO, outCenter))
        return true;

    if (AreaTriggerTeleport const* entrance = sObjectMgr->GetMapEntranceTrigger(map->GetId()))
    {
        if (ResolveSafeGroundPosition(map,
            entrance->target_X,
            entrance->target_Y,
            entrance->target_Z,
            entrance->target_Orientation,
            outCenter))
        {
            return true;
        }

        outCenter.Relocate(
            entrance->target_X,
            entrance->target_Y,
            entrance->target_Z + 0.35f,
            entrance->target_Orientation);
        return true;
    }

    return false;
}

bool PlayerHousingMgr::GetStyleMapId(uint8 styleId, uint32& mapId) const
{
    StyleDefinition style;
    if (!GetStyleDefinition(styleId, style))
        return false;

    mapId = style.mapId;
    return mapId != 0;
}

bool PlayerHousingMgr::EnsureSession(HouseRecord const& house, uint32 mapId, std::string& reason)
{
    std::lock_guard<std::mutex> guard(_lock);

    auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
    if (sessionItr != _sessionsByOwner.end())
    {
        if (sessionItr->second.mapId == mapId && sInstanceSaveMgr->GetInstanceSave(sessionItr->second.instanceId))
        {
            if (sessionItr->second.styleId != house.styleId || sessionItr->second.stage != house.stage)
            {
                sessionItr->second.styleId = house.styleId;
                sessionItr->second.stage = house.stage;
                sessionItr->second.initialized = false;
            }
            return true;
        }

        _ownerByInstance.erase(sessionItr->second.instanceId);
        _sessionsByOwner.erase(sessionItr);
    }

    uint32 instanceId = sMapMgr->GenerateInstanceId();
    if (!sInstanceSaveMgr->AddInstanceSave(mapId, instanceId, Difficulty(0)))
    {
        reason = "Could not allocate a new housing instance.";
        return false;
    }

    Session session;
    session.ownerGuid = house.ownerGuid;
    session.instanceId = instanceId;
    session.mapId = mapId;
    session.styleId = house.styleId;
    session.stage = house.stage;
    session.initialized = false;

    _ownerByInstance[instanceId] = house.ownerGuid;
    _sessionsByOwner[house.ownerGuid] = session;
    return true;
}

bool PlayerHousingMgr::SpawnStyleObject(Session& session, Map* map, StyleObjectDefinition const& objectDef, Position const& anchor)
{
    if (!map)
        return false;

    float sinO = std::sin(anchor.GetOrientation());
    float cosO = std::cos(anchor.GetOrientation());
    float rotatedX = objectDef.offsetX * cosO - objectDef.offsetY * sinO;
    float rotatedY = objectDef.offsetX * sinO + objectDef.offsetY * cosO;

    float x = anchor.GetPositionX() + rotatedX;
    float y = anchor.GetPositionY() + rotatedY;
    float z = SnapToGround(map, x, y, anchor.GetPositionZ()) + objectDef.offsetZ;
    float o = anchor.GetOrientation() + objectDef.orientationOffset;

    GameObject* object = map->SummonGameObject(objectDef.gameobjectEntry, x, y, z, o, 0.0f, 0.0f, 0.0f, 0.0f, 0, false);
    if (!object)
        return false;

    object->SetGameObjectFlag(GO_FLAG_NOT_SELECTABLE | GO_FLAG_INTERACT_COND);
    object->ReplaceAllDynamicFlags(GO_DYNFLAG_LO_NO_INTERACT);
    session.spawnedStyleObjects.push_back(object->GetGUID());
    return true;
}

bool PlayerHousingMgr::SpawnFurnitureObject(Session& session, Map* map, uint32 placementId, uint8 spawnType, uint32 spawnEntry, uint32 displayId, float scale, float collisionRadius, float x, float y, float z, float o)
{
    if (!map)
        return false;

    float snappedZ = SnapToGround(map, x, y, z);
    if (std::fabs(snappedZ - z) < 5.0f)
        z = snappedZ;

    SpawnedFurnitureRef spawnedRef;
    spawnedRef.spawnType = spawnType;
    spawnedRef.collisionRadius = std::max(0.1f, collisionRadius);

    if (spawnType == FURNITURE_SPAWN_TYPE_CREATURE)
    {
        Position spawnPosition;
        spawnPosition.Relocate(x, y, z, o);
        TempSummon* creature = map->SummonCreature(spawnEntry, spawnPosition);
        if (!creature)
            return false;

        if (displayId != 0)
        {
            creature->SetDisplayId(displayId);
            creature->SetNativeDisplayId(displayId);
        }

        creature->SetObjectScale(std::max(0.1f, scale));
        creature->SetReactState(REACT_PASSIVE);
        creature->SetImmuneToAll(true);
        creature->SetControlled(true, UNIT_STATE_ROOT);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        spawnedRef.guid = creature->GetGUID();
    }
    else
    {
        GameObject* object = map->SummonGameObject(spawnEntry, x, y, z, o, 0.0f, 0.0f, 0.0f, 0.0f, 0, false);
        if (!object)
            return false;

        object->SetObjectScale(std::max(0.1f, scale));
        object->SetGameObjectFlag(GO_FLAG_NOT_SELECTABLE | GO_FLAG_INTERACT_COND);
        object->ReplaceAllDynamicFlags(GO_DYNFLAG_LO_NO_INTERACT);
        spawnedRef.guid = object->GetGUID();
    }

    session.spawnedFurniture[placementId] = spawnedRef;
    return true;
}

void PlayerHousingMgr::DespawnSessionObjects(Session& session, Map* map)
{
    if (!map)
        return;

    for (ObjectGuid const& guid : session.spawnedStyleObjects)
        if (GameObject* object = map->GetGameObject(guid))
            object->AddObjectToRemoveList();

    for (auto const& [placementId, spawnedRef] : session.spawnedFurniture)
    {
        (void)placementId;
        if (spawnedRef.spawnType == FURNITURE_SPAWN_TYPE_CREATURE)
        {
            if (Creature* creature = map->GetCreature(spawnedRef.guid))
                creature->AddObjectToRemoveList();
            continue;
        }

        if (GameObject* object = map->GetGameObject(spawnedRef.guid))
            object->AddObjectToRemoveList();
    }

    if (session.stewardGuid)
        if (Creature* steward = map->GetCreature(session.stewardGuid))
            steward->AddObjectToRemoveList();

    session.spawnedStyleObjects.clear();
    session.spawnedFurniture.clear();
    session.stewardGuid.Clear();
}

bool PlayerHousingMgr::InitializeSession(ObjectGuid::LowType ownerGuid, std::string& reason)
{
    std::lock_guard<std::mutex> guard(_lock);

    auto sessionItr = _sessionsByOwner.find(ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
    {
        reason = "House session does not exist.";
        return false;
    }

    Session& session = sessionItr->second;
    Map* map = sMapMgr->FindMap(session.mapId, session.instanceId);
    if (!map)
    {
        reason = "Could not load housing instance map.";
        return false;
    }

    if (session.initialized)
        return true;

    // Housing must never contain hostile dungeon spawns. Any DB-spawned creatures/gameobjects
    // are removed on session init so maps can be swapped without risking "bad guys".
    {
        std::vector<Creature*> toRemoveCreatures;
        toRemoveCreatures.reserve(map->GetCreatureBySpawnIdStore().size());
        for (auto const& pair : map->GetCreatureBySpawnIdStore())
            if (pair.second)
                toRemoveCreatures.push_back(pair.second);

        for (Creature* creature : toRemoveCreatures)
            creature->AddObjectToRemoveList();

        std::vector<GameObject*> toRemoveGameObjects;
        toRemoveGameObjects.reserve(map->GetGameObjectBySpawnIdStore().size());
        for (auto const& pair : map->GetGameObjectBySpawnIdStore())
            if (pair.second)
                toRemoveGameObjects.push_back(pair.second);

        for (GameObject* gameobject : toRemoveGameObjects)
            gameobject->AddObjectToRemoveList();
    }

    DespawnSessionObjects(session, map);

    auto styleItr = _stylesById.find(session.styleId);
    if (styleItr == _stylesById.end())
    {
        reason = "House style definition not found.";
        return false;
    }

    Position center;
    StyleDefinition const& style = styleItr->second;
    if (!ResolveSafeGroundPosition(map, style.spawnX, style.spawnY, style.spawnZ, style.spawnO, center))
    {
        if (AreaTriggerTeleport const* entrance = sObjectMgr->GetMapEntranceTrigger(map->GetId()))
        {
            if (!ResolveSafeGroundPosition(map,
                entrance->target_X,
                entrance->target_Y,
                entrance->target_Z,
                entrance->target_Orientation,
                center))
            {
                center.Relocate(
                    entrance->target_X,
                    entrance->target_Y,
                    entrance->target_Z + 0.35f,
                    entrance->target_Orientation);
            }
        }
        else
        {
            reason = "Could not resolve a safe spawn point for this house style.";
            return false;
        }
    }

    auto styleObjectItr = _styleObjects.find(session.styleId);
    if (styleObjectItr != _styleObjects.end())
    {
        for (StyleObjectDefinition const& objectDef : styleObjectItr->second)
        {
            if (objectDef.minStage > session.stage)
                continue;

            SpawnStyleObject(session, map, objectDef, center);
        }
    }

    float forwardX = std::cos(center.GetOrientation());
    float forwardY = std::sin(center.GetOrientation());
    float rightX = -forwardY;
    float rightY = forwardX;

    float stewardX = center.GetPositionX() + (forwardX * 7.0f) + (rightX * 2.0f);
    float stewardY = center.GetPositionY() + (forwardY * 7.0f) + (rightY * 2.0f);
    float stewardZ = SnapToGround(map, stewardX, stewardY, center.GetPositionZ()) + 0.35f;
    Position stewardPosition;
    stewardPosition.Relocate(stewardX, stewardY, stewardZ, center.GetOrientation());

    if (TempSummon* steward = map->SummonCreature(_stewardEntry, stewardPosition))
    {
        session.stewardGuid = steward->GetGUID();
        steward->SetDisplayId(_stewardDisplayId);
        steward->SetNativeDisplayId(_stewardDisplayId);
    }
    else
    {
        LOG_WARN("module", "mod-playerhousing: Failed to summon steward entry {} for owner {}.",
            _stewardEntry, session.ownerGuid);
    }

    QueryResult furnitureResult = CharacterDatabase.Query(
        "SELECT placement_id, catalog_id, spawn_type, spawn_entry, display_id, scale, collision_radius, pos_x, pos_y, pos_z, orientation "
        "FROM mod_playerhousing_placement WHERE owner_guid={} AND (map_id=0 OR map_id={}) ORDER BY placement_id",
        ownerGuid, session.mapId);
    if (furnitureResult)
    {
        do
        {
            Field* fields = furnitureResult->Fetch();
            if (!fields)
                continue;

            uint32 placementId = fields[0].Get<uint32>();
            uint32 catalogId = fields[1].Get<uint32>();
            uint8 spawnType = fields[2].Get<uint8>();
            uint32 spawnEntry = fields[3].Get<uint32>();
            uint32 displayId = fields[4].Get<uint32>();
            float scale = std::max(0.1f, fields[5].Get<float>());
            float collisionRadius = std::max(0.1f, fields[6].Get<float>());
            float x = fields[7].Get<float>();
            float y = fields[8].Get<float>();
            float z = fields[9].Get<float>();
            float o = fields[10].Get<float>();

            if (spawnEntry == 0)
            {
                auto catalogItr = _catalogById.find(catalogId);
                if (catalogItr == _catalogById.end())
                    continue;

                spawnType = FURNITURE_SPAWN_TYPE_GAMEOBJECT;
                spawnEntry = catalogItr->second.gameobjectEntry;
                displayId = 0;
                scale = 1.0f;
                collisionRadius = _placementDefaultCollisionRadius;
            }

            SpawnFurnitureObject(session, map, placementId, spawnType, spawnEntry, displayId, scale, collisionRadius, x, y, z, o);
        } while (furnitureResult->NextRow());
    }

    session.initialized = true;
    return true;
}

void PlayerHousingMgr::RemovePlayerTrackingLocked(ObjectGuid playerGuid, bool eraseReturnLocation)
{
    auto playerItr = _playerOwnerByGuid.find(playerGuid);
    if (playerItr != _playerOwnerByGuid.end())
    {
        ObjectGuid::LowType ownerGuid = playerItr->second;
        auto sessionItr = _sessionsByOwner.find(ownerGuid);
        if (sessionItr != _sessionsByOwner.end())
            sessionItr->second.occupants.erase(playerGuid);

        _playerOwnerByGuid.erase(playerItr);
    }

    if (eraseReturnLocation)
        _returnLocations.erase(playerGuid);
}

void PlayerHousingMgr::RemovePlayerTracking(ObjectGuid playerGuid, Player* player, bool eraseReturnLocation, bool unbind)
{
    uint32 mapId = 0;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto playerItr = _playerOwnerByGuid.find(playerGuid);
        if (playerItr != _playerOwnerByGuid.end())
        {
            auto sessionItr = _sessionsByOwner.find(playerItr->second);
            if (sessionItr != _sessionsByOwner.end())
                mapId = sessionItr->second.mapId;
        }
        RemovePlayerTrackingLocked(playerGuid, eraseReturnLocation);
    }

    if (unbind && mapId != 0)
        sInstanceSaveMgr->PlayerUnbindInstance(playerGuid, mapId, Difficulty(0), true, player);
}

void PlayerHousingMgr::ClearPendingPlacement(ObjectGuid playerGuid, Player* player)
{
    bool removeTemporarySpell = false;

    {
        std::lock_guard<std::mutex> guard(_lock);
        auto pendingItr = _pendingPlacementByPlayer.find(playerGuid);
        if (pendingItr == _pendingPlacementByPlayer.end())
            return;

        removeTemporarySpell = pendingItr->second.temporarySpellGranted;
        _pendingPlacementByPlayer.erase(pendingItr);
    }

    if (removeTemporarySpell && player)
        player->removeSpell(PLACEMENT_TARGET_SPELL_ID, SPEC_MASK_ALL, true);
}

void PlayerHousingMgr::CleanupExpiredPendingPlacement(Player* player)
{
    if (!player)
        return;

    bool removeTemporarySpell = false;
    bool expired = false;
    std::time_t const now = std::time(nullptr);

    {
        std::lock_guard<std::mutex> guard(_lock);
        auto pendingItr = _pendingPlacementByPlayer.find(player->GetGUID());
        if (pendingItr == _pendingPlacementByPlayer.end())
            return;

        if (pendingItr->second.expiresAt > now)
            return;

        removeTemporarySpell = pendingItr->second.temporarySpellGranted;
        _pendingPlacementByPlayer.erase(pendingItr);
        expired = true;
    }

    if (removeTemporarySpell)
        player->removeSpell(PLACEMENT_TARGET_SPELL_ID, SPEC_MASK_ALL, true);

    if (expired)
        ChatHandler(player->GetSession()).SendSysMessage("Housing: Targeted placement expired. Select furniture again.");
}

bool PlayerHousingMgr::EnterHouseByOwnerGuid(Player* player, ObjectGuid::LowType ownerGuid, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    if (player->IsInCombat())
    {
        reason = "You cannot enter housing while in combat.";
        return false;
    }

    if (player->IsInFlight())
    {
        reason = "You cannot enter housing while in flight.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(ownerGuid, house))
    {
        if (ownerGuid != player->GetGUID().GetCounter())
        {
            reason = "That player does not own a house yet.";
            return false;
        }

        if (!CreateStarterHouse(ownerGuid, reason) || !GetHouseRecord(ownerGuid, house))
        {
            if (reason.empty())
                reason = "Could not create your starter house.";
            return false;
        }
    }

    if (!CanVisitHouse(player, house, reason))
        return false;

    uint32 houseMapId = 0;
    if (!GetStyleMapId(house.styleId, houseMapId))
    {
        reason = "House style map could not be resolved.";
        return false;
    }

    if (!EnsureSession(house, houseMapId, reason))
        return false;

    uint32 instanceId = 0;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto sessionItr = _sessionsByOwner.find(ownerGuid);
        if (sessionItr == _sessionsByOwner.end())
        {
            reason = "Session was not created.";
            return false;
        }

        RemovePlayerTrackingLocked(player->GetGUID(), false);

        if (_returnLocations.find(player->GetGUID()) == _returnLocations.end() || !IsHousingMap(player->GetMapId()))
        {
            _returnLocations[player->GetGUID()] = WorldLocation(
                player->GetMapId(),
                player->GetPositionX(),
                player->GetPositionY(),
                player->GetPositionZ(),
                player->GetOrientation());
        }

        sessionItr->second.occupants.insert(player->GetGUID());
        sessionItr->second.styleId = house.styleId;
        sessionItr->second.stage = house.stage;
        _playerOwnerByGuid[player->GetGUID()] = ownerGuid;
        instanceId = sessionItr->second.instanceId;
    }

    InstanceSave* save = sInstanceSaveMgr->GetInstanceSave(instanceId);
    if (!save)
    {
        reason = "Housing session instance was lost.";
        return false;
    }

    sInstanceSaveMgr->PlayerCreateBoundInstancesMaps(player->GetGUID());
    sInstanceSaveMgr->PlayerBindToInstance(player->GetGUID(), save, false, player);

    Map* map = sMapMgr->CreateMap(houseMapId, player);
    if (!map)
    {
        reason = Acore::StringFormat("Could not create housing map {}.", houseMapId);
        return false;
    }

    if (!InitializeSession(ownerGuid, reason))
        return false;

    Position center;
    if (!GetHouseCenter(map, house.styleId, center))
    {
        reason = "Could not resolve house location.";
        return false;
    }

    if (!player->TeleportTo(houseMapId, center.GetPositionX(), center.GetPositionY(), center.GetPositionZ(), center.GetOrientation(), TELE_TO_GM_MODE, nullptr, true))
    {
        reason = "Teleport to house failed.";
        return false;
    }

    return true;
}

bool PlayerHousingMgr::EnterOwnHouse(Player* player, std::string& reason)
{
    if (!player)
    {
        reason = "Player is not available.";
        return false;
    }

    return EnterHouseByOwnerGuid(player, player->GetGUID().GetCounter(), reason);
}

bool PlayerHousingMgr::VisitHouse(Player* player, std::string const& ownerName, std::string& reason)
{
    if (!player)
    {
        reason = "Player is not available.";
        return false;
    }

    ObjectGuid::LowType ownerGuid = 0;
    std::string normalizedName;
    if (!ResolvePlayerGuid(ownerName, ownerGuid, normalizedName))
    {
        reason = "Character name was not found.";
        return false;
    }

    return EnterHouseByOwnerGuid(player, ownerGuid, reason);
}

bool PlayerHousingMgr::LeaveHouse(Player* player, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    if (!IsHousingMap(player->GetMapId()))
    {
        reason = "You are not currently in a house.";
        return false;
    }

    WorldLocation returnLocation;
    bool hasReturn = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto itr = _returnLocations.find(player->GetGUID());
        if (itr != _returnLocations.end())
        {
            returnLocation = itr->second;
            hasReturn = true;
        }
    }

    bool teleportOk = hasReturn ? player->TeleportTo(returnLocation) : player->TeleportToEntryPoint();
    if (!teleportOk)
    {
        reason = "Could not leave house.";
        return false;
    }

    return true;
}

void PlayerHousingMgr::RefreshSessionFromHouse(HouseRecord const& house)
{
    uint32 styleMapId = 0;
    GetStyleMapId(house.styleId, styleMapId);

    std::lock_guard<std::mutex> guard(_lock);

    auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
        return;

    Session& session = sessionItr->second;
    session.stage = house.stage;
    session.styleId = house.styleId;
    session.initialized = false;

    if (styleMapId != 0 && styleMapId != session.mapId)
    {
        if (Map* map = sMapMgr->FindMap(session.mapId, session.instanceId))
            DespawnSessionObjects(session, map);

        _ownerByInstance.erase(session.instanceId);
        _sessionsByOwner.erase(sessionItr);
        return;
    }

    if (Map* map = sMapMgr->FindMap(session.mapId, session.instanceId))
        DespawnSessionObjects(session, map);
}

bool PlayerHousingMgr::UpgradeHouse(Player* player, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason) || !GetHouseRecord(player->GetGUID().GetCounter(), house))
        {
            if (reason.empty())
                reason = "Could not create starter house.";
            return false;
        }
    }

    StageDefinition nextStage;
    if (!GetStageDefinition(house.stage + 1, nextStage))
    {
        reason = "Your house is already at maximum stage.";
        return false;
    }

    if (player->GetMoney() < nextStage.costCopper)
    {
        reason = Acore::StringFormat("You need {} for the next upgrade.", FormatMoney(nextStage.costCopper));
        return false;
    }

    player->ModifyMoney(-int32(nextStage.costCopper));
    CharacterDatabase.Execute("UPDATE mod_playerhousing_house SET stage={} WHERE owner_guid={}", nextStage.stage, house.ownerGuid);

    house.stage = nextStage.stage;
    RefreshSessionFromHouse(house);

    reason = Acore::StringFormat("House upgraded to stage {}.", uint32(nextStage.stage));
    return true;
}

bool PlayerHousingMgr::SetPrivacy(Player* player, bool isPrivate, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason))
            return false;
    }

    CharacterDatabase.Execute("UPDATE mod_playerhousing_house SET is_private={} WHERE owner_guid={}", isPrivate ? 1 : 0, player->GetGUID().GetCounter());
    reason = isPrivate ? "House privacy set to private." : "House privacy set to public.";
    return true;
}

bool PlayerHousingMgr::SetStyle(Player* player, std::string const& styleCode, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    uint8 styleId = 0;
    std::string normalized = ToLower(styleCode);
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto styleItr = _styleIdByCode.find(normalized);
        if (styleItr == _styleIdByCode.end())
        {
            reason = "Unknown style. Available styles: human, gnome, tauren, undead.";
            return false;
        }
        styleId = styleItr->second;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason) || !GetHouseRecord(player->GetGUID().GetCounter(), house))
            return false;
    }

    CharacterDatabase.Execute("UPDATE mod_playerhousing_house SET style_id={} WHERE owner_guid={}", styleId, house.ownerGuid);
    GrantStarterUnlocks(house.ownerGuid, styleId);

    house.styleId = styleId;
    RefreshSessionFromHouse(house);

    reason = Acore::StringFormat("House style set to {}.", normalized);
    return true;
}

bool PlayerHousingMgr::InviteGuest(Player* player, std::string const& guestName, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason) || !GetHouseRecord(player->GetGUID().GetCounter(), house))
            return false;
    }

    ObjectGuid::LowType guestGuid = 0;
    std::string normalizedGuest;
    if (!ResolvePlayerGuid(guestName, guestGuid, normalizedGuest))
    {
        reason = "Guest character not found.";
        return false;
    }

    if (guestGuid == house.ownerGuid)
    {
        reason = "You are already the owner.";
        return false;
    }

    CharacterDatabase.Execute(
        "INSERT IGNORE INTO mod_playerhousing_acl (owner_guid, guest_guid) VALUES ({}, {})",
        house.ownerGuid, guestGuid);

    reason = Acore::StringFormat("Invited {} to your house.", normalizedGuest);
    return true;
}

bool PlayerHousingMgr::RemoveGuest(Player* player, std::string const& guestName, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        reason = "You do not own a house yet.";
        return false;
    }

    ObjectGuid::LowType guestGuid = 0;
    std::string normalizedGuest;
    if (!ResolvePlayerGuid(guestName, guestGuid, normalizedGuest))
    {
        reason = "Guest character not found.";
        return false;
    }

    CharacterDatabase.Execute(
        "DELETE FROM mod_playerhousing_acl WHERE owner_guid={} AND guest_guid={}",
        house.ownerGuid, guestGuid);

    reason = Acore::StringFormat("Removed {} from your guest list.", normalizedGuest);
    return true;
}

uint32 PlayerHousingMgr::GetPlacedFurnitureCount(ObjectGuid::LowType ownerGuid) const
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM mod_playerhousing_placement WHERE owner_guid={}", ownerGuid);
    if (!result)
        return 0;

    return (*result)[0].Get<uint32>();
}

bool PlayerHousingMgr::IsCatalogUnlocked(ObjectGuid::LowType ownerGuid, uint32 catalogId) const
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT 1 FROM mod_playerhousing_unlock WHERE owner_guid={} AND catalog_id={} LIMIT 1",
        ownerGuid, catalogId);
    return result != nullptr;
}

std::unordered_set<uint32> PlayerHousingMgr::LoadUnlockedCatalogSet(ObjectGuid::LowType ownerGuid) const
{
    std::unordered_set<uint32> out;

    QueryResult result = CharacterDatabase.Query(
        "SELECT catalog_id FROM mod_playerhousing_unlock WHERE owner_guid={}", ownerGuid);
    if (!result)
        return out;

    do
    {
        Field* fields = result->Fetch();
        if (fields)
            out.insert(fields[0].Get<uint32>());
    } while (result->NextRow());

    return out;
}

uint32 PlayerHousingMgr::GetNextPlacementId(ObjectGuid::LowType ownerGuid) const
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT IFNULL(MAX(placement_id), 0) + 1 FROM mod_playerhousing_placement WHERE owner_guid={}",
        ownerGuid);
    if (!result)
        return 1;

    return (*result)[0].Get<uint32>();
}

bool PlayerHousingMgr::IsCatalogAllowedForHouse(CatalogDefinition const& catalog, HouseRecord const& house) const
{
    if (!catalog.active)
        return false;

    if (house.stage < catalog.minStage)
        return false;

    if (catalog.styleMask == 0)
        return true;

    uint32 styleMask = GetStyleMaskFor(house.styleId);
    return (catalog.styleMask & styleMask) != 0;
}

bool PlayerHousingMgr::EnsureOwnerEditingContext(Player* player, HouseRecord& house, Session& session, Map*& map, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    if (!IsHousingMap(player->GetMapId()))
    {
        reason = "You must be inside your house.";
        return false;
    }

    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        reason = "You do not own a house yet.";
        return false;
    }

    map = player->GetMap();
    if (!map || !IsHousingMap(map->GetId()))
    {
        reason = "House map context is unavailable.";
        return false;
    }

    std::lock_guard<std::mutex> guard(_lock);

    auto playerOwnerItr = _playerOwnerByGuid.find(player->GetGUID());
    if (playerOwnerItr == _playerOwnerByGuid.end())
    {
        reason = "You are not in a managed house session.";
        return false;
    }

    if (playerOwnerItr->second != house.ownerGuid)
    {
        reason = "Only the owner can edit this house.";
        return false;
    }

    auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
    {
        reason = "House session was not found.";
        return false;
    }

    if (sessionItr->second.instanceId != map->GetInstanceId() || sessionItr->second.mapId != map->GetId())
    {
        reason = "You are not in your active house session.";
        return false;
    }

    session = sessionItr->second;
    return true;
}

bool PlayerHousingMgr::UnlockCatalogInternal(Player* player, uint32 catalogId, std::string& reason, bool chargePlayer)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason) || !GetHouseRecord(player->GetGUID().GetCounter(), house))
            return false;
    }

    CatalogDefinition catalog;
    if (!GetCatalogDefinition(catalogId, catalog))
    {
        reason = "Catalog entry not found.";
        return false;
    }

    if (!IsCatalogAllowedForHouse(catalog, house))
    {
        reason = "This item is not available for your current style/stage.";
        return false;
    }

    if (IsCatalogUnlocked(house.ownerGuid, catalog.catalogId))
    {
        reason = "You already unlocked that catalog item.";
        return false;
    }

    if (chargePlayer && player->GetMoney() < catalog.unlockCostCopper)
    {
        reason = Acore::StringFormat("You need {} to unlock this item.", FormatMoney(catalog.unlockCostCopper));
        return false;
    }

    if (chargePlayer && catalog.unlockCostCopper > 0)
        player->ModifyMoney(-int32(catalog.unlockCostCopper));

    CharacterDatabase.Execute(
        "INSERT INTO mod_playerhousing_unlock (owner_guid, catalog_id) VALUES ({}, {})",
        house.ownerGuid, catalog.catalogId);

    reason = Acore::StringFormat("Unlocked catalog item {} ({})", catalog.catalogId, catalog.displayName);
    return true;
}

bool PlayerHousingMgr::UnlockCatalog(Player* player, uint32 catalogId, std::string& reason)
{
    return UnlockCatalogInternal(player, catalogId, reason, true);
}

bool PlayerHousingMgr::PlaceFurniture(Player* player, uint32 catalogId, std::string& reason)
{
    if (!player)
    {
        reason = "Player is not available.";
        return false;
    }

    Position target;
    target.Relocate(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
    return PlaceFurnitureAt(player, catalogId, target, reason);
}

bool PlayerHousingMgr::ValidatePlacementRequest(Player* player, uint32 catalogId, HouseRecord& house, CatalogDefinition& catalog, StageDefinition& stage, std::string& reason)
{
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;

    if (!GetCatalogDefinition(catalogId, catalog))
    {
        reason = "Catalog entry not found.";
        return false;
    }

    if (!IsCatalogAllowedForHouse(catalog, house))
    {
        reason = "This item is not available for your current style/stage.";
        return false;
    }

    if (!IsCatalogUnlocked(house.ownerGuid, catalog.catalogId))
    {
        reason = "You have not unlocked this catalog item yet.";
        return false;
    }

    if (!GetStageDefinition(house.stage, stage))
    {
        reason = "Stage definition not found.";
        return false;
    }

    if (GetPlacedFurnitureCount(house.ownerGuid) >= stage.maxItems)
    {
        reason = "You reached this stage's furniture limit.";
        return false;
    }

    return true;
}

bool PlayerHousingMgr::ValidateItemPlacementRequest(Player* player, FurnitureItemDefinition const& itemDef, HouseRecord& house, StageDefinition& stage, std::string& reason)
{
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;

    (void)session;
    (void)map;

    if (!itemDef.active)
    {
        reason = "That furniture item is currently disabled.";
        return false;
    }

    if (!GetStageDefinition(house.stage, stage))
    {
        reason = "Stage definition not found.";
        return false;
    }

    if (house.stage < itemDef.requiredStage)
    {
        reason = Acore::StringFormat("Your house must be at least stage {} to place this item.", uint32(itemDef.requiredStage));
        return false;
    }

    if (itemDef.styleMask != 0)
    {
        uint32 styleMask = GetStyleMaskFor(house.styleId);
        if ((itemDef.styleMask & styleMask) == 0)
        {
            reason = "That furniture item cannot be used with your current house style.";
            return false;
        }
    }

    if (GetPlacedFurnitureCount(house.ownerGuid) >= stage.maxItems)
    {
        reason = "You reached this stage's furniture limit.";
        return false;
    }

    if (!player->HasItemCount(itemDef.itemEntry, 1, false))
    {
        reason = "You no longer have that furniture item.";
        return false;
    }

    return true;
}

bool PlayerHousingMgr::BeginSpellPlacement(Player* player, uint32 catalogId, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    CatalogDefinition catalog;
    StageDefinition stage;
    if (!ValidatePlacementRequest(player, catalogId, house, catalog, stage, reason))
        return false;
    (void)stage;

    bool hadPlacementSpell = player->HasSpell(PLACEMENT_TARGET_SPELL_ID);
    if (!hadPlacementSpell)
        player->learnSpell(PLACEMENT_TARGET_SPELL_ID, true);

    if (!player->HasSpell(PLACEMENT_TARGET_SPELL_ID))
    {
        reason = "Could not enable placement spell.";
        return false;
    }

    bool keepTemporaryGrant = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto pendingItr = _pendingPlacementByPlayer.find(player->GetGUID());
        if (pendingItr != _pendingPlacementByPlayer.end())
            keepTemporaryGrant = pendingItr->second.temporarySpellGranted;
    }

    PendingPlacement pending;
    pending.ownerGuid = house.ownerGuid;
    pending.catalogId = catalog.catalogId;
    pending.sourceItemEntry = 0;
    pending.displayName = catalog.displayName;
    pending.spawnType = FURNITURE_SPAWN_TYPE_GAMEOBJECT;
    pending.spawnEntry = catalog.gameobjectEntry;
    pending.displayId = 0;
    pending.scale = 1.0f;
    pending.collisionRadius = _placementDefaultCollisionRadius;
    pending.minDistance = _placementDefaultMinDistance;
    pending.orientationOffset = 0.0f;
    pending.consumeOnPlace = false;
    pending.temporarySpellGranted = keepTemporaryGrant || !hadPlacementSpell;
    pending.expiresAt = std::time(nullptr) + PLACEMENT_PENDING_TIMEOUT_SECONDS;

    {
        std::lock_guard<std::mutex> guard(_lock);
        _pendingPlacementByPlayer[player->GetGUID()] = pending;
    }

    // Flare has cooldown in core data; clear it so placement casts are not rate-limited.
    player->RemoveSpellCooldown(PLACEMENT_TARGET_SPELL_ID, true);

    reason = Acore::StringFormat(
        "Selected {} (id {}). Cast Flare and click a location to place it.",
        catalog.displayName, catalog.catalogId);
    return true;
}

bool PlayerHousingMgr::BeginItemPlacement(Player* player, Item* item, std::string& reason)
{
    if (!_enabled || !player || !item)
    {
        reason = "Housing is disabled.";
        return false;
    }

    FurnitureItemDefinition itemDef;
    if (!GetFurnitureItemDefinition(item->GetEntry(), itemDef))
    {
        reason = "That item is not a housing furniture item.";
        return false;
    }

    HouseRecord house;
    StageDefinition stage;
    if (!ValidateItemPlacementRequest(player, itemDef, house, stage, reason))
        return false;
    (void)stage;

    PendingPlacement pending;
    pending.ownerGuid = house.ownerGuid;
    pending.catalogId = itemDef.catalogId;
    pending.sourceItemEntry = itemDef.itemEntry;
    pending.displayName = itemDef.displayName;
    pending.spawnType = itemDef.spawnType;
    pending.spawnEntry = itemDef.spawnEntry;
    pending.displayId = itemDef.displayId;
    pending.scale = itemDef.scale;
    pending.collisionRadius = itemDef.collisionRadius;
    pending.minDistance = itemDef.minDistance;
    pending.orientationOffset = itemDef.orientationOffset;
    pending.consumeOnPlace = itemDef.consumeOnPlace;
    pending.temporarySpellGranted = false;
    pending.expiresAt = std::time(nullptr) + PLACEMENT_PENDING_TIMEOUT_SECONDS;

    {
        std::lock_guard<std::mutex> guard(_lock);
        _pendingPlacementByPlayer[player->GetGUID()] = pending;
    }

    player->RemoveSpellCooldown(PLACEMENT_TARGET_SPELL_ID, true);
    reason = Acore::StringFormat("Placing {}: choose a location with the targeting circle.", itemDef.displayName);
    return true;
}

bool PlayerHousingMgr::CancelSpellPlacement(Player* player, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    bool hadPending = false;
    {
        std::lock_guard<std::mutex> guard(_lock);
        hadPending = _pendingPlacementByPlayer.find(player->GetGUID()) != _pendingPlacementByPlayer.end();
    }

    if (!hadPending)
    {
        reason = "No pending targeted placement.";
        return false;
    }

    ClearPendingPlacement(player->GetGUID(), player);
    reason = "Canceled pending targeted placement.";
    return true;
}

bool PlayerHousingMgr::HandlePlacementSpellCast(Player* player, Spell* spell, std::string& reason)
{
    reason.clear();

    if (!_enabled || !player || !spell || !spell->GetSpellInfo())
        return false;

    if (spell->GetSpellInfo()->Id != PLACEMENT_TARGET_SPELL_ID)
        return false;

    PendingPlacement pending;
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto pendingItr = _pendingPlacementByPlayer.find(player->GetGUID());
        if (pendingItr == _pendingPlacementByPlayer.end())
            return false;

        pending = pendingItr->second;
    }

    if (pending.expiresAt <= std::time(nullptr))
    {
        ClearPendingPlacement(player->GetGUID(), player);
        reason = "Pending furniture placement expired. Select furniture again.";
        return true;
    }

    WorldLocation const* destination = spell->m_targets.GetDstPos();
    if (!destination)
    {
        player->RemoveSpellCooldown(PLACEMENT_TARGET_SPELL_ID, true);
        reason = "No ground target selected. Cast Flare again and click a location.";
        return true;
    }

    Position target;
    target.Relocate(
        destination->GetPositionX(),
        destination->GetPositionY(),
        destination->GetPositionZ(),
        player->GetOrientation());

    if (PlaceFurnitureResolved(
            player,
            pending.catalogId,
            pending.sourceItemEntry,
            pending.spawnType,
            pending.spawnEntry,
            pending.displayId,
            pending.scale,
            pending.collisionRadius,
            pending.minDistance,
            pending.orientationOffset,
            pending.consumeOnPlace,
            pending.displayName,
            target,
            reason))
    {
        ClearPendingPlacement(player->GetGUID(), player);
        player->RemoveSpellCooldown(PLACEMENT_TARGET_SPELL_ID, true);
        return true;
    }

    // Keep selection active for retriable failures, but clear cooldown so another target cast can happen immediately.
    player->RemoveSpellCooldown(PLACEMENT_TARGET_SPELL_ID, true);

    if (reason == "Catalog entry not found." ||
        reason == "This item is not available for your current style/stage." ||
        reason == "You have not unlocked this catalog item yet." ||
        reason == "You no longer have that furniture item.")
    {
        ClearPendingPlacement(player->GetGUID(), player);
    }

    return true;
}

bool PlayerHousingMgr::ListPlacementChoices(Player* player, std::vector<std::pair<uint32, std::string>>& choices, std::string& reason)
{
    choices.clear();

    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;

    (void)session;
    (void)map;

    std::unordered_set<uint32> unlocked = LoadUnlockedCatalogSet(house.ownerGuid);
    if (unlocked.empty())
    {
        reason = "No catalog items unlocked yet.";
        return false;
    }

    std::vector<CatalogDefinition> catalogList;
    {
        std::lock_guard<std::mutex> guard(_lock);
        catalogList.reserve(_catalogById.size());
        for (auto const& [catalogId, catalog] : _catalogById)
        {
            (void)catalogId;
            catalogList.push_back(catalog);
        }
    }

    std::sort(catalogList.begin(), catalogList.end(), [](CatalogDefinition const& left, CatalogDefinition const& right)
    {
        if (left.sortOrder != right.sortOrder)
            return left.sortOrder < right.sortOrder;
        return left.catalogId < right.catalogId;
    });

    for (CatalogDefinition const& catalog : catalogList)
    {
        if (!catalog.active)
            continue;

        if (!IsCatalogAllowedForHouse(catalog, house))
            continue;

        if (unlocked.find(catalog.catalogId) == unlocked.end())
            continue;

        choices.emplace_back(catalog.catalogId, catalog.displayName);
    }

    if (choices.empty())
    {
        reason = "No unlocked catalog items are available for your current style/stage.";
        return false;
    }

    return true;
}

bool PlayerHousingMgr::PlaceFurnitureAt(Player* player, uint32 catalogId, Position const& target, std::string& reason)
{
    HouseRecord house;
    CatalogDefinition catalog;
    StageDefinition stage;
    if (!ValidatePlacementRequest(player, catalogId, house, catalog, stage, reason))
        return false;

    return PlaceFurnitureResolved(
        player,
        catalog.catalogId,
        0,
        FURNITURE_SPAWN_TYPE_GAMEOBJECT,
        catalog.gameobjectEntry,
        0,
        1.0f,
        _placementDefaultCollisionRadius,
        _placementDefaultMinDistance,
        0.0f,
        false,
        catalog.displayName,
        target,
        reason);
}

bool PlayerHousingMgr::PlaceFurnitureResolved(Player* player, uint32 catalogId, uint32 sourceItemEntry, uint8 spawnType, uint32 spawnEntry, uint32 displayId, float scale, float collisionRadius, float minDistance, float orientationOffset, bool consumeOnPlace, std::string const& displayName, Position const& target, std::string& reason)
{
    HouseRecord house;
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;
    (void)session;

    if (spawnType > FURNITURE_SPAWN_TYPE_CREATURE || spawnEntry == 0)
    {
        reason = "That furniture spawn definition is invalid.";
        return false;
    }

    if (spawnType == FURNITURE_SPAWN_TYPE_GAMEOBJECT)
    {
        if (!sObjectMgr->GetGameObjectTemplate(spawnEntry))
        {
            reason = "That furniture gameobject entry does not exist.";
            return false;
        }
    }
    else if (!sObjectMgr->GetCreatureTemplate(spawnEntry))
    {
        reason = "That furniture creature entry does not exist.";
        return false;
    }

    CatalogDefinition catalog;
    bool hasCatalog = (catalogId != 0 && GetCatalogDefinition(catalogId, catalog));
    if (catalogId != 0 && !hasCatalog && sourceItemEntry == 0)
    {
        reason = "Catalog entry not found.";
        return false;
    }

    if (sourceItemEntry == 0 && hasCatalog)
    {
        if (!IsCatalogAllowedForHouse(catalog, house))
        {
            reason = "This item is not available for your current style/stage.";
            return false;
        }

        if (!IsCatalogUnlocked(house.ownerGuid, catalog.catalogId))
        {
            reason = "You have not unlocked this catalog item yet.";
            return false;
        }
    }

    StageDefinition stage;
    if (!GetStageDefinition(house.stage, stage))
    {
        reason = "Stage definition not found.";
        return false;
    }

    if (GetPlacedFurnitureCount(house.ownerGuid) >= stage.maxItems)
    {
        reason = "You reached this stage's furniture limit.";
        return false;
    }

    if (sourceItemEntry != 0 && !player->HasItemCount(sourceItemEntry, 1, false))
    {
        reason = "You no longer have that furniture item.";
        return false;
    }

    float objectScale = std::max(0.1f, scale);
    float objectCollisionRadius = std::max(0.1f, collisionRadius);
    float objectMinDistance = std::max(0.0f, minDistance);

    Position center;
    if (!GetHouseCenter(map, house.styleId, center))
    {
        reason = "House center could not be resolved.";
        return false;
    }

    float px = target.GetPositionX();
    float py = target.GetPositionY();
    float pz = target.GetPositionZ();
    if (!IsPlacementPointValid(player, map, center, stage, objectMinDistance, objectCollisionRadius, px, py, pz, reason))
        return false;

    float po = std::atan2(player->GetPositionY() - py, player->GetPositionX() - px);
    po = NormalizeAngle(po + orientationOffset);

    uint32 placementId = GetNextPlacementId(house.ownerGuid);
    CharacterDatabase.Execute(
        "INSERT INTO mod_playerhousing_placement "
        "(owner_guid, placement_id, catalog_id, source_item_entry, map_id, spawn_type, spawn_entry, display_id, scale, collision_radius, min_distance, pos_x, pos_y, pos_z, orientation) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        house.ownerGuid, placementId, catalogId, sourceItemEntry, map->GetId(), uint32(spawnType), spawnEntry, displayId, objectScale, objectCollisionRadius, objectMinDistance, px, py, pz, po);

    auto rollbackPlacement = [&]()
    {
        CharacterDatabase.Execute(
            "DELETE FROM mod_playerhousing_placement WHERE owner_guid={} AND placement_id={}",
            house.ownerGuid, placementId);
    };

    {
        std::lock_guard<std::mutex> guard(_lock);
        auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
        if (sessionItr == _sessionsByOwner.end())
        {
            rollbackPlacement();
            reason = "House session was not found.";
            return false;
        }

        if (!SpawnFurnitureObject(sessionItr->second, map, placementId, spawnType, spawnEntry, displayId, objectScale, objectCollisionRadius, px, py, pz, po))
        {
            rollbackPlacement();
            reason = "Failed to spawn furniture object.";
            return false;
        }
    }

    if (sourceItemEntry != 0 && consumeOnPlace)
    {
        if (!player->HasItemCount(sourceItemEntry, 1, false))
        {
            CharacterDatabase.Execute(
                "DELETE FROM mod_playerhousing_placement WHERE owner_guid={} AND placement_id={}",
                house.ownerGuid, placementId);

            std::lock_guard<std::mutex> guard(_lock);
            auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
            if (sessionItr != _sessionsByOwner.end())
            {
                auto spawnedItr = sessionItr->second.spawnedFurniture.find(placementId);
                if (spawnedItr != sessionItr->second.spawnedFurniture.end())
                {
                    if (spawnedItr->second.spawnType == FURNITURE_SPAWN_TYPE_CREATURE)
                    {
                        if (Creature* creature = map->GetCreature(spawnedItr->second.guid))
                            creature->AddObjectToRemoveList();
                    }
                    else if (GameObject* gameObject = map->GetGameObject(spawnedItr->second.guid))
                    {
                        gameObject->AddObjectToRemoveList();
                    }

                    sessionItr->second.spawnedFurniture.erase(spawnedItr);
                }
            }

            reason = "You no longer have that furniture item.";
            return false;
        }

        player->DestroyItemCount(sourceItemEntry, 1, true);
    }

    std::string finalName = displayName;
    if (finalName.empty() && hasCatalog)
        finalName = catalog.displayName;
    if (finalName.empty())
        finalName = "furniture";

    reason = Acore::StringFormat("Placed {} as placement #{}.", finalName, placementId);
    return true;
}

bool PlayerHousingMgr::MoveFurniture(Player* player, uint32 placementId, std::string& reason)
{
    HouseRecord house;
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;

    QueryResult placementResult = CharacterDatabase.Query(
        "SELECT p.catalog_id, p.spawn_type, p.spawn_entry, p.display_id, p.scale, p.collision_radius, p.min_distance, p.pos_x, p.pos_y, p.pos_z, p.orientation "
        "FROM mod_playerhousing_placement p WHERE p.owner_guid={} AND p.placement_id={}",
        house.ownerGuid, placementId);
    if (!placementResult)
    {
        reason = "Placement ID not found.";
        return false;
    }

    uint32 catalogId = (*placementResult)[0].Get<uint32>();
    uint8 spawnType = (*placementResult)[1].Get<uint8>();
    uint32 spawnEntry = (*placementResult)[2].Get<uint32>();
    uint32 displayId = (*placementResult)[3].Get<uint32>();
    float scale = std::max(0.1f, (*placementResult)[4].Get<float>());
    float collisionRadius = std::max(0.1f, (*placementResult)[5].Get<float>());
    float minDistance = std::max(0.0f, (*placementResult)[6].Get<float>());

    if (spawnEntry == 0)
    {
        CatalogDefinition catalog;
        if (!GetCatalogDefinition(catalogId, catalog))
        {
            reason = "Placement references an invalid catalog item.";
            return false;
        }

        spawnType = FURNITURE_SPAWN_TYPE_GAMEOBJECT;
        spawnEntry = catalog.gameobjectEntry;
        displayId = 0;
        scale = 1.0f;
        collisionRadius = _placementDefaultCollisionRadius;
        minDistance = _placementDefaultMinDistance;
    }

    StageDefinition stage;
    if (!GetStageDefinition(house.stage, stage))
    {
        reason = "Stage definition not found.";
        return false;
    }

    Position center;
    if (!GetHouseCenter(map, house.styleId, center))
    {
        reason = "House center could not be resolved.";
        return false;
    }

    float px = player->GetPositionX();
    float py = player->GetPositionY();
    float pz = player->GetPositionZ();
    if (!IsPlacementPointValid(player, map, center, stage, minDistance, collisionRadius, px, py, pz, reason, placementId))
        return false;

    float po = player->GetOrientation();
    CharacterDatabase.Execute(
        "UPDATE mod_playerhousing_placement SET map_id={}, pos_x={}, pos_y={}, pos_z={}, orientation={} "
        "WHERE owner_guid={} AND placement_id={}",
        map->GetId(), px, py, pz, po, house.ownerGuid, placementId);

    std::lock_guard<std::mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
    if (sessionItr == _sessionsByOwner.end())
    {
        reason = "House session was not found.";
        return false;
    }

    auto spawnedItr = sessionItr->second.spawnedFurniture.find(placementId);
    if (spawnedItr != sessionItr->second.spawnedFurniture.end())
    {
        if (spawnedItr->second.spawnType == FURNITURE_SPAWN_TYPE_CREATURE)
        {
            if (Creature* oldCreature = map->GetCreature(spawnedItr->second.guid))
                oldCreature->AddObjectToRemoveList();
        }
        else if (GameObject* oldObject = map->GetGameObject(spawnedItr->second.guid))
        {
            oldObject->AddObjectToRemoveList();
        }
        sessionItr->second.spawnedFurniture.erase(spawnedItr);
    }

    if (!SpawnFurnitureObject(sessionItr->second, map, placementId, spawnType, spawnEntry, displayId, scale, collisionRadius, px, py, pz, po))
    {
        reason = "Failed to move furniture object.";
        return false;
    }

    reason = Acore::StringFormat("Moved placement #{}.", placementId);
    return true;
}

bool PlayerHousingMgr::RemoveFurniture(Player* player, uint32 placementId, std::string& reason)
{
    HouseRecord house;
    Session session;
    Map* map = nullptr;
    if (!EnsureOwnerEditingContext(player, house, session, map, reason))
        return false;

    QueryResult placementResult = CharacterDatabase.Query(
        "SELECT 1 FROM mod_playerhousing_placement WHERE owner_guid={} AND placement_id={}",
        house.ownerGuid, placementId);
    if (!placementResult)
    {
        reason = "Placement ID not found.";
        return false;
    }

    CharacterDatabase.Execute(
        "DELETE FROM mod_playerhousing_placement WHERE owner_guid={} AND placement_id={}",
        house.ownerGuid, placementId);

    std::lock_guard<std::mutex> guard(_lock);
    auto sessionItr = _sessionsByOwner.find(house.ownerGuid);
    if (sessionItr != _sessionsByOwner.end())
    {
        auto spawnedItr = sessionItr->second.spawnedFurniture.find(placementId);
        if (spawnedItr != sessionItr->second.spawnedFurniture.end())
        {
            if (spawnedItr->second.spawnType == FURNITURE_SPAWN_TYPE_CREATURE)
            {
                if (Creature* creature = map->GetCreature(spawnedItr->second.guid))
                    creature->AddObjectToRemoveList();
            }
            else if (GameObject* object = map->GetGameObject(spawnedItr->second.guid))
            {
                object->AddObjectToRemoveList();
            }
            sessionItr->second.spawnedFurniture.erase(spawnedItr);
        }
    }

    reason = Acore::StringFormat("Removed placement #{}.", placementId);
    return true;
}

bool PlayerHousingMgr::ListFurniture(Player* player, std::vector<std::string>& lines, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        reason = "You do not own a house yet.";
        return false;
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT p.placement_id, p.catalog_id, p.source_item_entry, p.pos_x, p.pos_y, p.pos_z "
        "FROM mod_playerhousing_placement p WHERE p.owner_guid={} ORDER BY p.placement_id",
        house.ownerGuid);
    if (!result)
    {
        lines.emplace_back("No furniture placed yet.");
        return true;
    }

    lines.emplace_back("Placed furniture:");

    do
    {
        Field* fields = result->Fetch();
        if (!fields)
            continue;

        uint32 placementId = fields[0].Get<uint32>();
        uint32 catalogId = fields[1].Get<uint32>();
        uint32 sourceItemEntry = fields[2].Get<uint32>();
        float x = fields[3].Get<float>();
        float y = fields[4].Get<float>();
        float z = fields[5].Get<float>();

        std::string catalogName = "Unknown Catalog";
        {
            std::lock_guard<std::mutex> guard(_lock);
            auto catalogItr = _catalogById.find(catalogId);
            if (catalogItr != _catalogById.end())
                catalogName = catalogItr->second.displayName;
            else if (sourceItemEntry != 0)
            {
                auto itemItr = _furnitureItemsByEntry.find(sourceItemEntry);
                if (itemItr != _furnitureItemsByEntry.end())
                    catalogName = itemItr->second.displayName;
            }
        }

        lines.push_back(Acore::StringFormat("#{}: {} (x={:.1f}, y={:.1f}, z={:.1f})", placementId, catalogName, x, y, z));
    } while (result->NextRow());

    return true;
}

bool PlayerHousingMgr::ListCatalog(Player* player, std::vector<std::string>& lines, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        if (!CreateStarterHouse(player->GetGUID().GetCounter(), reason) || !GetHouseRecord(player->GetGUID().GetCounter(), house))
            return false;
    }

    std::unordered_set<uint32> unlocked = LoadUnlockedCatalogSet(house.ownerGuid);

    std::vector<CatalogDefinition> catalogList;
    {
        std::lock_guard<std::mutex> guard(_lock);
        catalogList.reserve(_catalogById.size());
        for (auto const& [catalogId, catalog] : _catalogById)
        {
            (void)catalogId;
            catalogList.push_back(catalog);
        }
    }

    std::sort(catalogList.begin(), catalogList.end(), [](CatalogDefinition const& left, CatalogDefinition const& right)
    {
        if (left.sortOrder != right.sortOrder)
            return left.sortOrder < right.sortOrder;
        return left.catalogId < right.catalogId;
    });

    lines.emplace_back("Catalog:");

    for (CatalogDefinition const& catalog : catalogList)
    {
        if (!catalog.active)
            continue;

        if (!IsCatalogAllowedForHouse(catalog, house))
            continue;

        bool isUnlocked = unlocked.find(catalog.catalogId) != unlocked.end();
        std::string status = isUnlocked ? "[Unlocked]" : Acore::StringFormat("[Locked: {}]", FormatMoney(catalog.unlockCostCopper));
        lines.push_back(Acore::StringFormat("{} {} - id {}", status, catalog.displayName, catalog.catalogId));
    }

    return true;
}

bool PlayerHousingMgr::GetHouseStatus(Player* player, std::vector<std::string>& lines, std::string& reason)
{
    if (!_enabled || !player)
    {
        reason = "Housing is disabled.";
        return false;
    }

    HouseRecord house;
    if (!GetHouseRecord(player->GetGUID().GetCounter(), house))
    {
        reason = "You do not own a house yet.";
        return false;
    }

    StyleDefinition style;
    StageDefinition stage;
    if (!GetStyleDefinition(house.styleId, style) || !GetStageDefinition(house.stage, stage))
    {
        reason = "House definitions are invalid.";
        return false;
    }

    lines.push_back(Acore::StringFormat("Style: {} ({})", style.displayName, style.styleCode));
    lines.push_back(Acore::StringFormat("Stage: {}", uint32(stage.stage)));
    lines.push_back(Acore::StringFormat("Privacy: {}", house.isPrivate ? "private" : "public"));
    lines.push_back(Acore::StringFormat("Max furniture at this stage: {}", stage.maxItems));
    lines.push_back(Acore::StringFormat("Placed furniture: {}", GetPlacedFurnitureCount(house.ownerGuid)));

    StageDefinition nextStage;
    if (GetStageDefinition(house.stage + 1, nextStage))
        lines.push_back(Acore::StringFormat("Next upgrade cost: {}", FormatMoney(nextStage.costCopper)));
    else
        lines.emplace_back("Next upgrade: max stage reached");

    return true;
}

bool PlayerHousingMgr::IsHousingMap(uint32 mapId) const
{
    if (!_enabled)
        return false;

    if (_housingMapIds.empty())
        return mapId == _defaultHousingMapId;

    return _housingMapIds.find(mapId) != _housingMapIds.end();
}

bool PlayerHousingMgr::IsInsideManagedHouse(Player const* player) const
{
    if (!_enabled || !player || !IsHousingMap(player->GetMapId()))
        return false;

    std::lock_guard<std::mutex> guard(_lock);
    auto playerItr = _playerOwnerByGuid.find(player->GetGUID());
    if (playerItr == _playerOwnerByGuid.end())
        return false;

    auto sessionItr = _sessionsByOwner.find(playerItr->second);
    if (sessionItr == _sessionsByOwner.end())
        return false;

    return sessionItr->second.instanceId == player->GetInstanceId() && sessionItr->second.mapId == player->GetMapId();
}

bool PlayerHousingMgr::IsStewardEntry(uint32 entry) const
{
    return _enabled && entry == _stewardEntry;
}

uint32 PlayerHousingMgr::GetStewardEntry() const
{
    return _stewardEntry;
}

uint32 PlayerHousingMgr::GetStewardDisplayId() const
{
    return _stewardDisplayId;
}
