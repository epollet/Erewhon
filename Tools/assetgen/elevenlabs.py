"""Effets sonores avec l'API ElevenLabs (text to sound effects), enregistrés en WAV mono."""
from __future__ import annotations

import array
import json
import urllib.error
import urllib.request
import wave
from pathlib import Path

from common import load_env

_ENDPOINT = "https://api.elevenlabs.io/v1/sound-generation"

# PCM brut 16 bits ; 44,1 kHz demande une offre Pro, on redescend sinon.
_FORMATS = (("pcm_44100", 44100), ("pcm_24000", 24000), ("pcm_22050", 22050))


def _to_mono(samples: array.array, rate: int, duration: float | None) -> array.array:
    """L'API renvoie du stéréo entrelacé : deux fois trop d'échantillons pour la durée demandée.
    On mélange les deux canaux (un son spatialisé dans Unreal doit être mono)."""
    seconds = len(samples) / rate
    stereo = duration is None or seconds > duration * 1.6
    if not stereo:
        return samples
    return array.array("h", ((samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)))


def trim_to_first_hit(samples: array.array, rate: int, keep: float, fade: float = 0.06) -> array.array:
    """Ne garde qu'un seul impact : du début de la première attaque jusqu'à `keep` secondes, avec fondu."""
    window = max(1, rate // 200)   # 5 ms
    peak = max(abs(s) for s in samples) or 1
    start = 0
    for i in range(0, len(samples), window):
        if max(abs(s) for s in samples[i:i + window]) > peak * 0.25:
            start = max(0, i - window * 2)
            break
    out = samples[start:start + int(keep * rate)]
    fade_len = min(len(out), int(fade * rate))
    for k in range(fade_len):
        idx = len(out) - fade_len + k
        out[idx] = int(out[idx] * (1.0 - k / fade_len))
    return out


def write_wav(path: Path, samples: array.array, rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(samples.tobytes())


def generate_sound(prompt: str, out_path: Path, *, duration: float | None = None, loop: bool = False,
                   influence: float = 0.4, trim: float | None = None) -> Path:
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

        samples = array.array("h")
        samples.frombytes(pcm[: len(pcm) // 2 * 2])
        samples = _to_mono(samples, rate, body.get("duration_seconds"))
        if trim:
            samples = trim_to_first_hit(samples, rate, trim)
        write_wav(out_path, samples, rate)
        return out_path
    raise RuntimeError(f"Aucun format accepté — {last_error}")
