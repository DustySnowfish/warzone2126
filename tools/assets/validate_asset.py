#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Checks a Warzone 2126 model against the art specification (art/art-spec-v1.md) and prints a
pass / warning / fail report.

Usage:
    validate_asset.py MODEL.glb [MODEL2.glb ...] [--kind KIND] [--json]
    validate_asset.py ASSETS_FOLDER             (checks every .glb under it)

KIND is one of: body, propulsion, turret, structure, module, wreck, feature. It is worked out
from the folder (assets/units/bodies/... and so on, spec section 9) and the file name when not
given. Exit code 0 when nothing failed, 1 when any check failed.

Needs only Python 3.8 or later: no extra packages. Run it on Windows with
    py validate_asset.py C:\\path\\to\\cobra.glb
"""

import json
import math
import os
import re
import struct
import sys
import zlib

# ---------------------------------------------------------------------------------------------
# The specification, as data (section numbers refer to art-spec-v1.md)

METRES_TO_UNITS = 32.0                     # section 2
HARD_TRIANGLE_LIMIT = 21844                # section 4

# Section 2: reference sizes (length, width, height in metres); units may be up to 1.5x
BODY_SIZES = {
    'viper': (1.9, 0.85, 0.4),
    'cobra': (2.5, 1.05, 0.65),
    'python': (3.2, 1.2, 0.65),
}
# Structure footprints in tiles (4 m per tile)
STRUCTURE_TILES = {
    'factory': 3, 'factory_module': 3, 'cyborg_factory': 2, 'research': 2, 'research_facility': 2,
    'research_module': 2, 'command_centre': 2, 'hq': 2, 'power_generator': 2, 'power_module': 2,
    'oil_derrick': 1, 'derrick': 1, 'repair_facility': 1, 'hmg_hardpoint': 1, 'hmg_bunker': 1,
    'bunker': 1, 'hardpoint': 1, 'sensor_tower': 1,
}

# Section 4: LOD0 triangle budgets (low, high) and which LODs are required
BUDGETS = {
    'body': {'viper': 2500, 'cobra': 3500, 'python': 4500, None: 4500},
    'propulsion': 3500,
    'turret': 2000,
    'structure': {1: 6000, 2: 12000, 3: 20000, None: 20000},
    'module': 6000,
    'wreck': None,      # 30-50% of the structure's LOD0, checked loosely
    'feature': 3000,
}
LODS_REQUIRED = {
    'body': ['LOD1', 'LOD2'], 'propulsion': ['LOD1', 'LOD2'], 'turret': ['LOD1'],
    'structure': ['LOD1', 'LOD2'], 'module': ['LOD1', 'LOD2'], 'wreck': [], 'feature': [],
}

# Section 5: texture sizes per kind
TEXTURE_SIZES = {
    'body': (1024, 2048), 'propulsion': (1024, 2048), 'turret': (1024, 1024),
    'structure': (2048, 2048), 'module': (1024, 2048), 'wreck': (1024, 2048), 'feature': (512, 1024),
}

# Section 6: required nodes per kind
REQUIRED_NODES = {
    'body': ['conn_turret_1', 'conn_propulsion'],
    'turret': ['yaw', 'pitch', 'conn_muzzle_1'],
    'propulsion': [], 'structure': [], 'module': [], 'wreck': [], 'feature': [],
}
WEAPON_STRUCTURES = ('hardpoint', 'bunker', 'sensor_tower')

KIND_FOLDERS = {
    'bodies': 'body', 'propulsion': 'propulsion', 'turrets': 'turret',
    'structures': 'structure', 'features': 'feature',
}
UTILITY_TURRETS = ('construct', 'sensor')

# ---------------------------------------------------------------------------------------------
# Report


class Report:
    def __init__(self, path):
        self.path = path
        self.items = []  # (level, check, message)

    def ok(self, check, message):
        self.items.append(('PASS', check, message))

    def warn(self, check, message):
        self.items.append(('WARN', check, message))

    def fail(self, check, message):
        self.items.append(('FAIL', check, message))

    @property
    def failed(self):
        return any(level == 'FAIL' for level, _, _ in self.items)

    def text(self):
        lines = [self.path]
        for level, check, message in self.items:
            lines.append('  %-4s  %-12s %s' % (level, check, message))
        fails = sum(1 for i in self.items if i[0] == 'FAIL')
        warns = sum(1 for i in self.items if i[0] == 'WARN')
        lines.append('  => %s (%d failed, %d warnings)' % ('FAILED' if fails else 'PASSED', fails, warns))
        return '\n'.join(lines)

    def as_dict(self):
        return {'file': self.path, 'passed': not self.failed,
                'checks': [{'level': l, 'check': c, 'message': m} for l, c, m in self.items]}

# ---------------------------------------------------------------------------------------------
# glTF and PNG reading (standard library only)


def read_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 20 or data[0:4] != b'glTF':
        raise ValueError('not a binary glTF (.glb) file')
    version, length = struct.unpack_from('<II', data, 4)
    if version != 2:
        raise ValueError('glTF version %d, expected 2' % version)
    offset = 12
    doc, binary = None, b''
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from('<II', data, offset)
        chunk = data[offset + 8: offset + 8 + chunk_len]
        if chunk_type == 0x4E4F534A:
            doc = json.loads(chunk.decode('utf-8'))
        elif chunk_type == 0x004E4942:
            binary = chunk
        offset += 8 + chunk_len
    if doc is None:
        raise ValueError('no JSON chunk')
    return doc, binary


def png_info(path):
    """(width, height, colour type, bit depth) from the PNG header."""
    with open(path, 'rb') as f:
        head = f.read(33)
    if head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        raise ValueError('not a PNG file')
    width, height, depth, colour = struct.unpack('>IIBB', head[16:26])
    return width, height, colour, depth


PNG_COLOUR = {0: 'greyscale', 2: 'RGB', 3: 'palette', 4: 'greyscale+alpha', 6: 'RGBA'}

# ---------------------------------------------------------------------------------------------
# Geometry helpers


def mat_mul(a, b):
    """Product of two column-major 4x4 matrices."""
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(node):
    """Column-major 4x4 matrix of a node."""
    if 'matrix' in node:
        return list(node['matrix'])
    t = node.get('translation', [0, 0, 0])
    q = node.get('rotation', [0, 0, 0, 1])
    s = node.get('scale', [1, 1, 1])
    x, y, z, w = q
    r = [
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
        0, 0, 0, 1,
    ]
    for col in range(3):
        for row in range(3):
            r[col * 4 + row] *= s[col]
    r[12], r[13], r[14] = t
    return r


def transform_point(m, p):
    x, y, z = p
    return (m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14])


def is_identity_ish(node):
    return ('matrix' not in node and node.get('scale', [1, 1, 1]) == [1, 1, 1]
            and node.get('rotation', [0, 0, 0, 1]) == [0, 0, 0, 1])

# ---------------------------------------------------------------------------------------------
# The checks


def guess_kind(path):
    parts = [p.lower() for p in re.split(r'[\\/]+', os.path.abspath(path))]
    name = os.path.splitext(os.path.basename(path))[0].lower()
    if name.endswith('_wreck'):
        return 'wreck'
    if name.endswith('_module'):
        return 'module'
    for folder, kind in KIND_FOLDERS.items():
        if folder in parts:
            return kind
    if name in BODY_SIZES:
        return 'body'
    if any(name.startswith(s) for s in STRUCTURE_TILES):
        return 'structure'
    return None


def walk_nodes(doc):
    """Yields (node index, node, world matrix, path of names) for every node in the default scene."""
    nodes = doc.get('nodes', [])
    scene = doc.get('scenes', [{}])[doc.get('scene', 0)] if doc.get('scenes') else {'nodes': list(range(len(nodes)))}
    stack = [(i, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], []) for i in scene.get('nodes', [])]
    while stack:
        index, parent, names = stack.pop()
        node = nodes[index]
        world = mat_mul(parent, node_matrix(node))
        path = names + [node.get('name', '')]
        yield index, node, world, path
        for child in node.get('children', []):
            stack.append((child, world, path))


def mesh_triangles(doc, mesh_index):
    total = 0
    for prim in doc['meshes'][mesh_index].get('primitives', []):
        mode = prim.get('mode', 4)
        if 'indices' in prim:
            count = doc['accessors'][prim['indices']]['count']
        else:
            count = doc['accessors'][prim['attributes']['POSITION']]['count']
        if mode == 4:
            total += count // 3
        elif mode in (5, 6):
            total += max(0, count - 2)
    return total


def check_asset(path, kind=None):
    rep = Report(path)
    name = os.path.splitext(os.path.basename(path))[0]
    folder = os.path.dirname(os.path.abspath(path))
    kind = kind or guess_kind(path)

    # 1. Naming (section 9)
    if re.fullmatch(r'[a-z0-9_]+', name):
        rep.ok('naming', 'file name "%s" is lower-case with underscores' % name)
    else:
        rep.fail('naming', 'file name "%s" must be lower-case letters, digits and underscores only' % name)
    if kind is None:
        rep.warn('kind', 'could not tell the asset type from the folder; checks for type-specific rules are skipped (use --kind)')
    else:
        rep.ok('kind', kind)

    try:
        doc, _ = read_glb(path)
    except Exception as e:  # noqa: BLE001
        rep.fail('format', 'cannot read: %s' % e)
        return rep
    rep.ok('format', 'glTF 2.0 binary')

    nodes = list(walk_nodes(doc))
    node_names = {n.get('name', '') for _, n, _, _ in nodes}

    # LOD nodes (section 4)
    lod_roots = {n.get('name'): i for i, n, _, _ in nodes if n.get('name') in ('LOD0', 'LOD1', 'LOD2')}

    def in_lod(path_names, lod):
        return lod in path_names

    # Triangles and bounding box (LOD0, or everything when there are no LOD nodes)
    tris = {'LOD0': 0, 'LOD1': 0, 'LOD2': 0}
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    has_uv = True
    has_tangents = True
    for _, node, world, names in nodes:
        if 'mesh' not in node:
            continue
        lod = next((l for l in ('LOD1', 'LOD2') if in_lod(names, l)), 'LOD0')
        tris[lod] += mesh_triangles(doc, node['mesh'])
        if lod != 'LOD0':
            continue
        for prim in doc['meshes'][node['mesh']].get('primitives', []):
            attrs = prim.get('attributes', {})
            has_uv = has_uv and 'TEXCOORD_0' in attrs
            has_tangents = has_tangents and 'TANGENT' in attrs
            acc = doc['accessors'][attrs['POSITION']]
            amin, amax = acc.get('min'), acc.get('max')
            if amin is None or amax is None:
                continue
            for cx in (amin[0], amax[0]):
                for cy in (amin[1], amax[1]):
                    for cz in (amin[2], amax[2]):
                        p = transform_point(world, (cx, cy, cz))
                        for k in range(3):
                            lo[k] = min(lo[k], p[k])
                            hi[k] = max(hi[k], p[k])

    if tris['LOD0'] == 0:
        rep.fail('geometry', 'no triangles found')
        return rep
    rep.ok('uv', 'texture coordinates present') if has_uv else rep.fail('uv', 'a mesh has no texture coordinates (TEXCOORD_0)')
    if has_tangents:
        rep.ok('tangents', 'tangents exported')
    else:
        rep.warn('tangents', 'no tangents exported (Blender: tick "Tangents"); normal maps may shade wrongly')

    # 3. Triangle budgets (section 4)
    if tris['LOD0'] > HARD_TRIANGLE_LIMIT:
        rep.fail('triangles', 'LOD0 has %d triangles, over the engine limit of %d' % (tris['LOD0'], HARD_TRIANGLE_LIMIT))
    budget = BUDGETS.get(kind)
    tiles = None
    if kind in ('structure', 'module', 'wreck'):
        base = name.replace('_wreck', '')
        tiles = next((t for s, t in STRUCTURE_TILES.items() if base == s or base.startswith(s)), None)
    if isinstance(budget, dict):
        budget = budget.get(name if kind == 'body' else tiles, budget[None])
    if budget:
        if tris['LOD0'] <= budget:
            rep.ok('triangles', 'LOD0 %d triangles (budget %d)' % (tris['LOD0'], budget))
        else:
            rep.fail('triangles', 'LOD0 %d triangles, over the budget of %d' % (tris['LOD0'], budget))
    else:
        rep.ok('triangles', 'LOD0 %d triangles' % tris['LOD0'])
    for lod in LODS_REQUIRED.get(kind, []):
        if lod not in lod_roots:
            rep.fail('lod', '%s missing (child node named %s under the root)' % (lod, lod))
        else:
            expected = 0.5 if lod == 'LOD1' else 0.2
            ratio = tris[lod] / max(tris['LOD0'], 1)
            if ratio > expected * 1.5:
                rep.warn('lod', '%s has %d triangles, %.0f%% of LOD0 (aim for about %.0f%%)' % (lod, tris[lod], ratio * 100, expected * 100))
            else:
                rep.ok('lod', '%s %d triangles (%.0f%% of LOD0)' % (lod, tris[lod], ratio * 100))

    # 2. Scale, origin and facing (section 2). glTF: +Y up, front +Z.
    size_m = [(hi[k] - lo[k]) for k in range(3)]  # metres (glTF units are metres)
    length, height, width = size_m[2], size_m[1], size_m[0]
    rep.ok('size', 'length %.2f m, width %.2f m, height %.2f m (%.0f x %.0f x %.0f game units)' % (
        length, width, height, length * METRES_TO_UNITS, width * METRES_TO_UNITS, height * METRES_TO_UNITS))
    if max(size_m) > 100:
        rep.fail('scale', 'model is %.0f m across: exported in centimetres or with an unapplied scale?' % max(size_m))
    elif max(size_m) < 0.05:
        rep.fail('scale', 'model is under 5 cm across: unapplied scale?')
    if kind == 'body' and name in BODY_SIZES:
        rl, rw, rh = BODY_SIZES[name]
        if not (0.8 * rl <= length <= 1.5 * rl):
            rep.fail('scale', 'body length %.2f m; reference %.2f m (0.8x to 1.5x allowed)' % (length, rl))
        else:
            rep.ok('scale', 'body length within 0.8x to 1.5x of the %.2f m reference' % rl)
        if width > length:
            rep.fail('facing', 'body is wider (X) than long (Z): front should face Blender -Y (glTF +Z)')
    if tiles:
        foot = tiles * 4.0
        if max(length, width) > foot + 0.05:
            rep.fail('footprint', '%.2f x %.2f m is outside the %d x %d tile footprint (%.0f m)' % (length, width, tiles, tiles, foot))
        else:
            rep.ok('footprint', 'inside the %d x %d tile footprint' % (tiles, tiles))
    ground = lo[1]
    if kind in ('body', 'propulsion', 'structure', 'module', 'wreck', 'feature'):
        if abs(ground) > 0.15:
            rep.fail('origin', 'lowest point is %.2f m from the origin; the origin must be at ground contact' % ground)
        else:
            rep.ok('origin', 'origin at ground contact')
        centre = ((lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2)
        if max(abs(centre[0]), abs(centre[1])) > max(0.25, 0.15 * max(length, width)):
            rep.warn('origin', 'footprint centre is (%.2f, %.2f) m from the origin; the origin should be centred under the part' % centre)
    roots = [doc['nodes'][i] for i in (doc.get('scenes', [{}])[doc.get('scene', 0)].get('nodes', []) if doc.get('scenes') else [])]
    if any(not is_identity_ish(r) for r in roots):
        rep.warn('transforms', 'a root node has rotation or scale: apply all transforms before export')

    # 6. Connectors (section 6)
    required = list(REQUIRED_NODES.get(kind, []))
    if kind == 'turret' and name.startswith(UTILITY_TURRETS):
        required = ['yaw']
    if kind == 'structure' and any(s in name for s in WEAPON_STRUCTURES):
        required.append('conn_turret_1')
    if kind == 'structure' and (name.startswith('factory') or name.startswith('cyborg_factory')):
        required.append('conn_exit')
    for r in required:
        if r in node_names:
            rep.ok('connectors', '%s present' % r)
        else:
            rep.fail('connectors', '%s missing' % r)
    for prefix in ('conn_turret_', 'conn_muzzle_', 'conn_module_'):
        numbers = sorted(int(n[len(prefix):]) for n in node_names if n.startswith(prefix) and n[len(prefix):].isdigit())
        if numbers and numbers != list(range(1, len(numbers) + 1)):
            rep.fail('connectors', '%s numbering has gaps: %s (number from 1 without gaps)' % (prefix, numbers))
    if kind == 'body' and 'fx_smoke_1' not in node_names:
        rep.warn('connectors', 'fx_smoke_1 missing (section 8: smoke point for damage effects)')

    # 5. Materials and textures
    materials = doc.get('materials', [])
    if not materials:
        rep.fail('material', 'no material')
        return rep
    if len(materials) > 1:
        rep.warn('material', '%d materials; the engine uses the first one only' % len(materials))
    mat = materials[0]
    pbr = mat.get('pbrMetallicRoughness', {})
    images = doc.get('images', [])
    textures = doc.get('textures', [])

    def image_for(tex_ref):
        if not tex_ref:
            return None
        tex = textures[tex_ref['index']]
        source = tex.get('source')
        if source is None:
            return None
        return images[source]

    expected_size = TEXTURE_SIZES.get(kind)
    slots = [('base colour', image_for(pbr.get('baseColorTexture')), '_basecolor', True),
             ('ORM', image_for(pbr.get('metallicRoughnessTexture')), '_orm', True),
             ('normal', image_for(mat.get('normalTexture')), '_normal', True),
             ('emissive', image_for(mat.get('emissiveTexture')), '_emissive', False)]
    occlusion = image_for(mat.get('occlusionTexture'))
    for label, img, suffix, required_map in slots:
        if img is None:
            (rep.fail if required_map else rep.ok)('textures', '%s map %s' % (label, 'missing' if required_map else 'not used (optional)'))
            continue
        if 'uri' not in img:
            rep.fail('textures', '%s texture is embedded in the .glb; export textures as separate PNG files (Images: Automatic)' % label)
            continue
        file = os.path.join(folder, img['uri'])
        check_texture(rep, label, file, suffix, expected_size, colour_kind='rgb')
    if occlusion is not None and image_for(pbr.get('metallicRoughnessTexture')) is not occlusion:
        rep.warn('textures', 'occlusion is a separate texture; pack it into the R channel of the ORM map (section 13)')

    # Team colour mask (section 5)
    tc = (mat.get('extras') or {}).get('wz_tcmask')
    if kind in ('body', 'propulsion', 'turret', 'structure', 'module'):
        if not tc:
            rep.fail('team colour', 'no wz_tcmask custom property on the material (Blender: material custom property, export Custom Properties)')
        else:
            check_texture(rep, 'team colour mask', os.path.join(folder, tc), '_tc', expected_size, colour_kind='grey')

    # 8. Wreck for structures
    if kind == 'structure':
        wreck = os.path.join(folder, name + '_wreck.glb')
        if os.path.exists(wreck):
            rep.ok('wreck', os.path.basename(wreck) + ' present')
        else:
            rep.warn('wreck', '%s not found next to the model (section 8)' % os.path.basename(wreck))

    return rep


def check_texture(rep, label, file, suffix, expected_size, colour_kind):
    base = os.path.basename(file)
    if not os.path.exists(file):
        rep.fail('textures', '%s file %s not found next to the model' % (label, base))
        return
    if not base.lower().endswith('.png'):
        rep.fail('textures', '%s file %s is not a PNG' % (label, base))
        return
    if not os.path.splitext(base)[0].endswith(suffix):
        rep.warn('textures', '%s file %s should end in %s (section 9)' % (label, base, suffix))
    try:
        w, h, colour, depth = png_info(file)
    except Exception as e:  # noqa: BLE001
        rep.fail('textures', '%s file %s: %s' % (label, base, e))
        return
    problems = []
    if w & (w - 1) or h & (h - 1):
        problems.append('%dx%d is not a power of two' % (w, h))
    if expected_size and (max(w, h) > expected_size[1] or max(w, h) < expected_size[0] // 2):
        problems.append('%dx%d; expected %d to %d for this asset type' % (w, h, expected_size[0], expected_size[1]))
    if colour_kind == 'grey' and colour not in (0, 4):
        rep.warn('team colour', '%s is %s; a greyscale PNG is expected (works, but larger)' % (base, PNG_COLOUR.get(colour, colour)))
    if problems:
        rep.fail('textures', '%s %s: %s' % (label, base, '; '.join(problems)))
    else:
        rep.ok('textures', '%s %s %dx%d %s' % (label, base, w, h, PNG_COLOUR.get(colour, colour)))


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    kind = None
    as_json = '--json' in argv
    for a in argv:
        if a.startswith('--kind='):
            kind = a.split('=', 1)[1]
    if '--kind' in argv:
        kind = argv[argv.index('--kind') + 1]
        args.remove(kind)
    if not args:
        print(__doc__)
        return 2
    files = []
    for a in args:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                files += [os.path.join(root, n) for n in sorted(names) if n.lower().endswith('.glb')]
        else:
            files.append(a)
    reports = [check_asset(f, kind) for f in files]
    if as_json:
        print(json.dumps([r.as_dict() for r in reports], indent=2))
    else:
        print('\n\n'.join(r.text() for r in reports))
        if len(reports) > 1:
            print('\n%d of %d assets passed' % (sum(not r.failed for r in reports), len(reports)))
    return 1 if any(r.failed for r in reports) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
