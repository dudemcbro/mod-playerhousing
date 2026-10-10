# Commands

Every `.house` command, and the GM tools.

## Player commands

The window does all of this; these are shortcuts. `.krook` works the same as `.house`.

| Command | What it does |
| --- | --- |
| `.house` | Opens the housing window (the addon is needed) |
| `.house home`, `leave`, `unstuck` | Go home (needs the House Key), leave the island, back to the landing spot |
| `.house krook` | Krook comes over to you on your island |
| `.house decorate [on\|off]` | Start or stop decorating (the window does this when it opens and closes) |
| `.house undo [steps]`, `redo` | Undo the last change (or that many, up to 20), or redo |
| `.house select [id\|next\|previous]`, `list` | Select a piece by number, the nearest, or the next out; list those within 40 yards |
| `.house move [id]`, `goto <id>`, `highlight <id\|0>` | Put a piece on the mouse; walk to it; a green ring under it (0: none) |
| `.house rotate <degrees> [id]`, `face [id]`, `here [id]`, `nudge <direction> [yards] [id]` | Turn a piece, face you, move it to you, nudge it |
| `.house ghost <item>`, `ghost move [id]`, `ghost place [another]`, `ghost cancel` | A new piece on the mouse, a placed one, set it down, never mind |
| `.house size <bigger\|smaller\|normal\|percent> [id]`, `tilt <forward\|back\|left\|right\|straight> [degrees] [id]` | Resize or tilt a piece, within the server's limits |
| `.house another [id]` | One more of this piece on the mouse, with its turn, size and tilt |
| `.house row <count> [yards] [right\|left\|forward\|back] [id]` | Copies of a piece in a straight row, one undo step |
| `.house group [add\|remove <id> \| clear]`, `match <height\|turn\|line\|space>` | Several pieces at once; line them up |
| `.house set [save <name> \| place <name> \| delete <name> \| list]` | Saved sets |
| `.house layout [save <name>\|load <name>\|delete <name>\|send <name> <player>\|copy\|list]` | Saved layouts |
| `.house grid <off\|yards>` | Snap to a grid of 0.25 to 4 yards |
| `.house pickup [id] [inside]`, `packup` | Put a piece away (`inside`: with what's in a building); put everything away (undoable) |
| `.house stand <dress <item>\|undress <slot\|all>\|look <race> <male\|female>\|trade> [id]` | A mannequin's gear and look |
| `.house collection [search]`, `get <item> [count]`, `visit [name]` | Search the Collection, get copies of an unlocked piece, visit someone |
| `.house weather <name>`, `time <name>`, `music <sound id\|off>` | The island's weather, time of day and music |
| `.house invite <name\|target\|party>`, `uninvite <name>`, `roommate <name>`, `unroommate <name>` | Guests and roommates |
| `.house privacy <private\|friends\|public>`, `greeting <text\|clear>`, `door [here\|reset]` | Who can visit, what they see, where they arrive |
| `.house like`, `visitors`, `sign <note>`, `guestbook [delete <id>]`, `report <text>` | Like an island, your visitor log, the guestbook, report an island to the GMs |
| `.house data <kind>`, `addon`, `seen`, `state` | Quiet ones for the addon |

Without an id, commands act on the selected piece.

GMs also have `.house key` (a House Key), `.house unlock <item|name|all> [player]`,
`.house relock ...`, `.house unlocks [player]`, `.house add` (Krook next to you for ten
minutes) and `.house phototour <start|next|stop|item>` (see Pictures of buildings, below).

## Moderation

Players report an island with `.house report <what's wrong>`: once per account per island while the report is open.
Online GMs are told at once. GM commands:

| Command | What it does |
| --- | --- |
| `.house reports [all]` | Open reports (or the last 20 of all), newest first |
| `.house close <id>` | Close a report |
| `.house inspect <player>` | Go to anyone's island, whatever its privacy |
| `.house hide <player>`, `unhide <player>` | Close an island to all but its guest list, and take it off the public and most liked lists |
| `.house cleargreeting <player>` | Clear an island's greeting |
| `.house gmpackup <player>` | Pack up an island: every piece goes back to the Collection of whoever placed it, and mannequin gear is mailed back |

GM actions are logged to the server log (module logger).
