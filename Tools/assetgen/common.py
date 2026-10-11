"""Outils partagés : lecture du .env, chemins du projet, requêtes HTTP JSON."""
from __future__ import annotations

import json
import re
import time
import urllib.error
import urllib.request
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
OUTPUT_ROOT = PROJECT_ROOT / "SourceArt" / "Generated"


def load_env() -> dict[str, str]:
    """Lit le .env à la racine du projet (jamais versionné)."""
    env: dict[str, str] = {}
    path = PROJECT_ROOT / ".env"
    if not path.exists():
        raise SystemExit(f"Fichier introuvable : {path}")
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\s*([A-Z0-9_]+)\s*=\s*(.*?)\s*$", line)
        if match:
            env[match.group(1)] = match.group(2).strip().strip('"')
    return env


def request_json(url: str, *, method: str = "GET", headers: dict[str, str] | None = None,
                 body: dict | None = None, timeout: float = 180.0, retries: int = 3) -> dict:
    """Requête JSON avec quelques nouvelles tentatives sur les erreurs transitoires."""
    data = json.dumps(body).encode("utf-8") if body is not None else None
    all_headers = {"Content-Type": "application/json", **(headers or {})}
    for attempt in range(retries):
        request = urllib.request.Request(url, data=data, method=method, headers=all_headers)
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")[:600]
            # 429 et 5xx : on patiente puis on réessaie ; le reste est une erreur de requête.
            if error.code in (429, 500, 502, 503, 504) and attempt < retries - 1:
                time.sleep(5 * (attempt + 1))
                continue
            raise RuntimeError(f"HTTP {error.code} sur {url} : {detail}") from None
        except urllib.error.URLError:
            if attempt < retries - 1:
                time.sleep(5 * (attempt + 1))
                continue
            raise
    raise RuntimeError("inaccessible")


def download(url: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=300) as response:
        path.write_bytes(response.read())
