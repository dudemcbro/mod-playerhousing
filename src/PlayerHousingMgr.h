#ifndef MOD_PLAYERHOUSING_MGR_H
#define MOD_PLAYERHOUSING_MGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"

#include <mutex>
#include <ctime>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Map;
class Player;
class Spell;
class Creature;
class Item;

class PlayerHousingMgr
{
public:
    struct HouseRecord
    {
        ObjectGuid::LowType ownerGuid{0};
        uint8 styleId{1};
        uint8 stage{0};
        bool isPrivate{true};
    };

    static PlayerHousingMgr* instance();

    void OnStartup();
    void OnPlayerLogin(Player* player);
    void OnPlayerLogout(Player* player);
    void OnPlayerUpdate(Player* player, uint32 diffMs);
    void OnPlayerMapChanged(Player* player);
    void OnPlayerDelete(ObjectGuid guid);
    void OnDestroyMap(Map* map);

    bool EnsureStarterHouse(Player* player, bool announce, std::string& reason);
    bool EnterOwnHouse(Player* player, std::string& reason);
    bool VisitHouse(Player* player, std::string const& ownerName, std::string& reason);
    bool LeaveHouse(Player* player, std::string& reason);
    bool UpgradeHouse(Player* player, std::string& reason);
    bool SetPrivacy(Player* player, bool isPrivate, std::string& reason);
    bool SetStyle(Player* player, std::string const& styleCode, std::string& reason);
    bool InviteGuest(Player* player, std::string const& guestName, std::string& reason);
    bool RemoveGuest(Player* player, std::string const& guestName, std::string& reason);
    bool UnlockCatalog(Player* player, uint32 catalogId, std::string& reason);
    bool PlaceFurniture(Player* player, uint32 catalogId, std::string& reason);
    bool BeginSpellPlacement(Player* player, uint32 catalogId, std::string& reason);
    bool BeginItemPlacement(Player* player, Item* item, std::string& reason);
    bool CancelSpellPlacement(Player* player, std::string& reason);
    bool HandlePlacementSpellCast(Player* player, Spell* spell, std::string& reason);
    bool ListPlacementChoices(Player* player, std::vector<std::pair<uint32, std::string>>& choices, std::string& reason);
    bool MoveFurniture(Player* player, uint32 placementId, std::string& reason);
    bool RemoveFurniture(Player* player, uint32 placementId, std::string& reason);
    bool ListFurniture(Player* player, std::vector<std::string>& lines, std::string& reason);
    bool ListCatalog(Player* player, std::vector<std::string>& lines, std::string& reason);
    bool GetHouseStatus(Player* player, std::vector<std::string>& lines, std::string& reason);

    bool IsHousingMap(uint32 mapId) const;
    bool IsInsideManagedHouse(Player const* player) const;
    bool IsStewardEntry(uint32 entry) const;
    uint32 GetStewardEntry() const;
    uint32 GetStewardDisplayId() const;

private:
    struct StyleDefinition
    {
        uint8 styleId{0};
        std::string styleCode;
        std::string displayName;
        uint32 mapId{0};
        float spawnX{0.0f};
        float spawnY{0.0f};
        float spawnZ{0.0f};
        float spawnO{0.0f};
    };

    struct StageDefinition
    {
        uint8 stage{0};
        uint32 costCopper{0};
        uint32 maxItems{0};
        float placeRadius{0.0f};
    };

    struct CatalogDefinition
    {
        uint32 catalogId{0};
        std::string displayName;
        uint32 gameobjectEntry{0};
        uint32 unlockCostCopper{0};
        uint8 minStage{0};
        uint32 styleMask{0};
        bool isDefault{false};
        bool active{true};
        uint32 sortOrder{0};
    };

    struct StyleObjectDefinition
    {
        uint8 styleId{0};
        uint8 minStage{0};
        uint8 objectIndex{0};
        uint32 gameobjectEntry{0};
        float offsetX{0.0f};
        float offsetY{0.0f};
        float offsetZ{0.0f};
        float orientationOffset{0.0f};
    };

    struct FurnitureItemDefinition
    {
        uint32 itemEntry{0};
        uint32 catalogId{0};
        uint8 spawnType{0}; // 0 = GO, 1 = Creature
        uint32 spawnEntry{0};
        uint32 displayId{0};
        float scale{1.0f};
        float collisionRadius{1.0f};
        float minDistance{1.5f};
        float orientationOffset{0.0f};
        uint8 requiredStage{0};
        uint32 styleMask{0};
        bool consumeOnPlace{true};
        bool active{true};
        uint32 sortOrder{0};
        std::string displayName;
    };

    struct SpawnedFurnitureRef
    {
        uint8 spawnType{0}; // 0 = GO, 1 = Creature
        ObjectGuid guid;
        float collisionRadius{1.0f};
    };

    struct Session
    {
        ObjectGuid::LowType ownerGuid{0};
        uint32 instanceId{0};
        uint32 mapId{0};
        uint8 styleId{1};
        uint8 stage{0};
        bool initialized{false};
        std::unordered_set<ObjectGuid> occupants;
        std::unordered_map<uint32, SpawnedFurnitureRef> spawnedFurniture;
        std::vector<ObjectGuid> spawnedStyleObjects;
        ObjectGuid stewardGuid;
    };

    struct PendingPlacement
    {
        ObjectGuid::LowType ownerGuid{0};
        uint32 catalogId{0};
        uint32 sourceItemEntry{0};
        std::string displayName;
        uint8 spawnType{0}; // 0 = GO, 1 = Creature
        uint32 spawnEntry{0};
        uint32 displayId{0};
        float scale{1.0f};
        float collisionRadius{1.0f};
        float minDistance{1.5f};
        float orientationOffset{0.0f};
        bool consumeOnPlace{false};
        bool temporarySpellGranted{false};
        std::time_t expiresAt{0};
    };

    PlayerHousingMgr() = default;

    void LoadConfig();
    bool LoadDefinitions();
    bool IsSupportedHousingMap(uint32 mapId) const;
    bool ResolveHousingMaps(std::string& reason);
    bool GetStyleMapId(uint8 styleId, uint32& mapId) const;

    bool ResolvePlayerGuid(std::string const& playerName, ObjectGuid::LowType& guidLow, std::string& normalizedName) const;
    bool GetHouseRecord(ObjectGuid::LowType ownerGuid, HouseRecord& outRecord) const;
    bool CreateStarterHouse(ObjectGuid::LowType ownerGuid, std::string& reason);
    void GrantStarterUnlocks(ObjectGuid::LowType ownerGuid, uint8 styleId);
    bool CanVisitHouse(Player const* visitor, HouseRecord const& house, std::string& reason) const;

    bool EnterHouseByOwnerGuid(Player* player, ObjectGuid::LowType ownerGuid, std::string& reason);
    bool EnsureSession(HouseRecord const& house, uint32 mapId, std::string& reason);
    bool InitializeSession(ObjectGuid::LowType ownerGuid, std::string& reason);
    void DespawnSessionObjects(Session& session, Map* map);
    bool SpawnStyleObject(Session& session, Map* map, StyleObjectDefinition const& objectDef, Position const& anchor);
    bool SpawnFurnitureObject(Session& session, Map* map, uint32 placementId, uint8 spawnType, uint32 spawnEntry, uint32 displayId, float scale, float collisionRadius, float x, float y, float z, float o);
    bool PlaceFurnitureAt(Player* player, uint32 catalogId, Position const& target, std::string& reason);
    bool PlaceFurnitureResolved(Player* player, uint32 catalogId, uint32 sourceItemEntry, uint8 spawnType, uint32 spawnEntry, uint32 displayId, float scale, float collisionRadius, float minDistance, float orientationOffset, bool consumeOnPlace, std::string const& displayName, Position const& target, std::string& reason);
    bool ValidatePlacementRequest(Player* player, uint32 catalogId, HouseRecord& house, CatalogDefinition& catalog, StageDefinition& stage, std::string& reason);
    bool ValidateItemPlacementRequest(Player* player, FurnitureItemDefinition const& itemDef, HouseRecord& house, StageDefinition& stage, std::string& reason);
    bool UnlockCatalogInternal(Player* player, uint32 catalogId, std::string& reason, bool chargePlayer);
    bool GetFurnitureItemDefinition(uint32 itemEntry, FurnitureItemDefinition& outDefinition) const;
    bool IsPlacementPointValid(Player* player, Map* map, Position const& center, StageDefinition const& stage, float minDistance, float collisionRadius, float& x, float& y, float& z, std::string& reason, uint32 ignorePlacementId = 0) const;

    bool EnsureOwnerEditingContext(Player* player, HouseRecord& house, Session& session, Map*& map, std::string& reason);
    bool IsCatalogAllowedForHouse(CatalogDefinition const& catalog, HouseRecord const& house) const;

    bool GetStyleDefinition(uint8 styleId, StyleDefinition& outStyle) const;
    bool GetStageDefinition(uint8 stage, StageDefinition& outStage) const;
    bool GetCatalogDefinition(uint32 catalogId, CatalogDefinition& outCatalog) const;
    bool GetHouseCenter(Map* map, uint8 styleId, Position& outCenter) const;
    bool ResolveSafeGroundPosition(Map* map, float seedX, float seedY, float seedZ, float orientation, Position& outPosition) const;
    float SnapToGround(Map* map, float x, float y, float z) const;

    uint32 GetPlacedFurnitureCount(ObjectGuid::LowType ownerGuid) const;
    bool IsCatalogUnlocked(ObjectGuid::LowType ownerGuid, uint32 catalogId) const;
    std::unordered_set<uint32> LoadUnlockedCatalogSet(ObjectGuid::LowType ownerGuid) const;
    uint32 GetNextPlacementId(ObjectGuid::LowType ownerGuid) const;

    void RemovePlayerTrackingLocked(ObjectGuid playerGuid, bool eraseReturnLocation);
    void RemovePlayerTracking(ObjectGuid playerGuid, Player* player, bool eraseReturnLocation, bool unbind);
    void ClearPendingPlacement(ObjectGuid playerGuid, Player* player);
    void CleanupExpiredPendingPlacement(Player* player);
    void RefreshSessionFromHouse(HouseRecord const& house);

    static uint32 GetStyleMaskFor(uint8 styleId);
    static std::string ToLower(std::string value);
    static std::string FormatMoney(uint64 copper);

private:
    mutable std::mutex _lock;

    bool _enabled{true};
    bool _autoProvisionOnLogin{true};
    bool _gmVisitBypass{true};
    bool _defaultPrivate{true};
    uint32 _defaultHousingMapId{169};
    std::unordered_set<uint32> _housingMapIds;
    uint32 _stewardEntry{900200};
    uint32 _stewardDisplayId{25384};
    std::string _defaultStyleCode{"human"};

    bool _definitionsLoaded{false};
    std::unordered_map<uint8, StyleDefinition> _stylesById;
    std::unordered_map<std::string, uint8> _styleIdByCode;
    std::unordered_map<uint8, StageDefinition> _stagesById;
    std::unordered_map<uint32, CatalogDefinition> _catalogById;
    std::unordered_map<uint32, FurnitureItemDefinition> _furnitureItemsByEntry;
    std::unordered_map<uint8, std::vector<uint32>> _styleStarterUnlocks;
    std::unordered_map<uint8, std::vector<StyleObjectDefinition>> _styleObjects;

    std::unordered_map<ObjectGuid::LowType, Session> _sessionsByOwner;
    std::unordered_map<uint32, ObjectGuid::LowType> _ownerByInstance;
    std::unordered_map<ObjectGuid, ObjectGuid::LowType> _playerOwnerByGuid;
    std::unordered_map<ObjectGuid, WorldLocation> _returnLocations;
    std::unordered_map<ObjectGuid, PendingPlacement> _pendingPlacementByPlayer;

    float _placementMaxSlopeDegrees{35.0f};
    float _placementSlopeSampleDistance{0.75f};
    float _placementDefaultMinDistance{1.5f};
    float _placementDefaultCollisionRadius{1.0f};
};

#define sPlayerHousingMgr PlayerHousingMgr::instance()

#endif
