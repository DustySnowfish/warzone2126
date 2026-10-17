#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Writes a glTF 2.0 orientation test model for the Warzone 2126 importer.

A box hull (size given in metres) with a wedge nose at the front. Each side has its own
colour so orientation and mirroring are easy to check in game:

    front (nose, +Z in glTF) : yellow
    back                     : grey
    model's left  (+X)       : red
    model's right (-X)       : blue
    top                      : white, with a team-colour stripe
    bottom                   : dark grey

Materials (ORM texture, for the PBR shader): front polished metal, sides brushed
metal, top painted, back rough.

Also writes a turret connector empty, conn_turret_1, on top of the hull.

Usage: make_test_model.py OUT_DIR NAME [length width height]
Writes OUT_DIR/NAME.glb, NAME_basecolor.png, NAME_orm.png, NAME_tc.png. Requires Pillow.
"""

import json
import os
import struct
import sys

from PIL import Image, ImageDraw

# Atlas layout: 4x2 cells, each a solid colour
CELLS = {
    'front': (0, 0, (230, 200, 40)),
    'back': (1, 0, (128, 128, 128)),
    'left': (2, 0, (200, 40, 40)),
    'right': (3, 0, (40, 70, 200)),
    'top': (0, 1, (235, 235, 235)),
    'bottom': (1, 1, (50, 50, 50)),
}
# Per-side material for the PBR test: (occlusion, roughness, metallic)
ORM = {
    'front': (1.0, 0.15, 1.0),   # polished metal
    'back': (1.0, 0.85, 0.0),    # rough concrete-like
    'left': (1.0, 0.35, 1.0),    # brushed metal
    'right': (1.0, 0.35, 1.0),
    'top': (1.0, 0.55, 0.0),     # painted
    'bottom': (0.6, 0.9, 0.0),
}
ATLAS = 512
CELL = ATLAS // 4


def cell_uv(name, u, v):
    cx, cy, _ = CELLS[name]
    pad = 0.08
    return ((cx + pad + u * (1 - 2 * pad)) / 4.0, (cy + pad + v * (1 - 2 * pad)) / 4.0)


def build(length, width, height):
    hx, hz = width / 2, length / 2
    nose = length * 0.25          # wedge length beyond the box front
    y0, y1 = 0.15 * height, height
    pos, nrm, uv, idx = [], [], [], []

    def add_tri(i0, i1, i2, n):
        # glTF front faces are counter-clockwise: order each triangle so its
        # geometric normal agrees with the intended outward normal.
        a, b, c = pos[i0], pos[i1], pos[i2]
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cr[k] * n[k] for k in range(3)) < 0:
            i1, i2 = i2, i1
        idx.extend([i0, i1, i2])

    def quad(name, a, b, c, d, n):
        base = len(pos)
        for p, (u, v) in zip((a, b, c, d), ((0, 1), (1, 1), (1, 0), (0, 0))):
            pos.append(p)
            nrm.append(n)
            uv.append(cell_uv(name, u, v))
        add_tri(base, base + 1, base + 2, n)
        add_tri(base, base + 2, base + 3, n)

    def tri(name, a, b, c, n):
        base = len(pos)
        for p, (u, v) in zip((a, b, c), ((0, 1), (1, 1), (0.5, 0))):
            pos.append(p)
            nrm.append(n)
            uv.append(cell_uv(name, u, v))
        add_tri(base, base + 1, base + 2, n)

    import math

    def norm(v):
        l = math.sqrt(sum(x * x for x in v))
        return tuple(x / l for x in v)

    # Box corners: front is +Z
    fl0, fr0 = (hx, y0, hz), (-hx, y0, hz)
    fl1, fr1 = (hx, y1, hz), (-hx, y1, hz)
    bl0, br0 = (hx, y0, -hz), (-hx, y0, -hz)
    bl1, br1 = (hx, y1, -hz), (-hx, y1, -hz)
    tip0, tip1 = (0, y0, hz + nose), (0, (y0 + y1) / 2, hz + nose)

    quad('back', br0, bl0, bl1, br1, (0, 0, -1))
    quad('left', bl0, fl0, fl1, bl1, (1, 0, 0))
    quad('right', fr0, br0, br1, fr1, (-1, 0, 0))
    quad('top', fr1, br1, bl1, fl1, (0, 1, 0))
    quad('bottom', fl0, bl0, br0, fr0, (0, -1, 0))
    # Wedge nose: two slanted side faces, a top face and the bottom
    quad('front', fl0, tip0, tip1, fl1, norm((hx, 0, nose)))
    quad('front', tip0, fr0, fr1, tip1, norm((-hx, 0, nose)))
    tri('front', fl1, tip1, fr1, norm((0, nose, (y1 - y0) / 2)))
    tri('bottom', fl0, fr0, tip0, (0, -1, 0))
    return pos, nrm, uv, idx, y1


def write_textures(out_dir, name):
    img = Image.new('RGB', (ATLAS, ATLAS), (0, 0, 0))
    tc = Image.new('L', (ATLAS, ATLAS), 0)
    d, dt = ImageDraw.Draw(img), ImageDraw.Draw(tc)
    for key, (cx, cy, col) in CELLS.items():
        box = (cx * CELL, cy * CELL, (cx + 1) * CELL - 1, (cy + 1) * CELL - 1)
        d.rectangle(box, fill=col)
    # Team-colour stripe across the top cell, front to back
    cx, cy, _ = CELLS['top']
    stripe = (cx * CELL + CELL * 3 // 8, cy * CELL, cx * CELL + CELL * 5 // 8, (cy + 1) * CELL - 1)
    d.rectangle(stripe, fill=(128, 128, 128))
    dt.rectangle(stripe, fill=255)
    img.save(os.path.join(out_dir, name + '_basecolor.png'))
    tc.save(os.path.join(out_dir, name + '_tc.png'))
    orm = Image.new('RGB', (ATLAS, ATLAS), (255, 255, 0))
    do = ImageDraw.Draw(orm)
    for key, (cx, cy, _) in CELLS.items():
        o, r, m = ORM[key]
        box = (cx * CELL, cy * CELL, (cx + 1) * CELL - 1, (cy + 1) * CELL - 1)
        do.rectangle(box, fill=(int(o * 255), int(r * 255), int(m * 255)))
    orm.save(os.path.join(out_dir, name + '_orm.png'))


def write_glb(out_dir, name, pos, nrm, uv, idx, top):
    def pack(fmt, rows):
        return b''.join(struct.pack(fmt, *r) for r in rows)

    blobs = [pack('<3f', pos), pack('<3f', nrm), pack('<2f', uv), struct.pack('<%dH' % len(idx), *idx)]
    views, offset, binary = [], 0, b''
    for i, b in enumerate(blobs):
        while len(binary) % 4:
            binary += b'\0'
        offset = len(binary)
        binary += b
        view = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(b)}
        view['target'] = 34963 if i == 3 else 34962
        views.append(view)
    while len(binary) % 4:
        binary += b'\0'

    mins = [min(p[i] for p in pos) for i in range(3)]
    maxs = [max(p[i] for p in pos) for i in range(3)]
    gltf = {
        'asset': {'version': '2.0', 'generator': 'Warzone 2126 make_test_model.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [
            {'name': name, 'children': [1, 2]},
            {'name': 'LOD0', 'mesh': 0},
            {'name': 'conn_turret_1', 'translation': [0, top, 0]},
        ],
        'meshes': [{'name': name, 'primitives': [{
            'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
            'indices': 3, 'material': 0}]}],
        'materials': [{
            'name': name,
            'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicRoughnessTexture': {'index': 1}},
            'occlusionTexture': {'index': 1},
            'extras': {'wz_tcmask': name + '_tc.png'},
        }],
        'textures': [{'source': 0}, {'source': 1}],
        'images': [{'uri': name + '_basecolor.png'}, {'uri': name + '_orm.png'}],
        'buffers': [{'byteLength': len(binary)}],
        'bufferViews': views,
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3', 'min': mins, 'max': maxs},
            {'bufferView': 1, 'componentType': 5126, 'count': len(nrm), 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': len(uv), 'type': 'VEC2'},
            {'bufferView': 3, 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'},
        ],
    }
    js = json.dumps(gltf, separators=(',', ':')).encode()
    while len(js) % 4:
        js += b' '
    out = struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(binary))
    out += struct.pack('<I4s', len(js), b'JSON') + js
    out += struct.pack('<I4s', len(binary), b'BIN\0') + binary
    with open(os.path.join(out_dir, name + '.glb'), 'wb') as f:
        f.write(out)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    out_dir, name = sys.argv[1], sys.argv[2]
    length, width, height = (float(x) for x in sys.argv[3:6]) if len(sys.argv) >= 6 else (2.5, 1.05, 0.65)
    os.makedirs(out_dir, exist_ok=True)
    pos, nrm, uv, idx, top = build(length, width, height)
    write_textures(out_dir, name)
    write_glb(out_dir, name, pos, nrm, uv, idx, top)
    print('Wrote %s/%s.glb (%d vertices, %d triangles)' % (out_dir, name, len(pos), len(idx) // 3))


if __name__ == '__main__':
    main()
