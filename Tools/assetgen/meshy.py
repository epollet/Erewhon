"""Modèles 3D avec l'API Meshy (Image to 3D)."""
from __future__ import annotations

import base64
import json
import time
from pathlib import Path

from common import download, load_env, request_json

_API = "https://api.meshy.ai/openapi/v1"


def _headers() -> dict[str, str]:
    return {"Authorization": "Bearer " + load_env()["MESHY_API_KEY"]}


def balance() -> int:
    return int(request_json(f"{_API}/balance", headers=_headers())["balance"])


def image_to_3d(image: Path, out_glb: Path, *, polycount: int, texture_prompt: str = "", ai_model: str = "latest") -> Path:
    """Crée une tâche Image to 3D, attend sa fin et télécharge le GLB.

    L'identifiant de tâche est mémorisé à côté du GLB : relancer le script après
    une coupure reprend la même tâche au lieu de repayer une génération.
    """
    task_file = out_glb.with_suffix(".task.json")
    task_id = json.loads(task_file.read_text())["id"] if task_file.exists() else None

    if not task_id:
        body = {
            "image_url": "data:image/jpeg;base64," + base64.b64encode(image.read_bytes()).decode("ascii"),
            "ai_model": ai_model,   # "meshy-6-lite" : 15 crédits au lieu de 30, pour les petits accessoires
            "should_remesh": True,
            "topology": "triangle",
            "target_polycount": polycount,
            "should_texture": True,
            "enable_pbr": False,
            "target_formats": ["glb"],
        }
        if texture_prompt:
            body["texture_prompt"] = texture_prompt[:800]
        task_id = request_json(f"{_API}/image-to-3d", method="POST", headers=_headers(), body=body)["result"]
        out_glb.parent.mkdir(parents=True, exist_ok=True)
        task_file.write_text(json.dumps({"id": task_id}))

    while True:
        task = request_json(f"{_API}/image-to-3d/{task_id}", headers=_headers())
        status = task.get("status")
        if status == "SUCCEEDED":
            download(task["model_urls"]["glb"], out_glb)
            if task.get("thumbnail_url"):
                download(task["thumbnail_url"], out_glb.with_suffix(".thumb.png"))
            return out_glb
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            task_file.unlink(missing_ok=True)
            raise RuntimeError(f"Tâche Meshy {task_id} : {status} — {task.get('task_error')}")
        print(f"    {out_glb.stem} : {status} {task.get('progress', 0)}%", flush=True)
        time.sleep(10)


def _wait(kind: str, task_id: str, label: str) -> dict:
    """Attend la fin d'une tâche Meshy (rigging, animations) et renvoie son résultat."""
    while True:
        task = request_json(f"{_API}/{kind}/{task_id}", headers=_headers())
        status = task.get("status")
        if status == "SUCCEEDED":
            return task["result"]
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            raise RuntimeError(f"Tâche Meshy {kind} {task_id} : {status} — {task.get('task_error')}")
        print(f"    {label} : {status} {task.get('progress', 0)}%", flush=True)
        time.sleep(8)


def _resumable(task_file: Path, create) -> str:
    """Identifiant de tâche mémorisé : une relance reprend la tâche au lieu de la repayer."""
    if task_file.exists():
        return json.loads(task_file.read_text())["id"]
    task_id = create()
    task_file.parent.mkdir(parents=True, exist_ok=True)
    task_file.write_text(json.dumps({"id": task_id}))
    return task_id


def rig(model_task_file: Path, out_fbx: Path, *, height: float) -> str:
    """Squelette automatique d'un personnage déjà généré (5 crédits). Renvoie l'id de la tâche de rigging."""
    model_task = json.loads(model_task_file.read_text())["id"]
    task_file = out_fbx.with_suffix(".rig.json")

    def create() -> str:
        try:
            return request_json(f"{_API}/rigging", method="POST", headers=_headers(),
                                body={"input_task_id": model_task, "height_meters": height})["result"]
        except RuntimeError as error:
            if "Input task not found" not in str(error):
                raise
        # La tâche d'origine a expiré chez Meshy : on envoie le modèle lui-même.
        glb = model_task_file.with_name(model_task_file.name.replace(".task.json", ".glb"))
        data = "data:application/octet-stream;base64," + base64.b64encode(glb.read_bytes()).decode()
        return request_json(f"{_API}/rigging", method="POST", headers=_headers(),
                            body={"model_url": data, "height_meters": height})["result"]

    task_id = _resumable(task_file, create)
    result = _wait("rigging", task_id, out_fbx.stem)
    if not out_fbx.exists():
        download(result["rigged_character_fbx_url"], out_fbx)
    return task_id


def animate(rig_task_id: str, action_id: int, out_fbx: Path) -> Path:
    """Une animation de la bibliothèque Meshy appliquée au squelette (3 crédits)."""
    task_file = out_fbx.with_suffix(".task.json")
    task_id = _resumable(task_file, lambda: request_json(f"{_API}/animations", method="POST", headers=_headers(),
                                                         body={"rig_task_id": rig_task_id, "action_id": action_id})["result"])
    result = _wait("animations", task_id, out_fbx.stem)
    download(result["animation_fbx_url"], out_fbx)
    return out_fbx
