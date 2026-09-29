#include "PlayerHousingMgr.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

using namespace Housing;

// Working with several pieces at once: a group selected with Ctrl-click moves, turns, lines
// up and is picked up together; rows of copies; saved sets set down with the circle; undo
// history; walking over to a piece; and the GM's photo tour for the addon's pictures.

namespace
{
    constexpr float PI_F = 3.14159265358979323846f;
    constexpr float TWO_PI_F = 6.28318530717958647692f;
    constexpr size_t MAX_GROUP = 25;        // pieces selected at once
    constexpr uint32 MAX_ROW = 20;          // copies in one row
    constexpr size_t MAX_SET_PIECES = 50;
    constexpr size_t SET_NAME_MAX = 40;
    constexpr uint32 MAX_UNDO_STEPS = 20;   // undone in one go
    constexpr size_t MAX_UNDO_PIECES = 300; // pieces those steps touch, past the first step

    float NormalizeAngle(float angle)
    {
        angle = std::fmod(angle, TWO_PI_F);
        return angle < 0.0f ? angle + TWO_PI_F : angle;
    }

    std::string CleanName(std::string const& name)
    {
        std::string clean;
        for (char c : name)
        {
            unsigned char u = static_cast<unsigned char>(c);
            if (u < 32 || c == '|' || c == 127)
                continue;
            clean += c;
        }
        size_t begin = clean.find_first_not_of(' ');
        if (begin == std::string::npos)
            return "";
        clean = clean.substr(begin, clean.find_last_not_of(' ') - begin + 1);
        PlayerHousingMgr::TruncateUtf8(clean, SET_NAME_MAX);
        return clean;
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    }

    std::string Pieces(size_t count)
    {
        return Acore::StringFormat("{} {}", count, count == 1 ? "piece" : "pieces");
    }

    // A piece's saved place in a set, relative to the set's first piece.
    struct SetPiece
    {
        uint32 itemEntry{0};
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float o{0.0f};
        float scale{1.0f};
        float pitch{0.0f};
        float roll{0.0f};
        uint32 look{0};
        int32 parent{-1};  // index of the piece it stands on in the set
    };

    std::vector<SetPiece> LoadSet(ObjectGuid::LowType ownerGuid, uint32 setId)
    {
        std::vector<SetPiece> pieces;
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT item_entry, pos_x, pos_y, pos_z, orientation, scale, pitch, roll, look, parent_index "
                "FROM mod_playerhousing_set_piece WHERE owner_guid={} AND set_id={} ORDER BY piece_index", ownerGuid, setId))
        {
            do
            {
                Field* fields = result->Fetch();
                SetPiece piece;
                piece.itemEntry = fields[0].Get<uint32>();
                piece.x = fields[1].Get<float>();
                piece.y = fields[2].Get<float>();
                piece.z = fields[3].Get<float>();
                piece.o = fields[4].Get<float>();
                piece.scale = fields[5].Get<float>();
                piece.pitch = fields[6].Get<float>();
                piece.roll = fields[7].Get<float>();
                piece.look = fields[8].Get<uint32>();
                piece.parent = int32(fields[9].Get<uint32>()) - 1;
                pieces.push_back(piece);
            } while (result->NextRow());
        }
        return pieces;
    }
}

// ---------------------------------------------------------------------------------------------
// The group

std::vector<uint32> PlayerHousingMgr::GetGroup(Player const* player) const
{
    std::vector<uint32> members;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session const* session = FindSessionOf(player);
    if (!session || !CanDecorate(player))
        return members;

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    auto selectedItr = session->selected.find(self);
    if (selectedItr != session->selected.end() && session->placements.count(selectedItr->second))
        members.push_back(selectedItr->second);
    auto groupItr = session->groups.find(self);
    if (groupItr != session->groups.end())
        for (uint32 id : groupItr->second)
            if (session->placements.count(id) && std::find(members.begin(), members.end(), id) == members.end())
                members.push_back(id);
    return members;
}

bool PlayerHousingMgr::SetGroupMember(Player* player, uint32 placementId, bool member, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;
    if (!session->placements.count(placementId))
    {
        reason = "No piece with that number here.";
        return false;
    }

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    uint32& primary = session->selected[self];
    if (!session->placements.count(primary))
        primary = 0;
    std::vector<uint32>& extras = session->groups[self];
    extras.erase(std::remove_if(extras.begin(), extras.end(),
        [&](uint32 id) { return id == primary || !session->placements.count(id); }), extras.end());
    if (!primary && !extras.empty())
    {
        primary = extras.front();
        extras.erase(extras.begin());
    }
    auto extra = std::find(extras.begin(), extras.end(), placementId);

    if (!member)
    {
        // The selected piece leaving hands its place to the next one.
        if (placementId == primary)
        {
            primary = extras.empty() ? 0 : extras.front();
            if (!extras.empty())
                extras.erase(extras.begin());
        }
        else if (extra != extras.end())
            extras.erase(extra);
    }
    else if (placementId != primary && extra == extras.end())
    {
        if (!primary)
            primary = placementId;
        else if (extras.size() + 1 >= MAX_GROUP)
        {
            reason = Acore::StringFormat("At most {} pieces at once.", MAX_GROUP);
            return false;
        }
        else
            extras.push_back(placementId);
    }
    if (extras.empty())
        session->groups.erase(self);

    size_t count = GetGroup(player).size();
    reason = count > 1 ? Acore::StringFormat("{} selected: they move together.", Pieces(count)) : "";
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::ToggleGroupMember(Player* player, uint32 placementId, std::string& reason)
{
    std::vector<uint32> group = GetGroup(player);
    bool member = std::find(group.begin(), group.end(), placementId) != group.end();
    return SetGroupMember(player, placementId, !member, reason);
}

void PlayerHousingMgr::ClearGroup(Player* player)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto playerItr = _playerOwnerByGuid.find(player->GetGUID());
    if (playerItr == _playerOwnerByGuid.end())
        return;
    auto sessionItr = _sessionsByOwner.find(playerItr->second);
    if (sessionItr != _sessionsByOwner.end())
        sessionItr->second.groups.erase(player->GetGUID().GetCounter());
    SendAddonState(player);
}

void PlayerHousingMgr::SetGroupHold(Player* player, bool held)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    if (held)
        _groupHold.insert(player->GetGUID());
    else
        _groupHold.erase(player->GetGUID());
}

bool PlayerHousingMgr::IsGroupHold(Player const* player) const
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    return _groupHold.count(player->GetGUID()) > 0;
}

void PlayerHousingMgr::SelectOne(Session& session, ObjectGuid::LowType self, uint32 placementId)
{
    auto selected = session.selected.find(self);
    if (selected == session.selected.end() || selected->second != placementId)
        session.groups.erase(self);
    session.selected[self] = placementId;
}

bool PlayerHousingMgr::KeepGroupAfterStep(Session& session, ObjectGuid::LowType self, uint32 selectedBefore,
    std::vector<uint32> const& groupBefore, std::vector<Change> const& changes)
{
    bool aboutGroup = false;
    if (!groupBefore.empty() && session.placements.count(selectedBefore))
        for (Change const& change : changes)
            if (change.placementId == selectedBefore
                    || std::find(groupBefore.begin(), groupBefore.end(), change.placementId) != groupBefore.end())
            {
                aboutGroup = true;
                break;
            }
    if (!aboutGroup)
    {
        session.groups.erase(self);
        return false;
    }

    // Applying the step selected each piece in turn: back to the group as it was.
    session.selected[self] = selectedBefore;
    std::vector<uint32> extras;
    for (uint32 id : groupBefore)
        if (id != selectedBefore && session.placements.count(id))
            extras.push_back(id);
    if (extras.empty())
        session.groups.erase(self);
    else
        session.groups[self] = extras;
    return true;
}

void PlayerHousingMgr::GroupRoots(Session const& session, std::vector<uint32> const& members, std::vector<uint32>& roots,
    std::vector<uint32>& carried) const
{
    std::set<uint32> carriedSet;
    for (uint32 member : members)
        for (uint32 id : CarriedBy(session, member, true))
            if (carriedSet.insert(id).second)
                carried.push_back(id);

    std::set<uint32> seen;
    for (uint32 member : members)
        if (!carriedSet.count(member) && seen.insert(member).second)
            roots.push_back(member);
}

bool PlayerHousingMgr::CommitGroup(Player* player, Session& session, std::vector<uint32> const& members, std::string const& label,
    std::function<void(Placement const& before, Placement& after)> const& move, std::string& reason, bool merge)
{
    std::vector<uint32> roots;
    std::vector<uint32> carried;
    GroupRoots(session, members, roots, carried);
    if (roots.empty())
    {
        reason = "Select some pieces first.";
        return false;
    }

    std::set<uint32> moving(roots.begin(), roots.end());
    moving.insert(carried.begin(), carried.end());

    std::vector<Change> changes;
    std::set<uint32> done;
    for (uint32 root : roots)
    {
        Placement const& before = session.placements[root];
        Placement after = before;
        move(before, after);
        after.o = NormalizeAngle(after.o);
        PieceDefinition const* piece = GetPiece(before.itemEntry);
        bool moved = after.x != before.x || after.y != before.y || after.z != before.z;
        // Onto a table, or off one.
        if (moved && piece && !piece->IsBuilding())
            after.parent = FindSurfaceUnder(session, after.x, after.y, after.z, moving);
        changes.push_back(Change{ root, before, after });
        done.insert(root);

        // What it carries keeps its place on it.
        float turn = after.o - before.o;
        float cosTurn = std::cos(turn);
        float sinTurn = std::sin(turn);
        for (uint32 id : CarriedBy(session, root, true))
        {
            if (!done.insert(id).second)
                continue;
            Placement const& carriedBefore = session.placements[id];
            Placement carriedAfter = carriedBefore;
            float relX = carriedBefore.x - before.x;
            float relY = carriedBefore.y - before.y;
            carriedAfter.x = after.x + relX * cosTurn - relY * sinTurn;
            carriedAfter.y = after.y + relX * sinTurn + relY * cosTurn;
            carriedAfter.z = carriedBefore.z + (after.z - before.z);
            carriedAfter.o = NormalizeAngle(carriedBefore.o + turn);
            changes.push_back(Change{ id, carriedBefore, carriedAfter });
        }
    }

    return Commit(player, session, label, std::move(changes), reason, merge, true);
}

bool PlayerHousingMgr::ShiftGroup(Player* player, Session& session, std::vector<uint32> const& members, float dx, float dy, float dz,
    float turn, bool snap, std::string const& verb, std::string const& tail, std::string& reason, bool merge)
{
    if (dx == 0.0f && dy == 0.0f && dz == 0.0f && turn == 0.0f)
        return true;

    std::vector<uint32> roots;
    std::vector<uint32> carried;
    GroupRoots(session, members, roots, carried);
    if (roots.empty())
    {
        reason = "Select some pieces first.";
        return false;
    }

    // They turn about their middle.
    float centerX = 0.0f;
    float centerY = 0.0f;
    for (uint32 root : roots)
    {
        centerX += session.placements[root].x;
        centerY += session.placements[root].y;
    }
    centerX /= float(roots.size());
    centerY /= float(roots.size());

    float cosTurn = std::cos(turn);
    float sinTurn = std::sin(turn);
    auto place = [&](Placement const& before, float& x, float& y)
    {
        float relX = before.x - centerX;
        float relY = before.y - centerY;
        x = centerX + relX * cosTurn - relY * sinTurn + dx;
        y = centerY + relX * sinTurn + relY * cosTurn + dy;
    };

    // With the grid on, the first piece lands on it and the rest keep their places around it.
    if (snap && (dx != 0.0f || dy != 0.0f) && GetGridSize(player->GetGUID().GetCounter()) > 0.0f)
    {
        float x;
        float y;
        place(session.placements[roots.front()], x, y);
        float snappedX = x;
        float snappedY = y;
        SnapToGrid(player->GetGUID().GetCounter(), snappedX, snappedY);
        dx += snappedX - x;
        dy += snappedY - y;
    }

    std::string label = verb + " " + Pieces(roots.size() + carried.size()) + tail;
    return CommitGroup(player, session, members, label, [&](Placement const& before, Placement& after)
        {
            place(before, after.x, after.y);
            after.z = before.z + dz;
            after.o = before.o + turn;
        }, reason, merge);
}

bool PlayerHousingMgr::PickUpGroup(Player* player, Session& session, std::vector<uint32> const& members, std::string& reason)
{
    // Each piece with what stands on it; a building's contents stay, as for one building.
    std::vector<Change> changes;
    std::set<uint32> seen;
    for (uint32 member : members)
    {
        if (seen.insert(member).second)
            changes.push_back(Change{ member, session.placements[member], std::nullopt });
        for (uint32 id : CarriedBy(session, member, false))
            if (seen.insert(id).second)
                changes.push_back(Change{ id, session.placements[id], std::nullopt });
    }

    _report = {};
    std::string failure;
    ApplyChanges(player, session, changes, true, failure);
    size_t count = changes.size();
    Record(player, "picked up " + Pieces(count), std::move(changes));
    session.groups.erase(player->GetGUID().GetCounter());

    reason = Acore::StringFormat("Picked up {}.{} ({})", Pieces(count), DescribeReturns(), CountsText(session.ownerGuid));
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::MoveGroupTo(Player* player, Session& session, std::vector<uint32> const& members, Position const& target,
    std::string& reason)
{
    Placement const& first = session.placements[members.front()];
    float tx = target.GetPositionX();
    float ty = target.GetPositionY();
    SnapToGrid(player->GetGUID().GetCounter(), tx, ty);
    float dx = tx - first.x;
    float dy = ty - first.y;
    // The whole group keeps its height above the ground: the first piece may stand on a
    // table left behind, and the rest shouldn't sink by the table's height.
    float dz = target.GetPositionZ() - FloorHeightNear(player, session, first, first.x, first.y);
    std::vector<uint32> roots;
    std::vector<uint32> carried;
    GroupRoots(session, members, roots, carried);
    return CommitGroup(player, session, members, "moved " + Pieces(roots.size() + carried.size()),
        [&](Placement const& before, Placement& after)
        {
            after.x = before.x + dx;
            after.y = before.y + dy;
            after.z = before.z + dz;
        }, reason);
}

bool PlayerHousingMgr::MatchGroup(Player* player, std::string const& how, std::string& reason)
{
    std::vector<uint32> members = GetGroup(player);
    if (members.size() < 2)
    {
        reason = "Select two or more pieces first: click one, then Ctrl-click the others (in edit mode, or while decorating).";
        return false;
    }

    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    Placement const reference = session->placements[members.front()];
    std::vector<uint32> roots;
    std::vector<uint32> carried;
    GroupRoots(*session, members, roots, carried);

    // Across the player's view: forward is the way they face, side is to their left.
    float po = player->GetOrientation();
    float forwardX = std::cos(po);
    float forwardY = std::sin(po);
    float sideX = -std::sin(po);
    float sideY = std::cos(po);
    size_t count = roots.size() + carried.size();

    if (how == "height")
        return CommitGroup(player, *session, members, "matched the height of " + Pieces(count),
            [&](Placement const& /*before*/, Placement& after) { after.z = reference.z; }, reason);
    if (how == "turn")
        return CommitGroup(player, *session, members, "matched the turn of " + Pieces(count),
            [&](Placement const& /*before*/, Placement& after) { after.o = reference.o; }, reason);
    if (how == "line")
    {
        float referenceForward = reference.x * forwardX + reference.y * forwardY;
        return CommitGroup(player, *session, members, "lined up " + Pieces(count), [&](Placement const& before, Placement& after)
            {
                float offset = referenceForward - (before.x * forwardX + before.y * forwardY);
                after.x = before.x + forwardX * offset;
                after.y = before.y + forwardY * offset;
            }, reason);
    }
    if (how == "space")
    {
        if (roots.size() < 3)
        {
            reason = "Spacing evenly takes three or more pieces (things on tables move with their table).";
            return false;
        }
        // The two ends stay; the rest spread out evenly between them, across the view.
        std::vector<std::pair<float, uint32>> order;
        for (uint32 root : roots)
            order.emplace_back(session->placements[root].x * sideX + session->placements[root].y * sideY, root);
        std::sort(order.begin(), order.end());
        std::map<uint32, float> shift;
        float start = order.front().first;
        float step = (order.back().first - start) / float(order.size() - 1);
        for (size_t i = 0; i < order.size(); ++i)
            shift[order[i].second] = start + step * float(i) - order[i].first;
        return CommitGroup(player, *session, members, "spaced out " + Pieces(count), [&](Placement const& before, Placement& after)
            {
                float offset = shift.count(before.id) ? shift[before.id] : 0.0f;
                after.x = before.x + sideX * offset;
                after.y = before.y + sideY * offset;
            }, reason);
    }

    reason = "Usage: .house match <height|turn|line|space>: the selected pieces line up with the first one.";
    return false;
}

// ---------------------------------------------------------------------------------------------
// New pieces: rows and sets

bool PlayerHousingMgr::FitsLimits(Session const& session, std::map<uint32, uint32> const& adding, std::string& reason) const
{
    uint32 furnishings = 0;
    uint32 buildings = 0;
    for (auto const& [id, placement] : session.placements)
    {
        PieceDefinition const* piece = GetPiece(placement.itemEntry);
        ++(piece && piece->IsBuilding() ? buildings : furnishings);
    }
    uint32 newFurnishings = 0;
    uint32 newBuildings = 0;
    for (auto const& [itemEntry, count] : adding)
    {
        PieceDefinition const* piece = GetPiece(itemEntry);
        (piece && piece->IsBuilding() ? newBuildings : newFurnishings) += count;
    }
    if (buildings + newBuildings > _maxBuildings)
    {
        reason = Acore::StringFormat("That would make {} buildings; the island takes {}.", buildings + newBuildings, _maxBuildings);
        return false;
    }
    if (furnishings + newFurnishings > _maxFurnishings)
    {
        reason = Acore::StringFormat("That would make {} furnishings; the island takes {}.", furnishings + newFurnishings, _maxFurnishings);
        return false;
    }
    return true;
}

bool PlayerHousingMgr::EnsurePieces(Player* player, std::map<uint32, uint32> const& needed, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::map<uint32, uint32> storage = GetStorage(self);
    auto consumes = _pendingConsumes.find(player->GetGUID());

    std::map<uint32, uint32> missing;
    for (auto const& [itemEntry, count] : needed)
    {
        uint32 reserved = 0;
        if (consumes != _pendingConsumes.end() && consumes->second.count(itemEntry))
            reserved = consumes->second.at(itemEntry);
        uint32 inBags = player->GetItemCount(itemEntry);
        uint32 have = (inBags > reserved ? inBags - reserved : 0) + (storage.count(itemEntry) ? storage[itemEntry] : 0);
        if (have < count)
            missing[itemEntry] = count - have;
    }
    if (missing.empty())
        return true;

    std::string names;
    uint32 listed = 0;
    std::vector<std::string> locked;
    for (auto const& [itemEntry, count] : missing)
    {
        PieceDefinition const* piece = GetPiece(itemEntry);
        if (!piece || !IsUnlocked(player, *piece))
            locked.push_back(PieceName(itemEntry));
        if (++listed <= 4)
            names += (names.empty() ? "" : ", ") + (count > 1 ? Acore::StringFormat("{} {}", count, PieceName(itemEntry)) : PieceName(itemEntry));
    }
    if (listed > 4)
        names += Acore::StringFormat(" and {} more kinds", listed - 4);

    if (!locked.empty())
    {
        std::string lockedNames;
        for (std::string const& name : locked)
            lockedNames += (lockedNames.empty() ? "" : ", ") + name;
        reason = "Still locked, so there are none to use: " + lockedNames + ".";
        return false;
    }
    if (!_freeMode)
    {
        reason = Acore::StringFormat("You need {} more: get them from your Collection first.", names);
        return false;
    }

    // FreeMode: the Collection hands them over, straight into House Storage (no bag space needed).
    for (auto const& [itemEntry, count] : missing)
        AddToStorage(self, itemEntry, int32(count));
    return true;
}

float PlayerHousingMgr::GroundHeightNear(Player* player, float x, float y, float z) const
{
    // The ground under the spot, searched from a little above: pieces' own collision is off.
    float ground = player->GetMap()->GetHeight(player->GetPhaseMask(), x, y, z + 5.0f, true, 50.0f);
    return ground > INVALID_HEIGHT + 1.0f ? ground : z;
}

float PlayerHousingMgr::FloorHeightNear(Player* player, Session const& session, Placement const& reference, float x, float y) const
{
    // What it stands on, down to the piece on the floor (tables carry things).
    Placement const* root = &reference;
    for (uint32 depth = 0; depth < 16 && root->parent; ++depth)
    {
        auto parent = session.placements.find(root->parent);
        if (parent == session.placements.end())
            break;
        root = &parent->second;
    }
    // Well above the ground under it: a building's floor, which the server can't see.
    if (root->z - GroundHeightNear(player, root->x, root->y, root->z) > 0.5f)
        return root->z;
    return GroundHeightNear(player, x, y, root->z);
}

bool PlayerHousingMgr::PlaceRow(Player* player, uint32 placementId, uint32 count, float spacing, std::string const& direction, std::string& reason)
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
        reason = "Mannequins don't come in rows: each wears its own gear.";
        return false;
    }

    Placement const source = itr->second;
    count = std::clamp<uint32>(count, 1, MAX_ROW);
    float size = piece->scale > 0.0f ? source.scale / piece->scale : 1.0f;
    if (spacing <= 0.0f)
        spacing = std::max(0.3f, 2.0f * piece->footprint * size);
    spacing = std::clamp(spacing, 0.1f, 30.0f);

    // Which way the row runs, from where the player faces: right unless told otherwise.
    float po = player->GetOrientation();
    float heading = po - PI_F / 2.0f;
    if (direction == "left")
        heading = po + PI_F / 2.0f;
    else if (direction == "forward")
        heading = po;
    else if (direction == "back")
        heading = po + PI_F;
    float stepX = std::cos(heading);
    float stepY = std::sin(heading);

    if (!FitsLimits(*session, { { piece->itemEntry, count } }, reason) || !EnsurePieces(player, { { piece->itemEntry, count } }, reason))
        return false;

    std::vector<Change> changes;
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    for (uint32 i = 1; i <= count; ++i)
    {
        Placement copy = source;
        copy.id = session->nextPlacementId++;
        copy.gear.clear();
        copy.placedBy = self != session->ownerGuid ? self : 0;
        copy.x = source.x + stepX * spacing * float(i);
        copy.y = source.y + stepY * spacing * float(i);
        // Along a table top, a building's floor, or the ground as it rises and falls.
        if (source.parent)
            copy.parent = FindSurfaceUnder(*session, copy.x, copy.y, copy.z);
        else
            copy.z = FloorHeightNear(player, *session, source, copy.x, copy.y);
        changes.push_back(Change{ copy.id, std::nullopt, copy });
    }

    std::string label = Acore::StringFormat("placed a row of {} {}", count, piece->name);
    if (!Commit(player, *session, label, std::move(changes), reason, false, true))
        return false;
    SelectOne(*session, self, placementId);
    reason = Acore::StringFormat("Placed a row of {} {} ({}).", count, piece->name, CountsText(session->ownerGuid));
    SendAddonState(player);
    return true;
}

std::vector<SavedSet> PlayerHousingMgr::GetSavedSets(ObjectGuid::LowType ownerGuid) const
{
    std::vector<SavedSet> sets;
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT s.set_id, s.name, DATE_FORMAT(s.saved_at, '%Y-%m-%d'), COUNT(p.piece_index) FROM mod_playerhousing_set s "
            "LEFT JOIN mod_playerhousing_set_piece p ON p.owner_guid = s.owner_guid AND p.set_id = s.set_id "
            "WHERE s.owner_guid={} GROUP BY s.set_id, s.name, s.saved_at ORDER BY s.set_id", ownerGuid))
    {
        do
        {
            Field* fields = result->Fetch();
            sets.push_back(SavedSet{ fields[0].Get<uint32>(), fields[1].Get<std::string>(), uint32(fields[3].Get<uint64>()),
                fields[2].Get<std::string>() });
        } while (result->NextRow());
    }
    return sets;
}

std::optional<SavedSet> PlayerHousingMgr::FindSavedSet(ObjectGuid::LowType ownerGuid, std::string const& nameOrNumber) const
{
    // A number (or #number) is the set's number, never its name: names have a letter.
    std::string wanted = ToLower(CleanName(nameOrNumber));
    std::string digits = !wanted.empty() && wanted[0] == '#' ? wanted.substr(1) : wanted;
    bool number = !digits.empty() && digits.size() < 10 && std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); });
    for (SavedSet const& set : GetSavedSets(ownerGuid))
        if (number ? set.id == uint32(std::strtoul(digits.c_str(), nullptr, 10)) : ToLower(set.name) == wanted)
            return set;
    return std::nullopt;
}

bool PlayerHousingMgr::SaveSet(Player* player, std::string const& name, std::string& reason)
{
    std::string setName = CleanName(name);
    if (setName.empty())
    {
        reason = "Give the set a name.";
        return false;
    }
    if (std::none_of(setName.begin(), setName.end(), [](unsigned char c) { return std::isalpha(c) || c >= 0x80; }))
    {
        reason = "A set's name needs a letter in it (numbers alone are the sets' own numbers).";
        return false;
    }

    std::vector<uint32> members = GetGroup(player);
    if (members.empty())
    {
        reason = "Select the pieces first: click one, then Ctrl-click the others (in edit mode, or while decorating).";
        return false;
    }

    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::vector<SavedSet> sets = GetSavedSets(self);
    if (sets.size() >= MAX_SAVED_SETS)
    {
        reason = Acore::StringFormat("You have {} sets, the most there can be. Delete one first.", sets.size());
        return false;
    }
    for (SavedSet const& set : sets)
        if (ToLower(set.name) == ToLower(setName))
        {
            reason = Acore::StringFormat("You already have a set called {}.", set.name);
            return false;
        }

    // The pieces and what stands on them, each relative to the first piece (as if it faced
    // east, so the set turns as a whole when set down).
    std::vector<uint32> ids;
    for (uint32 member : members)
    {
        if (std::find(ids.begin(), ids.end(), member) == ids.end())
            ids.push_back(member);
        for (uint32 id : CarriedBy(*session, member, false))
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
    }
    if (ids.size() > MAX_SET_PIECES)
    {
        reason = Acore::StringFormat("A set holds at most {} pieces.", MAX_SET_PIECES);
        return false;
    }

    // Across, from the first piece (it goes where the circle is clicked); up, from the lowest
    // (it goes on the ground there), so a vase picked before its table doesn't sink it.
    Placement const anchor = session->placements[ids.front()];
    float cosA = std::cos(-anchor.o);
    float sinA = std::sin(-anchor.o);
    float baseZ = anchor.z;
    for (uint32 id : ids)
        baseZ = std::min(baseZ, session->placements[id].z);
    uint32 setId = sets.empty() ? 1 : sets.back().id + 1;
    std::string escaped = setName;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("INSERT INTO mod_playerhousing_set (owner_guid, set_id, name) VALUES ({}, {}, '{}')", self, setId, escaped);
    for (size_t index = 0; index < ids.size(); ++index)
    {
        Placement const& placement = session->placements[ids[index]];
        float relX = placement.x - anchor.x;
        float relY = placement.y - anchor.y;
        auto parent = std::find(ids.begin(), ids.end(), placement.parent);
        uint32 parentIndex = placement.parent && parent != ids.end() ? uint32(parent - ids.begin()) + 1 : 0;
        trans->Append("INSERT INTO mod_playerhousing_set_piece (owner_guid, set_id, piece_index, item_entry, pos_x, pos_y, pos_z, orientation, "
                      "scale, pitch, roll, look, parent_index) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
            self, setId, index, placement.itemEntry, relX * cosA - relY * sinA, relX * sinA + relY * cosA, placement.z - baseZ,
            NormalizeAngle(placement.o - anchor.o), placement.scale, placement.pitch, placement.roll, placement.look, parentIndex);
    }
    // Written now, so the window's list asked for right after has it.
    CharacterDatabase.DirectCommitTransaction(trans);

    reason = Acore::StringFormat("Saved {} as the set {}. Set it down anywhere from the Layouts tab, or .house set place {}.",
        Pieces(ids.size()), setName, setName);
    return true;
}

bool PlayerHousingMgr::DeleteSet(Player* player, uint32 setId, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::optional<SavedSet> set = FindSavedSet(self, std::to_string(setId));
    if (!set)
    {
        reason = "That set is gone.";
        return false;
    }
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("DELETE FROM mod_playerhousing_set_piece WHERE owner_guid={} AND set_id={}", self, setId);
    trans->Append("DELETE FROM mod_playerhousing_set WHERE owner_guid={} AND set_id={}", self, setId);
    CharacterDatabase.DirectCommitTransaction(trans);
    reason = Acore::StringFormat("Deleted the set {}.", set->name);
    return true;
}

uint32 PlayerHousingMgr::MoverForRadius(float radius) const
{
    // The smallest circle that holds it, else the biggest there is.
    uint32 best = 0;
    float bestRadius = 0.0f;
    uint32 biggest = 0;
    float biggestRadius = -1.0f;
    for (auto const& [spell, mover] : _moverBySpell)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
        float circle = info ? info->Effects[0].CalcRadius() : 0.0f;
        if (circle > biggestRadius)
        {
            biggest = mover;
            biggestRadius = circle;
        }
        if (circle >= radius && (!best || circle < bestRadius))
        {
            best = mover;
            bestRadius = circle;
        }
    }
    return best ? best : biggest;
}

bool PlayerHousingMgr::StartSetPlacement(Player* player, uint32 setId, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::optional<SavedSet> set = FindSavedSet(self, std::to_string(setId));
    std::vector<SetPiece> pieces = set ? LoadSet(self, setId) : std::vector<SetPiece>{};
    if (pieces.empty())
    {
        reason = "That set is gone.";
        return false;
    }

    float radius = 1.0f;
    for (SetPiece const& piece : pieces)
        if (PieceDefinition const* definition = GetPiece(piece.itemEntry))
            radius = std::max(radius, std::sqrt(piece.x * piece.x + piece.y * piece.y) + definition->footprint);
    uint32 mover = MoverForRadius(radius);
    if (!mover)
    {
        reason = "The targeting circle isn't set up on this server.";
        return false;
    }

    CancelMove(player);
    if (!player->AddItem(mover, 1))
    {
        reason = "Your bags are full: make room for a Move a Piece item first.";
        return false;
    }
    _pendingMoves[player->GetGUID()] = PendingMove{ 0, mover, setId };
    reason = Acore::StringFormat("Right-click Move a Piece in your bags, then click where {} should go. It faces you.", set->name);
    SendAddonState(player);
    return true;
}

bool PlayerHousingMgr::StampSet(Player* player, Session& session, uint32 setId, Position const& target, std::string& reason)
{
    ObjectGuid::LowType self = player->GetGUID().GetCounter();
    std::optional<SavedSet> set = FindSavedSet(self, std::to_string(setId));
    std::vector<SetPiece> pieces = set ? LoadSet(self, setId) : std::vector<SetPiece>{};
    if (pieces.empty())
    {
        reason = "That set is gone.";
        return false;
    }

    std::map<uint32, uint32> needed;
    for (SetPiece const& piece : pieces)
    {
        if (!GetPiece(piece.itemEntry))
        {
            reason = "A piece in that set no longer exists.";
            return false;
        }
        ++needed[piece.itemEntry];
    }
    if (!FitsLimits(session, needed, reason) || !EnsurePieces(player, needed, reason))
        return false;

    // It faces the player, like a piece placed by hand (straight or diagonal on the grid).
    float tx = target.GetPositionX();
    float ty = target.GetPositionY();
    float facing = NormalizeAngle(std::atan2(player->GetPositionY() - ty, player->GetPositionX() - tx));
    if (GetGridSize(self) > 0.0f)
    {
        float step = PI_F / 4.0f;
        facing = NormalizeAngle(std::round(facing / step) * step);
        SnapToGrid(self, tx, ty);
    }

    float cosF = std::cos(facing);
    float sinF = std::sin(facing);
    // Every piece's number first: a vase can come before the table it stands on.
    std::vector<uint32> newIds;
    for (size_t index = 0; index < pieces.size(); ++index)
        newIds.push_back(session.nextPlacementId++);
    std::vector<Change> changes;
    for (size_t index = 0; index < pieces.size(); ++index)
    {
        SetPiece const& piece = pieces[index];
        PieceDefinition const* definition = GetPiece(piece.itemEntry);
        Placement placement;
        placement.id = newIds[index];
        placement.itemEntry = piece.itemEntry;
        placement.x = tx + piece.x * cosF - piece.y * sinF;
        placement.y = ty + piece.x * sinF + piece.y * cosF;
        placement.z = target.GetPositionZ() + piece.z;
        placement.o = NormalizeAngle(facing + piece.o);
        placement.scale = std::clamp(piece.scale, definition->scale * _sizeMin, definition->scale * _sizeMax);
        placement.pitch = definition->IsCreature() ? 0.0f : piece.pitch;
        placement.roll = definition->IsCreature() ? 0.0f : piece.roll;
        placement.look = piece.look;
        placement.placedBy = self != session.ownerGuid ? self : 0;
        placement.parent = piece.parent >= 0 && size_t(piece.parent) < newIds.size() && size_t(piece.parent) != index ? newIds[piece.parent] : 0;
        changes.push_back(Change{ placement.id, std::nullopt, placement });
    }

    std::string label = Acore::StringFormat("set down {} ({})", set->name, Pieces(pieces.size()));
    if (!Commit(player, session, label, std::move(changes), reason, false, true))
        return false;
    reason = Acore::StringFormat("Set down {}: {} ({}).", set->name, Pieces(pieces.size()), CountsText(session.ownerGuid));
    return true;
}

// ---------------------------------------------------------------------------------------------
// Undo history, going to a piece

bool PlayerHousingMgr::UndoSteps(Player* player, uint32 steps, std::string& reason)
{
    steps = std::clamp<uint32>(steps, 1, MAX_UNDO_STEPS);
    uint32 done = 0;
    size_t pieces = 0;
    std::string last;
    for (uint32 i = 0; i < steps; ++i)
    {
        // Big steps wait between goes (see Undo): checked once here, then as many as a few
        // hundred pieces in one go, the rest after the wait.
        size_t next = 0;
        {
            std::lock_guard<std::recursive_mutex> guard(_lock);
            auto journal = _journals.find(player->GetGUID().GetCounter());
            if (journal != _journals.end() && !journal->second.undo.empty())
                next = journal->second.undo.back().changes.size();
        }
        if (i > 0 && pieces + next > MAX_UNDO_PIECES)
        {
            last = "The rest in a moment: that's a lot of pieces at once.";
            break;
        }
        std::string stepReason;
        if (!Undo(player, stepReason, i == 0))
        {
            last = stepReason;
            break;
        }
        last = stepReason;
        pieces += next;
        ++done;
    }
    if (steps == 1 || done == 0)
        reason = last;
    else if (done == steps)
        reason = Acore::StringFormat("Undid {} steps.", done);
    else
        reason = Acore::StringFormat("Undid {} of {} steps. {}", done, steps, last);
    return done > 0;
}

std::vector<std::string> PlayerHousingMgr::JournalLabels(Player const* player, bool redo, uint32 limit) const
{
    std::vector<std::string> labels;
    std::lock_guard<std::recursive_mutex> guard(_lock);
    auto itr = _journals.find(player->GetGUID().GetCounter());
    if (itr == _journals.end() || itr->second.island != GetIslandOwner(player))
        return labels;
    auto const& steps = redo ? itr->second.redo : itr->second.undo;
    for (auto step = steps.rbegin(); step != steps.rend() && labels.size() < limit; ++step)
        labels.push_back(step->label);
    return labels;
}

bool PlayerHousingMgr::GoTo(Player* player, uint32 placementId, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(player, reason);
    if (!session)
        return false;

    auto itr = session->placements.find(placementId);
    PieceDefinition const* piece = itr != session->placements.end() ? GetPiece(itr->second.itemEntry) : nullptr;
    if (!piece)
    {
        reason = "No piece with that number here.";
        return false;
    }

    // In front of it, facing it; from the player's side if its front is off the island.
    Placement const& placement = itr->second;
    float size = piece->scale > 0.0f ? placement.scale / piece->scale : 1.0f;
    float distance = std::max(1.5f, piece->footprint * size + 1.0f);
    float angle = placement.o;
    float x = placement.x + std::cos(angle) * distance;
    float y = placement.y + std::sin(angle) * distance;
    if (!IsSpotOnIsland(x, y, placement.z))
    {
        angle = std::atan2(player->GetPositionY() - placement.y, player->GetPositionX() - placement.x);
        x = placement.x + std::cos(angle) * distance;
        y = placement.y + std::sin(angle) * distance;
    }
    float z = FloorHeightNear(player, *session, placement, x, y);
    player->NearTeleportTo(x, y, z + 0.1f, NormalizeAngle(angle + PI_F));

    SelectPlacement(player, placementId);
    reason = Acore::StringFormat("Here's the {}.", piece->name);
    SendAddonState(player);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The photo tour: a GM's island becomes a studio, one building at a time, for the addon's
// pictures of buildings (a model window can't draw them).

void PlayerHousingMgr::DespawnPhoto(Session& session, Map* map)
{
    if (!session.photoGuid)
        return;
    if (map)
        if (GameObject* object = map->GetGameObject(session.photoGuid))
            object->AddObjectToRemoveList();
    session.photoGuid.Clear();
}

bool PlayerHousingMgr::PhotoTour(Player* gm, std::string const& what, std::string& reason)
{
    std::lock_guard<std::recursive_mutex> guard(_lock);
    Session* session = GetOwnerSession(gm, reason, true);
    if (!session)
        return false;
    Map* map = gm->GetMap();

    std::vector<uint32> buildings;
    for (auto const& [itemEntry, piece] : _pieces)
        if (piece.IsBuilding())
            buildings.push_back(itemEntry);

    DespawnPhoto(*session, map);
    if (what == "stop" || buildings.empty())
    {
        _photoTours.erase(gm->GetGUID());
        SendAddon(gm, "photo\tdone");
        reason = buildings.empty() ? "There are no buildings to photograph." : "Photo tour over.";
        return true;
    }

    auto current = _photoTours.find(gm->GetGUID());
    uint32 itemEntry = 0;
    if (what == "start" || what.empty())
        itemEntry = buildings.front();
    else if (what == "next")
    {
        auto next = current == _photoTours.end() ? buildings.begin()
                                                 : std::upper_bound(buildings.begin(), buildings.end(), current->second);
        if (next == buildings.end())
            return PhotoTour(gm, "stop", reason);
        itemEntry = *next;
    }
    else
        itemEntry = uint32(std::strtoul(what.c_str(), nullptr, 10));

    PieceDefinition const* piece = GetPiece(itemEntry);
    GameObjectTemplate const* goInfo = piece ? sObjectMgr->GetGameObjectTemplate(piece->goEntry) : nullptr;
    if (!piece || !piece->IsBuilding() || !goInfo)
    {
        reason = "Usage: .house phototour <start|next|stop|building item>";
        return false;
    }

    // The studio: out from the landing spot, where the island is open.
    Position const& landing = _layout.landing;
    float studioX = landing.GetPositionX() + std::cos(landing.GetOrientation()) * 60.0f;
    float studioY = landing.GetPositionY() + std::sin(landing.GetOrientation()) * 60.0f;
    float studioZ = GroundHeightNear(gm, studioX, studioY, landing.GetPositionZ());
    float facing = NormalizeAngle(landing.GetOrientation() + PI_F);  // toward the camera, by the landing

    GameObject* object = new GameObject();
    if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), piece->goEntry, map, PHASEMASK_NORMAL, studioX, studioY, studioZ, facing,
            G3D::Quat(0.0f, 0.0f, 0.0f, 0.0f), 100, GO_STATE_READY))
    {
        delete object;
        reason = "Couldn't set the building up.";
        return false;
    }
    object->SetRespawnTime(0);
    object->SetSpawnedByDefault(false);
    object->SetObjectScale(goInfo->size);
    object->SetVisibilityDistanceOverride(VisibilityDistanceType::Large);
    if (!map->AddToMap(object))
    {
        delete object;
        reason = "Couldn't set the building up.";
        return false;
    }
    object->SetPhaseMask(session->phaseMask, true);
    object->EnableCollision(false);
    session->photoGuid = object->GetGUID();
    _photoTours[gm->GetGUID()] = itemEntry;

    // The camera: in front of it, far enough back to see all of it.
    float distance = std::max(12.0f, piece->footprint * 2.4f);
    float cameraX = studioX + std::cos(facing) * distance;
    float cameraY = studioY + std::sin(facing) * distance;
    float cameraZ = GroundHeightNear(gm, cameraX, cameraY, studioZ + 2.0f);
    gm->NearTeleportTo(cameraX, cameraY, cameraZ + 0.1f, NormalizeAngle(facing + PI_F));

    size_t index = std::find(buildings.begin(), buildings.end(), itemEntry) - buildings.begin();
    SendAddon(gm, Acore::StringFormat("photo\t{}\t{}\t{}", itemEntry, index + 1, buildings.size()));
    reason = Acore::StringFormat("Photo tour: {} ({} of {}).", piece->name, index + 1, buildings.size());
    return true;
}
