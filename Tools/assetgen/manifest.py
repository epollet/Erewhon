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
    "cobbles": "a village path of rounded cobblestones seen from above, warm grey and ochre stones of varied sizes tightly fitted with thin earthy joints, a few tiny moss tufts",
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
    "sign_apothecary": ("a plain classic rectangular wooden shop sign: a few horizontal dark weathered planks, the word \"Apothecary\" painted in large clean cream serif capital letters, centered, no decorations, no flowers, no symbols, no border ornaments, the sign fills the entire frame edge to edge, flat front view", "21:9"),
    "flame": ("a single stylized hand-painted campfire flame shape seen from the front: one tall soft teardrop of fire with a few licking tongues, bright yellow-white core fading to orange then deep red at the tips, centered, filling the frame height, on a pure flat black background, nothing else", "1:1"),
    "sign_town_hall": ("a plain classic rectangular wooden shop sign: a few horizontal dark weathered planks, the word \"Mairie\" painted in large clean cream serif capital letters, centered, no decorations, no flowers, no symbols, no border ornaments, the sign fills the entire frame edge to edge, flat front view", "21:9"),
    "moon": ("a full moon seen through a telescope: a perfectly round pale cream and soft grey disc with gentle painted craters and maria, softly glowing edge, centered, filling about 90% of the frame, on a pure flat black background, nothing else", "1:1"),
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
    "cart": "a rustic two-wheeled wooden hand cart with two long handles",
    "crate": "an empty open wooden harvest crate made of light slats",
    "barrel": "a sturdy oak barrel with dark iron hoops",
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
    "shelf": "a tall narrow rustic wooden shelf unit with four empty shelves",
    "table": "a sturdy rustic wooden work table with four legs",
    "mortar": "a grey stone mortar with a stone pestle and a few green leaves",
    "cauldron": "a small black cast iron cauldron on three legs with a bubbling green potion",
    "dried_grass": "a small bundle of dried golden grass tied with twine",
    "dried_flower_white": "a small bundle of dried white daisies tied with twine",
    "dried_flower_blue": "a small bundle of dried blue cornflowers tied with twine",
    "dried_flower_purple": "a small bundle of dried violet bellflowers tied with twine",
    "dried_herb": "a small bundle of dried thyme sprigs tied with twine",
    "dried_fern": "a dried brownish fern frond",
    "dried_broadleaf": "a few dried broad plantain leaves tied with twine",
    "dried_weed_dock": "a bundle of dried dock leaves with rusty seed stalks tied with twine",
    "dried_weed_nettle": "a small bundle of dried nettle sprigs tied with twine",
    "dye_blue": "a small corked glass bottle of deep blue fabric dye with a cloth swatch dyed blue tied to its neck",
    "dye_purple": "a small corked glass bottle of rich violet fabric dye with a violet cloth swatch tied to its neck",
    "dye_green": "a small corked glass bottle of deep green fabric dye with a green cloth swatch tied to its neck",
    "dye_rust": "a small corked glass bottle of rusty red-brown fabric dye with a rust colored cloth swatch tied to its neck",
    "bouquet_fresh": "a small terracotta pot holding a fresh meadow bouquet of white daisies, blue cornflowers, violet bellflowers and a fern frond",
    "bouquet_dry": "a small terracotta pot holding a bouquet of dried flowers and golden dried grass",
    "potpourri": "a small round wooden bowl of colorful dried flower petals and lavender, with a soft sweet glow",
    "drying_rack": "a rustic wooden herb drying rack with bundles of herbs hanging upside down",
    "workbench": "a small rustic apothecary workbench with jars, twine and scissors",
    "mushroom_cep": "a pair of plump wild porcini mushrooms with brown caps",
    "mushroom_chanterelle": "a small handful of golden chanterelle mushrooms",
    "pitcher": "an empty rustic glazed terracotta water pitcher with a handle",
    "pitcher_water": "a rustic glazed terracotta water pitcher with a handle, filled to the brim with clear fresh water",
    "pitcher_brew": "a rustic glazed terracotta pitcher with a handle, filled with a glowing colorful herbal brew giving off a little steam",
    "vial": "a single small empty clear glass vial with a cork stopper",
    "cauldron_stand": "a black wrought iron tripod stand with an adjustable ratchet hook for hanging a cauldron over a fire",
    "herbal_book": "a thick old leather-bound herbal recipe book with a pressed flower and a brass clasp on the cover",
    "firewood_rack": "a rustic wooden firewood rack stacked with split logs",
    "firewood": "a small bundle of dry firewood sticks and kindling tied with a twine string",
    "crushed_grass": "a small wooden bowl of crushed green grass paste",
    "crushed_flower_white": "a small wooden bowl of crushed white daisy petals with yellow specks",
    "crushed_flower_blue": "a small wooden bowl of crushed bright blue cornflower petals",
    "crushed_flower_purple": "a small wooden bowl of crushed violet bellflower petals",
    "crushed_herb": "a small wooden bowl of crushed aromatic thyme leaves",
    "crushed_fern": "a small wooden bowl of crushed dark green fern fronds",
    "crushed_broadleaf": "a small wooden bowl of crushed broad green plantain leaves",
    "crushed_weed_dock": "a small wooden bowl of crushed dock leaves with reddish seeds",
    "crushed_weed_nettle": "a small wooden bowl of crushed deep green nettle leaves",
    "potion_base": "a small round glass potion flask with a cork, filled with a pale cloudy green liquid",
    "potion_speed": "a small round glass potion flask with a cork, filled with a glowing bright blue liquid, a tiny feather tied to its neck",
    "potion_night": "a small round glass potion flask with a cork, filled with a glowing deep violet liquid with tiny star sparkles",
    "potion_strength": "a small sturdy glass potion bottle with a cork, filled with a glowing deep green liquid, a tiny iron ring tied to its neck",
    "potion_jump": "a small tall glass potion flask with a cork, filled with a glowing fresh lime green liquid with rising bubbles",
    "potion_shrink": "a tiny glass potion vial with a cork, filled with a glowing pale pink liquid",
    "potion_grow": "a big round glass potion flask with a cork, filled with a glowing warm amber orange liquid",
}

ICON_PROMPT = (
    "Game inventory icon of {subject}. {style}. Single object, centered, filling about 80% of the frame, "
    "soft rim light, plain warm parchment-beige background, no text, no frame, no border."
)

# ── Modèles 3D (Gemini → Meshy) ─────────────────────────────────────────────
# Une image de concept est générée d'abord, puis convertie en modèle par Meshy.
MODELS: dict[str, dict] = {
    "cauldron_stand": {
        "subject": "a black wrought iron cauldron stand for an open hearth: three splayed legs, one tall upright post at the back with a toothed ratchet bar, and a horizontal arm at the top ending in a hanging chain with a hook, empty, no cauldron, no fire",
        "polycount": 4000,
        "ai_model": "meshy-6-lite",
    },
    "firewood_rack": {
        "subject": "a rustic wooden firewood rack: a simple open frame of planks with two shelves, half filled with neatly stacked short split firewood logs",
        "polycount": 5000,
        "ai_model": "meshy-6-lite",
    },
    "pitcher": {
        "subject": "a rustic glazed terracotta water pitcher with a round belly, a spout and a handle, empty, standing upright, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_grass": {
        "subject": "a small bundle of freshly cut long green grass stems tied in the middle with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_flower_white": {
        "subject": "a small bunch of freshly picked white daisies with green stems tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_flower_blue": {
        "subject": "a small bunch of freshly picked blue cornflowers with green stems tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_flower_purple": {
        "subject": "a small bunch of freshly picked violet bellflowers with green stems tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_herb": {
        "subject": "a small bunch of freshly picked thyme sprigs tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_fern": {
        "subject": "two freshly cut green fern fronds tied together at the stems with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_broadleaf": {
        "subject": "a few freshly picked broad green plantain leaves on short stems tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_weed_dock": {
        "subject": "a bunch of freshly cut dock leaves with reddish seed stalks tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_weed_nettle": {
        "subject": "a small bunch of freshly cut nettle stems with serrated leaves tied with a twine string, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_mushroom_cep": {
        "subject": "two freshly picked porcini mushrooms lying side by side, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "bundle_mushroom_chanterelle": {
        "subject": "a small heap of freshly picked golden chanterelle mushrooms, lying on nothing, isolated",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "fountain": {
        "subject": "a rustic village stone water fountain seen from the front three-quarter: a tall sturdy fieldstone pillar with a bronze spout high up near its top, pouring water straight down onto an empty flat stone slab at ground level directly in front of the pillar; a small stone basin on the left side only; nothing at all standing on the slab, no bucket, no barrel, no pot",
        "polycount": 8000,
    },
    "mushroom_cep": {
        "subject": "a small cluster of three wild porcini mushrooms with plump brown caps and thick pale stems, growing from a bit of moss",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "mushroom_chanterelle": {
        "subject": "a small cluster of five wild golden chanterelle mushrooms with wavy funnel shaped caps, growing from a bit of moss",
        "polycount": 2500,
        "ai_model": "meshy-6-lite",
    },
    "tree_pine": {
        "subject": "a tall old Scots pine tree with a straight reddish trunk and a rounded crown of dark green needle clusters high up, isolated tree",
        "polycount": 15000,
    },
    "tree_beech": {
        "subject": "a tall forest beech tree with a smooth grey trunk and a high, airy crown of fresh bright green leaves, isolated tree",
        "polycount": 15000,
    },
    "shelf": {
        "subject": "a tall narrow rustic wooden shelf unit seen from the front: two side boards, a plank back, and exactly four evenly spaced empty shelves plus a top board, completely empty, nothing on the shelves",
        "polycount": 3000,
        "ai_model": "meshy-6-lite",
    },
    "backpack": {
        "subject": "a rustic canvas backpack in olive green with brown leather straps, a buckled top flap and two side loops, seen from the back, standing upright, isolated",
        "polycount": 4000,
        "ai_model": "meshy-6-lite",
    },
    "drying_rack": {
        "subject": "a rustic wooden herb drying rack: a standing wooden frame with three horizontal wooden bars, empty, nothing hanging on it",
        "polycount": 3000,
        "ai_model": "meshy-6-lite",
    },
    "workbench": {
        "subject": "a small rustic apothecary workbench: a wooden table with a small raised shelf at the back holding a few empty glass jars, twine spools and a pair of scissors, cozy",
        "polycount": 6000,
    },
    "potpourri": {
        "subject": "a small round wooden bowl filled with colorful dried flower petals, lavender sprigs and dried rose buds",
        "polycount": 3000,
        "ai_model": "meshy-6-lite",
    },
    "fireplace": {
        "subject": "a rustic stone cottage fireplace seen straight from the front: rounded fieldstones, a wide and tall empty open hearth with a flat stone floor, a thick dark wooden mantel beam above the opening, the stone chimney breast rising a little above the mantel, no fire, no logs, no pot, nothing inside",
        "polycount": 8000,
    },
    "potion_base": {
        "subject": "a small round glass potion flask with a cork stopper, filled with a pale cloudy green liquid, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_speed": {
        "subject": "a small round glass potion flask with a cork stopper, filled with a glowing bright blue liquid, a small white feather tied to its neck with string, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_night": {
        "subject": "a small round glass potion flask with a cork stopper, filled with a glowing deep violet liquid with tiny golden star sparkles, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_strength": {
        "subject": "a small sturdy square glass potion bottle with a cork stopper, filled with a deep green liquid, a small iron ring tied to its neck, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_jump": {
        "subject": "a tall thin glass potion vial with a cork stopper, filled with a glowing lime green liquid with bubbles, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_shrink": {
        "subject": "a tiny short glass potion vial with a cork stopper, filled with a pale pink liquid, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "potion_grow": {
        "subject": "a big round-bellied glass potion flask with a cork stopper, filled with a glowing warm amber orange liquid, standing upright, isolated",
        "polycount": 2000,
        "ai_model": "meshy-6-lite",
    },
    "bowl": {
        "subject": "a small empty round carved wooden bowl, simple and smooth, standing upright, isolated",
        "polycount": 1500,
        "ai_model": "meshy-6-lite",
    },
    "cauldron": {
        "subject": "a small round black cast iron witch cauldron on three short legs, with two small side handles, half filled with a bubbling green potion",
        "polycount": 4000,
        "ai_model": "meshy-6-lite",
    },
    "mortar": {
        "subject": "a small grey stone mortar bowl with a stone pestle resting inside it, a few green leaves in the bowl",
        "polycount": 3000,
        "ai_model": "meshy-6-lite",
    },
    "firewood": {
        "subject": "a small neat pile of four chopped firewood logs with bark, stacked crosswise for a campfire",
        "polycount": 3000,
        "ai_model": "meshy-6-lite",
    },
    "bench": {
        "subject": "a rustic wooden park bench for a village square, three thick seat planks and a curved backrest on dark wrought iron legs and armrests, seen from the front three-quarter",
        "polycount": 5000,
        "ai_model": "meshy-6-lite",
    },
    "flower_pot": {
        "subject": "a round terracotta flower pot overflowing with colorful cottage flowers, red geraniums, white daisies and trailing ivy",
        "polycount": 6000,
    },
    "tree_willow": {
        "subject": "a large majestic weeping willow tree with a thick gnarled trunk and a huge dome of long drooping leafy branches reaching almost to the ground, lush green foliage, isolated tree",
        "polycount": 20000,
    },
    "tree_linden": {
        "subject": "a large old linden tree with a sturdy trunk and a wide, dense, rounded canopy of bright green leaves, isolated village square tree",
        "polycount": 20000,
    },
    "tree_oak": {
        "subject": "a large old village oak tree with a short sturdy trunk and a very wide, dense, rounded crown of deep green leaves, a classic shade tree for a village square, isolated tree",
        "polycount": 20000,
    },
    "cart_body": {
        "subject": "the wooden body of a rustic two-wheeled hand cart WITHOUT ANY WHEELS: a completely empty open flat wooden bed with low plank sides and nothing inside, a bare wooden axle beam sticking out on both sides under the middle of the bed, two short wooden legs at the back to rest on, and two long wooden handles extending forward, side view three-quarter, no wheels at all",
        "polycount": 8000,
    },
    "cart_wheel": {
        "subject": "a single large rustic wooden cart wheel with twelve spokes, a thick wooden hub and an iron rim, seen straight from the side, standing upright",
        "polycount": 4000,
    },
    "cart": {
        "subject": "a rustic wooden two-wheeled hand cart for farm goods: a completely empty open flat wooden bed with low plank sides and nothing inside, two large spoked wooden wheels with iron rims at the middle, and two long wooden handles extending forward, side view three-quarter",
        "polycount": 10000,
    },
    "crate": {
        "subject": "a single empty open-top wooden harvest crate made of light slatted planks with hand-hold cut-outs on the short sides, stackable",
        "polycount": 3000,
    },
    "player": {
        "subject": "a young countryside adventurer standing in a relaxed T-pose, short brown hair, cream linen shirt with rolled sleeves, olive trousers, brown leather boots, friendly face, full body, arms stretched horizontally",
        "polycount": 12000,
    },
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


# ── Personnages animés (Meshy : squelette puis animations de la bibliothèque) ──
# modèle → hauteur (m) et animations {nom: action_id}. Fichiers : SourceArt/Generated/models/<modèle>_rigged.fbx,
# SourceArt/Generated/models/anims/<modèle>_<nom>.fbx.
RIGS: dict[str, dict] = {
    "player": {
        "height": 1.72,
        "animations": {"idle": 0, "walk": 30, "run": 14, "jump": 466, "sit": 33, "crouch": 524,
                       "carry": 551, "push": 261, "pickup": 276},
    },
}

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
    "amb_fire": ("a crackling wood fire in a stone hearth, close up: lots of sharp crackles and pops of burning dry logs over a soft roar of flames, continuous", 12.0, True),
    "sickle_swish": ("a quick swish of a small sickle cutting through tall grass, one short swing", 0.6, False, 0.45),
    "amb_cart_roll": ("the steady rolling of a wooden hand cart on a dirt path: wooden wheels creaking softly, light rumble of the axle, gravel crunch, continuous", 8.0, True),
    "amb_water_pour": ("a steady stream of fresh water pouring from a fountain spout into a wooden barrel, splashing and gurgling, continuous", 8.0, True),
    "mortar_grind": ("a stone pestle grinding and crushing fresh herbs in a stone mortar, two short grinding strokes", 1.0, False),
    "cauldron_bubble": ("a small cauldron of thick potion bubbling and blooping gently, short", 2.5, False),
    "potion_drink": ("a person uncorking a small glass bottle with a pop and drinking a gulp, then a small satisfied breath", 1.6, False),
}
