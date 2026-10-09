#!/usr/bin/env python3
"""Fits the addon's preview framing to each piece from screenshots the game takes itself.

The client's preview frame has a camera of its own that the addon can't read, and a model made
smaller than its own size isn't drawn at all, so the addon shows each object at its own size,
moved back from the camera. How far, how high and how far off center each model sits differs
from model to model (their measured bounds aren't always where the geometry is). So:

  1. task: writes the addon's PreviewTask.lua, a list of shots: three per piece (at the usual
     distance turned two ways a quarter turn apart, and twice as far back).
  2. In the game, /reload: PreviewTour.lua shows each shot in the preview (in the middle of the
     screen, on black) with a strip of squares spelling out the piece and the shot, and takes a
     screenshot. About 1.4 seconds a shot.
  3. fit: reads the screenshots (the strip says which is which), measures where each model
     landed, works out the frame's camera from all of them together, and writes PreviewFix.lua:
     how near and how high each piece starts, and how far its middle really is from the
     measured one. Run task again afterwards with --check to see the fixed framing.

    python3 preview_fit.py task --wow "/path/to/World of Warcraft" [--all] [--check]
    python3 preview_fit.py fit --wow "/path/to/World of Warcraft" [--since 2026-10-08T18:30]

Needs Pillow.
"""

import argparse
import datetime
import glob
import json
import math
import os
import re
import statistics
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is missing: apt install python3-pil (or pip install pillow)")

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON = os.path.join(os.path.dirname(os.path.dirname(HERE)), "client-addon", "PlayerHousing")
BITS = 24
FACING = 0.785          # the shots' turn: a three-quarter view
SHOTS = [               # shot number: zoom, facing
    (1.0, FACING),
    (1.0, FACING + math.pi / 2),
    (0.5, FACING),
]
# The preview, in UI units (PlayerHousing.lua, PreviewTour.lua): the model area is 200 by 190
# from (10, -28) of the 220 wide preview; the strip, 26 squares of 8, is centered 228 down.
STRIP_SQUARES = BITS + 2
STRIP_UNITS = STRIP_SQUARES * 8
MODEL_W, MODEL_H = 200, 190
TARGET = 0.62           # of the model area's height: how big a piece should look


def find_dir(parent, name):
    for entry in os.listdir(parent):
        if entry.lower() == name.lower():
            return os.path.join(parent, entry)
    return None


def load_models():
    """PieceModels.lua: item -> (model, length, depth, height, fit, midX, midY, midZ)."""
    models = {}
    text = open(os.path.join(ADDON, "PieceModels.lua"), encoding="utf-8").read()
    for m in re.finditer(r'\[(\d+)\] = \{ ("(?:[^"\\]|\\.)*"|false), ([^}]*)\}', text):
        nums = [float(x) for x in m.group(3).split(",") if x.strip()]
        if m.group(2) == "false":
            continue  # a world model: the preview shows a floor plan, nothing to frame
        models[int(m.group(1))] = (m.group(2).strip('"').replace("\\\\", "\\"), *nums)
    return models


def usual(model):
    """The addon's usual framing numbers for a model (FrameModel in PlayerHousing.lua)."""
    _, length, depth, height, fit, midx, midy, midz = model
    size = max(fit if fit > 0 else max(length, depth, height), 0.5)
    return size, max(2.0, 2.0 * size), max(1.0, size / 3.0), midx, midy, midz


PARAMS = os.path.join(HERE, "preview_params.json")
# Shot numbers (0-15) and their settings: zoom, lift, facing. 0-2: the first look; 3-4: how a
# lift and a distance move it; 5-8: up and down, far back, for pieces not seen yet.
SHOT_SETTINGS = {
    0: (1.0, 0.0, FACING), 1: (1.0, 0.0, FACING + math.pi / 2), 2: (0.5, 0.0, FACING),
    3: (1.0, 1.0, FACING), 4: (0.7, -1.0, FACING),
    5: (0.5, -3.0, FACING), 6: (0.5, -1.5, FACING), 7: (0.5, 1.5, FACING), 8: (0.5, 3.0, FACING),
    9: (0.3, 0.0, FACING), 10: (0.3, -3.0, FACING), 11: (0.3, 3.0, FACING),
}


CUSTOM = os.path.join(HERE, "preview_custom.json")


def load_custom():
    """Aimed shots, per piece: "item:number" -> zoom, lift, facing (numbers 12-15)."""
    return {tuple(map(int, k.split(":"))): tuple(v) for k, v in json.load(open(CUSTOM)).items()} if os.path.exists(CUSTOM) else {}


def setting(item, number, custom=None):
    if number in SHOT_SETTINGS:
        return SHOT_SETTINGS[number]
    return (custom if custom is not None else load_custom())[(item, number)]


def load_shots():
    path = os.path.join(HERE, "preview_shots.json")
    shots = {}
    if os.path.exists(path):
        for key, value in json.load(open(path)).items():
            item, number = key.split(":")
            shots[(int(item), int(number))] = value
    return shots


def cmd_task(args):
    """The next run: for each piece, the shots it doesn't have yet that tell something new."""
    models = load_models()
    shots = load_shots()
    items = sorted(i for i, m in models.items()
                   if m[0] and m[0] != "player" and not m[0].startswith("creature:") and (args.all or i < 940000))
    run = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
    lines = []
    for item in items:
        have = {n: v for (i, n), v in shots.items() if i == item}
        whole = any(not v.get("empty") and not v.get("clipped") for v in have.values())
        # Seen whole: how a lift and a distance move it. Never seen whole (cut off, or out of
        # the frame): farther back, and up and down, until it is.
        wanted = [0, 1, 2, 3, 4] if whole or not have else [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
        for number in wanted:
            if number in have:
                continue
            zoom, lift, facing = SHOT_SETTINGS[number]
            lines.append("{ %d, %d, %.3f, %.3f, %.3f }," % (item, number, zoom, lift, facing))
    with open(os.path.join(ADDON, "PreviewTask.lua"), "w", encoding="utf-8") as out:
        out.write("-- Written by tools/pictures/preview_fit.py: the preview tour's shots (PreviewTour.lua),\n"
                  "-- { item, shot, zoom, lift, facing }. It runs once per run name after /reload.\n")
        out.write('PlayerHousing_PreviewTask = { run = "%s", shots = {\n' % run)
        for line in lines:
            out.write("    " + line + "\n")
        out.write("} }\n")
    print("%d pieces, %d shots (about %d minutes), run %s" % (len(items), len(lines), len(lines) * 1.4 / 60 + 1, run))


def cmd_refine(args):
    """Aimed shots for the pieces the first shots missed, from the measured camera: never seen
    whole: four heights around where the formula puts it, far enough back to show it whole;
    seen whole once: its framing, and a little above and below."""
    models = load_models()
    shots = load_shots()
    text = open(os.path.join(ADDON, "PreviewFix.lua"), encoding="utf-8").read()
    cam = re.search(r"distance = ([-\d.]+), aim = ([-\d.]+), focal = ([-\d.]+), middle = \{ ([-\d.]+), ([-\d.]+), ([-\d.]+), ([-\d.]+) \}", text)
    c0, P, f, k0, k1, k2, k3 = map(float, cam.groups())
    fixes = {int(m.group(1)): (float(m.group(2)), float(m.group(3)))
             for m in re.finditer(r"\[(\d+)\] = \{ ([-\d.]+), ([-\d.]+),", text)}
    custom = {}
    lines = []
    for item, model in sorted(models.items()):
        if item >= 940000 and not args.all:
            continue
        size, away, m, midx, midy, midz = usual(model)
        whole = sum(1 for (i, n), v in shots.items() if i == item and not v.get("empty") and not v.get("clipped"))
        if whole >= 2:
            continue
        if whole == 0:
            d = c0 + 4.5 * size
            middle = k0 + k1 * size + k2 * midz + k3 * d - P * d / f
            plan = [(d, middle + step * size) for step in (-1.2, -0.4, 0.4, 1.2)]
        else:
            d, middle = fixes.get(item, (c0 + 2 * size, 0.25 * size))
            plan = [(d * 1.15, middle - P * (d * 0.15) / f - 0.25 * size), (d * 1.15, middle - P * (d * 0.15) / f + 0.25 * size)]
        for number, (d, middle) in zip(range(12, 16), plan):
            zoom = away / max(d - c0, 0.05)
            lift = (middle - 0.25 * size) / m
            custom["%d:%d" % (item, number)] = [round(zoom, 4), round(lift, 4), FACING]
            lines.append("{ %d, %d, %.4f, %.4f, %.3f }," % (item, number, zoom, lift, FACING))
    json.dump(custom, open(CUSTOM, "w"), indent=0)
    run = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
    with open(os.path.join(ADDON, "PreviewTask.lua"), "w", encoding="utf-8") as out:
        out.write("-- Written by tools/pictures/preview_fit.py: the preview tour's shots (PreviewTour.lua),\n"
                  "-- { item, shot, zoom, lift, facing }. It runs once per run name after /reload.\n")
        out.write('PlayerHousing_PreviewTask = { run = "%s", shots = {\n' % run)
        for line in lines:
            out.write("    " + line + "\n")
        out.write("} }\n")
    print("%d aimed shots for %d pieces (about %d minutes), run %s" % (len(lines), len({l.split(",")[0] for l in lines}), len(lines) * 1.4 / 60 + 1, run))


def red(px):
    r, g, b = px[:3]
    return r > 180 and g < 70 and b < 70


def read_shot(path):
    """The strip's code and the model's outline in the model area, or None."""
    image = Image.open(path).convert("RGB")
    w, h = image.size
    pix = image.load()
    # The strip is in the middle third of the screen: find runs of red (its end squares).
    for y in range(h // 3, 2 * h // 3):
        x = w // 4
        reds = []
        while x < 3 * w // 4:
            if red(pix[x, y]):
                start = x
                while x < w and red(pix[x, y]):
                    x += 1
                reds.append((start, x))
            x += 1
        if len(reds) < 2:
            continue
        left, right = reds[0], reds[-1]
        square = (right[1] - left[0]) / STRIP_SQUARES
        if square < 4 or abs((left[1] - left[0]) - square) > square * 0.4:
            continue
        # The strip's middle row: find its top from the red end square.
        top = y
        while top > 0 and red(pix[left[0] + 1, top - 1]):
            top -= 1
        cy = top + square / 2
        code = 0
        for bit in range(BITS):
            cx = left[0] + square * (bit + 1.5)
            r, g, b = pix[int(cx), int(cy)]
            code = code * 2 + (1 if (r + g + b) / 3 > 160 else 0)
        unit = square / 8.0
        # Model area from the strip: the preview is 220 wide, the strip 208 and centered; the
        # strip's top is 228 down, the model area's 28 down.
        area_left = left[0] - 6 * unit + 10 * unit
        area_top = top - 200 * unit
        aw, ah = MODEL_W * unit, MODEL_H * unit
        # A few units in from the black's edge (the world can show at its border).
        x0, y0 = int(area_left + 4 * unit), int(area_top + 4 * unit)
        x1, y1 = int(area_left + aw - 4 * unit), int(area_top + ah - 4 * unit)
        # The game's "Screen Captured" notice (yellow, in the middle of the screen) can be in a
        # shot: its yellow down to its dark olive edges doesn't count in that band. Then lone
        # specks (fewer than two lit neighbours on the 2-pixel grid) don't count either.
        bx0, bx1, by0, by1 = w / 2 - 125, w / 2 + 125, h / 2 - 22, h / 2 + 22
        lit = set()
        for yy in range(y0, y1, 2):
            for xx in range(x0, x1, 2):
                r, g, b = pix[xx, yy]
                if max(r, g, b) <= 30:
                    continue
                if bx0 <= xx <= bx1 and by0 <= yy <= by1 and (max(r, g, b) <= 50 or (
                        b <= 0.45 * max(r, g) and g >= 0.4 * r)):
                    continue
                lit.add((xx, yy))
        xs, ys = [], []
        for xx, yy in lit:
            if sum((xx + dx, yy + dy) in lit for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2))) >= 2:
                xs.append(xx)
                ys.append(yy)
        result = {"item": code // 16, "shot": code % 16, "unit": unit, "file": os.path.basename(path)}
        if len(xs) < 20:
            result["empty"] = True
            return result
        cx0, cy0 = (x0 + x1) / 2, (y0 + y1) / 2
        result.update({
            # Relative to the model area's middle, in UI units; y up.
            "left": (min(xs) - cx0) / unit, "right": (max(xs) - cx0) / unit,
            "top": (cy0 - min(ys)) / unit, "bottom": (cy0 - max(ys)) / unit,
            "clipped": min(xs) <= x0 + 2 or max(xs) >= x1 - 3 or min(ys) <= y0 + 2 or max(ys) >= y1 - 3,
        })
        return result
    return None


def measure(args):
    """Reads new screenshots into preview_shots.json (a later shot of the same number wins)."""
    shots = load_shots()
    shots_dir = find_dir(args.wow, "Screenshots")
    since = datetime.datetime.fromisoformat(args.since).timestamp() if args.since else 0
    files = sorted(p for p in glob.glob(os.path.join(shots_dir, "*")) if os.path.getmtime(p) >= since)
    read = unread = 0
    for path in files:
        result = read_shot(path)
        if not result or (result["shot"] not in SHOT_SETTINGS and (result["item"], result["shot"]) not in load_custom()):
            unread += 1
            continue
        shots[(result["item"], result["shot"])] = result
        read += 1
    json.dump({"%d:%d" % k: v for k, v in shots.items()}, open(os.path.join(HERE, "preview_shots.json"), "w"), indent=0)
    print("%d screenshots read, %d not (%d shots kept in all)" % (read, unread, len(shots)))
    return shots


def cmd_fit(args):
    import numpy as np
    shots = measure(args) if args.wow else load_shots()
    models = load_models()
    custom = load_custom()
    usable = {}
    for (item, number), v in shots.items():
        if item in models and not v.get("empty") and not v.get("clipped"):
            usable.setdefault(item, []).append((number, v))

    def distance(c0, item, number):
        size, away = usual(models[item])[:2]
        return c0 + away / setting(item, number, custom)[0]

    # The camera's place (c0): the one that keeps each piece's screen size times its distance
    # the same across distances.
    def spread(c0):
        total, count = 0.0, 0
        for item, rows in usable.items():
            sizes = [max(v["top"] - v["bottom"], 1) * distance(c0, item, n) for n, v in rows if setting(item, n, custom)[2] == FACING]
            if len(sizes) >= 2:
                mean = sum(sizes) / len(sizes)
                total += sum((x / mean - 1) ** 2 for x in sizes)
                count += len(sizes)
        return total / max(count, 1)
    c0 = min((spread(c / 4.0), c / 4.0) for c in range(-8, 81))[1]

    # Heights: y D = P D + f m lift + G (G per piece): P (where the camera aims), f (screen units
    # per model unit at distance 1) and every piece's G, by least squares.
    index = {item: k for k, item in enumerate(sorted(usable))}
    rows_a, rows_b = [], []
    for item, rows in usable.items():
        m = usual(models[item])[2]
        for number, v in rows:
            d = distance(c0, item, number)
            lift = setting(item, number, custom)[1]
            y = (v["top"] + v["bottom"]) / 2
            row = [0.0] * (2 + len(index))
            row[0], row[1], row[2 + index[item]] = d, m * lift, 1.0
            rows_a.append(row)
            rows_b.append(y * d)
    solution = np.linalg.lstsq(np.array(rows_a), np.array(rows_b), rcond=None)[0]
    P, f = solution[0], solution[1]
    G = {item: solution[2 + k] for item, k in index.items()}
    print("camera: %.2f from the frame's origin, aims %.1f units above the middle, %.1f units per unit at 1 (%d pieces, %d shots)"
          % (c0, P, f, len(usable), len(rows_b)))

    # Each shot says where the piece's middle really is, as a height: put - (y - P) D / f
    # (put: where the usual framing put it). Per piece, the average; for all, a formula from
    # size, measured middle and distance, for pieces never measured.
    zc, X, Y = {}, [], []
    for item, rows in usable.items():
        size, away, m, midx, midy, midz = usual(models[item])
        for number, v in rows:
            zoom, lift, _ = setting(item, number, custom)
            d = distance(c0, item, number)
            value = 0.25 * size + m * lift - ((v["top"] + v["bottom"]) / 2 - P) * d / f
            zc.setdefault(item, []).append(value)
            X.append([1.0, size, midz, d])
            Y.append(value)
    coef = np.linalg.lstsq(np.array(X), np.array(Y), rcond=None)[0]
    ratios = sorted(max(v["top"] - v["bottom"], v["right"] - v["left"]) * distance(c0, item, n) / (f * usual(models[item])[0])
                    for item, rows in usable.items() for n, v in rows)
    ratio = ratios[len(ratios) // 2]
    print("formula: middle height %.2f %+.2f size %+.2f midZ %+.3f D; screen size %.2f x size" % (tuple(coef) + (ratio,)))

    # Per piece: the distance that makes it TARGET of the area's height, and the height of its
    # middle that puts it on the area's middle there.
    fixes = {}
    for item, rows in usable.items():
        size = usual(models[item])[0]
        extent = max(max(v["top"] - v["bottom"], v["right"] - v["left"]) * distance(c0, item, n) for n, v in rows)
        d_new = max(c0 + 0.5, extent / (TARGET * MODEL_H))
        put = statistics.mean(zc[item]) - P * d_new / f
        dmx = dmy = 0.0
        a = next((v for n, v in rows if n == 0), None)
        b = next((v for n, v in rows if n == 1), None)
        if a and b:
            xa = (a["left"] + a["right"]) / 2 * distance(c0, item, 0) / f
            xb = (b["left"] + b["right"]) / 2 * distance(c0, item, 1) / f
            t1, t2 = FACING, FACING + math.pi / 2
            det = math.sin(t1) * math.cos(t2) - math.cos(t1) * math.sin(t2)
            dmx = (xa * math.cos(t2) - xb * math.cos(t1)) / det
            dmy = (math.sin(t1) * xb - math.sin(t2) * xa) / det
            dmx, dmy = (round(v, 2) if abs(v) > 0.05 * size else 0.0 for v in (dmx, dmy))
        fixes[item] = (round(d_new, 3), round(put, 3), dmx, dmy)
    unseen = sorted(i for i in models if i not in usable and any(k[0] == i for k in shots))
    print("measured framing for %d pieces; %d measured but never seen whole (they get the formula): %s"
          % (len(fixes), len(unseen), unseen[:12]))
    with open(args.out or os.path.join(ADDON, "PreviewFix.lua"), "w", encoding="utf-8") as out:
        out.write("-- Written by tools/pictures/preview_fit.py from the preview tour's screenshots (PreviewTour.lua).\n"
                  "-- The preview frame's camera, as measured: how far in front of a model's origin it is, how far\n"
                  "-- above the area's middle it aims (UI units), its focal length (UI units per model unit at\n"
                  "-- distance 1), the formula for where a model's middle shows (constant, x size, x middle z, x\n"
                  "-- distance), and how big a model shows for its size.\n")
        out.write("PlayerHousing_PreviewCamera = { distance = %.3f, aim = %.2f, focal = %.2f, middle = { %.3f, %.3f, %.3f, %.4f }, size = %.3f, target = %.2f }\n"
                  % ((c0, P, f) + tuple(coef) + (ratio, TARGET)))
        out.write("-- Per piece, measured: { camera distance, middle height, middle x, middle y } in model units.\n")
        out.write("PlayerHousing_PreviewFix = {\n")
        for item, fix in sorted(fixes.items()):
            out.write("    [%d] = { %.3f, %.3f, %.2f, %.2f },\n" % ((item,) + fix))
        out.write("}\n")

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    task = sub.add_parser("task")
    task.add_argument("--wow", required=False)
    task.add_argument("--all", action="store_true", help="the catalog's pieces too (much longer)")
    refine = sub.add_parser("refine")
    refine.add_argument("--all", action="store_true")
    fit = sub.add_parser("fit")
    fit.add_argument("--wow", help="read new screenshots first (else only the saved measurements)")
    fit.add_argument("--out", help="where to write the fixes (default: the addon's PreviewFix.lua)")
    fit.add_argument("--since", help="only screenshots from this time on (ISO, local)")
    args = parser.parse_args()
    {"task": cmd_task, "fit": cmd_fit, "refine": cmd_refine}[args.command](args)


if __name__ == "__main__":
    main()
