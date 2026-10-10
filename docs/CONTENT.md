# Content

Where the pieces, their models and their unlock rules come from, and how to change them. The full list: [UNLOCKS.md](UNLOCKS.md).

The pieces, their models and what unlocks them are written as a Python list in
`tools/content/pieces.py`. `tools/content/build_content.py` turns it into
`sql/db_world/base/mod_playerhousing_world_content.sql` (items, objects, pieces and rules), `docs/UNLOCKS.md`, the addon's model list
(`client-addon/PlayerHousing/PieceModels.lua`) and the items the client patch adds
(`tools/gm-island-cleared/client_items.tsv`). Each item's bag icon is picked from its name
by `tools/content/icons.py` (a piece's `icon` field overrides it). It reads the world
database (to copy models and behavior from existing objects) and the client data's `dbc`
folder (for model sizes, names and icons):

```
python3 tools/content/build_content.py --dbc /path/to/data/dbc \
    --mysql "mysql -uacore -pacore acore_world"
```

The builder also writes `sql/db_world/base/mod_playerhousing_world_catalog.sql`: every
other object model in the game (about 2,000), one piece each, named after its object (or
its model when the object's name is an internal one), sized from the game data, and
marked as fitting on tables when small. Big models (whole areas, ships), collision shapes
and markers are left out. These pieces are used only with `PlayerHousing.Catalog =
everything`: then everyone has them from the start, in the Collection's Catalog (search it
by name). The file uses its own item and object ranges (items 940000 and up), so the
curated content and the catalog never step on each other, and applying it with `curated`
does nothing visible.

Rules in `mod_playerhousing_piece_rule`: level, achievement, reputation rank, quest, kill,
exploring an area, skill level, or never (GM and UnlockAll only). Rules in the same group
must all be met; any complete group unlocks the piece. Achievements come first wherever one
fits, since players already see and track those.
