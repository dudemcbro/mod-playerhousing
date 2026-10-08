#!/usr/bin/env python3
"""Writes the buildings' ghosts for the client patch: a see-through block the size of each
building, which follows the player while they place or move it (a creature can't show a
world model, so the building itself can't be the ghost). One model (.m2 and its 00.skin) per
building, from the ghostblock lines of client_items.tsv (written by
tools/content/build_content.py), and one texture they share: pale blue, see-through, with a
brighter edge so each side shows its outline.

    make_ghost_blocks.py client_items.tsv OUT_DIR

Prints each file written, relative to OUT_DIR (with forward slashes), for the patch's list.

The models are WotLK's (version 264): one static bone, one Stand animation, 24 corners (each
side on its own, for its outline), both faces of each side drawn, blended, unlit, and not
hiding what's behind them. Where it can, everything is laid out the way Blizzard's own doodads
and their textures are (the client takes those as creature models), down to the texture's
format: DXT5, as the game's own models' textures with alpha are.
"""

import math
import os
import struct
import sys

TEXTURE = "World\\PlayerHousing\\GhostBlock.blp"
TEXTURE_SIZE = 64
EDGE = 3                   # texels of brighter edge on each side of the texture
FILL = (255, 215, 150)     # blue, green, red: pale blue
EDGE_COLOR = (255, 250, 235)
FILL_ALPHA = 80
EDGE_ALPHA = 235

M2_VERSION = 264
HEADER_SIZE = 0x130


def rgb565(color):
    blue, green, red = color
    return (red >> 3) << 11 | (green >> 2) << 5 | blue >> 3


def blp(size=TEXTURE_SIZE):
    """A BLP2 in DXT5 (8-bit alpha), every mip level down to 1x1, as the game's own textures
    with alpha are: each 4x4 block has the two colors and two alphas as its ends, and every
    texel takes one or the other."""
    edge_color, fill_color = rgb565(EDGE_COLOR), rgb565(FILL)
    levels = []
    level_size = size
    while True:
        # The edge stays about the same share of the texture at every level.
        edge = max(1, round(EDGE * level_size / size)) if level_size > 2 else 0
        blocks = max(1, level_size // 4)
        level = bytearray()
        for block_y in range(blocks):
            for block_x in range(blocks):
                alpha_bits = 0
                color_bits = 0
                for index in range(16):
                    x, y = block_x * 4 + index % 4, block_y * 4 + index // 4
                    inside = x < level_size and y < level_size
                    on_edge = inside and (x < edge or y < edge or x >= level_size - edge or y >= level_size - edge)
                    # Alpha: index 0 is the first end (the edge's), 1 the second (the fill's).
                    # Color: in DXT5 always four colors, 0 the first end, 1 the second.
                    alpha_bits |= (0 if on_edge else 1) << (3 * index)
                    color_bits |= (0 if on_edge else 1) << (2 * index)
                level += struct.pack("<BB", EDGE_ALPHA, FILL_ALPHA) + alpha_bits.to_bytes(6, "little")
                level += struct.pack("<HHI", edge_color, fill_color, color_bits)
        levels.append(bytes(level))
        if level_size == 1:
            break
        level_size //= 2

    header_size = 4 + 4 + 4 + 8 + 16 * 4 * 2 + 256 * 4
    offsets, sizes, data = [], [], b""
    for level in levels:
        offsets.append(header_size + len(data))
        sizes.append(len(level))
        data += level
    offsets += [0] * (16 - len(offsets))
    sizes += [0] * (16 - len(sizes))
    # Version 1, DXT (2), 8-bit alpha, DXT5 (7), with mips; no palette (its space is left empty).
    header = b"BLP2" + struct.pack("<I4B2I", 1, 2, 8, 7, 1, size, size)
    header += struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes) + bytes(256 * 4)
    assert len(header) == header_size
    return header + data


def box_sides(box):
    """The six sides of a box as (corners, facing), corners going round each side."""
    x0, y0, z0, x1, y1, z1 = box
    return [
        ([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0)),
        ([(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)], (-1, 0, 0)),
        ([(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], (0, 1, 0)),
        ([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0)),
        ([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1)),
        ([(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)], (0, 0, -1)),
    ]


class Writer:
    """The M2's data after its header: each block 16-byte aligned, found by its offset."""

    def __init__(self, start):
        self.data = bytearray(b"\0" * start)

    def add(self, blob):
        while len(self.data) % 16:
            self.data.append(0)
        offset = len(self.data)
        self.data += blob
        return offset

    def array(self, blob, count):
        """An M2Array's (count, offset); an empty one points nowhere."""
        return (count, self.add(blob)) if count else (0, 0)


def empty_track():
    # interpolation none, no global sequence, no timestamps, no values.
    return struct.pack("<Hh2I2I", 0, -1, 0, 0, 0, 0)


def m2(name, box):
    """The model: its .m2 and its 00.skin."""
    radius = math.sqrt(sum(((box[i + 3] - box[i]) / 2) ** 2 for i in range(3)))
    middle = tuple((box[i] + box[i + 3]) / 2 for i in range(3))

    vertices = bytearray()
    triangles = []
    uv = [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)]
    for corners, facing in box_sides(box):
        base = len(vertices) // 48
        for corner, (u, v) in zip(corners, uv):
            vertices += struct.pack("<3f4B4B3f2f2f", *corner, 255, 0, 0, 0, 0, 0, 0, 0, *facing, u, v, 0.0, 0.0)
        triangles += [base, base + 1, base + 2, base, base + 2, base + 3]
    vertex_count = len(vertices) // 48

    w = Writer(HEADER_SIZE)
    name_bytes = name.encode("ascii") + b"\0"
    name_array = w.array(name_bytes, len(name_bytes))

    # Stand (animation 0), kept in this file (flag 0x20), looping.
    bounds = struct.pack("<6ff", *box, radius)
    sequence = struct.pack("<HHIfIhHIII", 0, 0, 3000, 0.0, 0x20, 0x7FFF, 0, 0, 0, 150) + bounds + struct.pack("<hH", -1, 0)
    assert len(sequence) == 64
    sequences = w.array(sequence, 1)
    # No sequence lookup table, as Blizzard's own doodads have none: the client then searches
    # the sequences in order. A table is a hash the client probes until it finds the animation
    # or an empty (-1) slot, so a full one (the Stand alone) never ends for any other
    # animation, and a creature asks for others (walking, flying): the game hangs.
    sequence_lookup = (0, 0)

    # One bone, not a key bone, never moving; the key bone table says so, as the doodads' do.
    bone = struct.pack("<iIhHI", -1, 0, -1, 0, 0) + empty_track() * 3 + struct.pack("<3f", 0.0, 0.0, 0.0)
    assert len(bone) == 88
    bones = w.array(bone, 1)
    key_bone_lookup = w.array(struct.pack("<h", -1), 1)
    vertex_array = w.array(bytes(vertices), vertex_count)

    texture_name = TEXTURE.encode("ascii") + b"\0"
    texture_name_offset = w.add(texture_name)
    # Type 0 (a file), no flags, as the doodads' textures.
    textures = w.array(struct.pack("<II2I", 0, 0, len(texture_name), texture_name_offset), 1)

    # See-through as drawn: one key, fully weighted (the display's own alpha does the rest).
    weight_times = w.add(struct.pack("<I", 0))
    weight_values = w.add(struct.pack("<h", 0x7FFF))
    weight_time_arrays = w.add(struct.pack("<2I", 1, weight_times))
    weight_value_arrays = w.add(struct.pack("<2I", 1, weight_values))
    texture_weights = w.array(struct.pack("<Hh2I2I", 0, -1, 1, weight_time_arrays, 1, weight_value_arrays), 1)

    # Texture type 0 (a file) is texture 0, as in the doodads.
    replaceable_lookup = w.array(struct.pack("<h", 0), 1)
    # Unlit (1), both sides (4), not writing depth (0x10); alpha blended (2).
    materials = w.array(struct.pack("<HH", 0x01 | 0x04 | 0x10, 2), 1)
    bone_lookup = w.array(struct.pack("<H", 0), 1)
    texture_lookup = w.array(struct.pack("<H", 0), 1)
    tex_unit_lookup = w.array(struct.pack("<h", 0), 1)
    transparency_lookup = w.array(struct.pack("<H", 0), 1)
    transform_lookup = w.array(struct.pack("<h", -1), 1)

    header = bytearray()
    header += b"MD20" + struct.pack("<I", M2_VERSION)
    header += struct.pack("<2I", *name_array)
    header += struct.pack("<I", 0)                      # global flags
    header += struct.pack("<2I", 0, 0)                  # global sequences
    header += struct.pack("<2I", *sequences)
    header += struct.pack("<2I", *sequence_lookup)
    header += struct.pack("<2I", *bones)
    header += struct.pack("<2I", *key_bone_lookup)
    header += struct.pack("<2I", *vertex_array)
    header += struct.pack("<I", 1)                      # skin profiles: 00.skin
    header += struct.pack("<2I", 0, 0)                  # colors
    header += struct.pack("<2I", *textures)
    header += struct.pack("<2I", *texture_weights)
    header += struct.pack("<2I", 0, 0)                  # texture transforms
    header += struct.pack("<2I", *replaceable_lookup)
    header += struct.pack("<2I", *materials)
    header += struct.pack("<2I", *bone_lookup)
    header += struct.pack("<2I", *texture_lookup)
    header += struct.pack("<2I", *tex_unit_lookup)
    header += struct.pack("<2I", *transparency_lookup)
    header += struct.pack("<2I", *transform_lookup)
    header += struct.pack("<6ff", *box, radius)         # bounds, for drawing
    header += struct.pack("<6ff", 0, 0, 0, 0, 0, 0, 0)  # no collision
    header += struct.pack("<2I", 0, 0) * 3              # collision triangles, vertices, normals
    header += struct.pack("<2I", 0, 0) * 2              # attachments and their lookup
    header += struct.pack("<2I", 0, 0)                  # events
    header += struct.pack("<2I", 0, 0)                  # lights
    header += struct.pack("<2I", 0, 0) * 2              # cameras and their lookup
    header += struct.pack("<2I", 0, 0)                  # ribbons
    header += struct.pack("<2I", 0, 0)                  # particles
    assert len(header) == HEADER_SIZE, len(header)
    model = bytes(header) + bytes(w.data[HEADER_SIZE:])

    # The skin: every corner, the triangles, one section, one batch.
    skin_header_size = 48
    s = Writer(skin_header_size)
    skin_vertices = s.array(struct.pack("<%dH" % vertex_count, *range(vertex_count)), vertex_count)
    skin_triangles = s.array(struct.pack("<%dH" % len(triangles), *triangles), len(triangles))
    skin_bones = s.array(b"\0\0\0\0" * vertex_count, vertex_count)
    section = struct.pack("<10H", 0, 0, 0, vertex_count, 0, len(triangles), 1, 0, 1, 0) + struct.pack("<3f3ff", *middle, *middle, radius)
    assert len(section) == 48
    skin_sections = s.array(section, 1)
    batch = struct.pack("<Bbh10H", 0x10, 0, 0, 0, 0, 0xFFFF, 0, 0, 1, 0, 0, 0, 0)
    assert len(batch) == 24
    skin_batches = s.array(batch, 1)
    skin_header = b"SKIN" + struct.pack("<2I", *skin_vertices) + struct.pack("<2I", *skin_triangles) + struct.pack("<2I", *skin_bones)
    skin_header += struct.pack("<2I", *skin_sections) + struct.pack("<2I", *skin_batches) + struct.pack("<I", 21)
    assert len(skin_header) == skin_header_size
    skin = skin_header + bytes(s.data[skin_header_size:])
    return model, skin


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    tsv, out_dir = sys.argv[1], sys.argv[2]
    blocks = []
    for line in open(tsv, encoding="utf-8"):
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "ghostblock":
            blocks.append((parts[2], [float(v) for v in parts[3].split()]))
    if not blocks:
        return

    def write(path, data):
        relative = path.replace("\\", "/")
        full = os.path.join(out_dir, relative)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(data)
        print(relative)

    write(TEXTURE, blp())
    for path, box in blocks:
        stem = os.path.splitext(path)[0]
        model, skin = m2(stem.replace("\\", "/").split("/")[-1], box)
        write(stem + ".m2", model)
        write(stem + "00.skin", skin)


if __name__ == "__main__":
    main()
