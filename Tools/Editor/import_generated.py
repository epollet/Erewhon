"""Importe SourceArt/Generated/ dans /Game/Generated et crée les matériaux de base.

À lancer avec l'éditeur fermé :
    UnrealEditor-Cmd.exe Erewhon.uproject -run=pythonscript -script=Tools/Editor/import_generated.py

Conventions de nommage (lues par le code du jeu) :
    /Game/Generated/Textures/T_<nom>       textures tuilables
    /Game/Generated/Icons/T_Icon_<id>      icônes d'inventaire
    /Game/Generated/Meshes/SM_<nom>        modèles Meshy
    /Game/Generated/Foliage/SM_<nom>       touffes d'herbe procédurales
    /Game/Generated/Materials/…            M_PaintedWorld, M_Foliage
"""
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceArt", "Generated")
GEN = "/Game/Generated"

tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log("[import_generated] " + message)


def import_file(path, destination, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths") or [])


def source_files(folder, extensions):
    root = os.path.join(SOURCE, folder)
    if not os.path.isdir(root):
        return []
    return sorted(os.path.join(root, f) for f in os.listdir(root) if f.lower().endswith(extensions))


# ── Textures et icônes ───────────────────────────────────────────────────────

def import_textures():
    for path in source_files("textures", (".jpg", ".png")):
        name = "T_" + os.path.splitext(os.path.basename(path))[0]
        import_file(path, GEN + "/Textures", name)
        if name == "T_font_hand":
            # Atlas de la police des panneaux : toujours en pleine résolution.
            texture = assets.load_asset(GEN + "/Textures/" + name)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            texture.set_editor_property("never_stream", True)
            assets.save_loaded_asset(texture)
        log("texture " + name)

    for path in source_files("icons", (".jpg", ".png")):
        name = "T_Icon_" + os.path.splitext(os.path.basename(path))[0]
        import_file(path, GEN + "/Icons", name)
        texture = assets.load_asset(GEN + "/Icons/" + name)
        if texture:
            # Icône d'interface : pas de mips ni de streaming, toujours nette.
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            texture.set_editor_property("never_stream", True)
            assets.save_loaded_asset(texture)
        log("icône " + name)


# ── Sons ─────────────────────────────────────────────────────────────────────

def import_sounds():
    """WAV générés → /Game/Generated/Sounds/S_<nom> ; les ambiances (amb_*) bouclent."""
    for path in source_files("sounds", (".wav",)):
        base = os.path.splitext(os.path.basename(path))[0]
        name = "S_" + base
        import_file(path, GEN + "/Sounds", name)
        sound = assets.load_asset(GEN + "/Sounds/" + name)
        if sound:
            sound.set_editor_property("looping", base.startswith("amb_"))
            assets.save_loaded_asset(sound)
    log("sons importés")


# ── Meshes ───────────────────────────────────────────────────────────────────

def import_mesh(path, folder, name):
    """Importe un glTF/GLB et renomme le StaticMesh obtenu en <name>."""
    destination = GEN + "/" + folder + "/" + name[3:]   # un dossier par modèle (matériaux, textures)
    imported = import_file(path, destination, name)
    meshes = [p for p in assets.list_assets(destination, recursive=True, include_folder=False)
              if assets.find_asset_data(p).asset_class_path.asset_name == "StaticMesh"]
    if not meshes:
        log("AUCUN mesh pour " + path + " " + str(imported))
        return None
    target = GEN + "/" + folder + "/" + name
    if meshes[0].split(".")[0] != target:
        if assets.does_asset_exist(target):
            assets.delete_asset(target)
        assets.rename_asset(meshes[0].split(".")[0], target)
    mesh = assets.load_asset(target)
    log("mesh " + target)
    return mesh


def add_box_collision(mesh):
    # Le sous-système n'existe pas en commandlet : on retombe sur l'ancienne bibliothèque.
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem:
        subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
    else:
        unreal.EditorStaticMeshLibrary.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)


def add_lods(mesh):
    """Niveaux de détail automatiques : l'herbe se compte par centaines de milliers d'instances."""
    options = unreal.EditorScriptingMeshReductionOptions()
    settings = []
    for percent, screen in ((1.0, 1.0), (0.5, 0.35), (0.25, 0.15), (0.1, 0.05)):
        setting = unreal.EditorScriptingMeshReductionSettings()
        setting.set_editor_property("percent_triangles", percent)
        setting.set_editor_property("screen_size", screen)
        settings.append(setting)
    options.set_editor_property("reduction_settings", settings)
    options.set_editor_property("auto_compute_lod_screen_size", False)
    unreal.EditorStaticMeshLibrary.set_lods(mesh, options)


def import_meshes():
    for path in source_files("models", (".glb",)):
        mesh = import_mesh(path, "Meshes", "SM_" + os.path.splitext(os.path.basename(path))[0])
        if mesh:
            # Collision simple automatique : les accessoires (tonneau, caisses) arrêtent le joueur.
            add_box_collision(mesh)
            add_lods(mesh)
            assets.save_loaded_asset(mesh)
    for path in source_files("grass", (".gltf",)):
        mesh = import_mesh(path, "Foliage", "SM_" + os.path.splitext(os.path.basename(path))[0])
        if mesh:
            add_lods(mesh)
            assets.save_loaded_asset(mesh)


# ── Matériaux ────────────────────────────────────────────────────────────────

def recreate(name, folder, asset_class, factory):
    """Réutilise l'asset s'il existe (il peut être référencé), sinon le crée.

    Les matériaux sont vidés de leurs nœuds : on les reconstruit à chaque import.
    """
    path = GEN + "/" + folder + "/" + name
    if assets.does_asset_exist(path):
        asset = assets.load_asset(path)
        if isinstance(asset, unreal.Material):
            mel.delete_all_material_expressions(asset)
        return asset
    return tools.create_asset(name, GEN + "/" + folder, asset_class, factory)


_connect = mel.connect_material_expressions


def connect(source, source_pin, target, target_pin):
    if not _connect(source, source_pin, target, target_pin):
        raise RuntimeError("connexion impossible : %s.%s -> %s.%s" % (source.get_name(), source_pin, target.get_name(), target_pin))


def node(material, expression_class, x, y):
    return mel.create_material_expression(material, expression_class, x, y)


def function_call(material, function_path, x, y):
    call = node(material, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    call.set_editor_property("material_function", unreal.load_asset(function_path))
    return call


def create_painted_world():
    """Texture peinte projetée sur les axes du monde, teintable."""
    material = recreate("M_PaintedWorld", "Materials", unreal.Material, unreal.MaterialFactoryNew())

    texture = node(material, unreal.MaterialExpressionTextureObjectParameter, -900, -100)
    texture.set_editor_property("parameter_name", "Albedo")
    default_texture = assets.load_asset(GEN + "/Textures/T_ground_grass")
    if default_texture:
        texture.set_editor_property("texture", default_texture)

    size = node(material, unreal.MaterialExpressionScalarParameter, -1100, 100)
    size.set_editor_property("parameter_name", "TileSize")
    size.set_editor_property("default_value", 400.0)
    size_xy = node(material, unreal.MaterialExpressionAppendVector, -900, 100)
    size_xyz = node(material, unreal.MaterialExpressionAppendVector, -750, 100)
    connect(size, "", size_xy, "A")
    connect(size, "", size_xy, "B")
    connect(size_xy, "", size_xyz, "A")
    connect(size, "", size_xyz, "B")

    projection = function_call(material, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", -550, 0)
    connect(texture, "", projection, "TextureObject")
    connect(size_xyz, "", projection, "TextureSize")

    tint = node(material, unreal.MaterialExpressionVectorParameter, -550, 250)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    tinted = node(material, unreal.MaterialExpressionMultiply, -250, 50)
    connect(projection, "XYZ Texture", tinted, "A")
    connect(tint, "", tinted, "B")
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = node(material, unreal.MaterialExpressionConstant, -250, 250)
    roughness.set_editor_property("r", 0.9)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_PaintedWorld")


def create_foliage():
    """Herbe : couleur des sommets, deux faces, ondulation au vent.

    WindIntensity et WindSpeed sont pilotés en jeu par ACelestialSystem.
    """
    material = recreate("M_Foliage", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)

    vertex_color = node(material, unreal.MaterialExpressionVertexColor, -800, 0)
    tint = node(material, unreal.MaterialExpressionVectorParameter, -800, 200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    tinted = node(material, unreal.MaterialExpressionMultiply, -500, 50)
    connect(vertex_color, "", tinted, "A")
    connect(tint, "", tinted, "B")
    mel.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    roughness = node(material, unreal.MaterialExpressionConstant, -500, 250)
    roughness.set_editor_property("r", 0.85)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    wind = function_call(material, "/Engine/Functions/Engine_MaterialFunctions01/WorldPositionOffset/SimpleGrassWind", -400, 450)
    for parameter_name, value, y in (("WindIntensity", 0.35, 350), ("WindSpeed", 0.6, 550)):
        parameter = node(material, unreal.MaterialExpressionScalarParameter, -800, y)
        parameter.set_editor_property("parameter_name", parameter_name)
        parameter.set_editor_property("default_value", value)
        connect(parameter, "", wind, parameter_name)
    connect(vertex_color, "A", wind, "WindWeight")
    no_offset = node(material, unreal.MaterialExpressionConstant3Vector, -800, 750)
    connect(no_offset, "", wind, "AdditionalWPO")
    mel.connect_material_property(wind, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_Foliage")


def scalar(material, name, value, x, y):
    parameter = node(material, unreal.MaterialExpressionScalarParameter, x, y)
    parameter.set_editor_property("parameter_name", name)
    parameter.set_editor_property("default_value", value)
    return parameter


def world_aligned_albedo(material, x, y, texture_name="Albedo", size_name="TileSize"):
    """Texture projetée sur le monde, répétée tous les « TileSize » cm."""
    texture = node(material, unreal.MaterialExpressionTextureObjectParameter, x - 400, y - 100)
    texture.set_editor_property("parameter_name", texture_name)
    default_texture = assets.load_asset(GEN + "/Textures/T_ground_grass")
    if default_texture:
        texture.set_editor_property("texture", default_texture)
    size = scalar(material, size_name, 400.0, x - 600, y + 100)
    size_xy = node(material, unreal.MaterialExpressionAppendVector, x - 400, y + 100)
    size_xyz = node(material, unreal.MaterialExpressionAppendVector, x - 250, y + 100)
    connect(size, "", size_xy, "A")
    connect(size, "", size_xy, "B")
    connect(size_xy, "", size_xyz, "A")
    connect(size, "", size_xyz, "B")
    projection = function_call(material, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", x, y)
    connect(texture, "", projection, "TextureObject")
    connect(size_xyz, "", projection, "TextureSize")
    return projection


def create_ground_blend():
    """Sol d'un secteur à chemins : herbe et terre mêlées selon un masque (UV du quad de sol)."""
    material = recreate("M_GroundBlend", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    grass = world_aligned_albedo(material, -300, -200)
    dirt = world_aligned_albedo(material, -300, 300, "AlbedoDirt", "DirtTileSize")

    mask = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 700)
    mask.set_editor_property("parameter_name", "DirtMask")
    mask.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
    mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    mixed = node(material, unreal.MaterialExpressionLinearInterpolate, 0, 0)
    connect(grass, "XYZ Texture", mixed, "A")
    connect(dirt, "XYZ Texture", mixed, "B")
    connect(mask, "R", mixed, "Alpha")
    mel.connect_material_property(mixed, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = node(material, unreal.MaterialExpressionConstant, 0, 250)
    roughness.set_editor_property("r", 0.92)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_GroundBlend")


def create_vertex_color():
    """Couleur portée par les sommets : ailes d'oiseaux et de papillons construites par le jeu."""
    material = recreate("M_VertexColor", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    vertex_color = node(material, unreal.MaterialExpressionVertexColor, -400, 0)
    mel.connect_material_property(vertex_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = node(material, unreal.MaterialExpressionConstant, -400, 200)
    roughness.set_editor_property("r", 0.7)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_VertexColor")


def create_glass():
    """Vitre des fenêtres : translucide, légèrement teintée, brillante."""
    material = recreate("M_Glass", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    material.set_editor_property("two_sided", True)
    color = node(material, unreal.MaterialExpressionConstant3Vector, -500, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.72, 0.86, 0.9, 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = scalar(material, "Opacity", 0.22, -500, 200)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    roughness = node(material, unreal.MaterialExpressionConstant, -500, 350)
    roughness.set_editor_property("r", 0.05)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_Glass")


def create_highlight():
    """Halo de ciblage : lueur chaude translucide, plus vive sur les contours, qui pulse doucement."""
    material = recreate("M_Highlight", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)

    color = node(material, unreal.MaterialExpressionVectorParameter, -900, -100)
    color.set_editor_property("parameter_name", "GlowColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.8, 0.35, 1.0))
    fresnel = node(material, unreal.MaterialExpressionFresnel, -900, 150)
    fresnel.set_editor_property("exponent", 2.0)
    rim = node(material, unreal.MaterialExpressionAdd, -650, 150)
    connect(fresnel, "", rim, "A")
    base = node(material, unreal.MaterialExpressionConstant, -800, 300)
    base.set_editor_property("r", 0.22)
    connect(base, "", rim, "B")

    time = node(material, unreal.MaterialExpressionTime, -1100, 450)
    speed = node(material, unreal.MaterialExpressionMultiply, -900, 450)
    connect(time, "", speed, "A")
    pulse_speed = node(material, unreal.MaterialExpressionConstant, -1100, 550)
    pulse_speed.set_editor_property("r", 3.5)
    connect(pulse_speed, "", speed, "B")
    sine = node(material, unreal.MaterialExpressionSine, -750, 450)
    connect(speed, "", sine, "")
    pulse = node(material, unreal.MaterialExpressionMultiply, -600, 450)
    connect(sine, "", pulse, "A")
    amplitude = node(material, unreal.MaterialExpressionConstant, -750, 550)
    amplitude.set_editor_property("r", 0.08)
    connect(amplitude, "", pulse, "B")

    opacity = node(material, unreal.MaterialExpressionAdd, -400, 300)
    connect(rim, "", opacity, "A")
    connect(pulse, "", opacity, "B")
    clamped = node(material, unreal.MaterialExpressionSaturate, -250, 300)
    connect(opacity, "", clamped, "")
    mel.connect_material_property(clamped, "", unreal.MaterialProperty.MP_OPACITY)

    glow = node(material, unreal.MaterialExpressionMultiply, -400, -50)
    connect(color, "", glow, "A")
    strength = node(material, unreal.MaterialExpressionConstant, -600, 0)
    strength.set_editor_property("r", 1.6)
    connect(strength, "", glow, "B")
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_Highlight")


def create_generated_mesh_master():
    """Parent commun des modèles Meshy : utilisable en instances et avec Nanite."""
    material = recreate("M_GeneratedMesh", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)   # pétales et feuilles fins
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    texture = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    texture.set_editor_property("parameter_name", "BaseColor")
    mel.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = node(material, unreal.MaterialExpressionConstant, -600, 300)
    roughness.set_editor_property("r", 0.8)
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_GeneratedMesh")
    return material


def create_bird_flap():
    """Comme M_GeneratedMesh, mais les ailes battent : le matériau soulève les sommets
    proportionnellement à leur distance au corps, le long de l'axe des ailes.

    Paramètres (une instance dynamique par oiseau, réglée par le jeu) :
      FlapAmount = amplitude du battement (≈0 en vol plané), SpanIsX = ailes sur l'axe X local (1)
      ou Y (0), Phase = décalage du battement.
    """
    material = recreate("M_BirdFlap", "Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    texture = node(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, -300)
    texture.set_editor_property("parameter_name", "BaseColor")
    mel.connect_material_property(texture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

    names = {0: ("FlapAmount", 1.0), 1: ("SpanIsX", 0.0), 2: ("Phase", 0.0)}

    def primitive_data(index, y):
        name, default = names[index]
        return scalar(material, name, default, -1400, y)

    local = node(material, unreal.MaterialExpressionLocalPosition, -1600, 200)
    x = node(material, unreal.MaterialExpressionComponentMask, -1400, 150)
    x.set_editor_property("r", True)
    y = node(material, unreal.MaterialExpressionComponentMask, -1400, 250)
    y.set_editor_property("g", True)
    connect(local, "", x, "")
    connect(local, "", y, "")
    span = node(material, unreal.MaterialExpressionLinearInterpolate, -1200, 200)
    connect(y, "", span, "A")
    connect(x, "", span, "B")
    connect(primitive_data(1, 350), "", span, "Alpha")
    distance = node(material, unreal.MaterialExpressionAbs, -1050, 200)
    connect(span, "", distance, "")

    time = node(material, unreal.MaterialExpressionTime, -1400, 500)
    rate = node(material, unreal.MaterialExpressionMultiply, -1200, 500)
    connect(time, "", rate, "A")
    connect(scalar(material, "FlapRate", 11.0, -1400, 600), "", rate, "B")
    phased = node(material, unreal.MaterialExpressionAdd, -1050, 500)
    connect(rate, "", phased, "A")
    connect(primitive_data(2, 700), "", phased, "B")
    wave = node(material, unreal.MaterialExpressionSine, -900, 500)
    connect(phased, "", wave, "")

    lift = node(material, unreal.MaterialExpressionMultiply, -750, 300)
    connect(distance, "", lift, "A")
    connect(wave, "", lift, "B")
    amplitude = node(material, unreal.MaterialExpressionMultiply, -600, 300)
    connect(lift, "", amplitude, "A")
    connect(primitive_data(0, 450), "", amplitude, "B")
    scaled = node(material, unreal.MaterialExpressionMultiply, -450, 300)
    connect(amplitude, "", scaled, "A")
    connect(scalar(material, "FlapStrength", 0.55, -600, 450), "", scaled, "B")

    zero = node(material, unreal.MaterialExpressionConstant2Vector, -450, 500)
    offset = node(material, unreal.MaterialExpressionAppendVector, -300, 400)
    connect(zero, "", offset, "A")
    connect(scaled, "", offset, "B")
    world = node(material, unreal.MaterialExpressionTransform, -150, 400)
    world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(offset, "", world, "")
    mel.connect_material_property(world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    mel.recompile_material(material)
    assets.save_loaded_asset(material)
    log("matériau M_BirdFlap")
    return material


def rebind_generated_materials(master, specials=None):
    """Remplace les matériaux créés à l'import par des instances de M_GeneratedMesh
    (ou d'un parent dédié pour les noms listés dans specials)."""
    for path in assets.list_assets(GEN + "/Meshes", recursive=False, include_folder=False):
        mesh = assets.load_asset(path.split(".")[0])
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        name = mesh.get_name()[3:]
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            source = slot.get_editor_property("material_interface")
            base_color = None
            if isinstance(source, unreal.MaterialInstance):
                for value in source.get_editor_property("texture_parameter_values"):
                    texture = value.get_editor_property("parameter_value")
                    label = str(value.get_editor_property("parameter_info").get_editor_property("name")).lower()
                    if texture and ("base" in label or "albedo" in label or "diffuse" in label or base_color is None):
                        base_color = texture
            instance_name = "MI_%s_%d" % (name, index)
            instance = recreate(instance_name, "Meshes/" + name, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(instance, (specials or {}).get(name, master))
            if base_color:
                mel.set_material_instance_texture_parameter_value(instance, "BaseColor", base_color)
            assets.save_loaded_asset(instance)
            mesh.set_material(index, instance)
        assets.save_loaded_asset(mesh)
        log("matériaux de " + mesh.get_name())


import_textures()
import_meshes()
create_painted_world()
create_foliage()
create_ground_blend()
create_vertex_color()
create_highlight()
create_glass()
if assets.does_asset_exist(GEN + "/Materials/M_PathBlend"):
    assets.delete_asset(GEN + "/Materials/M_PathBlend")   # remplacé par M_GroundBlend
rebind_generated_materials(create_generated_mesh_master(), {"bird": create_bird_flap()})
import_sounds()
log("terminé")
