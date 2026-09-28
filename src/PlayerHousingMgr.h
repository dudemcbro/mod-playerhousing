#ifndef MOD_PLAYERHOUSING_MGR_H
#define MOD_PLAYERHOUSING_MGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Position.h"

#include <array>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class GameObject;
class Item;
class Map;
class Player;
class WorldObject;
class WorldSession;
struct ItemTemplate;

namespace Housing
{
    // Items, objects and texts owned by the module (see sql/db_world).
    constexpr uint32 HOUSE_KEY_ITEM = 902000;
    constexpr uint32 KEY_SPELL = 18282;        // "Dummy Spell": lets the House Key be used
    constexpr uint32 HOOK_MARKER_GO = 903990;
    constexpr uint32 MANNEQUIN_ENTRY = 900201;  // the figure that shows a stand's gear
    // "Move a Piece" items, one per targeting circle size (tools/content/build_content.py):
    // handed out to move a piece with the circle, gone once used.
    constexpr uint32 MOVER_ITEM_FIRST = 901190;
    constexpr uint32 MOVER_ITEM_LAST = 901199;
    constexpr uint32 PLAYER_MENU_ID = 900300;  // gossip menu id for menus opened by .house

    constexpr uint32 TEXT_HOME = 900300;
    constexpr uint32 TEXT_COLLECTION = 900301;
    constexpr uint32 TEXT_PIECE = 900302;
    constexpr uint32 TEXT_VISIT = 900303;
    constexpr uint32 TEXT_SETTINGS = 900304;
    constexpr uint32 TEXT_STORAGE = 900305;
    constexpr uint32 TEXT_HELP = 900306;
    constexpr uint32 TEXT_HOOK = 900307;
    constexpr uint32 TEXT_GUESTS = 900308;
    constexpr uint32 TEXT_NEARBY = 900309;
    constexpr uint32 TEXT_STAND = 900310;

    enum PieceKind : uint8
    {
        PIECE_FURNISHING = 0,
        PIECE_BUILDING = 1
    };

    enum PieceFlags : uint32
    {
        PIECE_FLAG_SURFACE = 0x01,        // other pieces can be put on top
        PIECE_FLAG_SMALL = 0x02,          // fits on a surface
        PIECE_FLAG_PER_CHARACTER = 0x04,  // unlock belongs to the character, not the account
        PIECE_FLAG_GIFT = 0x08,           // given on first login
        PIECE_FLAG_WRECKAGE = 0x10,       // standing on the island at the first visit
        PIECE_FLAG_STAND = 0x20           // a mannequin that wears real gear from the bags
    };

    enum Category : uint8
    {
        CATEGORY_STARTER = 0,
        CATEGORY_BUILDINGS,
        CATEGORY_EXPLORATION,
        CATEGORY_DUNGEONS,
        CATEGORY_RAIDS,
        CATEGORY_REPUTATION,
        CATEGORY_PROFESSIONS,
        CATEGORY_HOLIDAYS,
        CATEGORY_CAPSTONES,
        CATEGORY_COUNT
    };

    enum RuleType : uint8
    {
        RULE_LEVEL = 1,        // param1 level
        RULE_ACHIEVEMENT = 2,  // param1 achievement id
        RULE_REPUTATION = 3,   // param1 faction id, param2 rank (7 = Exalted)
        RULE_QUEST = 4,        // param1 quest id (rewarded)
        RULE_KILL = 5,         // param1 creature entry (killer and group members nearby)
        RULE_EXPLORE = 6,      // param1 area or zone id
        RULE_SKILL = 7,        // param1 skill id, param2 value
        RULE_NEVER = 8         // only through a GM or UnlockAll
    };

    enum Privacy : uint8
    {
        PRIVACY_PUBLIC = 0,
        PRIVACY_PRIVATE = 1,
        PRIVACY_FRIENDS = 2
    };

    enum HouseFlags : uint32
    {
        HOUSE_FLAG_WRECKAGE_PLACED = 0x01,
        HOUSE_FLAG_HALL_NOTICE = 0x02,     // things from the old guild hall went to storage
        HOUSE_FLAG_LAYOUT_COPYABLE = 0x04  // visitors may save a copy of the layout
    };

    enum CharacterFlags : uint32
    {
        CHAR_FLAG_KEY_GIVEN = 0x01,
        CHAR_FLAG_VETERAN_DONE = 0x02,
        CHAR_FLAG_GREETED = 0x04,
        CHAR_FLAG_ADJUST_ALL = 0x08,     // adjust menu after placing anything
        CHAR_FLAG_ADJUST_NEVER = 0x10,   // never; neither flag: after placing buildings
        CHAR_FLAG_UNLOCKED_ONLY = 0x20   // the Collection lists only unlocked pieces
    };

    // When the piece menu opens by itself right after placing.
    enum AdjustMode : uint8
    {
        ADJUST_BUILDINGS = 0,
        ADJUST_ALL = 1,
        ADJUST_NEVER = 2
    };

    enum Tip : uint32
    {
        TIP_FIRST_PLACE = 0x01,
        TIP_FIRST_UNLOCK = 0x02,
        TIP_FIRST_STORAGE = 0x04,
        TIP_LIMIT = 0x08,
        TIP_DECORATE = 0x10
    };

    enum MenuSourceType : uint8
    {
        SOURCE_PLAYER = 0,
        SOURCE_ITEM,
        SOURCE_CREATURE,
        SOURCE_GAMEOBJECT
    };

    struct MenuSource
    {
        MenuSourceType type{SOURCE_PLAYER};
        ObjectGuid guid;
    };

    // Rules in the same group must all be met; any complete group unlocks the piece (so a
    // raid trophy can come from the 10-player or the 25-player achievement).
    struct PieceRule
    {
        uint8 group{0};
        uint8 type{0};
        uint32 param1{0};
        uint32 param2{0};
    };

    struct PieceDefinition
    {
        uint32 itemEntry{0};
        uint8 kind{PIECE_FURNISHING};
        uint8 category{CATEGORY_STARTER};
        std::string name;
        uint32 goEntry{0};
        uint32 editGoEntry{0};
        float scale{1.0f};
        float footprint{1.0f};
        float height{1.0f};
        uint32 flags{0};
        uint32 copyCost{0};
        uint32 sortOrder{0};
        std::string hint;
        uint32 legacyCatalogId{0};
        // The rectangle on the ground in the piece's own frame (x forward): inside a building.
        float outlineMinX{0.0f};
        float outlineMinY{0.0f};
        float outlineMaxX{0.0f};
        float outlineMaxY{0.0f};
        std::vector<PieceRule> rules;

        bool IsBuilding() const { return kind == PIECE_BUILDING; }
        bool HasOutline() const { return outlineMaxX > outlineMinX && outlineMaxY > outlineMinY; }
        bool HasFlag(uint32 flag) const { return (flags & flag) != 0; }
    };

    struct LayoutDefinition
    {
        std::string code;
        uint32 mapId{1};
        Position landing;
        float stewardOffsetX{7.0f};
        float stewardOffsetY{2.0f};
        float centerX{0.0f};
        float centerY{0.0f};
        float radius{250.0f};
    };

    // An item on a stand. It keeps its own guid (and so its enchants and gems) while it's
    // there: it leaves the owner's inventory but stays in item_instance, like mail.
    struct GearItem
    {
        uint32 itemGuid{0};
        uint32 itemEntry{0};
    };

    struct Placement
    {
        uint32 id{0};
        uint32 itemEntry{0};
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float o{0.0f};
        float scale{1.0f};
        float pitch{0.0f};                // tilt, radians: + tips its front down
        float roll{0.0f};                 // tilt, radians: + leans it to its right
        uint32 look{0};                   // stands: race | gender << 8
        uint32 parent{0};                 // the surface it stands on: it moves with it
        std::map<uint8, GearItem> gear;   // stands: equipment slot -> item
    };

    // One undoable step: every placement it touched, as it was before and after.
    struct Change
    {
        uint32 placementId{0};
        std::optional<Placement> before;
        std::optional<Placement> after;
    };

    struct JournalEntry
    {
        std::string label;
        std::vector<Change> changes;
    };

    struct HouseRecord
    {
        ObjectGuid::LowType ownerGuid{0};
        uint8 privacy{PRIVACY_PRIVATE};
        uint32 flags{0};
        std::string greeting;
    };

    struct SavedLayout
    {
        uint32 id{0};
        std::string name;
        std::string source;   // who it came from, when sent or copied
        std::string savedAt;  // YYYY-MM-DD
        uint32 pieces{0};
    };

    struct VisitEntry
    {
        ObjectGuid::LowType ownerGuid{0};
        std::string ownerName;
    };
}

class PlayerHousingMgr
{
public:
    static PlayerHousingMgr* instance();

    // ---- lifecycle and player hooks (PlayerHousingMgr.cpp)
    void OnStartup();
    void OnPlayerLogin(Player* player);
    void OnPlayerLogout(Player* player);
    void OnPlayerUpdate(Player* player, uint32 diffMs);
    void OnPlayerMapChanged(Player* player);
    void OnPlayerDelete(ObjectGuid guid);
    void OnBeforeSetPhaseMask(uint32 oldPhaseMask, uint32 newPhaseMask, bool& useCombinedPhases) const;

    bool IsEnabled() const { return _enabled; }
    bool IsFreeMode() const { return _freeMode; }
    bool IsUnlockAll() const { return _unlockAll; }
    uint32 GetStewardEntry() const { return _stewardEntry; }
    uint32 GetStewardDisplayId() const { return _stewardDisplayId; }
    bool IsStewardEntry(uint32 entry) const { return _enabled && entry == _stewardEntry; }
    static bool IsHousingPhase(uint32 phaseMask);

    // ---- travel (PlayerHousingMgr.cpp)
    bool EnterOwnHouse(Player* player, std::string& reason);
    bool VisitHouse(Player* player, ObjectGuid::LowType ownerGuid, std::string& reason);
    bool VisitHouseByName(Player* player, std::string const& ownerName, std::string& reason);
    bool RequestGoHome(Player* player, std::string& reason);
    bool LeaveHouse(Player* player, std::string& reason);
    bool Unstuck(Player* player, std::string& reason);
    bool GiveHouseKey(Player* player, std::string& reason);

    // Where the player stands: 0 when not on anyone's island.
    ObjectGuid::LowType GetIslandOwner(Player const* player) const;
    bool IsOnOwnIsland(Player const* player) const;
    bool IsDecorating(Player const* player) const;

    // ---- pieces and editing (HousingPieces.cpp)
    Housing::PieceDefinition const* GetPiece(uint32 itemEntry) const;
    std::vector<Housing::PieceDefinition const*> GetPiecesInCategory(uint8 category) const;
    bool HandlePlacementCast(Player* player, Item* castItem, Position const& target, std::string& reason);
    bool SetDecorating(Player* player, bool on, std::string& reason);
    bool PickUp(Player* player, uint32 placementId, bool withInside, std::string& reason);
    bool Rotate(Player* player, uint32 placementId, float degrees, std::string& reason);
    bool Nudge(Player* player, uint32 placementId, float forward, float left, float up, std::string& reason);
    bool FaceMe(Player* player, uint32 placementId, std::string& reason);
    bool MoveHere(Player* player, uint32 placementId, std::string& reason);
    // Percent of the piece's normal size, or (relative) percentage points more or less.
    bool Resize(Player* player, uint32 placementId, float percent, bool relative, std::string& reason);
    // Degrees; straighten sets both back to level.
    bool Tilt(Player* player, uint32 placementId, float forwardDegrees, float rightDegrees, bool straighten, std::string& reason);
    bool PlaceAnother(Player* player, uint32 placementId, std::string& reason);
    uint32 GetPendingCopy(Player const* player) const;
    // Grid snapping, in yards (0: off).
    float GetGridSize(ObjectGuid::LowType guid) const;
    void SetGridSize(Player* player, float yards, std::string& reason) const;
    float GetMinSize() const { return _sizeMin; }
    float GetMaxSize() const { return _sizeMax; }
    float GetMaxTilt() const { return _tiltMax; }
    bool PlaceOnHook(Player* player, uint32 surfacePlacementId, uint32 itemEntry, std::string& reason);
    bool StartMove(Player* player, uint32 placementId, std::string& reason);
    bool HandleMoveCast(Player* player, Item* castItem, Position const& target, std::string& reason);
    void CancelMove(Player* player);
    uint32 GetPendingMover(Player const* player) const;
    static bool IsMoverItem(uint32 itemEntry) { return itemEntry >= Housing::MOVER_ITEM_FIRST && itemEntry <= Housing::MOVER_ITEM_LAST; }
    bool PackUpEverything(Player* player, std::string& reason);
    bool Undo(Player* player, std::string& reason);
    bool Redo(Player* player, std::string& reason);
    std::string UndoLabel(Player const* player) const;
    std::string RedoLabel(Player const* player) const;
    uint32 GetSelectedPlacement(Player const* player) const;
    uint32 ResolvePlacementArgument(Player* player, uint32 placementId) const;
    std::optional<Housing::Placement> GetPlacement(Player const* player, uint32 placementId) const;
    uint32 GetPlacementForObject(Player const* player, ObjectGuid const& guid) const;
    uint32 GetSurfaceForMarker(Player const* player, ObjectGuid const& guid) const;
    std::vector<std::pair<Housing::Placement, float>> GetNearbyPlacements(Player const* player, float range) const;
    std::vector<Housing::Placement> GetPiecesInside(ObjectGuid::LowType ownerGuid, uint32 buildingPlacementId) const;
    void CountPlaced(ObjectGuid::LowType ownerGuid, uint32& furnishings, uint32& buildings) const;
    uint32 GetMaxFurnishings() const { return _maxFurnishings; }
    uint32 GetMaxBuildings() const { return _maxBuildings; }
    std::map<uint32, uint32> GetStorage(ObjectGuid::LowType ownerGuid) const;
    bool TakeFromStorage(Player* player, uint32 itemEntry, bool all, std::string& reason);
    std::string CountsText(ObjectGuid::LowType ownerGuid) const;
    void SelectPlacement(Player const* player, uint32 placementId);
    ObjectGuid GetObjectForPlacement(Player const* player, uint32 placementId) const;
    void ProcessPendingConsumes(Player* player);

    // ---- saved layouts (HousingLayouts.cpp)
    std::vector<Housing::SavedLayout> GetSavedLayouts(ObjectGuid::LowType ownerGuid) const;
    std::optional<Housing::SavedLayout> GetSavedLayout(ObjectGuid::LowType ownerGuid, uint32 layoutId) const;
    std::optional<Housing::SavedLayout> FindSavedLayout(ObjectGuid::LowType ownerGuid, std::string const& nameOrNumber) const;
    uint32 GetMaxSavedLayouts() const { return _maxSavedLayouts; }
    // layoutId 0: a new layout with this name; otherwise save the island over that one.
    bool SaveLayout(Player* player, uint32 layoutId, std::string const& name, std::string& reason);
    bool RenameLayout(Player* player, uint32 layoutId, std::string const& name, std::string& reason);
    bool DeleteLayout(Player* player, uint32 layoutId, std::string& reason);
    bool SwitchLayout(Player* player, uint32 layoutId, std::string& reason);
    // Pieces the layout needs that the player doesn't have anywhere: item entry -> how many.
    std::map<uint32, uint32> LayoutShortfall(Player* player, uint32 layoutId) const;
    void DescribeShortfall(Player* player, std::map<uint32, uint32> const& missing, uint32& gettable, uint64& cost, uint32& locked) const;
    bool GetMissingForLayout(Player* player, uint32 layoutId, std::string& reason);
    bool IsLayoutCopyable(ObjectGuid::LowType ownerGuid) const;
    void SetLayoutCopyable(Player* player, bool copyable, std::string& reason) const;
    bool CopyIslandLayout(Player* visitor, std::string& reason);
    bool SendLayout(Player* player, uint32 layoutId, std::string const& recipientName, std::string& reason);

    // ---- stands (HousingStands.cpp)
    static int8 StandSlotFor(ItemTemplate const* proto, std::map<uint8, Housing::GearItem> const& worn);
    static char const* StandSlotName(uint8 slot);
    static std::string StandItemName(uint32 itemEntry);
    static std::string LookName(uint32 look);
    std::vector<Item*> GetWearableItems(Player* player) const;
    bool PutOnStand(Player* player, uint32 placementId, uint32 itemGuid, std::string& reason);
    bool TakeOffStand(Player* player, uint32 placementId, int32 slot, std::string& reason);
    bool ChangeStandFigure(Player* player, uint32 placementId, std::string& reason);
    bool SendMannequinLook(WorldSession* session, ObjectGuid const& guid) const;

    // ---- collection (HousingCollection.cpp)
    bool IsUnlocked(Player const* player, Housing::PieceDefinition const& piece, std::set<uint32> const* known = nullptr) const;
    std::set<uint32> LoadUnlocks(Player const* player) const;
    std::string DescribeProgress(Player const* player, Housing::PieceDefinition const& piece) const;
    void CollectionCounts(Player const* player, int32 category, uint32& unlocked, uint32& total, std::set<uint32> const* known = nullptr) const;
    bool GetCopy(Player* player, uint32 itemEntry, std::string& reason);
    bool GetCopies(Player* player, uint32 itemEntry, uint32 count, std::string& reason);
    // Unlocked since the player last saw them listed.
    std::set<uint32> LoadNewUnlocks(Player const* player) const;
    void MarkSeen(Player const* player, std::vector<uint32> const& itemEntries) const;
    std::vector<Housing::PieceDefinition const*> SearchPieces(std::string const& text) const;
    bool IsCollectionUnlockedOnly(ObjectGuid::LowType guid) const;
    void SetCollectionUnlockedOnly(Player* player, bool unlockedOnly) const;
    uint32 CountPlacedOf(ObjectGuid::LowType ownerGuid, uint32 itemEntry) const;
    bool GetOneOfEverything(Player* player, std::string& reason);
    void EvaluateUnlocks(Player* player, uint8 ruleType, uint32 param, bool announce, uint32 value = 0);
    uint32 CreditPastProgress(Player* player);
    bool GmUnlock(Player* target, std::string const& what, bool unlock, std::string& reason);
    void OnCreatureKilled(Player* killer, uint32 creatureEntry);

    // ---- people (HousingPeople.cpp)
    bool GetHouseRecord(ObjectGuid::LowType ownerGuid, Housing::HouseRecord& outRecord) const;
    bool EnsureHouse(ObjectGuid::LowType ownerGuid) const;
    bool CanVisit(Player const* visitor, Housing::HouseRecord const& house, std::string& reason) const;
    bool SetPrivacy(Player* player, uint8 privacy, std::string& reason);
    bool CyclePrivacy(Player* player, std::string& reason);
    bool InviteGuest(Player* player, ObjectGuid::LowType guestGuid, std::string& reason);
    bool InviteGuestByName(Player* player, std::string const& name, std::string& reason);
    bool InviteTarget(Player* player, std::string& reason);
    bool InviteParty(Player* player, std::string& reason);
    bool RemoveGuest(Player* player, ObjectGuid::LowType guestGuid, std::string& reason);
    bool RemoveGuestByName(Player* player, std::string const& name, std::string& reason);
    std::vector<Housing::VisitEntry> GetGuests(ObjectGuid::LowType ownerGuid) const;
    std::vector<Housing::VisitEntry> GetVisitList(Player const* player, uint8 list) const;
    bool SetGreeting(Player* player, std::string const& greeting, std::string& reason);
    static char const* PrivacyName(uint8 privacy);

    // ---- misc helpers shared by the scripts
    void Say(Player* player, std::string const& text) const;
    void Tip(Player* player, uint32 tip, std::string const& text);
    void SendAddonState(Player* player) const;
    uint8 GetAdjustMode(ObjectGuid::LowType guid) const;
    void SetAdjustMode(Player* player, uint8 mode, std::string& reason) const;
    bool ShouldAdjustAfterPlacing(Player const* player, uint32 itemEntry) const;
    static char const* AdjustModeName(uint8 mode);
    bool ResolvePlayerGuid(std::string const& playerName, ObjectGuid::LowType& guidLow, std::string& normalizedName) const;
    std::string NameOf(ObjectGuid::LowType guid) const;
    static std::string FormatMoney(uint64 copper);
    static std::string FormatYards(float yards);
    static char const* CategoryName(uint8 category);

private:
    struct SpawnedPiece
    {
        ObjectGuid guid;
        bool editCopy{false};
    };

    // One per occupied island. Islands share the spot on an open-world map and are kept apart
    // by giving each owner an exact phase of their own (see IsHousingPhase).
    struct Session
    {
        ObjectGuid::LowType ownerGuid{0};
        uint32 phaseMask{0};
        uint32 mapId{0};
        bool initialized{false};
        bool decorating{false};
        uint32 nextPlacementId{1};
        uint32 selected{0};
        std::unordered_set<ObjectGuid> occupants;
        std::map<uint32, Housing::Placement> placements;
        std::unordered_map<uint32, SpawnedPiece> spawned;
        std::unordered_map<uint32, ObjectGuid> markers;  // surface placement id -> hook marker
        ObjectGuid stewardGuid;
    };

    struct Journal
    {
        std::deque<Housing::JournalEntry> undo;
        std::deque<Housing::JournalEntry> redo;
    };

    // What the last batch of changes did with items, for the message that follows.
    struct ApplyReport
    {
        uint32 toBags{0};
        uint32 toStorage{0};
        uint32 placed{0};
        uint32 gearToBags{0};
        uint32 gearMailed{0};
        std::vector<std::string> gearMissing;  // couldn't go back on a stand: no longer in the bags
    };

    // What a mannequin wears, for the mirror image data its viewers ask for.
    struct MannequinLook
    {
        uint32 displayId{0};
        uint8 race{1};
        uint8 gender{0};
        std::array<uint32, 11> displays{};
    };

    struct PendingTrip
    {
        std::time_t at{0};
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    PlayerHousingMgr() = default;

    // PlayerHousingMgr.cpp
    void LoadConfig();
    bool LoadDefinitions();
    void ConvertLegacyData();
    bool EnterHouse(Player* player, ObjectGuid::LowType ownerGuid, std::string& reason);
    bool EnsureSession(ObjectGuid::LowType ownerGuid);
    bool InitializeSession(ObjectGuid::LowType ownerGuid, std::string& reason);
    void DespawnSessionObjects(Session& session, Map* map);
    void EndSessionIfEmpty(ObjectGuid::LowType ownerGuid);
    ObjectGuid::LowType RemovePlayerTracking(ObjectGuid playerGuid, bool eraseReturnLocation);
    bool TryAdmitGroupBot(Player* bot);
    void ApplyHousePhase(Player* player, uint32 phaseMask) const;
    void RestoreNormalPhase(Player* player) const;
    bool IsInHousingArea(WorldObject const* object) const;
    bool IsOnIslandGround(float x, float y) const;
    Map* GetHousingMap() const;
    void OnArrived(Player* player, ObjectGuid::LowType ownerGuid);
    void GiveFirstLoginItems(Player* player);
    uint32 GetCharacterFlags(ObjectGuid::LowType guid, uint32* tips = nullptr) const;
    void SetCharacterFlag(ObjectGuid::LowType guid, uint32 flag, bool tip) const;
    void UpdatePendingTrip(Player* player);

    // HousingPieces.cpp
    Session* GetOwnerSession(Player* player, std::string& reason);
    Session const* FindSessionOf(Player const* player) const;
    bool SpawnPlacement(Session& session, Map* map, Housing::Placement const& placement);
    bool SpawnStand(Session& session, Map* map, Housing::Placement const& placement);
    void RemoveSpawned(Map* map, ObjectGuid const& guid);
    void DespawnPlacement(Session& session, Map* map, uint32 placementId);
    void RespawnPlacement(Session& session, Map* map, uint32 placementId);
    void SpawnMarkers(Session& session, Map* map);
    void DespawnMarkers(Session& session, Map* map);
    void SpawnSteward(Session& session, Map* map);
    void PlaceStarterWreckage(Session& session, Map* map);
    bool CheckLimit(Session const& session, Housing::PieceDefinition const& piece, std::string& reason) const;
    bool IsSpotOnIsland(float x, float y, float z) const;
    bool ReturnItem(Player* player, uint32 itemEntry, bool& toStorage);
    bool TakeItem(Player* player, uint32 itemEntry);
    void AddToStorage(ObjectGuid::LowType ownerGuid, uint32 itemEntry, int32 delta) const;
    bool ApplyChanges(Player* player, Session& session, std::vector<Housing::Change> const& changes, bool towardsAfter, std::string& reason);
    bool ApplyState(Player* player, Session& session, Map* map, uint32 placementId, std::optional<Housing::Placement> const& target, std::string& reason);
    void Record(Player* player, std::string const& label, std::vector<Housing::Change> changes);
    bool Transform(Player* player, uint32 placementId, std::string const& label, float dx, float dy, float dz, float dO, bool absoluteO, float o, std::string& reason);
    // Checks the island, applies the changes and records them as one step; the first change
    // is the piece the label names.
    bool Commit(Player* player, Session& session, std::string const& label, std::vector<Housing::Change> changes, std::string& reason);
    void SnapToGrid(ObjectGuid::LowType guid, float& x, float& y) const;
    // Pieces that go wherever this one goes: what stands on it, and for a building (when
    // includeInside) what's inside it; each with what stands on them in turn.
    std::vector<uint32> CarriedBy(Session const& session, uint32 placementId, bool includeInside) const;
    uint32 FindSurfaceUnder(Session const& session, float x, float y, float z, std::set<uint32> const& exclude = {}) const;
    bool ChangeStand(Player* player, uint32 placementId, Housing::Placement const& after, std::string const& label, std::string& reason);
    bool MoveGearToStand(Player* player, ObjectGuid::LowType ownerGuid, uint32 placementId, uint8 slot, uint32 itemGuid, std::string& reason);
    void ReturnGear(Player* player, ObjectGuid::LowType ownerGuid, uint32 placementId, uint8 slot, Housing::GearItem const& gear);
    void LoadGear(ObjectGuid::LowType ownerGuid, std::map<uint32, Housing::Placement>& placements) const;
    void SavePlacement(ObjectGuid::LowType ownerGuid, Housing::Placement const& placement, uint32 mapId) const;
    void DeletePlacement(ObjectGuid::LowType ownerGuid, uint32 placementId) const;
    std::string PieceName(uint32 itemEntry) const;
    std::string DescribeReturns() const;
    std::string DescribeItemReturns() const;


    // HousingLayouts.cpp
    std::vector<Housing::Placement> LoadSavedPieces(ObjectGuid::LowType ownerGuid, uint32 layoutId) const;
    // fromLayout 0 copies fromOwner's island as it is now.
    void WriteLayout(ObjectGuid::LowType ownerGuid, uint32 layoutId, std::string const& name, std::string const& source,
        ObjectGuid::LowType fromOwner, uint32 fromLayout) const;
    uint32 NextLayoutId(ObjectGuid::LowType ownerGuid) const;

    // HousingCollection.cpp
    bool RuleMet(Player const* player, Housing::PieceRule const& rule) const;
    // Whether any rule group of the piece is complete; `triggered` marks rules the current
    // event satisfies by itself.
    template <typename Triggered>
    bool AnyGroupMet(Player const* player, Housing::PieceDefinition const& piece, Triggered triggered) const;
    bool Unlock(Player* player, Housing::PieceDefinition const& piece, bool announce);
    static bool IsAreaExplored(Player const* player, uint32 areaId);

    mutable std::recursive_mutex _lock;

    bool _enabled{true};
    bool _freeMode{false};
    bool _unlockAll{false};
    bool _gmVisitBypass{false};
    uint8 _defaultPrivacy{Housing::PRIVACY_PRIVATE};
    uint32 _stewardEntry{900200};
    uint32 _stewardDisplayId{25384};
    uint32 _maxFurnishings{200};
    uint32 _maxBuildings{10};
    uint32 _keyDelaySeconds{5};
    float _sizeMin{0.5f};   // times the piece's normal size
    float _sizeMax{2.0f};
    float _tiltMax{45.0f};  // degrees either way
    uint32 _maxSavedLayouts{5};
    std::string _layoutCode{"cleared"};

    Housing::LayoutDefinition _layout;
    std::map<uint32, Housing::PieceDefinition> _pieces;
    std::map<std::pair<uint8, uint32>, std::vector<uint32>> _piecesByRule;  // (rule type, param1) -> pieces

    std::unordered_map<ObjectGuid::LowType, Session> _sessionsByOwner;
    std::unordered_map<ObjectGuid, ObjectGuid::LowType> _playerOwnerByGuid;
    std::unordered_map<ObjectGuid, WorldLocation> _returnLocations;
    std::unordered_map<ObjectGuid::LowType, Journal> _journals;
    std::unordered_map<ObjectGuid, PendingTrip> _pendingTrips;
    std::unordered_set<ObjectGuid> _arrivals;  // teleported onto an island, greeting not shown yet
    // Items used to place pieces; removed on the player's next update because the cast that
    // placed them still holds the item.
    std::unordered_map<ObjectGuid, std::map<uint32, uint32>> _pendingConsumes;
    std::unordered_map<ObjectGuid, MannequinLook> _mannequins;

    struct PendingMove
    {
        uint32 placementId{0};
        uint32 moverItem{0};
    };
    std::unordered_map<ObjectGuid, PendingMove> _pendingMoves;
    std::map<uint32, uint32> _moverBySpell;  // circle spell -> the "Move a Piece" item using it

    // "Place another like this": the next one placed takes the original's turn, size and tilt.
    struct PendingCopy
    {
        uint32 itemEntry{0};
        float o{0.0f};
        float scale{1.0f};
        float pitch{0.0f};
        float roll{0.0f};
    };
    std::unordered_map<ObjectGuid, PendingCopy> _pendingCopies;
    ApplyReport _report;
};

#define sPlayerHousingMgr PlayerHousingMgr::instance()

#endif
