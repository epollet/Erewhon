"""Squelette et animations d'un quadrupède généré (Meshy ne rigge que les bipèdes par son API).

Lancé par Blender, sans interface :
    Tools/Blender/blender.exe -b -P Tools/assetgen/quadruped_rig.py -- <nom> [<nom>…]

Pour chaque SourceArt/Generated/models/<nom>.glb :
- mise à l'échelle (hauteur au garrot réelle), corps le long de +Y, tête vers +Y ;
- les quatre pattes repérées dans les points proches du sol (quatre groupes : avant/arrière, gauche/droite) ;
- un squelette : racine, bassin, trois vertèbres, cou, tête, queue (trois os), trois os par patte ;
- des poids par proximité des os, par zones (les pattes ne tirent pas le ventre, la tête ne tire pas le dos) ;
- des animations sur place : attente, marche (allure à quatre temps), trot (diagonales), broutage ;
- export au format des personnages Meshy : <nom>_rigged.fbx et anims/<nom>_<anim>.fbx,
  plus un aperçu <nom>_rig.png pour vérifier le squelette.
"""
import math
import os
import sys

import bpy
import mathutils
from mathutils import Vector

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
MODELS = os.path.join(ROOT, "SourceArt", "Generated", "models")

# Hauteur totale (m) et allures de chaque animal ; « tail » : longueur relative de la queue à chercher.
SPECIES = {
    "fox": {"height": 0.72, "anims": ["idle", "walk", "trot", "sniff"]},
    "wild_boar": {"height": 0.95, "anims": ["idle", "walk", "trot", "sniff"]},
    "deer": {"height": 1.25, "anims": ["idle", "walk", "trot", "graze"]},
    # Le rongeur est riggé dix fois plus grand (les poids automatiques échouent en dessous) ; le jeu le réduit.
    "field_mouse": {"height": 0.7, "anims": ["idle", "walk", "trot", "sniff"]},
}
FPS = 30


def log(message):
    print("[quadrupede] " + message, flush=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = FPS


def import_model(name, height):
    bpy.ops.import_scene.gltf(filepath=os.path.join(MODELS, name + ".glb"))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    for o in bpy.context.scene.objects:
        o.select_set(o in meshes)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    # Plus de parent ni de transformation : les sommets en coordonnées monde.
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in list(bpy.context.scene.objects):
        if o != mesh:
            bpy.data.objects.remove(o, do_unlink=True)
    # À l'échelle, posé au sol, centré.
    points = [v.co.copy() for v in mesh.data.vertices]
    low = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    high = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    scale = height / max(high.z - low.z, 1e-6)
    center = (low + high) * 0.5
    mesh.data.transform(mathutils.Matrix.Scale(scale, 4) @ mathutils.Matrix.Translation(Vector((-center.x, -center.y, -low.z))))
    # Le corps le long de Y : l'axe principal des points (vus de dessus), puis recentré.
    pts = [v.co for v in mesh.data.vertices]
    mx = sum(p.x for p in pts) / len(pts)
    my = sum(p.y for p in pts) / len(pts)
    sxx = sum((p.x - mx) ** 2 for p in pts)
    syy = sum((p.y - my) ** 2 for p in pts)
    sxy = sum((p.x - mx) * (p.y - my) for p in pts)
    angle = 0.5 * math.atan2(2 * sxy, sxx - syy)   # direction de plus grande étendue
    mesh.data.transform(mathutils.Matrix.Rotation(math.pi / 2 - angle, 4, "Z") @ mathutils.Matrix.Translation(Vector((-mx, -my, 0))))
    mesh.data.update()
    pts = [v.co for v in mesh.data.vertices]
    cx = (min(p.x for p in pts) + max(p.x for p in pts)) * 0.5
    cy = (min(p.y for p in pts) + max(p.y for p in pts)) * 0.5
    mesh.data.transform(mathutils.Matrix.Translation(Vector((-cx, -cy, 0))))
    mesh.data.update()
    return mesh


def head_toward_plus_y(mesh):
    """La tête est au bout le plus haut (le cou la porte au-dessus des épaules) ; on la tourne vers +Y."""
    verts = [v.co for v in mesh.data.vertices]
    y0, y1 = min(v.y for v in verts), max(v.y for v in verts)
    span = y1 - y0
    front = [v.z for v in verts if v.y > y1 - span * 0.25]
    back = [v.z for v in verts if v.y < y0 + span * 0.25]
    if max(back) > max(front):
        mesh.data.transform(mathutils.Matrix.Rotation(math.radians(180), 4, "Z"))
        mesh.data.update()


def find_legs(mesh):
    """Quatre groupes de points près du sol : (avant gauche, avant droite, arrière gauche, arrière droite).

    Un regroupement en quatre amas (k-moyennes), parti des quatre coins de l'empreinte au sol.
    """
    verts = [v.co.copy() for v in mesh.data.vertices]
    height = max(v.z for v in verts)
    y0, y1 = min(v.y for v in verts), max(v.y for v in verts)
    span = y1 - y0
    feet = [v for v in verts if v.z < height * 0.12 and y0 + span * 0.06 < v.y < y1 - span * 0.04]
    # D'abord l'avant et l'arrière (deux groupes le long du corps), puis la gauche et la droite dans chacun.
    front_y, back_y = max(v.y for v in feet), min(v.y for v in feet)
    for _ in range(12):
        fronts = [v for v in feet if abs(v.y - front_y) < abs(v.y - back_y)]
        backs = [v for v in feet if abs(v.y - front_y) >= abs(v.y - back_y)]
        front_y = sum(v.y for v in fronts) / max(1, len(fronts))
        back_y = sum(v.y for v in backs) / max(1, len(backs))
    centers = {}
    for key, group in (("f", fronts), ("b", backs)):
        mid = sum(v.x for v in group) / max(1, len(group))
        for side, members in (("l", [v for v in group if v.x < mid]), ("r", [v for v in group if v.x >= mid])):
            members = members or group
            c = sum((Vector((v.x, v.y, 0)) for v in members), Vector()) / len(members)
            # Les deux pattes d'une paire au même niveau du corps (le modèle est symétrique).
            c.y = front_y if key == "f" else back_y
            centers[key + side] = c
    # Gauche, droite : de part et d'autre de l'axe du corps.
    for front in ("f", "b"):
        a, b = centers[front + "l"], centers[front + "r"]
        if a.x > b.x:
            centers[front + "l"], centers[front + "r"] = b, a
        if abs(a.x - b.x) < height * 0.03:
            # Pattes confondues (vues de profil) : on les écarte de part et d'autre.
            c = (a + b) * 0.5
            centers[front + "l"] = Vector((c.x - height * 0.06, c.y, 0))
            centers[front + "r"] = Vector((c.x + height * 0.06, c.y, 0))
    return centers, height, y0, y1


def body_profile(mesh, y):
    """Le dessous et le dessus du corps à l'abscisse y (au-dessus des pattes)."""
    verts = [v.co for v in mesh.data.vertices if abs(v.co.y - y) < (mesh.dimensions.y * 0.06)]
    zs = sorted(v.z for v in verts)
    if not zs:
        return 0.4, 0.8
    return zs[int(len(zs) * 0.35)], zs[-1]


def build_armature(mesh):
    legs, height, y0, y1 = find_legs(mesh)
    front_y = (legs["fl"].y + legs["fr"].y) * 0.5
    back_y = (legs["bl"].y + legs["br"].y) * 0.5
    _, top_front = body_profile(mesh, front_y)
    _, top_back = body_profile(mesh, back_y)
    # Le dos : un peu sous le haut du corps ; les articulations des pattes : à mi-hauteur du tronc.
    back_z = top_back * 0.82
    shoulder_z = top_front * 0.8
    verts = [v.co for v in mesh.data.vertices]
    head_tip = max(verts, key=lambda v: v.y)
    head_slice = [v for v in verts if v.y > y1 - (y1 - y0) * 0.18]
    head_center = sum(head_slice, Vector()) / len(head_slice)
    tail_slice = [v for v in verts if v.y < y0 + (y1 - y0) * 0.12]
    tail_tip = sum(tail_slice, Vector()) / len(tail_slice)

    bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
    armature = bpy.context.active_object
    armature.name = "Armature"
    data = armature.data
    data.name = "Armature"
    edit = data.edit_bones
    for b in list(edit):
        edit.remove(b)

    def bone(name, head, tail, parent=None, connect=False):
        b = edit.new(name)
        b.head, b.tail = Vector(head), Vector(tail)
        if parent:
            b.parent = edit[parent]
            b.use_connect = connect
        return b

    bone("root", (0, 0, 0), (0, height * 0.25, 0))
    pelvis = Vector((0, back_y, back_z))
    shoulders = Vector((0, front_y, shoulder_z))
    bone("pelvis", pelvis, pelvis.lerp(shoulders, 0.33), "root")
    bone("spine1", pelvis.lerp(shoulders, 0.33), pelvis.lerp(shoulders, 0.66), "pelvis", True)
    bone("spine2", pelvis.lerp(shoulders, 0.66), shoulders, "spine1", True)
    neck_end = Vector((0, head_center.y - (y1 - y0) * 0.06, max(head_center.z, shoulder_z)))
    bone("neck", shoulders, neck_end, "spine2", True)
    bone("head", neck_end, Vector((0, head_tip.y, head_center.z)), "neck", True)
    tail_base = Vector((0, y0 + (back_y - y0) * 0.75, back_z * 1.02))
    tail_end = Vector((0, tail_tip.y, tail_tip.z))
    for i in range(3):
        bone("tail%d" % (i + 1), tail_base.lerp(tail_end, i / 3), tail_base.lerp(tail_end, (i + 1) / 3),
             "pelvis" if i == 0 else "tail%d" % i, i > 0)
    for key, joint_z, parent in (("fl", shoulder_z, "spine2"), ("fr", shoulder_z, "spine2"), ("bl", back_z, "pelvis"), ("br", back_z, "pelvis")):
        foot = legs[key]
        top = Vector((foot.x * 0.85, foot.y, joint_z * 0.92))
        # Le genou (avant) plie vers l'arrière, le jarret (arrière) vers l'avant… au repos, presque droit.
        bend = -0.03 if key[0] == "f" else 0.05
        knee = Vector((foot.x, foot.y + bend * height, joint_z * 0.45))
        ankle = Vector((foot.x, foot.y, joint_z * 0.1))
        bone(key + "_upper", top, knee, parent)
        bone(key + "_lower", knee, ankle, key + "_upper", True)
        bone(key + "_foot", ankle, Vector((foot.x, foot.y + height * 0.04, 0.0)), key + "_lower", True)
    for b in edit:
        # L'axe X local de chaque os : l'axe gauche-droite du monde (les pattes basculent d'avant en arrière autour).
        b.align_roll(Vector((0, 1, 0)) if abs((b.tail - b.head).normalized().z) > 0.7 else Vector((0, 0, 1)))
    bpy.ops.object.mode_set(mode="OBJECT")
    return armature, legs, height, (y0, y1), (back_z, shoulder_z)


def segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (p - (a + ab * t)).length


def skin(mesh, armature, legs, height, span, levels):
    """Les poids automatiques de Blender ; s'ils échouent (maillage non fermé), les nôtres, par zones."""
    # Les poils et les touffes font un maillage ouvert, où la diffusion de chaleur échoue : on la calcule
    # sur une copie refermée (remaillage en voxels), puis on reporte les poids sur le vrai modèle.
    proxy = mesh.copy()
    proxy.data = mesh.data.copy()
    bpy.context.scene.collection.objects.link(proxy)
    remesh = proxy.modifiers.new("Remesh", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = height / 70.0
    bpy.ops.object.select_all(action="DESELECT")
    proxy.select_set(True)
    bpy.context.view_layer.objects.active = proxy
    bpy.ops.object.modifier_apply(modifier="Remesh")
    proxy.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    try:
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    except RuntimeError as error:
        log("poids automatiques : " + str(error))
    for name in [b.name for b in armature.data.bones]:
        if name not in mesh.vertex_groups:
            mesh.vertex_groups.new(name=name)
    transfer = mesh.modifiers.new("Transfer", "DATA_TRANSFER")
    transfer.object = proxy
    transfer.use_vert_data = True
    transfer.data_types_verts = {"VGROUP_WEIGHTS"}
    transfer.vert_mapping = "POLYINTERP_NEAREST"
    transfer.layers_vgroup_select_src = "ALL"
    transfer.layers_vgroup_select_dst = "NAME"
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.modifier_apply(modifier="Transfer")
    bpy.data.objects.remove(proxy, do_unlink=True)
    modifier = mesh.modifiers.new("Armature", "ARMATURE")
    modifier.object = armature
    mesh.parent = armature
    unweighted = sum(1 for v in mesh.data.vertices if not v.groups or max((g.weight for g in v.groups), default=0) < 0.01)
    log("poids automatiques : %d sommets sans poids sur %d" % (unweighted, len(mesh.data.vertices)))
    if unweighted < len(mesh.data.vertices) * 0.02:
        return
    log("repli sur les poids par zones")
    for group in list(mesh.vertex_groups):
        mesh.vertex_groups.remove(group)
    for m in list(mesh.modifiers):
        mesh.modifiers.remove(m)
    skin_zones(mesh, armature, legs, height, span, levels)


def skin_zones(mesh, armature, legs, height, span, levels):
    """Poids par proximité des os, chaque os ne tirant que sa zone du corps."""
    y0, y1 = span
    back_z, shoulder_z = levels
    bones = {b.name: (armature.matrix_world @ b.head_local, armature.matrix_world @ b.tail_local) for b in armature.data.bones if b.name != "root"}
    groups = {name: mesh.vertex_groups.new(name=name) for name in bones}
    belly = min(back_z, shoulder_z) * 0.62   # sous le ventre : les pattes
    for v in mesh.data.vertices:
        p = v.co
        candidates = []
        for name, (a, b) in bones.items():
            is_leg = name[:2] in ("fl", "fr", "bl", "br")
            if is_leg:
                foot = legs[name[:2]]
                # Une patte ne prend que les points sous le ventre, de son côté et de son bout du corps.
                if p.z > belly * 1.15 and not name.endswith("upper"):
                    continue
                if (p.x < 0) != (foot.x < 0) and abs(p.x) > height * 0.03:
                    continue
                if abs(p.y - foot.y) > (y1 - y0) * 0.22:
                    continue
                if name.endswith("upper") and p.z > belly * 1.5:
                    continue
            elif p.z < belly * 0.7 and abs(p.y - (legs["fl"].y + legs["bl"].y) * 0.5) < (y1 - y0) * 0.35 and not name.startswith("tail") and name not in ("head", "neck"):
                continue
            d = segment_distance(p, a, b)
            candidates.append((d, name))
        if not candidates:
            candidates = [(segment_distance(p, *bones["spine1"]), "spine1")]
        candidates.sort()
        best = candidates[:3]
        weights = [1.0 / max(d, height * 0.01) ** 4 for d, _ in best]
        total = sum(weights)
        for (d, name), w in zip(best, weights):
            if w / total > 0.02:
                groups[name].add([v.index], w / total, "REPLACE")
    modifier = mesh.modifiers.new("Armature", "ARMATURE")
    modifier.object = armature
    mesh.parent = armature


def head_yaw(mesh, span, levels):
    """De combien la tête est tournée (radians, vue de dessus) : du garrot vers le milieu de la tête."""
    y0, y1 = span
    _, shoulder_z = levels
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = mesh.evaluated_get(depsgraph)
    verts = [mesh.matrix_world @ v.co for v in evaluated.data.vertices]
    head = [v for v in verts if v.y > y1 - (y1 - y0) * 0.3 and v.z > shoulder_z * 0.9]
    if not head:
        return 0.0
    # Le museau : le point de la tête le plus loin de son centre, à l'horizontale, à hauteur des yeux ou dessous
    # (les oreilles, plus haut, ne comptent pas). Sa direction donne celle du regard.
    c = sum(head, Vector()) / len(head)
    top = max(v.z for v in head)
    snout = [v for v in head if v.z < c.z + (top - c.z) * 0.25]
    nose = max(snout or head, key=lambda v: (v.x - c.x) ** 2 + (v.y - c.y) ** 2)
    return math.atan2(nose.x - c.x, nose.y - c.y)


def straighten_head(mesh, armature, span, levels):
    """Meshy livre parfois la tête tournée vers le côté : on la remet dans l'axe du corps (cou et tête), et ce
    devient la pose de repos — maillage compris."""
    yaw = head_yaw(mesh, span, levels)
    log("tête tournée de %.0f°" % math.degrees(yaw))
    if abs(yaw) < math.radians(6):
        return
    bpy.context.view_layer.objects.active = armature
    best = None
    for sign in (1.0, -1.0):
        for name, share in (("neck", 0.45), ("head", 0.55)):
            pb = armature.pose.bones[name]
            pb.rotation_mode = "XYZ"
            pb.rotation_euler = mathutils.Euler((0, 0, sign * yaw * share))
        bpy.context.view_layer.update()
        after = abs(head_yaw(mesh, span, levels))
        if best is None or after < best[0]:
            best = (after, sign)
    if best[0] > abs(yaw) * 0.6:
        # Ni dans un sens ni dans l'autre la tête ne revient dans l'axe : on n'y touche pas.
        for name in ("neck", "head"):
            armature.pose.bones[name].rotation_euler = mathutils.Euler((0, 0, 0))
        bpy.context.view_layer.update()
        log("tête laissée telle quelle")
        return
    for name, share in (("neck", 0.45), ("head", 0.55)):
        armature.pose.bones[name].rotation_euler = mathutils.Euler((0, 0, best[1] * yaw * share))
    bpy.context.view_layer.update()
    log("tête redressée : reste %.0f°" % math.degrees(best[0]))
    # Le maillage prend la pose, puis le squelette en fait son repos.
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.modifier_apply(modifier="Armature")
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.select_all(action="SELECT")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    modifier = mesh.modifiers.new("Armature", "ARMATURE")
    modifier.object = armature


def key_pose(armature, frame, rotations, location=None):
    for name, euler in rotations.items():
        pb = armature.pose.bones[name]
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = euler
        pb.keyframe_insert("rotation_euler", frame=frame)
    if location is not None:
        pb = armature.pose.bones["root"]
        pb.location = location
        pb.keyframe_insert("location", frame=frame)


def leg_swing(phase, stride, lift):
    """Une patte au cours d'un pas : balancement avant/arrière, et flexion quand elle se lève (phase 0 → 1)."""
    swing = math.sin(phase * 2 * math.pi) * stride
    lifted = max(0.0, math.sin(phase * 2 * math.pi + math.pi / 2)) ** 2 * lift if math.cos(phase * 2 * math.pi) > 0 else 0.0
    return swing, lifted


def make_action(armature, anim, height):
    action = bpy.data.actions.new(anim)
    armature.animation_data_create()
    armature.animation_data.action = action
    rest = {b.name: mathutils.Euler((0, 0, 0)) for b in armature.pose.bones}
    frames = {"idle": 90, "walk": 30, "trot": 18, "sniff": 90, "graze": 120}[anim]
    phases = {"walk": {"bl": 0.0, "fl": 0.25, "br": 0.5, "fr": 0.75}, "trot": {"fl": 0.0, "br": 0.0, "fr": 0.5, "bl": 0.5}}
    for f in range(frames + 1):
        t = f / frames
        pose = {k: v.copy() for k, v in rest.items()}
        loc = Vector((0, 0, 0))
        if anim in phases:
            stride = math.radians(24 if anim == "walk" else 32)
            lift = math.radians(40 if anim == "walk" else 55)
            for leg, offset in phases[anim].items():
                swing, lifted = leg_swing((t + offset) % 1.0, stride, lift)
                front = leg[0] == "f"
                # Rotation autour de X : le haut de la patte avance (+) ou recule (−) ; le bas plie vers l'arrière (avant) ou l'avant (arrière).
                pose[leg + "_upper"] = mathutils.Euler((-swing, 0, 0))
                # Le genou avant plie le pied vers l'arrière ; le jarret ramène le pied sous le corps.
                pose[leg + "_lower"] = mathutils.Euler(((-lifted if front else lifted * 0.7), 0, 0))
                pose[leg + "_foot"] = mathutils.Euler(((lifted * 0.5 if front else -lifted * 0.4), 0, 0))
            bob = math.sin(t * 4 * math.pi) * height * (0.012 if anim == "walk" else 0.025)
            loc = Vector((0, 0, bob))
            pose["spine1"] = mathutils.Euler((0, math.sin(t * 2 * math.pi) * math.radians(3), 0))
            pose["neck"] = mathutils.Euler((math.sin(t * 4 * math.pi) * math.radians(3), 0, 0))
            pose["tail1"] = mathutils.Euler((math.radians(-8), 0, math.sin(t * 2 * math.pi) * math.radians(12)))
            pose["tail2"] = mathutils.Euler((0, 0, math.sin(t * 2 * math.pi - 0.6) * math.radians(14)))
        elif anim == "idle":
            breath = math.sin(t * 2 * math.pi)
            pose["spine1"] = mathutils.Euler((breath * math.radians(1.2), 0, 0))
            look = math.sin(t * 2 * math.pi) * math.radians(18) if 0.3 < t < 0.8 else 0.0
            pose["neck"] = mathutils.Euler((math.radians(-4), 0, look * 0.5))
            pose["head"] = mathutils.Euler((0, 0, look * 0.5))
            pose["tail1"] = mathutils.Euler((0, 0, math.sin(t * 4 * math.pi) * math.radians(10)))
            pose["tail2"] = mathutils.Euler((0, 0, math.sin(t * 4 * math.pi - 0.7) * math.radians(14)))
        else:
            # Flairer ou brouter : la tête descend au sol, quelques petits mouvements, puis remonte.
            down = min(1.0, t / 0.2) if t < 0.8 else max(0.0, (1.0 - t) / 0.2)
            depth = math.radians(55 if anim == "graze" else 35)
            nibble = math.sin(t * 2 * math.pi * 8) * math.radians(4) * down
            # Un os le long de +Y tourné de −θ autour de X : son bout descend.
            pose["neck"] = mathutils.Euler((-depth * down, 0, 0))
            pose["head"] = mathutils.Euler((-depth * 0.4 * down + nibble, 0, 0))
            pose["spine2"] = mathutils.Euler((-math.radians(6) * down, 0, 0))
            pose["tail1"] = mathutils.Euler((0, 0, math.sin(t * 6 * math.pi) * math.radians(8)))
        key_pose(armature, f + 1, {k: v for k, v in pose.items() if k != "root"}, loc)
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = frames + 1
    return action


def export_fbx(path, objects, animate):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=animate, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                             bake_anim_force_startend_keying=True, path_mode="COPY", embed_textures=True,
                             apply_scale_options="FBX_SCALE_ALL", armature_nodetype="NULL")


def preview(name, armature, mesh, frame=1, suffix=""):
    """Un aperçu de profil, le squelette par-dessus le modèle : pour vérifier les pattes."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = 800, 600
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    armature.show_in_front = True
    armature.data.display_type = "STICK"
    world = bpy.data.worlds.new("w")
    scene.world = world
    size = max(mesh.dimensions)
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = size * 1.3
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (size * 3, 0, mesh.dimensions.z * 0.5)
    cam.rotation_euler = (math.radians(90), 0, math.radians(90))
    scene.camera = cam
    scene.render.filepath = os.path.join(MODELS, name + "_rig" + suffix + ".png")
    scene.frame_set(frame)
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


def rig(name):
    spec = SPECIES[name]
    reset()
    mesh = import_model(name, spec["height"])
    head_toward_plus_y(mesh)
    armature, legs, height, span, levels = build_armature(mesh)
    log("%s : pattes %s" % (name, {k: tuple(round(c, 3) for c in v) for k, v in legs.items()}))
    skin(mesh, armature, legs, height, span, levels)
    straighten_head(mesh, armature, span, levels)
    export_fbx(os.path.join(MODELS, name + "_rigged.fbx"), [armature, mesh], False)
    preview(name, armature, mesh)
    for anim in spec["anims"]:
        make_action(armature, anim, height)
        export_fbx(os.path.join(MODELS, "anims", "%s_%s.fbx" % (name, anim)), [armature], True)
        if anim == "walk":
            preview(name, armature, mesh, 8, "_walk")
    log(name + " : fini")


if __name__ == "__main__":
    names = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else list(SPECIES)
    for n in names:
        rig(n)
