#!/usr/bin/env python3
"""Renders a page being turned, as 48 kHz stereo 16-bit WAV, with the standard library only.

    python3 make_page.py [out dir]

A page turned is one soft stroke, like a blade drawn slowly along a whetstone: an airy
wash of hiss and, through it, a faint ringing band near 4 kHz that moves a little as the
stroke goes, swelling in and falling away to silence, roughened only slightly by the
grain of the stone. Nothing in it is sudden. Every turn lasts exactly 0.68 s. The owner
chose it by ear over crackles (too hard), a slide that ended in a landing (a tap on the
desk) and a quicker swell (too hard): three of the candidates, K (the ring as heard
first), L (softer, less ring, the top at 6.5 kHz) and M (softest and darkest), take
turns as the variations.

Ten variations are written (pl_page_0.wav ... pl_page_9.wav), K, L and M in turn, each
with its band a little higher or lower, so turning many pages does not repeat one sound; each has a mirror (pl_page_back_0.wav ...) for a page turned back, as a page
turned forward crosses right to left. Deterministic: the same script gives the same
bytes on every machine. Writes into this game's assets/audio/ unless told otherwise.
"""
import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 48000
VARIATIONS = 10
SECONDS = 0.68
PEAK = 0.09

# The three strokes: how long each swells in (seconds), how loud its ringing band and its
# airy wash are, where its top is softened (Hz), how much the stone's grain roughens it,
# and its loudness (RMS over the body of the stroke; the slide it replaced was 0.0137).
STROKES = {
    "K": {"rise": 0.17, "ring": 0.60, "wash": 0.55, "top": 8000, "grit": 0.20, "loudness": 0.0100},
    "L": {"rise": 0.17, "ring": 0.40, "wash": 0.60, "top": 6500, "grit": 0.15, "loudness": 0.0090},
    "M": {"rise": 0.204, "ring": 0.25, "wash": 0.65, "top": 5500, "grit": 0.10, "loudness": 0.0085},
}


class Filter:
    """A two-pole filter (RBJ cookbook): high-, low- or band-pass, retunable."""

    def __init__(self, kind, corner, q=0.707):
        self.kind, self.q = kind, q
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
        self.tune(corner)

    def tune(self, corner):
        w = 2 * math.pi * corner / RATE
        alpha = math.sin(w) / (2 * self.q)
        a0 = 1 + alpha
        c = math.cos(w)
        if self.kind == "high":
            self.b0 = self.b2 = (1 + c) / 2 / a0
            self.b1 = -(1 + c) / a0
        elif self.kind == "low":
            self.b0 = self.b2 = (1 - c) / 2 / a0
            self.b1 = (1 - c) / a0
        else:
            self.b0, self.b1, self.b2 = alpha / a0, 0.0, -alpha / a0
        self.a1, self.a2 = -2 * c / a0, (1 - alpha) / a0

    def __call__(self, x):
        y = self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def swell(t, seconds, rise):
    """A smooth rise over `rise` seconds, then a fall to silence over the rest."""
    if t < rise:
        v = t / rise
        return v * v * (3 - 2 * v)
    v = min(1.0, (t - rise) / (seconds - rise))
    return math.cos(math.pi * v) * 0.5 + 0.5


def stroke(seed, recipe, band_from, band_to):
    """One turn, SECONDS long, its ringing band moving from `band_from` to `band_to` Hz."""
    rng = random.Random(seed)
    count = int(round(SECONDS * RATE))
    ring = [Filter("band", band_from, 4.0), Filter("band", band_from, 4.0)]
    air = [Filter("high", 1200), Filter("low", 6000)]
    soft = [Filter("low", recipe["top"]), Filter("low", recipe["top"])]
    grain = 0.0
    left, right = [0.0] * count, [0.0] * count
    for index in range(count):
        t = index / RATE
        u = t / SECONDS
        if index % 32 == 0:
            centre = band_from + (band_to - band_from) * (u * u * (3 - 2 * u))
            for f in ring:
                f.tune(centre)
        # The stone's grain: a slow, smoothed flutter in loudness.
        grain += (rng.uniform(-1, 1) - grain) * (220 / RATE)
        noise = rng.uniform(-1, 1)
        sample = ring[1](ring[0](noise)) * recipe["ring"]
        wash = noise
        for f in air:
            wash = f(wash)
        sample += wash * recipe["wash"]
        for f in soft:
            sample = f(sample)
        sample *= swell(t, SECONDS, recipe["rise"]) * (1 + 6 * recipe["grit"] * grain)
        left[index] = sample * (0.8 + 0.2 * u)
        right[index] = sample * (1.0 - 0.2 * u)
    body = math.sqrt(sum(v * v for v in left + right) / (2 * count) * 2.2)
    gain = recipe["loudness"] / max(body, 1e-9)
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right)) * gain
    if peak > PEAK:
        gain *= PEAK / peak
    return [v * gain for v in left], [v * gain for v in right]


def write(path, left, right):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            frames += struct.pack("<hh", int(round(l * 32767)), int(round(r * 32767)))
        output.writeframes(bytes(frames))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets" / "audio"
    out.mkdir(parents=True, exist_ok=True)
    for number in range(VARIATIONS):
        # K, L and M in turn, each time from a new seed with its band moved a little.
        label = "KLM"[number % 3]
        shift = ((number * 5) % 7 - 3) * 60
        left, right = stroke(163 + number, STROKES[label], 3800 + shift, 4300 + shift)
        write(out / f"pl_page_{number}.wav", left, right)
        write(out / f"pl_page_back_{number}.wav", right, left)
        print(f"pl_page_{number}: {label}, {len(left) / RATE:.3f} s, band {3800 + shift}-{4300 + shift} Hz")

if __name__ == "__main__":
    main()
