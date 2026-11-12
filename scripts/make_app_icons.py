#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vloom Audio contributors
"""Builds the app icons from resources/app/vloomdaw-source.png (2048 x 2048,
transparent background):

  vloomdaw.ico   Windows: 16 .. 256 px, the artwork fills the square
  vloomdaw.icns  macOS: 16 .. 1024 px, artwork 824 / 1024 (Apple's icon grid)
  vloomdaw.png   512 px, the window icon set through Qt (all platforms)

Needs Pillow:  python -m pip install pillow==11.3.0
Run from the repository root:  python scripts/make_app_icons.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "resources" / "app"
SOURCE = APP / "vloomdaw-source.png"

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
MAC_CANVAS, MAC_ART = 1024, 824


def artwork(image: Image.Image) -> Image.Image:
    """The icon square alone: opaque bounds, stray faint pixels around it dropped."""
    opaque = image.getchannel("A").point(lambda v: 255 if v > 200 else 0)
    left, top, right, bottom = opaque.getbbox()
    side = max(right - left, bottom - top)
    cx, cy = (left + right) // 2, (top + bottom) // 2
    box = (cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side)
    return image.crop(box)


def padded(art: Image.Image, canvas: int, size: int) -> Image.Image:
    out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    scaled = art.resize((size, size), Image.Resampling.LANCZOS)
    offset = (canvas - size) // 2
    out.alpha_composite(scaled, (offset, offset))
    return out


def main() -> None:
    art = artwork(Image.open(SOURCE).convert("RGBA"))

    # Windows and the Qt window icon: a 2 % margin keeps antialiased edges whole.
    full = padded(art, 1024, 1004)
    full.save(APP / "vloomdaw.ico", sizes=[(s, s) for s in ICO_SIZES])
    full.resize((512, 512), Image.Resampling.LANCZOS).save(APP / "vloomdaw.png", optimize=True)

    padded(art, MAC_CANVAS, MAC_ART).save(APP / "vloomdaw.icns")
    print("wrote", ", ".join(p.name for p in sorted(APP.glob("vloomdaw.*"))))


if __name__ == "__main__":
    main()
