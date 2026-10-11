"""Touffes d'herbe procédurales, exportées en glTF (.gltf avec buffer embarqué).

Chaque brin est un ruban courbé et effilé. Les couleurs de sommet portent le
dégradé peint (pied sombre, pointe claire) et l'alpha porte le poids du vent :
0 au pied, 1 à la pointe — le matériau M_Foliage s'en sert pour l'ondulation.
Gabarit de 1 m de haut : le jeu met chaque touffe à l'échelle de sa hauteur.
"""
from __future__ import annotations

import base64
import json
import math
import random
import struct
from pathlib import Path

def _srgb(r, g, b):
    """Couleur sRGB 0-255 → linéaire : glTF stocke les couleurs de sommet en linéaire."""
    def channel(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (channel(r), channel(g), channel(b))


# Variantes : (nom, brins, rayon de la touffe en m, couleur du pied, couleur de la pointe, épis)
VARIANTS = [
    ("grass_tall_a", 64, 0.42, _srgb(72, 124, 46), _srgb(142, 198, 78), False),
    ("grass_tall_b", 54, 0.36, _srgb(80, 130, 44), _srgb(170, 206, 90), False),
    ("grass_seed", 30, 0.22, _srgb(74, 126, 46), _srgb(158, 196, 82), True),
]

_SEGMENTS = 4


def _lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _blade(rng: random.Random, base_xz, root_color, tip_color, seed_head: bool, dome: float = 1.0):
    """Sommets (pos, normale, couleur RGBA) et triangles d'un brin.

    dome ∈ ]0, 1] raccourcit les brins du bord : la touffe prend une forme de coussin.
    """
    height = rng.uniform(0.6, 1.0) * dome
    width = rng.uniform(0.045, 0.075)
    # Les brins du bord penchent vers l'extérieur, ceux du centre se dressent.
    outward = math.atan2(base_xz[1], base_xz[0]) if (base_xz[0] or base_xz[1]) else rng.uniform(0, math.tau)
    lean_angle = outward + rng.uniform(-0.8, 0.8)
    lean = rng.uniform(0.25, 0.6) * height * (1.6 - dome)
    # Le ruban fait face à une direction indépendante de son inclinaison.
    face = lean_angle + math.pi / 2 + rng.uniform(-0.6, 0.6)
    side = (math.cos(face), math.sin(face))
    lean_dir = (math.cos(lean_angle), math.sin(lean_angle))
    hue_shift = rng.uniform(-0.04, 0.04)

    vertices, indices = [], []
    for i in range(_SEGMENTS + 1):
        t = i / _SEGMENTS
        y = height * t
        bend = lean * t * t
        cx = base_xz[0] + lean_dir[0] * bend
        cz = base_xz[1] + lean_dir[1] * bend
        half = width * 0.5 * (1.0 - t) ** 0.8 + 0.001
        color = _lerp(root_color, tip_color, t ** 0.9)
        color = (min(1, color[0] + hue_shift), color[1], max(0, color[2] - hue_shift * 0.5))
        # Normales tirées vers le haut : éclairage doux et uniforme, sans facettes.
        normal = (side[1] * 0.35, 0.94, -side[0] * 0.35)
        for s in (-1, 1):
            vertices.append(((cx + side[0] * half * s, y, cz + side[1] * half * s), normal, (*color, t ** 1.5)))
    for i in range(_SEGMENTS):
        a = i * 2
        indices += [a, a + 1, a + 2, a + 1, a + 3, a + 2]

    if seed_head:
        # Épi doré : deux losanges croisés au sommet.
        tip_x = base_xz[0] + lean_dir[0] * lean
        tip_z = base_xz[1] + lean_dir[1] * lean
        gold = _srgb(236, 208, 128)
        for angle in (face, face + math.pi / 2):
            d = (math.cos(angle) * 0.011, math.sin(angle) * 0.011)
            start = len(vertices)
            for (dx, dy, dz) in ((0, -0.01, 0), (d[0], 0.04, d[1]), (0, 0.10, 0), (-d[0], 0.04, -d[1])):
                vertices.append(((tip_x + dx, height + dy, tip_z + dz), (0, 1, 0), (*gold, 1.0)))
            indices += [start, start + 1, start + 2, start, start + 2, start + 3]
    return vertices, indices


def build_clump(name: str, blades: int, radius: float, root, tip, seed_heads: bool, seed: int):
    rng = random.Random(seed)
    vertices, indices = [], []
    for _ in range(blades):
        r = radius * math.sqrt(rng.random())
        a = rng.uniform(0, math.tau)
        dome = 1.0 - 0.5 * (r / radius) ** 2
        blade_vertices, blade_indices = _blade(rng, (r * math.cos(a), r * math.sin(a)), root, tip,
                                               seed_heads and rng.random() < 0.3, dome)
        offset = len(vertices)
        vertices += blade_vertices
        indices += [offset + i for i in blade_indices]
    return vertices, indices


def write_gltf(path: Path, name: str, vertices, indices) -> None:
    positions = b"".join(struct.pack("<3f", *v[0]) for v in vertices)
    normals = b"".join(struct.pack("<3f", *v[1]) for v in vertices)
    colors = b"".join(struct.pack("<4f", *v[2]) for v in vertices)
    index_bytes = b"".join(struct.pack("<H", i) for i in indices)
    pad = b"\x00" * ((4 - len(index_bytes) % 4) % 4)
    blob = positions + normals + colors + index_bytes + pad

    xs = [v[0][0] for v in vertices]
    ys = [v[0][1] for v in vertices]
    zs = [v[0][2] for v in vertices]
    n = len(vertices)
    views = [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(positions), "target": 34962},
        {"buffer": 0, "byteOffset": len(positions), "byteLength": len(normals), "target": 34962},
        {"buffer": 0, "byteOffset": len(positions) + len(normals), "byteLength": len(colors), "target": 34962},
        {"buffer": 0, "byteOffset": len(positions) + len(normals) + len(colors), "byteLength": len(index_bytes), "target": 34963},
    ]
    gltf = {
        "asset": {"version": "2.0", "generator": "Erewhon grass.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "materials": [{"name": "M_Foliage", "doubleSided": True}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2},
            "indices": 3, "material": 0,
        }]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3",
             "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode("ascii")}],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(gltf), encoding="utf-8")


def generate_all(out_dir: Path) -> list[Path]:
    paths = []
    for seed, (name, blades, radius, root, tip, seed_heads) in enumerate(VARIANTS):
        vertices, indices = build_clump(name, blades, radius, root, tip, seed_heads, seed * 7919 + 17)
        path = out_dir / f"{name}.gltf"
        write_gltf(path, name, vertices, indices)
        paths.append(path)
    return paths
