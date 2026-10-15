"""Génère les assets d'Erewhon dans SourceArt/Generated/.

    python Tools/assetgen/generate.py all
    python Tools/assetgen/generate.py icons --only cerpe,jean
    python Tools/assetgen/generate.py models --force --only merchant

Étapes : textures, icons, concepts (images pour Meshy), models (Meshy), grass (procédural), sounds (ElevenLabs).
Un fichier déjà présent n'est jamais régénéré sans --force : relancer ne coûte rien.
L'import dans Unreal se fait ensuite avec Tools/Editor/import_generated.py.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import grass  # noqa: E402
import manifest  # noqa: E402
from common import OUTPUT_ROOT  # noqa: E402
from gemini import generate_image  # noqa: E402

STEPS = ("textures", "paintings", "icons", "concepts", "models", "rigs", "grass", "sounds")


def _wanted(name: str, only: set[str]) -> bool:
    return not only or name in only


def _run(label: str, path: Path, force: bool, action) -> None:
    if path.exists() and not force:
        print(f"  = {label} (déjà présent)")
        return
    print(f"  + {label} …", flush=True)
    try:
        action()
    except Exception as error:  # une erreur n'arrête pas le lot
        print(f"  ! {label} : {error}")


def textures(only: set[str], force: bool) -> None:
    for name, subject in manifest.TEXTURES.items():
        if _wanted(name, only):
            path = OUTPUT_ROOT / "textures" / f"{name}.jpg"
            prompt = manifest.TEXTURE_PROMPT.format(subject=subject, style=manifest.STYLE)
            _run(f"texture {name}", path, force, lambda p=path, q=prompt: generate_image(q, p))


def paintings(only: set[str], force: bool) -> None:
    for name, (subject, ratio) in manifest.PAINTINGS.items():
        if _wanted(name, only):
            path = OUTPUT_ROOT / "textures" / f"{name}.jpg"
            prompt = manifest.PAINTING_PROMPT.format(subject=subject, style=manifest.STYLE)
            _run(f"peinture {name}", path, force, lambda p=path, q=prompt, r=ratio: generate_image(q, p, aspect_ratio=r))


def icons(only: set[str], force: bool) -> None:
    for name, subject in manifest.ICONS.items():
        if _wanted(name, only):
            path = OUTPUT_ROOT / "icons" / f"{name}.jpg"
            prompt = manifest.ICON_PROMPT.format(subject=subject, style=manifest.STYLE)
            _run(f"icône {name}", path, force, lambda p=path, q=prompt: generate_image(q, p))


def concepts(only: set[str], force: bool) -> None:
    for name, spec in manifest.MODELS.items():
        if _wanted(name, only):
            path = OUTPUT_ROOT / "concepts" / f"{name}.jpg"
            prompt = spec.get("concept_prompt", manifest.CONCEPT_PROMPT).format(subject=spec["subject"], style=manifest.STYLE)
            _run(f"concept {name}", path, force, lambda p=path, q=prompt: generate_image(q, p))


def models(only: set[str], force: bool) -> None:
    from meshy import balance, image_to_3d

    print(f"  Crédits Meshy disponibles : {balance()}")
    for name, spec in manifest.MODELS.items():
        if not _wanted(name, only):
            continue
        concept = OUTPUT_ROOT / "concepts" / f"{name}.jpg"
        if not concept.exists():
            print(f"  ! modèle {name} : lancer d'abord l'étape concepts")
            continue
        path = OUTPUT_ROOT / "models" / f"{name}.glb"
        _run(f"modèle {name}", path, force,
             lambda p=path, c=concept, s=spec: image_to_3d(c, p, polycount=s["polycount"],
                                                           texture_prompt=f"{s['subject']}. {manifest.STYLE}",
                                                           ai_model=s.get("ai_model", "latest"), symmetry=s.get("symmetry", "auto")))


def rigs(only: set[str], force: bool) -> None:
    from meshy import animate, rig

    for name, spec in manifest.RIGS.items():
        if not _wanted(name, only):
            continue
        models = OUTPUT_ROOT / "models"
        model_task = models / f"{name}.task.json"
        if not model_task.exists():
            print(f"  ! squelette {name} : générer d'abord le modèle")
            continue
        print(f"  + squelette {name}", flush=True)
        rig_task = rig(model_task, models / f"{name}_rigged.fbx", height=spec["height"])
        for anim, action in spec["animations"].items():
            path = models / "anims" / f"{name}_{anim}.fbx"
            _run(f"animation {name}_{anim}", path, force, lambda p=path, a=action: animate(rig_task, a, p))


def sounds(only: set[str], force: bool) -> None:
    from elevenlabs import generate_sound

    for name, (prompt, duration, loop, *rest) in manifest.SOUNDS.items():
        if _wanted(name, only):
            path = OUTPUT_ROOT / "sounds" / f"{name}.wav"
            trim = rest[0] if rest else None
            _run(f"son {name}", path, force,
                 lambda p=path, q=prompt, d=duration, l=loop, t=trim: generate_sound(q, p, duration=d, loop=l, trim=t))


def grass_step(only: set[str], force: bool) -> None:
    for path in grass.generate_all(OUTPUT_ROOT / "grass"):
        print(f"  + herbe {path.stem}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("step", choices=(*STEPS, "all"))
    parser.add_argument("--only", default="", help="noms séparés par des virgules")
    parser.add_argument("--force", action="store_true", help="régénère même si le fichier existe")
    args = parser.parse_args()

    only = {name.strip() for name in args.only.split(",") if name.strip()}
    steps = STEPS if args.step == "all" else (args.step,)
    runners = {"textures": textures, "paintings": paintings, "icons": icons, "concepts": concepts, "models": models, "rigs": rigs, "grass": grass_step, "sounds": sounds}
    for step in steps:
        print(f"[{step}]")
        runners[step](only, args.force)


if __name__ == "__main__":
    main()
