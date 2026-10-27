#!/usr/bin/env python3
"""Render this game's effects as 48 kHz stereo 16-bit WAV, with the standard library only.

    python3 games/<id>/audio_src/make_sfx.py            writes into this game's assets/audio/
    python3 games/<id>/audio_src/make_sfx.py out_dir    writes somewhere else

Deterministic: the same script gives the same bytes on every machine.
"""
import math
import struct
import sys
import wave
from pathlib import Path

RATE = 48000


def render(seconds, voice):
    """voice(t) returns a sample in -1..1; a short fade at each end prevents clicks."""
    count = int(seconds * RATE)
    fade = int(0.004 * RATE)
    samples = []
    for index in range(count):
        edge = min(1.0, index / fade, (count - 1 - index) / fade)
        samples.append(max(-1.0, min(1.0, voice(index / RATE))) * edge)
    return samples


def write(path, samples):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        frames = bytearray()
        for sample in samples:
            value = int(round(sample * 32767 * 0.8))  # leave headroom: effects sit over music
            frames += struct.pack("<hh", value, value)
        output.writeframes(bytes(frames))
    print("wrote", path, f"{len(samples) / RATE:.2f} s")


def press(t):
    # A soft wooden tick: a fast-decaying tone with a little second harmonic.
    return math.exp(-t * 38) * (math.sin(2 * math.pi * 520 * t) + 0.3 * math.sin(2 * math.pi * 1040 * t)) * 0.6


def solved(t):
    # Three rising notes, each ringing into the next.
    total = 0.0
    for index, pitch in enumerate((523.25, 659.25, 783.99)):
        start = index * 0.11
        if t >= start:
            total += math.exp(-(t - start) * 5) * math.sin(2 * math.pi * pitch * (t - start)) * 0.32
    return total


def deal(t):
    # A short soft sweep, like a cloth drawn off the board.
    return math.exp(-t * 14) * math.sin(2 * math.pi * (300 + 500 * t) * t) * 0.35


def main():
    default = Path(__file__).resolve().parents[1] / "assets" / "audio"
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    out.mkdir(parents=True, exist_ok=True)
    write(out / "pl_press.wav", render(0.16, press))
    write(out / "pl_solved.wav", render(0.9, solved))
    write(out / "pl_deal.wav", render(0.3, deal))


if __name__ == "__main__":
    main()
