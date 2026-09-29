#!/usr/bin/env python3
"""Renders the addon's pictures of buildings from Blizzard's own model files.

Each building of the housing catalogue is a world model (.wmo). This script downloads the model
(its root file, its group files and its textures) from wago.tools, which serves the files of the
live Classic clients by FileDataID, draws it with a small software renderer (numpy) from a
three-quarter view slightly above, and writes it to the addon as Pictures/<item>.tga (256x256,
24-bit uncompressed), then lists every picture there in Pictures.lua for the preview.

The look is the same for every building: textures, alpha-tested leaves and fences, two-sided
cloth, an ambient light plus a sun from the front left, on a plain dark background. The
props placed inside a model (doodads, .m2 files such as lanterns or barrels) are not drawn.

    python3 render_buildings.py                         # all buildings, into the addon
    python3 render_buildings.py --items 902220 902221   # just these
    python3 render_buildings.py --png /tmp/previews     # also PNG copies (and a contact sheet)

Options: --out (the addon's Pictures folder), --cache (where downloads are kept, default
~/.cache/playerhousing-wmo), --branch (which client's files to use, default wow_classic_titan:
Wrath content, then wow_classic and wow if a file is missing), --size (pixels, default 256),
--supersample (default 4), --no-lua (don't rewrite Pictures.lua). The view of a building
can be turned with --yaw ITEM=DEGREES; the defaults are in VIEWS below.

The first run downloads about 400 files (30 MB) into the cache; drawing all the buildings then
takes about a minute.

Needs Python 3 with numpy and Pillow (Ubuntu or Debian: apt install python3-numpy python3-pil,
or pip install numpy pillow) and access to wago.tools. The FileDataIDs of the models are in
BUILDINGS below; for a model without one, --listfile points to the community listfile
(https://github.com/wowdev/wow-listfile, community-listfile.csv) to look it up by path.

The photo tour (/housing phototour in the game, then make_pictures.py) is an alternative that
takes the pictures from real screenshots instead.
"""

import argparse
import io
import math
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.request

try:
    import numpy as np
except ImportError:
    sys.exit("numpy is missing: apt install python3-numpy (or pip install numpy)")
try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is missing: apt install python3-pil (or pip install pillow)")

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON = os.path.normpath(os.path.join(HERE, "..", "..", "client-addon", "PlayerHousing"))

# (item, name, FileDataID of the root .wmo, the 3.3.5 client's path of the model)
BUILDINGS = [
    (902211, "Burnt Westfall Farmhouse", 108022, r"World\wmo\Azeroth\Buildings\Westfall_human_farm_Burnt\WestFallFarmHouseburnt.wmo"),
    (902212, "Burnt Duskwood Farmhouse", 106738, r"World\wmo\Azeroth\Buildings\Duskwood_Human_Farm_Burnt\DuskwoodFarmHouseburnt.wmo"),
    (902213, "Broken House", 115761, r"World\wmo\Outland\BrokenBuildings\Thebroken_house01.wmo"),
    (902214, "Ruined Guard Tower", 106885, r"World\wmo\Azeroth\Buildings\GuardTower_Ruined\ruinedhumanguardtower01.wmo"),
    (902220, "Westfall Farmhouse", 108016, r"World\wmo\Azeroth\Buildings\Westfall_human_farm\Westfall_human_farm.wmo"),
    (902221, "Duskwood Farmhouse", 106735, r"World\wmo\Azeroth\Buildings\Duskwood_human_farm\Duskwood_human_farm.wmo"),
    (902222, "Redridge Farmhouse", 107172, r"World\wmo\Azeroth\Buildings\RedRidge_human_farm\RedRidge_human_farm.wmo"),
    (902223, "Redridge Barn", 107138, r"World\wmo\Azeroth\Buildings\RedRidge_Barn\RedRidge_Barn.wmo"),
    (902224, "Duskwood Stable", 106779, r"World\wmo\Azeroth\Buildings\Duskwood_Stable\Duskwood_Stable.wmo"),
    (902225, "Duskwood Blacksmith", 106732, r"World\wmo\Azeroth\Buildings\Duskwood_Blacksmith\Duskwood_Blacksmith.wmo"),
    (902226, "Redridge Lumber Mill", 107194, r"World\wmo\Azeroth\Buildings\RedRidge_Lumbermill\RedRidge_Lumbermill.wmo"),
    (902227, "Redridge Chapel", 106698, r"World\wmo\Azeroth\Buildings\Chapel\RedridgeChapel.wmo"),
    (902228, "Duskwood Two-Story House", 106740, r"World\wmo\Azeroth\Buildings\Duskwood_HumanTwoStory\Duskwood_HumanTwoStory.wmo"),
    (902229, "Human Guard Tower", 106882, r"World\wmo\Azeroth\Buildings\GuardTower\GuardTower_intact.wmo"),
    (902230, "Night Elf Tent", 113721, r"world\wmo\kalimdor\pvp\collidabledoodads\netents\nightelftent01_pvp.wmo"),
    (902231, "Night Elf Druid Tower", 112844, r"World\wmo\Kalimdor\Buildings\NightElfDruidTower\DSNightElfDruidTower.wmo"),
    # The listfile knows this one as world/wmo/outland/draenibuildings/outland_draeni_hut_1.wmo.
    (902232, "Draenei Hut", 115843, r"World\Outland\PassiveDoodads\DraeniBuildings\outland_draeni_hut_1.wmo"),
    (902233, "Northrend Human House", 114992, r"World\wmo\Northrend\Buildings\Human\ND_Human_House02\ND_Human_House02.wmo"),
    (902234, "Tall Human Tower", 115009, r"world\wmo\northrend\buildings\human\nd_human_tower_open.wmo"),
    (902235, "The Skybreaker", 114904, r"World\wmo\Northrend\Buildings\Human\ND_AllianceGunship.wmo"),
    (902241, "Orc Zeppelin House", 113103, r"World\wmo\Kalimdor\Buildings\OrcZeppelinHouse\OrcZeppelinHouse.wmo"),
    (902242, "Orc Great Hall", 112993, r"World\wmo\Kalimdor\Buildings\OrcGreatHall\AbandonedOrcGreatHall.wmo"),
    (902243, "Orc Barracks", 112899, r"World\wmo\Kalimdor\Buildings\OrcBarracks\AbandonedOrcBarracks.wmo"),
    (902244, "Tauren Druid Tent", 113144, r"world\wmo\kalimdor\buildings\taurendruidtent\taurendruidtent.wmo"),
    (902247, "Winter Tauren Smoke Hut", 115351, r"World\wmo\Northrend\Buildings\WinterTauren\ND_WinterTauren_SmokeHut\ND_WinterTauren_SmokeHut.wmo"),
    (902248, "Orgrim's Hammer", 115250, r"World\wmo\Northrend\Buildings\WinterOrc\ND_HordeGunship.wmo"),
    (902252, "Pirate Ship Run Aground", 111402, r"World\wmo\Dungeon\MD_Pirateship\Pirateship.wmo"),
    (902253, "Old Stratholme Farm", 108209, r"World\wmo\buildings\OldStrat_farm.wmo"),
    (902254, "Ulduar Tower", 249894, r"world\wmo\dungeon\ulduar\ulduar_tower01.wmo"),
    (902255, "Wintergrasp Tower", 115685, r"World\wmo\Northrend\Wintergrasp\WG_Tower01.wmo"),
    (902256, "Wintergrasp Tower (Horde)", 115685, r"World\wmo\Northrend\Wintergrasp\WG_Tower01.wmo"),
    (902257, "Blizzard's Test House", 116212, r"World\wmo\PlayerHousing\Human\HumanLevelOneTest.wmo"),
]

# The camera goes around the building (yaw, degrees: 0 looks from +X, the model's north, 90 from
# +Y, its west) and looks down on it (pitch). Per building: the side that shows its front.
DEFAULT_VIEW = {"yaw": 225.0, "pitch": 27.0}
VIEWS = {
    902211: {"yaw": 315.0},  # the farmhouses' door and steps face -Y
    902212: {"yaw": 315.0},
    902214: {"yaw": 45.0},   # the towers' ramps face +X
    902220: {"yaw": 315.0},
    902221: {"yaw": 315.0},
    902222: {"yaw": 315.0},
    902223: {"yaw": 315.0},  # the barn's open doors
    902224: {"yaw": 315.0},  # the stable's open side
    902227: {"yaw": 45.0},   # the chapel's steps under the steeple
    902229: {"yaw": 45.0},
    902232: {"yaw": 315.0},  # the hut's ramp
    902233: {"yaw": 315.0},  # the porch
    902234: {"yaw": 45.0},
    902242: {"yaw": 45.0},
    902243: {"yaw": 45.0},
    902253: {"yaw": 315.0},
    902255: {"yaw": 135.0},  # the gate and its ramp
    902256: {"yaw": 135.0},
    902257: {"yaw": 315.0},
}

BACKGROUND = (22, 22, 24)      # plain, dark and neutral, for the addon's dark panel
MARGIN = 0.05                  # of the picture, on each side of the building
FIELD_OF_VIEW = 30.0           # degrees: a mild perspective
AMBIENT = 0.50                 # light everywhere ...
SUN = 0.70                     # ... plus a sun ...
SUN_YAW = -35.0                # ... from the front left of the camera (degrees from it) ...
SUN_PITCH = 50.0               # ... and above
ALPHA_KEY = 0.5                # alpha-tested materials: what is kept

WAGO = "https://wago.tools/api/casc/%d?download&branch=%s"
BRANCHES = ("wow_classic_titan", "wow_classic", "wow")
USER_AGENT = "mod-playerhousing render_buildings.py (numpy software renderer)"


# --- Files --------------------------------------------------------------------------------------

class Files:
    """Blizzard's files by FileDataID, downloaded from wago.tools once and kept in a cache."""

    def __init__(self, cache, branches, listfile=None):
        self.cache = cache
        self.branches = branches
        self.listfile_path = listfile
        self._paths = None
        self.downloads = 0

    def get(self, fdid, magic=None):
        """The file's bytes, or None if no branch has it. magic: what it must start with."""
        for branch in self.branches:
            path = os.path.join(self.cache, branch, str(fdid))
            if os.path.exists(path):
                data = open(path, "rb").read()
                if data and (not magic or data.startswith(magic)):
                    return data
            missing = path + ".missing"
            if os.path.exists(missing):
                continue
            data = self._download(fdid, branch)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if data and (not magic or data.startswith(magic)):
                with open(path, "wb") as out:
                    out.write(data)
                return data
            open(missing, "w").close()
        return None

    def _download(self, fdid, branch):
        request = urllib.request.Request(WAGO % (fdid, branch), headers={"User-Agent": USER_AGENT})
        for attempt in range(4):
            try:
                with urllib.request.urlopen(request, timeout=120) as response:
                    data = response.read()
                self.downloads += 1
                if data.startswith(b"{") and b"error" in data[:200]:
                    return None  # {"error":"File not found"}
                return data
            except urllib.error.HTTPError as error:
                if error.code in (400, 404):
                    return None
                reason = error
            except (urllib.error.URLError, OSError) as error:
                reason = error
            time.sleep(2 * (attempt + 1))
        print("  download of %d (%s) failed: %s" % (fdid, branch, reason))
        return None

    def fdid(self, path):
        """A FileDataID by client path, from the community listfile (only for old-style files)."""
        if self._paths is None:
            self._paths = {}
            if not self.listfile_path or not os.path.exists(self.listfile_path):
                print("  no listfile (--listfile) to look up %s" % path)
                return None
            with open(self.listfile_path, encoding="utf-8", errors="replace") as listfile:
                for line in listfile:
                    number, _, name = line.rstrip("\n").partition(";")
                    if name.endswith((".wmo", ".blp")):
                        self._paths[name] = int(number)
        return self._paths.get(path.replace("\\", "/").lower())


# --- World models -------------------------------------------------------------------------------

def chunks(data, start=0, end=None):
    """(tag, offset of the data, size) of each chunk, tags read the right way round."""
    end = len(data) if end is None else end
    offset = start
    while offset + 8 <= end:
        tag = data[offset:offset + 4][::-1].decode("latin-1")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        yield tag, offset + 8, size
        offset += 8 + size


class Material:
    # MOMT flags
    UNLIT = 0x01
    UNCULLED = 0x04
    CLAMP_S = 0x40
    CLAMP_T = 0x80

    def __init__(self, flags, shader, blend, texture):
        self.flags = flags
        self.shader = shader
        self.blend = blend        # 0 opaque, 1 alpha key, 2 alpha blend, 3 add, 4 mod, 5 mod2x ...
        self.texture = texture    # FileDataID, or None


class Model:
    """A world model's triangles, ready to draw: numpy arrays over all its groups."""

    def __init__(self):
        self.positions = []
        self.normals = []
        self.uvs = []
        self.triangles = []
        self.materials_of_triangles = []
        self.materials = []
        self.doodads = 0
        self.groups = 0


def read_model(files, root_fdid, path=""):
    """The model of a root .wmo (by FileDataID; path: its client path, for old-style files)."""
    root = files.get(root_fdid, b"REVM")
    if root is None:
        raise RuntimeError("root file %d not found" % root_fdid)
    names = b""
    raw_materials = []
    group_fdids = []
    groups = 0
    doodad_sets = []
    for tag, offset, size in chunks(root):
        if tag == "MOHD":
            groups = struct.unpack_from("<I", root, offset + 4)[0]
        elif tag == "MOTX":
            names = root[offset:offset + size]
        elif tag == "MOMT":
            for index in range(size // 64):
                raw_materials.append(struct.unpack_from("<16I", root, offset + 64 * index))
        elif tag == "GFID":
            group_fdids = list(struct.unpack_from("<%dI" % (size // 4), root, offset))
        elif tag == "MODS":
            for index in range(size // 32):
                doodad_sets.append(struct.unpack_from("<20sIII", root, offset + 32 * index))

    def texture(value):
        if names:  # an offset into MOTX, the old way
            end = names.find(b"\0", value)
            name = names[value:end].decode("latin-1") if 0 <= value < len(names) else ""
            return files.fdid(name) if name else None
        return value or None  # a FileDataID

    model = Model()
    for flags, shader, blend, texture_1 in (m[:4] for m in raw_materials):
        model.materials.append(Material(flags, shader, blend, texture(texture_1)))

    if not group_fdids:  # an old-style root: its groups are <root>_000.wmo, <root>_001.wmo ...
        stem = re.sub(r"\.wmo$", "", path.replace("\\", "/").lower())
        group_fdids = [files.fdid("%s_%03d.wmo" % (stem, index)) for index in range(groups)]
        if None in group_fdids:
            raise RuntimeError("no GFID chunk and group files not found by name (--listfile)")
    model.groups = groups
    if doodad_sets:
        model.doodads = doodad_sets[0][2]
    base = 0
    for group_fdid in group_fdids[:groups]:
        data = files.get(group_fdid, b"REVM")
        if data is None:
            print("  group %d missing" % group_fdid)
            continue
        base += read_group(data, model, base)
    if not model.triangles:
        raise RuntimeError("nothing to draw in it")
    model.positions = np.concatenate(model.positions).astype(np.float64)
    model.normals = np.concatenate(model.normals).astype(np.float64)
    model.uvs = np.concatenate(model.uvs).astype(np.float64)
    model.triangles = np.concatenate(model.triangles)
    model.materials_of_triangles = np.concatenate(model.materials_of_triangles)
    return model


def read_group(data, model, base):
    """Adds a group file's drawn triangles to the model; returns its number of vertices."""
    for tag, offset, size in chunks(data):
        if tag == "MOGP":
            mogp_offset, mogp_size = offset, size
            break
    else:
        return 0
    flags = struct.unpack_from("<I", data, mogp_offset + 8)[0]
    positions = normals = uvs = indices = None
    batches = []
    for tag, offset, size in chunks(data, mogp_offset + 0x44, mogp_offset + mogp_size):
        if tag == "MOVT":
            positions = np.frombuffer(data, np.float32, size // 4, offset).reshape(-1, 3)
        elif tag == "MONR":
            normals = np.frombuffer(data, np.float32, size // 4, offset).reshape(-1, 3)
        elif tag == "MOTV" and uvs is None:
            uvs = np.frombuffer(data, np.float32, size // 4, offset).reshape(-1, 2)
        elif tag == "MOVI":
            indices = np.frombuffer(data, np.uint16, size // 2, offset).astype(np.int64)
        elif tag == "MOVX":
            indices = np.frombuffer(data, np.uint32, size // 4, offset).astype(np.int64)
        elif tag == "MOBA":
            for index in range(size // 24):
                large, start, count, flags_b, material = struct.unpack_from(
                    "<10xHIH4xBB", data, offset + 24 * index)
                batches.append((start, count, large if flags_b & 2 else material))
    if positions is None or indices is None or not batches or flags & 0x4000000:  # antiportal
        return 0  # nothing drawn: its vertices aren't added either
    count = len(positions)
    if normals is None:
        normals = np.zeros((count, 3), np.float32)
    if uvs is None:
        uvs = np.zeros((count, 2), np.float32)
    for start, length, material in batches:
        if material >= len(model.materials):
            continue
        triangles = indices[start:start + length].reshape(-1, 3) + base
        model.triangles.append(triangles)
        model.materials_of_triangles.append(np.full(len(triangles), material, np.int32))
    model.positions.append(positions)
    model.normals.append(normals)
    model.uvs.append(uvs)
    return count


# --- Textures -----------------------------------------------------------------------------------

class Texture:
    """A BLP texture as float RGBA arrays, one per mipmap level (0 is the largest)."""

    def __init__(self, image):
        # Colour and alpha are reduced apart: many opaque textures keep a mask in their alpha,
        # and a premultiplied reduction would blacken the colour wherever that mask is 0.
        colour = image.convert("RGBA").convert("RGB")
        alpha = image.convert("RGBA").getchannel("A")
        self.levels = []
        while True:
            self.levels.append(np.dstack([np.asarray(colour, np.float32), np.asarray(alpha, np.float32)]) / 255.0)
            if min(colour.size) <= 1:
                break
            half = (max(1, colour.size[0] // 2), max(1, colour.size[1] // 2))
            colour = colour.resize(half, Image.BOX)
            alpha = alpha.resize(half, Image.BOX)
        self.height, self.width = self.levels[0].shape[:2]


GREY = Texture(Image.new("RGBA", (4, 4), (160, 160, 160, 255)))


TEXTURES = {}


def load_texture(files, fdid):
    if fdid in TEXTURES:
        return TEXTURES[fdid]
    data = files.get(fdid, b"BLP") if fdid else None
    texture = GREY
    if data:
        try:
            texture = Texture(Image.open(io.BytesIO(data)))
        except Exception as error:  # an odd BLP: grey rather than nothing
            print("  texture %d unreadable: %s" % (fdid, error))
    elif fdid:
        print("  texture %d missing" % fdid)
    TEXTURES[fdid] = texture
    return texture


def sample(level, u, v, clamp_s, clamp_t):
    """Bilinear sample of a mipmap level at texture coordinates u, v: (n, 4)."""
    height, width = level.shape[:2]
    x = u * width - 0.5
    y = v * height - 0.5
    x0 = np.floor(x)
    y0 = np.floor(y)
    fx = (x - x0)[:, None]
    fy = (y - y0)[:, None]
    x0 = x0.astype(np.int64)
    y0 = y0.astype(np.int64)
    x1 = x0 + 1
    y1 = y0 + 1
    if clamp_s:
        x0 = np.clip(x0, 0, width - 1)
        x1 = np.clip(x1, 0, width - 1)
    else:
        x0 %= width
        x1 %= width
    if clamp_t:
        y0 = np.clip(y0, 0, height - 1)
        y1 = np.clip(y1, 0, height - 1)
    else:
        y0 %= height
        y1 %= height
    top = level[y0, x0] * (1 - fx) + level[y0, x1] * fx
    bottom = level[y1, x0] * (1 - fx) + level[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


# --- Drawing ------------------------------------------------------------------------------------

def direction(yaw, pitch):
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    return np.array([math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), math.sin(pitch)])


def render(model, files, view, size):
    """The model drawn in a size x size float RGB array (0..255)."""
    positions = model.positions
    triangles = model.triangles
    used = np.unique(triangles)
    low = positions[used].min(axis=0)
    high = positions[used].max(axis=0)
    centre = (low + high) / 2
    radius = np.linalg.norm(positions[used] - centre, axis=1).max()

    # The camera, far enough for a mild perspective, looking at the middle of the building.
    back = direction(view["yaw"], view["pitch"])
    eye = centre + back * radius / math.sin(math.radians(FIELD_OF_VIEW / 2))
    forward = -back
    right = np.cross(forward, [0.0, 0.0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    relative = positions - eye
    depth = relative @ forward
    sx = (relative @ right) / depth
    sy = -(relative @ up) / depth
    # Fit the building in the picture with a margin, centred.
    left, right_edge = sx[used].min(), sx[used].max()
    top, bottom = sy[used].min(), sy[used].max()
    scale = size * (1 - 2 * MARGIN) / max(right_edge - left, bottom - top)
    screen_x = (sx - (left + right_edge) / 2) * scale + size / 2
    screen_y = (sy - (top + bottom) / 2) * scale + size / 2
    inverse_w = 1.0 / depth

    sun = direction(view["yaw"] + SUN_YAW, SUN_PITCH)

    # Which way do the triangles face? Their winding against the vertex normals says which
    # side is the front.
    corners = positions[triangles]
    face_normals = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    agree = np.einsum("ij,ij->i", face_normals, model.normals[triangles].sum(axis=1))
    winding = 1.0 if (agree > 0).mean() >= 0.5 else -1.0
    face_normals *= winding
    facing = np.einsum("ij,ij->i", face_normals, eye - corners.mean(axis=1)) > 0

    depth_buffer = np.zeros((size, size), np.float64)   # 1/w, 0 = nothing yet
    colour = np.empty((size, size, 3), np.float64)
    colour[:] = BACKGROUND

    opaque = []
    blended = []
    for index in range(len(triangles)):
        material = model.materials[model.materials_of_triangles[index]]
        if not facing[index] and not material.flags & Material.UNCULLED:
            continue
        (blended if material.blend >= 2 else opaque).append(index)
    # Opaque and alpha-tested first, then blended ones from the back to the front.
    opaque.sort(key=lambda index: model.materials_of_triangles[index])
    blended.sort(key=lambda index: -depth[triangles[index]].mean())

    context = {
        "x": screen_x, "y": screen_y, "iw": inverse_w, "uv": model.uvs, "normals": model.normals,
        "sun": sun, "depth": depth_buffer, "colour": colour, "size": size,
    }
    for index in opaque + blended:
        material = model.materials[model.materials_of_triangles[index]]
        texture = load_texture(files, material.texture)
        draw_triangle(context, triangles[index], material, texture, facing[index])
    return colour


def draw_triangle(ctx, triangle, material, texture, facing):
    size = ctx["size"]
    a, b, c = triangle
    xs = ctx["x"]
    ys = ctx["y"]
    x0, x1, x2 = xs[a], xs[b], xs[c]
    y0, y1, y2 = ys[a], ys[b], ys[c]
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
    if abs(area) < 1e-9:
        return
    xmin = max(int(math.floor(min(x0, x1, x2))), 0)
    xmax = min(int(math.ceil(max(x0, x1, x2))), size - 1)
    ymin = max(int(math.floor(min(y0, y1, y2))), 0)
    ymax = min(int(math.ceil(max(y0, y1, y2))), size - 1)
    if xmin > xmax or ymin > ymax:
        return
    px = np.arange(xmin, xmax + 1) + 0.5
    py = (np.arange(ymin, ymax + 1) + 0.5)[:, None]
    e0 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) / area
    e1 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) / area
    e2 = 1.0 - e0 - e1
    inside = (e0 >= 0) & (e1 >= 0) & (e2 >= 0)
    rows, cols = np.nonzero(inside)
    if not len(rows):
        return
    b0 = e0[rows, cols]
    b1 = e1[rows, cols]
    b2 = 1.0 - b0 - b1
    rows += ymin
    cols += xmin
    iw = ctx["iw"]
    w0, w1, w2 = iw[a], iw[b], iw[c]
    inverse = b0 * w0 + b1 * w1 + b2 * w2
    blend = material.blend
    depth = ctx["depth"]
    visible = inverse > depth[rows, cols]
    if not visible.any():
        return
    rows, cols, inverse = rows[visible], cols[visible], inverse[visible]
    p0 = b0[visible] * w0 / inverse
    p1 = b1[visible] * w1 / inverse
    p2 = 1.0 - p0 - p1

    uv = ctx["uv"]
    u = p0 * uv[a, 0] + p1 * uv[b, 0] + p2 * uv[c, 0]
    v = p0 * uv[a, 1] + p1 * uv[b, 1] + p2 * uv[c, 1]
    # The mipmap level: texels per pixel over the whole triangle.
    uv_area = abs((uv[b, 0] - uv[a, 0]) * (uv[c, 1] - uv[a, 1]) - (uv[b, 1] - uv[a, 1]) * (uv[c, 0] - uv[a, 0]))
    ratio = uv_area * texture.width * texture.height / abs(area)
    level = 0 if ratio <= 1 else min(int(round(0.5 * math.log2(ratio))), len(texture.levels) - 1)
    texel = sample(texture.levels[level], u, v, material.flags & Material.CLAMP_S, material.flags & Material.CLAMP_T)

    if blend == 1:
        keep = texel[:, 3] >= ALPHA_KEY
        if not keep.any():
            return
        rows, cols, inverse, texel = rows[keep], cols[keep], inverse[keep], texel[keep]
        p0, p1, p2 = p0[keep], p1[keep], p2[keep]

    if material.flags & Material.UNLIT:
        light = np.ones(len(rows))
    else:
        normals = ctx["normals"]
        normal = p0[:, None] * normals[a] + p1[:, None] * normals[b] + p2[:, None] * normals[c]
        normal /= np.maximum(np.linalg.norm(normal, axis=1), 1e-9)[:, None]
        if not facing:
            normal = -normal
        light = AMBIENT + SUN * np.maximum(normal @ ctx["sun"], 0.0)
    rgb = texel[:, :3] * 255.0 * light[:, None]

    colour = ctx["colour"]
    if blend <= 1:
        colour[rows, cols] = rgb
        depth[rows, cols] = inverse
        return
    alpha = texel[:, 3:4]
    below = colour[rows, cols]
    if blend == 3:
        colour[rows, cols] = below + rgb * alpha
    elif blend == 4:
        colour[rows, cols] = below * texel[:, :3]
    elif blend == 5:
        colour[rows, cols] = below * texel[:, :3] * 2
    else:
        colour[rows, cols] = below * (1 - alpha) + rgb * alpha


# --- Output -------------------------------------------------------------------------------------

def write_tga(image, path):
    """24-bit uncompressed TGA (image type 2), rows from the bottom up as the format's default."""
    image = image.convert("RGB")
    width, height = image.size
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0)
    pixels = np.asarray(image, np.uint8)[::-1, :, ::-1]  # bottom row first, BGR
    with open(path, "wb") as out:
        out.write(header + pixels.tobytes())


def write_lua(pictures, lua):
    have = sorted(int(name[:-4]) for name in os.listdir(pictures) if re.match(r"\d+\.tga$", name, re.I))
    lines = ["-- Buildings with a picture for the preview (Pictures/<item>.tga), rendered from the game's",
             "-- models by tools/pictures/render_buildings.py (or taken by a GM's photo tour,",
             "-- /housing phototour, and tools/pictures/make_pictures.py).",
             "PlayerHousing_Pictures = {"]
    lines += ["    [%d] = true," % item for item in have]
    lines.append("}")
    with open(lua, "w") as out:
        out.write("\n".join(lines) + "\n")
    return len(have)


def contact_sheet(entries, path, columns=8):
    """entries: [(item, name, image)] in one PNG grid with labels."""
    from PIL import ImageDraw
    cell = 256
    label = 18
    rows = (len(entries) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell, rows * (cell + label)), (8, 8, 8))
    draw = ImageDraw.Draw(sheet)
    for number, (item, name, image) in enumerate(entries):
        x = (number % columns) * cell
        y = (number // columns) * (cell + label)
        sheet.paste(image.resize((cell, cell)), (x, y))
        draw.text((x + 4, y + cell + 3), "%d %s" % (item, name), fill=(220, 220, 220))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--items", type=int, nargs="*", help="only these buildings (item ids)")
    parser.add_argument("--out", default=os.path.join(ADDON, "Pictures"), help="the addon's Pictures folder")
    parser.add_argument("--lua", default=os.path.join(ADDON, "Pictures.lua"), help="the Pictures.lua to rewrite")
    parser.add_argument("--no-lua", action="store_true", help="don't rewrite Pictures.lua")
    parser.add_argument("--cache", default=os.path.join(os.path.expanduser("~"), ".cache", "playerhousing-wmo"),
                        help="where downloaded files are kept")
    parser.add_argument("--branch", action="append", help="client branch(es) on wago.tools to take files from, "
                        "in order (default: %s)" % ", ".join(BRANCHES))
    parser.add_argument("--listfile", help="community-listfile.csv, for models without FileDataIDs inside")
    parser.add_argument("--size", type=int, default=256, help="pixels square (a power of two)")
    parser.add_argument("--supersample", type=int, default=4, help="drawn this many times larger, then reduced")
    parser.add_argument("--yaw", action="append", default=[], metavar="ITEM=DEGREES", help="turn a building's view")
    parser.add_argument("--pitch", type=float, help="look down at this angle (degrees) instead")
    parser.add_argument("--png", help="also save PNG copies (and contact.png, all of them) in this folder")
    args = parser.parse_args()

    files = Files(args.cache, tuple(args.branch or BRANCHES), args.listfile)
    yaws = {}
    for entry in args.yaw:
        item, _, degrees = entry.partition("=")
        yaws[int(item)] = float(degrees)
    os.makedirs(args.out, exist_ok=True)
    if args.png:
        os.makedirs(args.png, exist_ok=True)

    done = {}
    sheet = []
    for item, name, fdid, path in BUILDINGS:
        if args.items and item not in args.items:
            continue
        view = dict(DEFAULT_VIEW, **VIEWS.get(item, {}))
        if item in yaws:
            view["yaw"] = yaws[item]
        if args.pitch is not None:
            view["pitch"] = args.pitch
        started = time.time()
        key = (fdid, view["yaw"], view["pitch"])
        if key in done:
            picture = done[key]
            print("%d %s: the same model as above" % (item, name))
        else:
            try:
                model = read_model(files, fdid, path)
            except RuntimeError as error:
                print("%d %s: skipped, %s" % (item, name, error))
                continue
            big = render(model, files, view, args.size * args.supersample)
            picture = Image.fromarray(np.clip(big, 0, 255).astype(np.uint8), "RGB")
            picture = picture.resize((args.size, args.size), Image.LANCZOS)
            done[key] = picture
            print("%d %s: %d groups, %d triangles, %d doodads not drawn, %.1fs" % (
                item, name, model.groups, len(model.triangles), model.doodads, time.time() - started))
        write_tga(picture, os.path.join(args.out, "%d.tga" % item))
        if args.png:
            picture.save(os.path.join(args.png, "%d.png" % item))
        sheet.append((item, name, picture))

    if args.png and sheet:
        contact_sheet(sheet, os.path.join(args.png, "contact.png"))
    if not args.no_lua:
        print("Pictures.lua lists %d buildings." % write_lua(args.out, args.lua))
    print("%d files downloaded." % files.downloads)


if __name__ == "__main__":
    main()
