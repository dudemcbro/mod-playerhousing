#!/usr/bin/env python3
"""Turns the photo tour's screenshots into the addon's pictures of buildings.

A GM on their own island runs /housing phototour in the game: each building is set up in front
of the camera, the addon hides the interface, takes a screenshot and remembers which building
it showed (PlayerHousingDB.photos, saved when the character logs out). This script finds those
screenshots, cuts a square from the middle of each, scales it to 256x256 and writes it to the
addon as Pictures/<item>.tga, then lists them in Pictures.lua for the preview.

    python3 make_pictures.py --wow "/path/to/World of Warcraft" [--character NAME] [--size 256]

Needs Pillow (Ubuntu or Debian: apt install python3-pil). Run it again after copying a new
version of the addon over the old one: it rebuilds Pictures.lua from the pictures already there.
"""

import argparse
import datetime
import glob
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is missing: apt install python3-pil (or pip install pillow)")


def find_dir(parent, name):
    """A folder by name, whatever its letter case (clients copied from Windows often differ)."""
    if not os.path.isdir(parent):
        return None
    for entry in os.listdir(parent):
        if entry.lower() == name.lower() and os.path.isdir(os.path.join(parent, entry)):
            return os.path.join(parent, entry)
    return None


def find_path(root, *parts):
    path = root
    for part in parts:
        path = find_dir(path, part)
        if not path:
            return None
    return path


def saved_photos(wow, character):
    """{item: 'MMDDYY_HHMMSS'} from PlayerHousing's saved variables, and where they came from."""
    wtf = find_path(wow, "WTF", "Account")
    if not wtf:
        sys.exit("No WTF/Account folder under %s: is that the client's folder?" % wow)
    found = []
    for path in glob.glob(os.path.join(wtf, "*", "*", "*", "SavedVariables", "*")):
        if os.path.basename(path).lower() != "playerhousing.lua":
            continue
        char = os.path.basename(os.path.dirname(os.path.dirname(path)))
        if character and char.lower() != character.lower():
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        block = re.search(r'\["photos"\]\s*=\s*\{(.*?)\}', text, re.S)
        if block:
            photos = {int(item): when for item, when in re.findall(r'\[(\d+)\]\s*=\s*"(\d{6}_\d{6})"', block.group(1))}
            if photos:
                found.append((os.path.getmtime(path), char, photos))
    if not found:
        sys.exit("No photo tour in the saved variables yet. In the game: go home, /housing phototour, then log out.")
    found.sort(key=lambda entry: entry[0])  # the newest save wins
    return found[-1][1], found[-1][2]


def screenshot_times(wow):
    """{datetime: path} of the client's screenshots."""
    folder = find_dir(wow, "Screenshots")
    if not folder:
        sys.exit("No Screenshots folder under %s." % wow)
    shots = {}
    for name in os.listdir(folder):
        match = re.match(r"WoWScrnShot_(\d{6})_(\d{6})\.(jpg|jpeg|tga|png)$", name, re.I)
        if match:
            when = datetime.datetime.strptime(match.group(1) + match.group(2), "%m%d%y%H%M%S")
            shots[when] = os.path.join(folder, name)
    return shots


def square(image, size):
    """The middle of the picture, square, where the tour puts the building."""
    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    return image.crop((left, top, left + side, top + side)).convert("RGB").resize((size, size), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wow", required=True, help="the client's folder (the one with Wow.exe)")
    parser.add_argument("--character", help="whose saved photo tour to use (default: the newest)")
    parser.add_argument("--size", type=int, default=256, help="pixels square (a power of two: 128, 256 or 512)")
    args = parser.parse_args()

    addon = find_path(args.wow, "Interface", "AddOns", "PlayerHousing")
    if not addon:
        sys.exit("The PlayerHousing addon isn't in %s/Interface/AddOns: copy it there first." % args.wow)
    pictures = os.path.join(addon, "Pictures")
    os.makedirs(pictures, exist_ok=True)

    made = 0
    missing = []
    try:
        character, photos = saved_photos(args.wow, args.character)
        shots = screenshot_times(args.wow)
    except SystemExit as stop:
        # Nothing new to convert: still list the pictures already there.
        print(stop)
        photos, shots, character = {}, {}, None
    for item, stamp in sorted(photos.items()):
        taken = datetime.datetime.strptime(stamp, "%m%d%y_%H%M%S")
        # The file's time can be a second or two after the addon noted it.
        near = [when for when in shots if abs((when - taken).total_seconds()) <= 3]
        if not near:
            missing.append(item)
            continue
        best = min(near, key=lambda when: abs((when - taken).total_seconds()))
        square(Image.open(shots[best]), args.size).save(os.path.join(pictures, "%d.tga" % item))
        made += 1
    if character:
        print("%s's photo tour: %d pictures made%s." % (character, made,
              (", no screenshot found for %d (%s)" % (len(missing), ", ".join(map(str, missing[:8])))) if missing else ""))

    have = sorted(int(name[:-4]) for name in os.listdir(pictures) if re.match(r"\d+\.tga$", name, re.I))
    lines = ["-- Buildings with a picture for the preview (Pictures/<item>.tga), written by",
             "-- tools/pictures/make_pictures.py after a GM's photo tour (/housing phototour).",
             "PlayerHousing_Pictures = {"]
    lines += ["    [%d] = true," % item for item in have]
    lines.append("}")
    with open(os.path.join(addon, "Pictures.lua"), "w") as out:
        out.write("\n".join(lines) + "\n")
    print("Pictures.lua lists %d buildings. /reload in the game to see them." % len(have))


if __name__ == "__main__":
    main()
