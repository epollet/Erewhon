"""Effets sonores avec l'API ElevenLabs (text to sound effects), enregistrés en WAV."""
from __future__ import annotations

import json
import urllib.error
import urllib.request
import wave
from pathlib import Path

from common import load_env

_ENDPOINT = "https://api.elevenlabs.io/v1/sound-generation"

# PCM brut 16 bits mono ; 44,1 kHz demande une offre Pro, on redescend sinon.
_FORMATS = (("pcm_44100", 44100), ("pcm_24000", 24000), ("pcm_22050", 22050))


def generate_sound(prompt: str, out_path: Path, *, duration: float | None = None, loop: bool = False,
                   influence: float = 0.4) -> Path:
    env = load_env()
    body = {"text": prompt, "prompt_influence": influence, "loop": loop, "model_id": "eleven_text_to_sound_v2"}
    if duration:
        body["duration_seconds"] = max(0.5, min(30.0, duration))
    data = json.dumps(body).encode("utf-8")

    last_error = ""
    for output_format, rate in _FORMATS:
        request = urllib.request.Request(f"{_ENDPOINT}?output_format={output_format}", data=data, method="POST",
                                         headers={"xi-api-key": env["ELEVENLAB_API_KEY"], "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                pcm = response.read()
        except urllib.error.HTTPError as error:
            last_error = f"HTTP {error.code} : {error.read().decode('utf-8', 'replace')[:300]}"
            if error.code in (401, 402, 403, 422):
                continue   # format refusé par l'offre : on tente le suivant
            raise RuntimeError(last_error) from None

        out_path.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(out_path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes(pcm)
        return out_path
    raise RuntimeError(f"Aucun format accepté — {last_error}")
