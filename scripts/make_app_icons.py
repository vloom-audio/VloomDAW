#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vloom Audio contributors
"""Builds the app and project document icons from
resources/app/vloomdaw-source.png (2048 x 2048, transparent background):

  vloomdaw.ico       Windows: 16 .. 256 px, the executable icon (index 0)
  vloom-project.ico  Windows: 16 .. 256 px, the .vloom project document icon (index 1)
  vloomdaw.icns      macOS: 16 .. 1024 px, artwork 824 / 1024 (Apple's icon grid)
  vloomdaw.png       512 px, the window icon set through Qt (all platforms)
  vloom-project.png  512 px, the .vloom project document artwork

Needs Pillow:  python -m pip install pillow==11.3.0
Run from the repository root:  python scripts/make_app_icons.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

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


def project_document(art: Image.Image) -> Image.Image:
    """Renders the 1024x1024 folded studio document icon with the Vloom emblem."""
    s = 1024
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    left, top, right, bottom, fold = 136, 40, 888, 976, 196
    poly = [(left, top), (right - fold, top), (right, top + fold), (right, bottom), (left, bottom)]

    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(x, y + 18) for x, y in poly], fill=(0, 0, 0, 120))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(20)))

    sheet = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sheet)
    sd.polygon(poly, fill=(24, 12, 42, 255), outline=(236, 72, 153, 220), width=10)
    emblem = art.resize((444, 444), Image.Resampling.LANCZOS)
    sheet.alpha_composite(emblem, ((s - 444) // 2, 260))

    font_path = ROOT / "resources" / "fonts" / "NotoSans-Bold.ttf"
    font = ImageFont.truetype(str(font_path), 70) if font_path.exists() else ImageFont.load_default()
    sd.rounded_rectangle((222, 774, 802, 898), radius=38, fill=(22, 11, 36, 240), outline=(168, 85, 247, 130), width=4)
    sd.text((512, 830), "VLOOM", fill=(248, 250, 252, 255), font=font, anchor="mm")
    sd.polygon([(right - fold, top), (right, top + fold), (right - fold, top + fold)], fill=(76, 40, 124, 255))
    out.alpha_composite(sheet)
    return out


def main() -> None:
    art = artwork(Image.open(SOURCE).convert("RGBA"))

    # Windows and the Qt window icon: a 2 % margin keeps antialiased edges whole.
    full = padded(art, 1024, 1004)
    full.save(APP / "vloomdaw.ico", sizes=[(s, s) for s in ICO_SIZES])
    full.resize((512, 512), Image.Resampling.LANCZOS).save(APP / "vloomdaw.png", optimize=True)

    doc = project_document(art)
    doc.save(APP / "vloom-project.ico", sizes=[(s, s) for s in ICO_SIZES])
    doc.resize((512, 512), Image.Resampling.LANCZOS).save(APP / "vloom-project.png", optimize=True)

    padded(art, MAC_CANVAS, MAC_ART).save(APP / "vloomdaw.icns")
    print("wrote", ", ".join(p.name for p in sorted(APP.glob("vloom*.*"))))


if __name__ == "__main__":
    main()
