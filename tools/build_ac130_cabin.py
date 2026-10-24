"""Build the VR AC-130 fire-control cabin asset from the FlightGear AC-130 model.

Source: "Lockheed AC-130 Hercules" by Emmanuel Baranger (helijah), weapons and
armament station by Jack Mermod; GPL (see data/ac130_cabin/Read-Me.txt).

usage: build_ac130_cabin.py <ac130.obj> <texture dir> <output dir> <material tile dir>

The crew figures, all seats and the faceted fuselage walls are dropped; the
fuselage shell and the officer's seat are rebuilt by ac130_cabin_procedural.

Writes <output>/cabin.bin and the referenced textures. Layout (little-endian):
  char[4] "OAC1"
  float[3] eye        seated eye point (model meters, y up)
  float[3] forward    horizontal unit direction from the eye toward the main screen
  u32 vertex_count, u32 index_count, u32 group_count
  group_count x { char[32] texture (empty = untextured, "@screen" = game frame), u32 first, u32 count }
  vertex_count x { float position[3], normal[3], uv[2] }
  index_count x u32
"""
import math, os, shutil, struct, sys

obj_path, texture_dir, out_dir, tile_dir = sys.argv[1:5]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac130_cabin_procedural as procedural
os.makedirs(out_dir, exist_ok=True)

# The battle management center sits between these fuselage stations.
STATION_X = (-7.4, -0.6)
WALLS = {'Battle'}
HALF_WIDTH = 1.62  # Procedural shell; anything outside it is dropped.
# Station 004 (armament panel above it) is the player's seat; its crew figure is removed.
SEAT, SCREEN, CREW = 'chair.004_chair.mesh.004', 'screen.004_screen.mesh.004', 'avantbras.001_avantbras.mesh.001'
SKIP_PREFIX = ('vitre', 'hublot')
SOURCE_ASPECT = 16 / 9

V, VT, VN = [], [], []
objects = {}; order = []; cur = None; mat = None
materials = {}; current_material = None
mtl_path = os.path.splitext(obj_path)[0] + '.mtl'
for line in open(mtl_path, encoding='utf-8', errors='replace'):
    p = line.split()
    if not p: continue
    if p[0] == 'newmtl': current_material = p[1]; materials[current_material] = ''
    elif p[0] == 'map_Kd' and current_material: materials[current_material] = os.path.basename(p[-1])
for line in open(obj_path, encoding='utf-8', errors='replace'):
    p = line.split()
    if not p: continue
    if p[0] == 'v': V.append(tuple(map(float, p[1:4])))
    elif p[0] == 'vt': VT.append(tuple(map(float, p[1:3])))
    elif p[0] == 'vn': VN.append(tuple(map(float, p[1:4])))
    elif p[0] == 'o': cur = line[2:].strip(); objects[cur] = []; order.append(cur)
    elif p[0] == 'usemtl': mat = p[1]
    elif p[0] == 'f' and cur:
        corners = []
        for tok in p[1:]:
            a = tok.split('/')
            idx = lambda s, n: (int(s) - 1 if int(s) > 0 else n + int(s)) if s else None
            corners.append((idx(a[0], len(V)), idx(a[1], len(VT)) if len(a) > 1 else None, idx(a[2], len(VN)) if len(a) > 2 else None))
        objects[cur].append((mat, corners))

def bbox(name):
    pts = [V[c[0]] for _, cs in objects[name] for c in cs]
    return [min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]
def center(name):
    lo, hi = bbox(name); return [(lo[k] + hi[k]) / 2 for k in range(3)]
def sub(a, b): return [a[k] - b[k] for k in range(3)]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def dot(a, b): return sum(a[k] * b[k] for k in range(3))
def norm(a):
    l = math.sqrt(dot(a, a)) or 1; return [x / l for x in a]

def included(name):
    if not objects[name] or name.startswith(SKIP_PREFIX + ('avantbras', 'chair')): return False
    if name in WALLS: return True
    lo, hi = bbox(name); c = center(name)
    # Wall-attached parts of the old faceted shell would float off the new one.
    if max(abs(lo[2]), abs(hi[2])) > HALF_WIDTH - 0.05 or hi[1] > 2.9: return False
    return STATION_X[0] <= c[0] <= STATION_X[1] and abs(c[2]) < HALF_WIDTH - 0.1 and 0 <= c[1] < 4.5

chairs = [n for n in order if n.startswith('chair')]
seat, screen = center(SEAT), center(SCREEN)
forward = norm([screen[0] - seat[0], 0, screen[2] - seat[2]])
# Seated eye: above the seat, leaning toward the console, level with the screen top.
eye = [seat[0] + forward[0] * 0.30, bbox(SCREEN)[1][1] + 0.05, seat[2] + forward[2] * 0.30]

groups = {}
vertices = []
def emit(group, verts):
    groups.setdefault(group, []).extend(range(len(vertices), len(vertices) + 3)); vertices.extend(verts)

for name in order:
    if not included(name): continue
    is_screen = name.startswith('screen')
    if is_screen:
        pts = [V[c[0]] for _, cs in objects[name] for c in cs]
        n = norm(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
        nearest = min(chairs, key=lambda ch: sum((center(ch)[k] - pts[0][k]) ** 2 for k in (0, 2)))
        if dot(n, sub(center(nearest), pts[0])) < 0: n = [-x for x in n]  # front faces the operator
        view = [-x for x in n]; up = [0, 1, 0]; right = norm(cross(view, up))
        us = [dot(p, right) for p in pts]; vs = [dot(p, up) for p in pts]
        width, height = max(us) - min(us), max(vs) - min(vs)
        # Fit the 16:9 frame inside the quad; uv outside 0..1 is the bezel (black).
        if width / height > SOURCE_ASPECT: su, sv = width / (height * SOURCE_ASPECT), 1.0
        else: su, sv = 1.0, height * SOURCE_ASPECT / width
        uv_of = lambda p: (((dot(p, right) - min(us)) / width - 0.5) * su + 0.5, ((max(vs) - dot(p, up)) / height - 0.5) * sv + 0.5)
    for material, corners in objects[name]:
        texture = '@screen' if is_screen else materials.get(material, '')
        if material in ('transparent', 'transparentExt') or texture in ('transp.png', 'fighter_pilot.png'): continue
        p0 = V[corners[0][0]]
        face_n = norm(cross(sub(V[corners[1][0]], p0), sub(V[corners[2][0]], p0)))
        def vert(c):
            pos = V[c[0]]
            nrm = VN[c[2]] if c[2] is not None else face_n
            uv = uv_of(pos) if is_screen else ((VT[c[1]][0], 1 - VT[c[1]][1]) if c[1] is not None else (0, 0))
            return (*pos, *nrm, *uv)
        for i in range(1, len(corners) - 1):
            emit(texture, [vert(corners[0]), vert(corners[i]), vert(corners[i + 1])])

# Rebuilt fuselage (closing the compartment around the stations) and the officer's seat.
extra = procedural.fuselage(STATION_X[0] - 1.2, STATION_X[1] + 1.0, HALF_WIDTH)
extra.merge(procedural.seat(seat[0], seat[2], bbox(SEAT)[0][1]))
for texture, verts in extra.groups.items():
    groups.setdefault(texture, []).extend(range(len(vertices), len(vertices) + len(verts))); vertices.extend(verts)
for tile, name in (('insulation', procedural.INSULATION), ('floor', procedural.FLOOR), ('gunmetal', procedural.FRAME), ('fabric', procedural.FABRIC)):
    from PIL import Image
    Image.open(os.path.join(tile_dir, tile + '.png')).convert('RGB').resize((1024, 1024), Image.LANCZOS).save(os.path.join(out_dir, name), optimize=True)

# Deduplicate identical vertices per group order.
procedural_textures = {procedural.INSULATION, procedural.FLOOR, procedural.FRAME, procedural.FABRIC}
order_groups = sorted(groups, key=lambda g: (g == '@screen', g))
unique = {}; out_vertices = []; out_indices = []; table = []
for g in order_groups:
    first = len(out_indices)
    for i in groups[g]:
        key = tuple(round(x, 6) for x in vertices[i])
        if key not in unique: unique[key] = len(out_vertices); out_vertices.append(vertices[i])
        out_indices.append(unique[key])
    table.append((g, first, len(out_indices) - first))

with open(os.path.join(out_dir, 'cabin.bin'), 'wb') as f:
    f.write(b'OAC1'); f.write(struct.pack('<6f', *eye, *forward))
    f.write(struct.pack('<3I', len(out_vertices), len(out_indices), len(table)))
    for g, first, count in table:
        name = g.encode()[:31]; f.write(name + b'\0' * (32 - len(name))); f.write(struct.pack('<2I', first, count))
    for v in out_vertices: f.write(struct.pack('<8f', *v))
    f.write(struct.pack('<%dI' % len(out_indices), *out_indices))
for g, _, _ in table:
    if g and not g.startswith('@') and os.path.exists(os.path.join(texture_dir, g)) and g not in procedural_textures:
        shutil.copyfile(os.path.join(texture_dir, g), os.path.join(out_dir, g))
print('eye', [round(x, 3) for x in eye], 'forward', [round(x, 3) for x in forward])
print('vertices', len(out_vertices), 'triangles', len(out_indices) // 3, 'groups', [(g, c // 3) for g, _, c in table])
