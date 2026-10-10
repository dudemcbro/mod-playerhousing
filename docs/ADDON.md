# The client addon

How the housing window works, the mouse and keys while holding a piece, and how the addon talks to the server.

`client-addon/PlayerHousing` is the housing window, and the server needs it: the House Key,
Krook and `.house` open it, and pieces are placed and moved with the mouse.

The addon sends its commands over AzerothCore's addon command channel (on unless the server
sets `AddonChannel = 0`): chat's flood limit doesn't count them, so moving a piece with the
mouse never gets anyone muted, and they don't fill the chat box. It checks the channel
answers when you log in, and sends them as chat otherwise. Nothing asks "are you sure"
before something Undo can put back; only saving over or deleting a layout or a set, and
throwing out a guestbook note, still ask.

The window has a toolbar (Go home or Leave, Undo, Redo and Help; right-click Undo for the
recent changes) and six tabs. Escape closes it.

- **Collection**: every piece there is, unlocked ones in color and locked ones grey, with
  how many you own on each icon. All, favorites, recently placed, or a category; only
  unlocked ones, or only the ones you own; search by name; sort by the Collection's order,
  name, cost, owned or unlock type. A click puts an unlocked piece on the mouse and keeps it
  in the preview beside the window with its details and Get 1 and Get 5. Hover an icon to
  preview the piece: its model, turning slowly, framed to fit. Buildings made of world
  models can't be drawn in a window, so they show a picture (see Pictures of buildings).
  Right-click a piece to star it as a favorite.
- **Placed**: the pieces on the island, nearest first, and a search. Click one for a green
  ring under it on the island; Move, Go and Put away.
- **Layouts**: save the island under a name, set a layout out again, send it, delete it.
  Its second page has sets: pieces saved together, set down anywhere from the mouse.
- **Guests**: invite by name, your target or your party; make a guest a roommate or
  remove them. Its second page is the guestbook.
- **Visit**: the islands of your party, guild and friends, the ones you're invited to,
  public ones and the most liked; or visit someone by name. On someone's island: Like, Copy
  their layout (when they allow it) and sign the guestbook.
- **Island**: privacy, greeting, weather, time of day and music (with a Music Box placed),
  the door, the visitor log, Pack up all, Unstuck and Call Krook.

In the preview, drag the model to turn it, use the mouse wheel to bring it nearer or farther
and right-drag to move it up or down; Reset puts it back. `/housing preview` says which model
it asked for and which the game loaded (for bug reports).

**Holding a piece**: a piece being placed or moved follows your mouse (with
[PlayerHousing.dll](../client-dll/README.md)) or, without the DLL, floats ahead of you. Furniture
and buildings with ordinary models are see-through copies of themselves; world-model buildings
(towers, farmhouses) and tilted pieces show as the real object, so their lean shows. What stands
on a piece, or is inside a building, comes along. Turning your character turns the held piece
with you.

| Mouse or key | Does |
| --- | --- |
| Move the mouse (with the DLL) | It follows the cursor: floor, table top, a wall (facing out), under a ceiling |
| Left-click, Shift-click | Set it down; Shift: then another of the same |
| Right-click, Escape | Never mind: a new piece stays in your Collection, a moved one where it was |
| Shift+right-click | Put a moved piece away in your Collection |
| Wheel | Zoom the camera, as ever |
| Shift+wheel, Ctrl+Shift+wheel | Turn it; finely (1 degree) |
| Ctrl+wheel | Raise, lower |
| Alt+wheel, Alt+Shift+wheel | Tilt it forward or back; to its side |
| Ctrl+Alt+wheel | Bigger, smaller |
| Middle-click, Ctrl+middle-click, Shift+middle-click | Stand it straight; normal size; step the grid |
| Arrows, G, Shift+G (without the DLL) | Push it farther, nearer, sideways; set it down; and another |

While you hold a piece the window steps aside and a small panel at the bottom of the screen
shows what you're holding, its size and tilt, which way its front points, and the keys above.
The keys are bound only while you hold a piece, so the usual ones come back afterwards;
bindings can't change in combat, so they wait for it to end.

It lands on the grid when the grid is on (not on a wall), and stands on a table top it's over
(held higher, it floats). A building always stands on the ground, wherever the mouse is. The
see-through ghosts need the client patch (see [Setting it up](../README.md#setting-it-up)) from this
version: without it they can't be seen, and `PlayerHousing.Ghosts = 0` carries every piece as
it is instead.

The window opens by itself when you arrive home (`/housing auto` turns that off).
`/housing` shows or hides it, and `/housing <command>` runs any `.house` command. A button
on the minimap's edge opens the window; drag it around the edge, or `/housing minimap` to
hide it. Key bindings: Key Bindings, Player Housing.

Install: copy the `PlayerHousing` folder into `World of Warcraft/Interface/AddOns/`. The
window can't open or close, or change tabs, in combat (a WoW rule for windows with item
buttons).

The addon talks to the server with the same `.house` commands, and reads what the server
whispers to the player with the addon prefix `HOUSING`: the island state
(`PlayerHousingMgr::SendAddonState`) and the tabs' lists (`src/HousingAddon.cpp`: `begin`,
a `row` a message, `end`). The piece list with names, icons and unlock hints
(`PieceInfo.lua`) comes from the content builder. `client-addon/test/harness.lua` runs the
addon outside the game against stubbed WoW functions:

```
lua5.1 client-addon/test/harness.lua $(sed -n 's|^\([A-Za-z]*\.lua\)$|client-addon/PlayerHousing/\1|p' \
    client-addon/PlayerHousing/PlayerHousing.toc)
```

## Pictures of buildings

A model window can't draw buildings made of world models, so the addon comes with a
picture of each (`Pictures/<item>.tga`, listed in `Pictures.lua`), and shows a floor plan to
scale for any building without one. `tools/pictures/render_buildings.py` makes them: it
downloads each building's model files and textures (from wago.tools, which serves the
game's files) and draws them all from the same three-quarter view. Run it again after adding
buildings (it needs numpy and Pillow: `apt install python3-numpy python3-pil`).

Pictures from your own client instead, with the game's own lighting, take a GM a few minutes:

1. As a GM, go home and type `/housing phototour`. The island turns clear and sunny, the
   camera goes to first person, and each building is set up in front of you in turn: the
   interface hides for a moment while the addon takes a screenshot. Leave the mouse and keys
   alone until it says it's done (`/housing phototour stop` ends it early).
2. Log out (or `/reload`), so the addon saves which screenshot shows which building.
3. Run `tools/pictures/make_pictures.py --wow "<your client folder>"` (it needs Pillow:
   `apt install python3-pil`). It cuts a square from the middle of each screenshot, writes
   `Interface/AddOns/PlayerHousing/Pictures/<item>.tga` and lists them in `Pictures.lua`.
4. `/reload`: buildings show their pictures.

Copying a new version of the addon over the old one brings back the rendered pictures: run
the script again (it only needs a moment) to use yours.
