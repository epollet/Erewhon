"""Atlas de la police manuscrite des panneaux (Kalam Bold, licence OFL).

Chaque caractère est rendu en blanc (opacité = couverture) dans une grande image, et ses mesures
sont écrites dans Content/Data/FontHand.json : le jeu dessine ensuite le texte lettre par lettre
sur les planches, sans dépendre du cache de polices de Slate.

    python Tools/assetgen/font_atlas.py
"""
from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "SourceArt" / "Fonts" / "Kalam-Bold.ttf"
ATLAS = ROOT / "SourceArt" / "Generated" / "textures" / "font_hand.png"
METRICS = ROOT / "Content" / "Data" / "FontHand.json"

SIZE = 96          # px
WIDTH = 2048       # largeur de l'atlas
PAD = 6
CHARS = "".join(chr(c) for c in range(32, 127)) + "éèêëàâäçùûüôöîïœÉÈÊÀÂÇÙÛÔÎ·’«»–"


def build() -> None:
    font = ImageFont.truetype(str(FONT), SIZE)
    ascent, descent = font.getmetrics()
    line = ascent + descent
    glyphs = {}
    cells = []
    x, y = PAD, PAD
    for ch in CHARS:
        left, _, right, _ = font.getbbox(ch, anchor="ls")
        width = max(1, right - left) + 2 * PAD
        if x + width > WIDTH:
            x, y = PAD, y + line + 2 * PAD
        cells.append((ch, x, y, width, left))
        # Clé = code du caractère : les clés JSON sont lues sans distinction de casse dans Unreal.
        glyphs[str(ord(ch))] = {"x": x, "y": y, "w": width, "h": line + 2 * PAD,
                      "xoff": left - PAD, "advance": font.getlength(ch)}
        x += width + PAD
    height = 1
    while height < y + line + 3 * PAD:
        height *= 2

    image = Image.new("RGBA", (WIDTH, height), (255, 255, 255, 0))
    mask = Image.new("L", (WIDTH, height), 0)
    draw = ImageDraw.Draw(mask)
    for ch, cx, cy, _, left in cells:
        draw.text((cx + PAD - left, cy + PAD + ascent), ch, font=font, fill=255, anchor="ls")
    image.putalpha(mask)
    ATLAS.parent.mkdir(parents=True, exist_ok=True)
    image.save(ATLAS)

    METRICS.write_text(json.dumps({
        "_doc": "Atlas T_font_hand (Kalam Bold, OFL) : position et mesures de chaque caractère (clé = code Unicode), en pixels.",
        "size": SIZE, "line": line + 2 * PAD, "ascent": ascent, "width": WIDTH, "height": height,
        "glyphs": glyphs,
    }, ensure_ascii=False, indent=0), encoding="utf-8", newline="\n")
    print(f"atlas {WIDTH}x{height}, {len(glyphs)} caractères : {ATLAS.name}, {METRICS.name}")


if __name__ == "__main__":
    build()
