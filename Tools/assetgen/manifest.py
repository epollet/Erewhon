"""Ce que l'on génère, et comment on le décrit aux modèles.

La direction artistique vise une ambiance pastorale peinte à la main : on décrit
le style plutôt que de citer un studio.
"""
from __future__ import annotations

STYLE = (
    "hand-painted anime pastoral art style, soft watercolor shading, gentle warm sunlight, "
    "rich but soft saturated colors, visible painterly brush texture, cozy countryside fantasy"
)

# ── Textures tuilables (Gemini) ─────────────────────────────────────────────
# Le nom devient T_<nom> dans /Game/Generated/Textures.
TEXTURES: dict[str, str] = {
    "ground_grass": "a lush meadow ground seen from above: short grass, clover leaves and tiny moss patches, mostly fresh green with a few warm yellow-green strokes",
    "ground_dirt": "a packed bare dirt country path seen from above, warm ochre and light brown earth with small scattered pebbles, no grass, no plants",
    "wood_planks": "horizontal weathered wooden wall planks, warm honey brown wood with darker seams",
    "roof_tiles": "overlapping terracotta clay roof tiles in rows, warm red-orange with soft moss accents",
    "stone": "rough rounded fieldstone foundation blocks with mortar, warm grey with hints of green moss",
    "wood_trim": "a single dark stained wooden plank surface with long straight parallel grain lines running horizontally, chocolate brown with warm highlights, no joints, no weave",
    "stone_slabs": "a floor of large irregular flat flagstone slabs fitted together with thin dark joints, warm grey and beige stone, slightly worn",
    "plaster": "a smooth lime plaster cottage wall, warm off-white cream with very subtle soft patches and faint brush marks, no bricks, no cracks",
    "roof_slate": "overlapping rounded slate roof shingles in rows, soft blue-grey with a few lighter and greener shingles",
    "floor_boards": "a wooden floor of long parallel planks running vertically, warm golden oak with darker thin joints and small knots",
}

TEXTURE_PROMPT = (
    "Seamless tileable square texture: {subject}. {style}. Flat even lighting, no cast shadows, "
    "no perspective, orthographic top-down, the pattern fills the whole frame edge to edge, no text, no border."
)

# ── Images peintes non tuilables (Gemini) : panneaux, carte murale ──────────
# nom → (description, format). Importées comme textures T_<nom>.
PAINTINGS: dict[str, tuple[str, str]] = {
    "sign_general_store": ("a plain classic rectangular wooden shop sign: a few horizontal dark weathered planks, the words \"General Store\" painted in large clean cream serif capital letters, centered, no decorations, no flowers, no symbols, no border ornaments, the sign fills the entire frame edge to edge, flat front view", "21:9"),
    "sign_sector_manager": ("a plain classic rectangular wooden shop sign: a few horizontal dark weathered planks, the words \"Sector Manager\" painted in large clean cream serif capital letters, centered, no decorations, no flowers, no symbols, no border ornaments, the sign fills the entire frame edge to edge, flat front view", "21:9"),
    "sign_tailor": ("a plain classic rectangular wooden shop sign: a few horizontal dark weathered planks, the words \"Tailor\" painted in large clean cream serif capital letters, centered, no decorations, no flowers, no symbols, no border ornaments, the sign fills the entire frame edge to edge, flat front view", "21:9"),
    "map_wall": ("a hand-painted parchment map of a countryside divided into a grid of square plots, meadows, a crossroads of dirt roads and a small cottage at the center, compass rose, no readable text, the parchment fills the entire frame", "4:3"),
}

PAINTING_PROMPT = "{subject}. {style}. No border outside the object, no extra text."

# ── Icônes d'inventaire (Gemini) ────────────────────────────────────────────
# Une par objet de Content/Data/Items.json → T_Icon_<id>.
ICONS: dict[str, str] = {
    "grass": "a small bundle of tall fresh green grass blades tied with a straw string",
    "flower_white": "a white daisy flower with a yellow heart",
    "flower_blue": "a small bouquet of blue cornflowers",
    "flower_purple": "a purple bellflower stem",
    "herb": "a sprig of aromatic thyme herb with small green leaves",
    "cerpe": "a small hand sickle with a curved steel blade and a wooden handle",
    "backpack": "a sturdy rustic canvas backpack in olive green with brown leather straps, a buckled flap and two side loops",
    "tshirt_orange": "a folded orange cotton t-shirt",
    "tshirt_blue": "a folded blue cotton t-shirt",
    "veste_dark": "a dark navy blue work jacket with two pockets",
    "manteau_brun": "a long warm brown wool coat with many pockets",
    "jean": "a pair of folded blue denim jeans",
    "short_red": "a pair of red cotton shorts",
    "short_blue": "a pair of blue cotton shorts",
    "fern": "a single green fern frond",
    "broadleaf": "a large broad round green leaf",
    "weed_dock": "a bunch of dock leaves with a reddish seed stalk",
    "weed_nettle": "a small bunch of fresh nettle and mint sprigs",
}

ICON_PROMPT = (
    "Game inventory icon of {subject}. {style}. Single object, centered, filling about 80% of the frame, "
    "soft rim light, plain warm parchment-beige background, no text, no frame, no border."
)

# ── Modèles 3D (Gemini → Meshy) ─────────────────────────────────────────────
# Une image de concept est générée d'abord, puis convertie en modèle par Meshy.
MODELS: dict[str, dict] = {
    "tailor": {
        "subject": "a cheerful middle-aged countryside tailor woman standing upright, yellow measuring tape draped around her neck, rolled-up sleeves on a white blouse, a teal patterned headscarf, a long rust-red skirt with a small apron holding pins, kind smile, arms relaxed along the body, full body",
        "polycount": 12000,
    },
    "merchant": {
        "subject": "a friendly old countryside merchant standing upright, round straw hat, green apron over a cream shirt, brown trousers, kind smile, arms relaxed along the body, full body",
        "polycount": 12000,
    },
    "cerpe": {
        "subject": "a small hand sickle with a curved steel blade and a long smooth wooden handle",
        "polycount": 3000,
    },
    "flower_white": {
        "subject": "a single wild daisy plant: one slender green stem with a few leaves and one white daisy blossom with a yellow heart on top",
        "polycount": 2500,
    },
    "flower_blue": {
        "subject": "a single wild cornflower plant: a slender green stem with thin leaves and two bright blue cornflower blossoms on top",
        "polycount": 2500,
    },
    "flower_purple": {
        "subject": "a single wild bellflower plant: a slender green stem with a few leaves and three hanging violet bell-shaped flowers",
        "polycount": 2500,
    },
    "herb": {
        "subject": "a small round clump of aromatic thyme herb with many tiny green leaves and a few tiny pink flowers",
        "polycount": 3000,
    },
    "barrel": {
        "subject": "an old wooden barrel with dark iron bands",
        "polycount": 3000,
    },
    "fern": {
        "subject": "a lush wild fern plant: many long arching green fronds with small leaflets, all growing from one base",
        "polycount": 4000,
    },
    "broadleaf": {
        "subject": "a low wild plant clump with a few very large broad rounded green leaves on short stems, like burdock or hosta",
        "polycount": 3000,
    },
    "bush": {
        "subject": "a round dense wild meadow bush with small fresh green leaves and a few tiny white flowers",
        "polycount": 4000,
    },
    "counter": {
        "subject": "a long rustic wooden shop counter: thick worn plank top, front panel of vertical planks, a few glass jars of preserves and a small brass scale on top",
        "polycount": 5000,
    },
    "weed_dock": {
        "subject": "a wild dock weed plant: a clump of long broad green leaves with tall reddish-brown seed stalks rising above",
        "polycount": 3000,
    },
    "weed_nettle": {
        "subject": "a dense clump of young wild nettles and mint: upright stems with many serrated green leaves",
        "polycount": 3000,
    },
    "bush_hazel": {
        "subject": "a tall wild hazel shrub: many upright leafy branches with round fresh green leaves, taller than wide",
        "polycount": 5000,
    },
    "bush_rose": {
        "subject": "a large rounded wild rose bush with dense green foliage and many small soft pink single flowers",
        "polycount": 5000,
    },
    "bush_box": {
        "subject": "a dense rounded dark green shrub with tiny glossy leaves, like an old wild boxwood, slightly irregular shape",
        "polycount": 4000,
    },
    "door_wood": {
        "subject": "a rustic wooden cottage door leaf only, without any frame or wall: vertical planks with a Z-shaped brace, black iron strap hinges and a round iron ring handle, seen straight from the front",
        "polycount": 4000,
    },
    "shutter": {
        "subject": "a single rustic wooden window shutter seen from the front: vertical planks painted soft teal blue with a Z-shaped brace and a small heart cut-out at the top",
        "polycount": 3000,
    },
    "window_box": {
        "subject": "a long wooden window flower box planter overflowing with small colorful flowers and trailing green ivy",
        "polycount": 5000,
    },
    "props_jars": {
        "subject": "a row of five glass jars of homemade preserves and grains with cloth-covered lids tied with string, standing on nothing, no shelf, no labels with text",
        "polycount": 4000,
    },
    "props_sacks": {
        "subject": "a small group of three plain burlap sacks of grain and flour tied with rope, slumped together, completely blank fabric with no writing or symbols",
        "polycount": 3000,
    },
    "props_bottles": {
        "subject": "a group of plain ceramic and glass bottles with corks and one round clay jug with a handle, no labels, no text",
        "polycount": 4000,
    },
    "cat_tabby": {
        "subject": "a gray striped tabby cat with bright yellow eyes, sitting calmly upright, tail wrapped around its front paws, full body",
        "polycount": 6000,
    },
    "bird": {
        "subject": "a small swallow-like songbird in flight seen from above at a slight angle, wings fully spread wide and flat, forked tail, dark blue back and cream belly",
        "polycount": 3000,
    },
    "sector_manager": {
        "subject": "a friendly countryside land surveyor manager standing upright, round spectacles, neat waistcoat over a white shirt with rolled sleeves, flat cap, holding a rolled map under one arm, kind smile, full body",
        "polycount": 12000,
    },
    "desk_maps": {
        "subject": "a sturdy rustic wooden desk covered with rolled maps, an open map, a brass compass, an inkwell with a quill and a small oil lamp",
        "polycount": 6000,
    },
    "crates": {
        "subject": "two rustic wooden crates stacked, the top one filled with apples",
        "polycount": 3000,
    },
}

CONCEPT_PROMPT = (
    "Concept render of {subject}. {style}. Stylized game asset, single object, centered, the entire object "
    "visible with margin around it, three-quarter front view, plain pure white background, no ground shadow, no text."
)


# ── Sons (ElevenLabs) ───────────────────────────────────────────────────────
# nom → (description, durée en s ou None, boucle). Fichiers : SourceArt/Generated/sounds/<nom>.wav
# Un 4e élément facultatif ne garde qu'un seul impact (durée en s) : un son de pas = un pas.
SOUNDS: dict[str, tuple] = {
    "amb_wind": ("gentle steady wind blowing across a wide open countryside meadow, soft airy whoosh, no birds, no voices", 20.0, True),
    "amb_grass": ("continuous soft rustling of tall dry grass brushing against legs while walking through a meadow", 8.0, True),
    "step_grass_1": ("one single soft footstep of a walking person on thick meadow grass, one step only", 0.5, False, 0.32),
    "step_grass_2": ("one single footstep in tall soft grass, gentle swish, one step only", 0.5, False, 0.32),
    "step_grass_3": ("one single light footstep on grass over soft earth, one step only", 0.5, False, 0.32),
    "step_dirt_1": ("one single footstep of a leather boot on a packed dirt path, slight gravel crunch, one step only", 0.5, False, 0.3),
    "step_dirt_2": ("one single footstep on a dry dusty country path, one step only", 0.5, False, 0.3),
    "step_dirt_3": ("one single boot step on firm earth with small pebbles, one step only", 0.5, False, 0.3),
    "step_stone_1": ("one single footstep of a soft leather boot on a stone slab floor inside a small cottage, short and dry, one step only", 0.5, False, 0.28),
    "step_stone_2": ("one single quiet heel step on a flagstone floor indoors, one step only", 0.5, False, 0.28),
    "step_wood_1": ("one single footstep of a leather boot on solid old wooden floorboards, warm muted knock, one step only", 0.5, False, 0.3),
    "step_wood_2": ("one single footstep on a wooden porch plank, soft hollow knock, one step only", 0.5, False, 0.3),
    "door_open": ("a short creak of old iron hinges as a wooden door swings open, one creak", 1.0, False, 0.75),
    "door_close": ("a wooden cottage door shutting with a soft wooden thump and a small latch click", 0.6, False, 0.5),
    "bird_chirp_1": ("a small songbird chirping twice, short and bright, outdoors", 1.0, False),
    "bird_chirp_2": ("a swallow twittering quickly in flight", 1.2, False),
    "bird_chirp_3": ("a single sparrow chirp, clear, distant countryside", 0.8, False),
    "bird_song_1": ("a skylark singing a short melodic song high above a meadow", 4.0, False),
    "bird_song_2": ("a blackbird singing a short gentle phrase in the countryside", 3.5, False),
}
