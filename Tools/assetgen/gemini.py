"""Génération d'images avec l'API Gemini (endpoint Interactions)."""
from __future__ import annotations

import base64
from pathlib import Path

from common import load_env, request_json

_ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/interactions"


def generate_image(prompt: str, out_path: Path, *, aspect_ratio: str = "1:1", image_size: str = "1K",
                   references: list[Path] | None = None) -> Path:
    """Génère une image JPEG à partir d'un prompt (et d'images de référence optionnelles)."""
    env = load_env()
    headers = {"x-goog-api-key": env["GEMINI_API_KEY"]}
    if env.get("GEMINI_API_REVISION"):
        headers["Api-Revision"] = env["GEMINI_API_REVISION"]

    content: list[dict] = [{"type": "text", "text": prompt}]
    for reference in references or []:
        content.append({
            "type": "image",
            "mime_type": "image/jpeg" if reference.suffix.lower() in (".jpg", ".jpeg") else "image/png",
            "data": base64.b64encode(reference.read_bytes()).decode("ascii"),
        })

    body = {
        "model": env.get("GEMINI_IMAGE_MODEL", "gemini-3.1-flash-image"),
        "input": content,
        "response_format": {
            "type": "image",
            "mime_type": "image/jpeg",   # seul format accepté en sortie
            "aspect_ratio": aspect_ratio,
            "image_size": image_size,
        },
    }
    response = request_json(_ENDPOINT, method="POST", headers=headers, body=body, timeout=300)

    for step in response.get("steps", []):
        if step.get("type") != "model_output":
            continue
        for part in step.get("content", []):
            if part.get("type") == "image" and part.get("data"):
                out_path.parent.mkdir(parents=True, exist_ok=True)
                out_path.write_bytes(base64.b64decode(part["data"]))
                return out_path
    raise RuntimeError(f"Aucune image dans la réponse : {str(response)[:400]}")
