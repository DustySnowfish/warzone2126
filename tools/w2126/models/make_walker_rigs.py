#!/usr/bin/env python3
"""Warzone 2126: placeholder walker legs (until the art arrives), see design/walkers.md.

Writes the leg part models (data/base/components/bodies/infantry2126/walker*_*.pie, on the infantry
texture page) and data/base/infantry2126/walkers.json: three rigs of legs that carry a vehicle body.

- walker2: two reverse-jointed (chicken-walker) legs, jet nozzles on the shins
- walker4: four splayed spider legs, diagonal pairs stepping together
- walker6: six splayed legs, tripod gait (three feet always down)

The rigs use the infantry rig format (joints, clips with rx/ry/rz/ty tracks) plus:
  hipHeight: height of the body's underside above the ground at rest (units, medium body)
  stride:    units covered per walk cycle
  legs:      per leg, its foot joint and the cycle phase when that foot lands (footprints)
Run from the repository root: python3 tools/w2126/models/make_walker_rigs.py
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_cyborg_rigs import PIE_DIR, write_pie, sample  # noqa: E402

OUT = os.path.join("data", "base", "infantry2126", "walkers.json")
TAU = 2 * math.pi


def track(fn, length=1.0, n=16):
	return sample(length, n, fn)


def biped():
	"""Chicken-walker: thigh slopes back and down to a rear-pointing knee, shin forward and down."""
	hip_y = 35
	joints = [("pelvis", "", (0, hip_y, 0), [(-10, 10, -3, 3, -6, 6, "joint")])]
	clip = {"pelvis": {"ty": track(lambda p: -1.6 * abs(math.cos(TAU * p)))}}
	legs = []
	for side, sx, phase in (("l", -1, 0.0), ("r", 1, 0.5)):
		joints += [
			(side + "_hip", "pelvis", (sx * 16, 0, 0), [(-5, 5, -6, 6, -7, 7, "frame"), (-5.5 + 2 * sx, 5.5 + 2 * sx, -3, 5, -8, 8, "plate")]),
			(side + "_thigh", side + "_hip", (0, -2, 0), [(-5, 5, -20, 1, -5.5, 5.5, "frame"), (-5.5, 5.5, -15, -3, 4, 6.5, "plate")]),
			(side + "_shin", side + "_thigh", (0, -20, 0), [(-3.8, 3.8, -22, 1, -4, 4, "steel"), (-3, 3, -6, 1, -7, -3, "joint"),
				(-3.5, 3.5, -13, -3, -8, -4, "rust")]),   # jet nozzle on the back of the shin
			(side + "_foot", side + "_shin", (0, -22, 0), [(-5, 5, -3.5, 0, -5, 12, "frame"), (-2.5, 2.5, -3.5, -1, -10, -5, "joint")]),
		]
		ph = phase
		# rest: thigh 40 degrees back, shin 80 forward, foot flat; walk: swing and fold
		swing = lambda p, ph=ph: -20 * math.cos(TAU * (p + ph))
		fold = lambda p, ph=ph: 16 * max(0.0, math.sin(TAU * (p + ph)))
		clip[side + "_thigh"] = {"rx": track(lambda p, s=swing, f=fold: 40 + s(p) + f(p))}
		clip[side + "_shin"] = {"rx": track(lambda p, f=fold: -80 - 2 * f(p))}
		clip[side + "_foot"] = {"rx": track(lambda p, s=swing, f=fold: 40 - s(p) + f(p))}
		legs.append({"foot": side + "_foot", "plant": round((0.5 - ph) % 1.0, 3)})
	idle = {"pelvis": {"ty": track(lambda p: 0.8 * math.sin(TAU * p), 3.0, 8)}}
	for side in ("l", "r"):
		idle[side + "_thigh"] = {"rx": track(lambda p: 40, 3.0, 2)}
		idle[side + "_shin"] = {"rx": track(lambda p: -80, 3.0, 2)}
		idle[side + "_foot"] = {"rx": track(lambda p: 40, 3.0, 2)}
	return joints, {"idle": {"length": 3.0, "tracks": idle}, "walk": {"length": 1.0, "stride": 80, "tracks": clip}}, legs, hip_y - 4


def spider(n_legs):
	"""Splayed legs: hip yaw points the leg out, the thigh rises to a high knee, the shin drops
	steeply to the foot. Steps by swinging the hip yaw forward with the foot lifted."""
	k = 1.0 if n_legs == 4 else 1.12
	hip_y = 24
	joints = [("pelvis", "", (0, hip_y, 0), [(-14 * k, 14 * k, -3, 2, -18 * k, 18 * k, "joint")])]
	clip = {"pelvis": {"ty": track(lambda p: -0.8 * abs(math.sin(TAU * p)))}}
	legs = []
	if n_legs == 4:
		# (name, x, z, rest yaw, phase): diagonal pairs together
		layout = [("fl", -1, 20, -145, 0.0), ("fr", 1, 20, -35, 0.5), ("rl", -1, -20, 145, 0.5), ("rr", 1, -20, 35, 0.0)]
	else:
		# tripod gait: front-left, middle-right, rear-left together, then the other three
		layout = [("fl", -1, 24, -140, 0.0), ("ml", -1, 0, 180, 0.5), ("rl", -1, -24, 140, 0.0),
			("fr", 1, 24, -40, 0.5), ("mr", 1, 0, 0, 0.0), ("rr", 1, -24, 40, 0.5)]
	for name, sx, z, yaw, ph in layout:
		joints += [
			(name + "_hip", "pelvis", (sx * 15 * k, 0, z * k), [(-4, 4, -4, 4, -4, 4, "frame")]),
			(name + "_thigh", name + "_hip", (0, 0, 0), [(0, 19, -3, 3, -3.2, 3.2, "frame"), (4, 15, 3, 4.5, -3.6, 3.6, "plate")]),
			(name + "_shin", name + "_thigh", (19, 0, 0), [(0, 34, -2.5, 2.5, -2.5, 2.5, "steel"), (-3, 3, -3, 3, -3.5, 3.5, "joint")]),
			(name + "_foot", name + "_shin", (34, 0, 0), [(-3, 3, -2, 2, -3, 3, "frame")]),
		]
		# forward is +z: for a leg pointing right (yaw near 0) forward is a lower yaw
		fwd = -1 if sx > 0 else 1
		swing = lambda p, ph=ph: 14 * math.cos(TAU * (p + ph))
		lift = lambda p, ph=ph: 14 * max(0.0, math.sin(TAU * (p + ph)))
		clip[name + "_hip"] = {"ry": track(lambda p, s=swing, y=yaw, f=fwd: y + f * s(p))}
		clip[name + "_thigh"] = {"rz": track(lambda p, l=lift: 40 + l(p))}
		clip[name + "_shin"] = {"rz": track(lambda p, l=lift: -120 - 0.5 * l(p))}
		clip[name + "_foot"] = {"rz": track(lambda p, l=lift: 80)}
		legs.append({"foot": name + "_foot", "plant": round((0.5 - ph) % 1.0, 3)})
	idle = {"pelvis": {"ty": track(lambda p: 0.6 * math.sin(TAU * p), 4.0, 8)}}
	for name, sx, z, yaw, ph in layout:
		idle[name + "_hip"] = {"ry": track(lambda p, y=yaw: y, 4.0, 2)}
		idle[name + "_thigh"] = {"rz": track(lambda p: 40, 4.0, 2)}
		idle[name + "_shin"] = {"rz": track(lambda p: -120, 4.0, 2)}
		idle[name + "_foot"] = {"rz": track(lambda p: 80, 4.0, 2)}
	stride = 90 if n_legs == 4 else 84
	return joints, {"idle": {"length": 4.0, "tracks": idle}, "walk": {"length": 1.0, "stride": stride, "tracks": clip}}, legs, hip_y - 2


def main():
	os.makedirs(PIE_DIR, exist_ok=True)
	out = {"version": 1, "rigs": {}}
	for rig, build in (("walker2", biped), ("walker4", lambda: spider(4)), ("walker6", lambda: spider(6))):
		joints, clips, legs, body_height = build()
		jl = []
		for name, parent, pos, parts in joints:
			model = "infantry2126/%s_%s.pie" % (rig, name)
			write_pie(os.path.join(PIE_DIR, os.path.basename(model)), parts, 1.0)
			jl.append({"name": name, "parent": parent, "pos": list(pos), "model": model})
		out["rigs"][rig] = {
			"walker": True,
			"bodyHeight": body_height,
			"legs": legs,
			"joints": jl,
			"clips": clips,
		}
		# icon model for the design screen's propulsion list: the legs at rest are drawn by the
		# engine; this is just the left hip so the component has a model
	with open(OUT, "w") as f:
		json.dump(out, f, indent="\t")
		f.write("\n")
	# Propulsion component models (design screen icons): one leg of each rig
	for rig, name in (("walker2", "prlwalk2.pie"), ("walker4", "prlwalk4.pie"), ("walker6", "prlwalk6.pie")):
		src = os.path.join(PIE_DIR, "%s_%s.pie" % (rig, "l_thigh" if rig == "walker2" else "fl_thigh"))
		with open(src) as fsrc, open(os.path.join("data", "base", "components", "prop", name), "w") as fdst:
			fdst.write(fsrc.read())
	print("wrote", OUT)


if __name__ == "__main__":
	main()
