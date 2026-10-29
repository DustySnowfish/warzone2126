#!/usr/bin/env python3
"""Warzone 2126: synthesized ambient sounds (GPL, generated here; no recorded samples).

wind_loop.ogg      - 24 s seamless wind bed with slow gusts (played as a stream, louder in storms)
surf1..3.ogg       - a wave breaking on the shore
thunder_near.ogg   - a close strike: sharp crack, then a rolling rumble
thunder_far1..2    - distant thunder: rumble only
battle_far1..4     - far-off battle: muffled booms and crackle, heard when fighting is out of view
wings.ogg          - a flock taking off: a burst of wing beats
fire_crackle.ogg   - a grass fire: low roar with crackles

Needs numpy, scipy and ffmpeg (libvorbis).
Run:  python3 tools/w2126/audio/make_ambient_sounds.py
"""
import os
import subprocess
import tempfile

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 44100
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "..", "data", "base", "audio", "sfx", "ambient")
rng = np.random.default_rng(2126)


def lowpass(x, hz, order=4):
    return sosfilt(butter(order, hz, btype="low", fs=SR, output="sos"), x)


def highpass(x, hz, order=2):
    return sosfilt(butter(order, hz, btype="high", fs=SR, output="sos"), x)


def bandpass(x, lo, hi, order=3):
    return sosfilt(butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def noise(seconds):
    return rng.standard_normal(int(seconds * SR))


def brown(seconds):
    b = np.cumsum(noise(seconds))
    b -= lowpass(b, 8.0, 2)  # remove drift
    return b / (np.max(np.abs(b)) + 1e-9)


def env(n, attack, decay, sustain=0.0):
    t = np.arange(n) / SR
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    d = np.exp(-np.maximum(t - attack, 0) / max(decay, 1e-4))
    return a * (sustain + (1 - sustain) * d)


def smooth_random(n, rate_hz, lo=0.0, hi=1.0):
    pts = int(n / SR * rate_hz) + 3
    vals = rng.uniform(lo, hi, pts)
    xs = np.linspace(0, n, pts)
    return np.interp(np.arange(n), xs, vals)


def normalize(x, peak):
    return x / (np.max(np.abs(x)) + 1e-9) * peak


def write(name, x, peak=0.8):
    x = normalize(x, peak)
    with tempfile.TemporaryDirectory() as td:
        wav = os.path.join(td, "t.wav")
        wavfile.write(wav, SR, (x * 32767).astype(np.int16))
        out = os.path.normpath(os.path.join(OUT, name))
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", "3", "-ac", "1", out], check=True)
    print("wrote", out, "%.1f s" % (len(x) / SR))


def wind_loop():
    seconds = 24.0
    n = int(seconds * SR)
    base = lowpass(noise(seconds + 2), 650)[:n + 2 * SR]
    gust = smooth_random(len(base), 0.35, 0.35, 1.0)
    hiss = highpass(lowpass(noise(len(base) / SR), 2600), 900) * 0.25 * gust ** 2
    x = (base * gust + hiss)
    # Seamless loop: crossfade the extra two seconds back over the start
    fade = np.linspace(0, 1, 2 * SR)
    head = x[:2 * SR] * fade + x[n:n + 2 * SR] * (1 - fade)
    out = np.concatenate([head, x[2 * SR:n]])
    write("wind_loop.ogg", out, 0.55)


def surf(i):
    seconds = rng.uniform(2.8, 3.8)
    n = int(seconds * SR)
    body = bandpass(noise(seconds), 180, 2400) * env(n, rng.uniform(0.25, 0.45), rng.uniform(0.9, 1.3))
    fizz = highpass(noise(seconds), 2500) * env(n, 0.6, 1.6) * 0.35 * smooth_random(n, 12, 0.3, 1.0)
    rumble = lowpass(noise(seconds), 140) * env(n, 0.2, 0.8) * 0.6
    write("surf%d.ogg" % i, body + fizz + rumble, 0.6)


def thunder(name, near):
    seconds = 6.5 if near else 7.5
    n = int(seconds * SR)
    rumble = lowpass(brown(seconds), 180 if near else 110)
    rolls = smooth_random(n, 3.0, 0.2, 1.0) ** 2
    start = 0.05 if near else rng.uniform(0.3, 0.6)
    x = rumble * rolls * env(n, start + 0.4, 2.4 if near else 2.8)
    if near:
        crack = highpass(noise(seconds), 400) * env(n, 0.003, 0.09) * 2.2
        crack += lowpass(noise(seconds), 1200) * env(n, 0.01, 0.35) * 1.2
        x = x + crack
    write(name, x, 0.85 if near else 0.6)


def battle(i):
    seconds = 3.0
    n = int(seconds * SR)
    x = np.zeros(n)
    t = np.arange(n) / SR
    for _ in range(rng.integers(1, 4)):
        at = rng.uniform(0.0, 1.4)
        f0 = rng.uniform(55, 80)
        sweep = f0 * np.exp(-np.maximum(t - at, 0) * 1.5)
        phase = 2 * np.pi * np.cumsum(sweep) / SR
        boom = np.sin(phase) * env(n, 0.01, rng.uniform(0.35, 0.7)) 
        boom = np.roll(boom, int(at * SR))
        boom[:int(at * SR)] = 0
        thump = lowpass(noise(seconds), 300) * np.roll(env(n, 0.01, 0.5), int(at * SR))
        x += boom * 0.8 + thump * 0.6
    if i % 2 == 0:
        # Far-off machine gun: a muffled rattle
        at = rng.uniform(0.3, 1.2)
        rate = rng.uniform(9, 13)
        clicks = (np.sin(2 * np.pi * rate * t) > 0.92).astype(float) * (t > at) * (t < at + rng.uniform(0.6, 1.1))
        x += lowpass(clicks * noise(seconds), 700) * 1.5
    write("battle_far%d.ogg" % i, lowpass(x, 500), 0.5)


def wings():
    seconds = 1.4
    n = int(seconds * SR)
    t = np.arange(n) / SR
    beats = 0.5 + 0.5 * np.sin(2 * np.pi * 14 * t + 3 * smooth_random(n, 4))
    x = bandpass(noise(seconds), 400, 4000) * beats ** 3 * env(n, 0.05, 0.45)
    write("wings.ogg", x, 0.5)


def fire_crackle():
    seconds = 3.0
    n = int(seconds * SR)
    roar = lowpass(noise(seconds), 400) * 0.5 * smooth_random(n, 2, 0.6, 1.0)
    pops = np.zeros(n)
    for _ in range(45):
        at = int(rng.uniform(0, seconds - 0.05) * SR)
        length = int(rng.uniform(0.002, 0.012) * SR)
        pops[at:at + length] += rng.uniform(0.4, 1.0) * rng.standard_normal(length)
    x = roar + highpass(pops, 1500) * 1.4
    x *= env(n, 0.15, 10.0, 1.0) * np.minimum(1, (seconds - np.arange(n) / SR) / 0.4)
    write("fire_crackle.ogg", x, 0.5)


if __name__ == "__main__":
    os.makedirs(os.path.normpath(OUT), exist_ok=True)
    wind_loop()
    for i in (1, 2, 3):
        surf(i)
    thunder("thunder_near.ogg", True)
    thunder("thunder_far1.ogg", False)
    thunder("thunder_far2.ogg", False)
    for i in (1, 2, 3, 4):
        battle(i)
    wings()
    fire_crackle()
