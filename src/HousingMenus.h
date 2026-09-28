#ifndef MOD_PLAYERHOUSING_MENUS_H
#define MOD_PLAYERHOUSING_MENUS_H

#include "PlayerHousingMgr.h"

class Player;

// The gossip menus behind the House Key, the .house command, the stewards and clicked
// pieces. All of them share one set of commands, so any menu can be opened from anywhere.
namespace HousingMenus
{
    void ShowHome(Player* player, Housing::MenuSource const& source);
    // justPlaced: opened by itself right after placing, with "Keep it here" and "Take it back" first.
    void ShowPiece(Player* player, Housing::MenuSource const& source, uint32 placementId, bool justPlaced = false);
    void ShowHook(Player* player, Housing::MenuSource const& source, uint32 surfacePlacementId);
    void ShowStandToGuest(Player* player, Housing::MenuSource const& source, uint32 placementId);
    void ShowCollection(Player* player, Housing::MenuSource const& source);
    void ShowCollectionSearch(Player* player, Housing::MenuSource const& source, std::string const& text);
    void ShowSavedLayouts(Player* player, Housing::MenuSource const& source);
    void ShowChest(Player* player, Housing::MenuSource const& source, uint32 placementId);
    void ShowMusicBox(Player* player, Housing::MenuSource const& source);
    void ShowStorage(Player* player, Housing::MenuSource const& source);
    void ShowVisit(Player* player, Housing::MenuSource const& source);
    void HandleSelect(Player* player, Housing::MenuSource const& source, uint32 sender, uint32 action, char const* code);
}

#endif
