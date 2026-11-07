#!/usr/bin/env python3
"""Warzone 2126: placeholder cyborgs (until the art arrives), see design/infantry.md.

A cyborg is a robot frame run by vat-grown human meatstock: the frame is built from rigid parts, one
model per joint, and the meatstock core shows through the chest. This writes:
  - data/base/components/bodies/infantry2126/infantry2126.png (+ _tcmask): flat colour patches
  - data/base/components/bodies/infantry2126/<rig>_<joint>.pie: one model per joint, its origin at the pivot
  - data/base/infantry2126/rigs.json: the joints, and the animation clips (idle, walk, run, fire)

Model space is the game's: x across, y up, z forward; 1 m = 32 units.
Run from the repository root: python3 tools/w2126/models/make_cyborg_rigs.py
"""
import json
import math
import os

from PIL import Image

PAGE = "infantry2126.png"
# next to the part models (a data-root path in the PIE: the texpages folder is the original game's)
TEX_DIR = os.path.join("data", "base", "components", "bodies", "infantry2126")
PIE_DIR = os.path.join("data", "base", "components", "bodies", "infantry2126")
RIG_FILE = os.path.join("data", "base", "infantry2126", "rigs.json")

# Colour patches on a 4 x 4 grid of a 128 px page: name -> (cell, colour, team colour)
PATCHES = {
	"frame":   (0, (58, 60, 62), False),     # dark gunmetal
	"plate":   (1, (150, 150, 146), True),   # armour plates (team colour)
	"joint":   (2, (34, 34, 36), False),     # actuators, near black
	"flesh":   (3, (196, 150, 150), False),  # the meatstock: pale grey-pink tissue
	"amber":   (4, (214, 140, 34), False),   # nutrient fluid
	"eye":     (5, (235, 235, 230), True),   # sensor slit (team colour)
	"steel":   (6, (104, 106, 108), False),  # bare steel
	"rust":    (7, (110, 72, 48), False),    # salvaged plates
}


def write_texture():
	size, cell = 128, 32
	img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
	mask = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	for idx, colour, team in PATCHES.values():
		x0, y0 = (idx % 4) * cell, (idx // 4) * cell
		for y in range(y0, y0 + cell):
			for x in range(x0, x0 + cell):
				# a little noise and an edge darkening so plates read as worn metal
				edge = min(x - x0, y - y0, x0 + cell - 1 - x, y0 + cell - 1 - y)
				k = 0.82 if edge < 2 else 1.0
				n = ((x * 7919 + y * 104729) % 17) / 17.0 * 0.12 + 0.94
				img.putpixel((x, y), tuple(min(255, int(c * k * n)) for c in colour) + (255,))
				if team:
					mask.putpixel((x, y), (255, 255, 255, 255))
	img.save(os.path.join(TEX_DIR, PAGE))
	mask.save(os.path.join(TEX_DIR, PAGE.replace(".png", "_tcmask.png")))


def uv(name):
	idx = PATCHES[name][0]
	u0, v0 = (idx % 4) * 0.25 + 0.03, (idx // 4) * 0.25 + 0.03
	return (u0, v0, u0 + 0.19, v0 + 0.19)


def box(points, polys, x0, x1, y0, y1, z0, z1, patch):
	"""A box, all six faces, outward winding as the PIE loader expects (as make_warship_pies.box)."""
	base = len(points)
	for y in (y0, y1):
		for z in (z0, z1):
			for x in (x0, x1):
				points.append((x, y, z))
	quads = [(4, 5, 7, 6), (0, 2, 3, 1), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
	u0, v0, u1, v1 = uv(patch)
	for a, b, c, d in quads:
		polys.append("\t200 3 %d %d %d %f %f %f %f %f %f\n" % (base + a, base + b, base + c, u0, v0, u1, v0, u1, v1))
		polys.append("\t200 3 %d %d %d %f %f %f %f %f %f\n" % (base + a, base + c, base + d, u0, v0, u1, v1, u0, v1))


def write_pie(path, parts, k):
	points, polys = [], []
	for p in parts:
		x0, x1, y0, y1, z0, z1, patch = p
		box(points, polys, x0 * k, x1 * k, y0 * k, y1 * k, z0 * k, z1 * k, patch)
	lines = ["PIE 4\n", "TYPE 10200\n", "TEXTURE 0 /components/bodies/infantry2126/%s\n" % PAGE, "TCMASK 0 /components/bodies/infantry2126/%s\n" % PAGE.replace(".png", "_tcmask.png"), "LEVELS 1\n", "LEVEL 1\n"]
	lines.append("POINTS %d\n" % len(points))
	lines += ["\t%.1f %.1f %.1f\n" % pt for pt in points]
	lines.append("POLYGONS %d\n" % len(polys))
	lines += polys
	with open(path, "w") as f:
		f.writelines(lines)


# Joints: name, parent, position relative to the parent (units at k = 1), parts (boxes relative to
# the joint: x0 x1 y0 y1 z0 z1 patch). Limbs hang down (-y) from their joint; feet point forward.
def light_frame():
	return [
		("pelvis", "", (0, 30, 0), [(-7, 7, -4, 3, -5, 4, "frame"), (-8, 8, -2, 1, -6, -4, "joint")]),
		("chest", "pelvis", (0, 3, 0), [
			(-10, 10, 2, 17, -6, 5, "frame"),        # chest cage
			(-11, -6, 8, 18, -7, 6, "plate"),        # left breast plate
			(6, 11, 8, 18, -7, 6, "plate"),          # right breast plate
			(-5, 5, 4, 14, 4, 6.5, "flesh"),         # the meatstock core behind the slots
			(-6, 6, 13, 15, 4.5, 7, "steel"),        # cage bars over it
			(-6, 6, 8, 9.5, 4.5, 7, "steel"),
			(-3, 3, 4, 17, -9, -6, "amber"),         # nutrient tank on the back
			(-1, 1, 0, 4, -2, 2, "joint")]),         # spine actuator
		("head", "chest", (0, 18, 1), [
			(-4, 4, 0, 6, -4, 4, "frame"),
			(-3, 3, 2.5, 4, 4, 5, "eye"),             # sensor slit (team colour)
			(-1, 1, 6, 9, -3, -1, "steel")]),        # antenna stub
		("l_upperarm", "chest", (-12.5, 15, 0), [(-3, 3, -11, 1, -3, 3, "frame"), (-4, 1, -4, 2, -4, 4, "plate")]),
		("l_forearm", "l_upperarm", (0, -11, 0), [(-2.5, 2.5, -10, 0, -2.5, 2.5, "steel"), (-1.5, 1.5, -2, 1, -2, 2, "joint")]),
		("l_hand", "l_forearm", (0, -10, 0), [(-2, 2, -4, 0, -2, 3, "joint")]),
		("r_upperarm", "chest", (12.5, 15, 0), [(-3, 3, -11, 1, -3, 3, "frame"), (-1, 4, -4, 2, -4, 4, "plate")]),
		("r_forearm", "r_upperarm", (0, -11, 0), [(-2.5, 2.5, -10, 0, -2.5, 2.5, "steel"), (-1.5, 1.5, -2, 1, -2, 2, "joint")]),
		("r_hand", "r_forearm", (0, -10, 0), [(-2, 2, -4, 0, -2, 3, "joint")]),
		("l_thigh", "pelvis", (-5, -2, 0), [(-3.2, 3.2, -13, 1, -3.5, 3.5, "frame"), (-3.6, 3.6, -9, -3, 2, 4.5, "plate")]),
		("l_shin", "l_thigh", (0, -13, 0), [(-2.6, 2.6, -13, 0, -2.5, 2.5, "steel"), (-1.5, 1.5, -12, -2, -4, -2.5, "joint")]),
		("l_foot", "l_shin", (0, -13, 0), [(-3, 3, -4, 0, -3, 7, "frame")]),
		("r_thigh", "pelvis", (5, -2, 0), [(-3.2, 3.2, -13, 1, -3.5, 3.5, "frame"), (-3.6, 3.6, -9, -3, 2, 4.5, "plate")]),
		("r_shin", "r_thigh", (0, -13, 0), [(-2.6, 2.6, -13, 0, -2.5, 2.5, "steel"), (-1.5, 1.5, -12, -2, -4, -2.5, "joint")]),
		("r_foot", "r_shin", (0, -13, 0), [(-3, 3, -4, 0, -3, 7, "frame")]),
	]


def heavy_frame():
	"""Broader and hunched: the chest sits lower and further forward, bigger shoulders, the core
	higher on the back."""
	j = {name: [name, parent, pos, parts] for name, parent, pos, parts in light_frame()}
	j["chest"][3] = [
		(-12, 12, 1, 16, -7, 6, "frame"),
		(-13, -7, 6, 17, -8, 7, "rust"),
		(7, 13, 6, 17, -8, 7, "plate"),
		(-5, 5, 4, 12, 5, 7.5, "flesh"),
		(-6, 6, 11, 13, 5.5, 8, "steel"),
		(-5, 5, 9, 19, -12, -7, "amber"),            # big tank high on the back
		(-1.5, 1.5, 0, 4, -2, 2, "joint")]
	j["head"][2] = (0, 15, 3)                         # head sunk between the shoulders
	j["l_upperarm"][2] = (-15, 13, 0)
	j["r_upperarm"][2] = (15, 13, 0)
	j["l_upperarm"][3] = [(-4, 4, -11, 1, -4, 4, "frame"), (-6, 1, -5, 3, -5, 5, "plate")]
	j["r_upperarm"][3] = [(-4, 4, -11, 1, -4, 4, "frame"), (-1, 6, -5, 3, -5, 5, "plate")]
	j["r_forearm"][3] = [(-3.5, 3.5, -12, 0, -3.5, 3.5, "frame")]   # the weapon is built into this arm
	j["l_thigh"][3] = [(-4, 4, -13, 1, -4, 4, "frame"), (-4.5, 4.5, -9, -3, 2.5, 5, "rust")]
	j["r_thigh"][3] = [(-4, 4, -13, 1, -4, 4, "frame"), (-4.5, 4.5, -9, -3, 2.5, 5, "plate")]
	j["l_foot"][3] = [(-3.6, 3.6, -4, 0, -4, 8, "frame")]
	j["r_foot"][3] = [(-3.6, 3.6, -4, 0, -4, 8, "frame")]
	return [tuple(v) for v in j.values()]


# Clips: per joint, channels rx / ry / rz (degrees) and ty (units), sampled as [time, value] keys.
def sample(length, n, fn):
	return [[round(length * i / n, 4), round(fn(i / n), 2)] for i in range(n + 1)]


def gait(length, swing, knee, arm, elbow, lean, bob, n=12):
	s = lambda p: math.sin(2 * math.pi * p)
	return {
		"length": length,
		"tracks": {
			"pelvis": {"ty": sample(length, n, lambda p: bob * abs(math.cos(2 * math.pi * p)) - bob)},
			"chest": {"rx": sample(length, n, lambda p: lean), "ry": sample(length, n, lambda p: 4 * s(p))},
			"l_thigh": {"rx": sample(length, n, lambda p: -swing * s(p))},
			"r_thigh": {"rx": sample(length, n, lambda p: swing * s(p))},
			# the knee bends while the leg swings forward (lifting the foot), straight in the stance
			"l_shin": {"rx": sample(length, n, lambda p: knee * max(0.0, s(p + 0.15)))},
			"r_shin": {"rx": sample(length, n, lambda p: knee * max(0.0, -s(p + 0.15)))},
			"l_foot": {"rx": sample(length, n, lambda p: -0.3 * knee * max(0.0, s(p + 0.3)))},
			"r_foot": {"rx": sample(length, n, lambda p: -0.3 * knee * max(0.0, -s(p + 0.3)))},
			"l_upperarm": {"rx": sample(length, n, lambda p: arm * s(p))},
			"l_forearm": {"rx": sample(length, n, lambda p: -elbow)},
		},
	}


def clips():
	idle = {
		"length": 4.0,
		"tracks": {
			"chest": {"rx": sample(4.0, 8, lambda p: 1.2 * math.sin(2 * math.pi * p * 2))},
			"head": {"ry": sample(4.0, 8, lambda p: 14 * math.sin(2 * math.pi * p))},
			"l_upperarm": {"rx": sample(4.0, 4, lambda p: -6), "rz": sample(4.0, 4, lambda p: -6)},
			"l_forearm": {"rx": sample(4.0, 4, lambda p: -18)},
			"l_thigh": {"rz": sample(4.0, 4, lambda p: -4)},
			"r_thigh": {"rz": sample(4.0, 4, lambda p: 4)},
		},
	}
	# The firing arm is posed by the engine (aimed at the target); "fire" is the recoil on top of it
	fire = {
		"length": 0.3,
		"tracks": {
			"chest": {"rx": [[0, 0], [0.05, -4], [0.3, 0]]},
			"r_upperarm": {"rx": [[0, 0], [0.04, 8], [0.3, 0]]},
			"r_forearm": {"rx": [[0, 0], [0.04, 6], [0.3, 0]]},
		},
	}
	return {
		"idle": idle,
		# stride: units travelled per cycle (two steps); the engine plays the cycle at the unit's speed
		"walk": dict(gait(1.0, 24, 38, 16, 18, 5, 1.2), stride=56),
		"run": dict(gait(0.7, 38, 70, 30, 60, 14, 2.5), stride=96),
		"fire": fire,
	}


RIGS = {
	# light cyborgs (and engineers, mechanics): fireteams of 3
	"light": {"frame": light_frame, "scale": 1.0, "figures": 3, "spacing": 34},
	# heavy cyborgs: fireteams of 2
	"heavy": {"frame": heavy_frame, "scale": 1.12, "figures": 2, "spacing": 40},
}


def main():
	os.makedirs(PIE_DIR, exist_ok=True)
	os.makedirs(TEX_DIR, exist_ok=True)
	os.makedirs(os.path.dirname(RIG_FILE), exist_ok=True)
	write_texture()
	out = {"version": 1, "rigs": {}}
	for rig, spec in RIGS.items():
		k = spec["scale"]
		joints = []
		for name, parent, pos, parts in spec["frame"]():
			model = "infantry2126/%s_%s.pie" % (rig, name)
			write_pie(os.path.join(PIE_DIR, os.path.basename(model)), parts, k)
			joints.append({"name": name, "parent": parent, "pos": [round(v * k, 2) for v in pos], "model": model})
		out["rigs"][rig] = {
			"figures": spec["figures"],
			"spacing": spec["spacing"],
			"scale": 1.0,
			"joints": joints,
			# where the cyborg's weapon (or tool) is drawn, relative to the joint
			"weaponJoint": "r_forearm",
			"weaponPos": [0, round(-8 * k, 2), round(1 * k, 2)],
			# the classic cyborg weapon models point their barrel along -z from the mount: turn it
			# to run along the forearm and out of the hand
			"weaponRot": [-90, 0, 0],
			"weaponScale": 0.75,
			# the arm that holds the weapon, raised to aim it forward
			"aim": {"r_upperarm": {"rx": -75, "rz": 8}, "r_forearm": {"rx": -15}, "l_upperarm": {"rx": -55, "rz": -20}, "l_forearm": {"rx": -45}},
			"clips": clips(),
		}
	with open(RIG_FILE, "w") as f:
		json.dump(out, f, indent="\t")
		f.write("\n")
	print("wrote", RIG_FILE, "and", sum(len(r["joints"]) for r in out["rigs"].values()), "part models")


if __name__ == "__main__":
	main()
