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


def image_to_3d(image: Path, out_glb: Path, *, polycount: int, texture_prompt: str = "") -> Path:
    """Crée une tâche Image to 3D, attend sa fin et télécharge le GLB.

    L'identifiant de tâche est mémorisé à côté du GLB : relancer le script après
    une coupure reprend la même tâche au lieu de repayer une génération.
    """
    task_file = out_glb.with_suffix(".task.json")
    task_id = json.loads(task_file.read_text())["id"] if task_file.exists() else None

    if not task_id:
        body = {
            "image_url": "data:image/jpeg;base64," + base64.b64encode(image.read_bytes()).decode("ascii"),
            "ai_model": "latest",
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
