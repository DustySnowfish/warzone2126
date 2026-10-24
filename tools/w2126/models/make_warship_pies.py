#!/usr/bin/env python3
"""Warzone 2126: placeholder models for the big warships (until the art arrives).

Each class gets a hull (the naval propulsion boat, prhnaval1.pie, stretched) and a deck body (the
Dragon body, drhbod14.pie, stretched) with one connector per turret mount along the deck.

Run from the repository root: python3 tools/w2126/models/make_warship_pies.py
"""
import os

ROOT = os.path.join("data", "mp", "components")
HULL_SRC = os.path.join(ROOT, "prop", "prhnaval1.pie")
BODY_SRC = os.path.join(ROOT, "bodies", "drhbod14.pie")

# name: (hull scale x, y, z), (body scale x, y, z), mounts
CLASSES = {
	"destroyer":   ((1.25, 1.2, 1.5), (0.8, 0.9, 1.25), 3),
	"cruiser":     ((1.5, 1.4, 2.0), (0.95, 1.0, 1.7), 5),
	"battleship":  ((1.8, 1.6, 2.5), (1.15, 1.1, 2.1), 6),
	"superbattle": ((2.2, 1.8, 3.0), (1.4, 1.2, 2.6), 8),
}


def scale_points(lines, sx, sy, sz):
	out = []
	in_points = False
	remaining = 0
	for line in lines:
		stripped = line.strip()
		if stripped.startswith("POINTS"):
			in_points = True
			remaining = int(stripped.split()[1])
			out.append(line)
			continue
		if in_points and remaining > 0:
			x, y, z = (float(v) for v in stripped.split()[:3])
			out.append("\t%d %d %d\n" % (round(x * sx), round(y * sy), round(z * sz)))
			remaining -= 1
			if remaining == 0:
				in_points = False
			continue
		out.append(line)
	return out


def top_height(lines):
	ys = []
	in_points = False
	remaining = 0
	for line in lines:
		s = line.strip()
		if s.startswith("POINTS"):
			in_points = True
			remaining = int(s.split()[1])
			continue
		if in_points and remaining > 0:
			ys.append(float(s.split()[1]))
			remaining -= 1
			if remaining == 0:
				in_points = False
	return max(ys)


def mount_positions(n, length):
	"""Connectors (x, forward, height-less): one line of mounts for up to 5, two lines beyond."""
	if n <= 5:
		step = length / (n + 1)
		return [(0, round(length / 2 - step * (i + 1))) for i in range(n)]
	rows = (n + 1) // 2
	step = length / (rows + 1)
	pos = []
	for i in range(n):
		row = i // 2
		side = -1 if i % 2 == 0 else 1
		pos.append((side * 14, round(length / 2 - step * (row + 1))))
	return pos


def main():
	hull_src = open(HULL_SRC).readlines()
	body_src = open(BODY_SRC).readlines()
	for name, (hs, bs, mounts) in CLASSES.items():
		hull = scale_points(hull_src, *hs)
		with open(os.path.join(ROOT, "prop", "prhnaval_%s.pie" % name), "w") as f:
			f.writelines(hull)
		body = scale_points(body_src, *bs)
		# Replace the connectors: the deck is about 110 units long in the source (z -68 to 52)
		cut = next(i for i, l in enumerate(body) if l.startswith("CONNECTORS"))
		body = body[:cut]
		height = round(top_height(body))
		body.append("CONNECTORS %d\n" % mounts)
		for x, forward in mount_positions(mounts, 110 * bs[2] * 0.8):
			body.append("\t%d %d %d\n" % (x, forward, height))
		with open(os.path.join(ROOT, "bodies", "drnaval_%s.pie" % name), "w") as f:
			f.writelines(body)
		print(name, "mounts", mounts, "deck height", height)


if __name__ == "__main__":
	main()
