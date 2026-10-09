# Player Housing: getting your game ready

A home of your own: an island shared by all your characters, to build and furnish as you
like. Setting up takes about five minutes.

You need the **PlayerHousing-client.zip** from your server (ask where they host it) and a
World of Warcraft 3.3.5a client.

## 1. Unzip into your game folder

Close the game. Unzip PlayerHousing-client.zip into your game folder, the one with
`Wow.exe` in it, and let it merge with the folders already there. Afterwards you should have:

```
World of Warcraft/
    Wow.exe
    PlayerHousing.dll               (new)
    PlayerHousingLauncher.exe       (new)
    Data/patch-H.MPQ                (new; your server may use another letter)
    Interface/AddOns/PlayerHousing/ (new)
```

## 2. Clear the cache once

Delete the `Cache` folder in your game folder (the game makes a new one). Without this the
game may keep showing housing items as question marks.

## 3. Start the game with the launcher

Start `PlayerHousingLauncher.exe` instead of `Wow.exe`. It starts the game as usual, plus the
small add-on that lets pieces follow your mouse.

- **A shortcut on your desktop:** point it at `PlayerHousingLauncher.exe` instead.
- **Lutris, Bottles or Steam (Linux, Steam Deck):** change the game's executable from
  `Wow.exe` to `PlayerHousingLauncher.exe`. Keep everything else the same.
- **Mac:** skip this step and start the game as usual. Everything works, but a piece you
  place floats in front of you instead of following the mouse.

## 4. Turn on the addon

On the character screen, click **AddOns** (bottom left) and make sure **Player Housing** is
ticked. If the game says it's out of date, tick **Load out of date AddOns**.

## 5. Get your House Key

Log in and go to the inn in any capital city: Stormwind, Ironforge, Darnassus, the Exodar,
Orgrimmar, Thunder Bluff, the Undercity, Silvermoon, Shattrath or Dalaran. **Krook**, a
little wolvar, stands beside the innkeeper. Talk to him and ask for a house.

Right-click the House Key (or type `/housing`) and click **Go home**. Krook's quests walk you
through the rest.

Each of your characters asks Krook for their own key; they all share the same island. Lost
your key? Krook has another.

## Using it, in short

- **Place:** click a piece in the Collection, move the mouse to where it goes, left-click.
- **Change:** with the housing window open, right-click a piece to pick it up again.
- **Turn, raise, tilt, resize:** hold a piece and use the mouse wheel with Shift, Ctrl, Alt
  or Ctrl+Alt (the plain wheel still zooms the camera). The panel at the bottom of the
  screen shows which is which.
- **Never mind:** right-click or Escape. **Oops:** Undo, at the top of the window.

## If something's not right

| What you see | Fix |
| --- | --- |
| Housing items are red question marks | Delete the `Cache` folder (step 2). Still there? Your game has another patch that wins: rename `Data/patch-H.MPQ` to a later letter, such as `patch-Z.MPQ`. On some HD client packs it has to go in `Data/enUS/` (your language's folder) as `patch-enUS-z.MPQ`. |
| You can't see the piece you're placing | Same as above: the patch isn't loading. |
| A building you can't see blocks you on the island, or you see a big hall where the island should be empty | Same: the patch isn't loading. |
| Typing `/housing` does nothing, or `.house` says you need the addon | The addon isn't on (step 4), or the folder isn't at `Interface/AddOns/PlayerHousing`. |
| Pieces float in front of you instead of following the mouse | The game wasn't started with `PlayerHousingLauncher.exe` (step 3). To check, type `/run print(PlayerHousing_DLLInfo())` in chat: it should say `ready`. |
| The housing window won't open | Not in combat: the game doesn't allow it then. |

Still stuck? Tell your server's staff what you see, and paste what
`/run print(PlayerHousing_DLLInfo())` says.
