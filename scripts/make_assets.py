#!/usr/bin/env python3
"""Regenerate the mod's panel textures and shortcut badge.

The panels are clean nine-slice textures (16 px insets on a 64 px canvas):
no baked-in arrows, rules or ornaments, so live UI never collides with art.
Run from the repository root:  python3 scripts/make_assets.py
"""

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "resources"
SIZE = 64


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def panel(name, outline, highlight, shadow, fill_top, fill_bottom, hairline):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Outer keyline, then a bevel: light on the top/left, dark on the bottom/right.
    d.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=9, fill=outline + (255,))
    d.rounded_rectangle([2, 2, SIZE - 3, SIZE - 3], radius=7, fill=shadow + (255,))
    d.rounded_rectangle([2, 2, SIZE - 5, SIZE - 5], radius=7, fill=highlight + (255,))
    d.rounded_rectangle([4, 4, SIZE - 3, SIZE - 3], radius=6, fill=shadow + (255,))
    d.rounded_rectangle([6, 6, SIZE - 7, SIZE - 7], radius=5, fill=hairline + (255,))

    # Vertical gradient fill, masked to the inner rounded rect so the
    # nine-slice center stretches smoothly at any panel size.
    grad = Image.new("RGBA", (SIZE, SIZE))
    gd = ImageDraw.Draw(grad)
    for y in range(SIZE):
        gd.line([(0, y), (SIZE, y)], fill=lerp(fill_top, fill_bottom, y / (SIZE - 1)) + (255,))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle([7, 7, SIZE - 8, SIZE - 8], radius=4, fill=255)
    img.paste(grad, (0, 0), mask)

    img.save(RES / name)
    print(f"wrote {name}")


def badge():
    """Circular shortcut badge: navy disc, gold ring, extreme-demon face."""
    s = 256
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([6, 6, s - 7, s - 7], fill=(9, 18, 32, 255))
    d.ellipse([10, 10, s - 11, s - 11], outline=(214, 170, 100, 255), width=5)
    d.ellipse([17, 17, s - 18, s - 18], outline=(38, 62, 92, 255), width=2)
    face = Image.open(RES / "extreme-demon.png").convert("RGBA")
    face = face.resize((176, 176), Image.LANCZOS)
    img.alpha_composite(face, ((s - 176) // 2, (s - 176) // 2))
    img.save(RES / "floating-icon.png")
    print("wrote floating-icon.png")


if __name__ == "__main__":
    panel(
        "leaderboard-bg.png",
        outline=(9, 25, 43), highlight=(122, 192, 233), shadow=(15, 50, 86),
        fill_top=(46, 122, 194), fill_bottom=(27, 82, 140), hairline=(19, 62, 106),
    )
    panel(
        "leaderboard-bg-midnight.png",
        outline=(4, 7, 12), highlight=(72, 90, 114), shadow=(10, 15, 23),
        fill_top=(31, 41, 57), fill_bottom=(18, 25, 37), hairline=(12, 18, 28),
    )
    badge()
