"""Procedural, higher-detail replacements for the low-poly parts of the AC-130 cabin.

All geometry is in the FlightGear model frame: meters, y up, x along the
fuselage, z across it. Every emitter returns {texture: [vertex, ...]} where a
vertex is (px, py, pz, nx, ny, nz, u, v) and each consecutive three form a
triangle. Texture names are files in data/ac130_cabin, or "@color:RRGGBB" /
"@emit:RRGGBB" for solid lit / unlit surfaces.
"""
import math

INSULATION, FLOOR, FRAME, FABRIC = 'insulation.png', 'floor.png', 'frame.png', 'fabric.png'
LIGHT = '@emit:c8d2e6'
RAIL = '@color:3a3d40'


def _norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


class mesh:
    def __init__(self):
        self.groups = {}

    def tri(self, texture, a, b, c):
        self.groups.setdefault(texture, []).extend((a, b, c))

    def quad(self, texture, a, b, c, d):
        self.tri(texture, a, b, c)
        self.tri(texture, a, c, d)

    def merge(self, other):
        for k, v in other.groups.items():
            self.groups.setdefault(k, []).extend(v)


def _box_uv(p, n, scale):
    ax = max(range(3), key=lambda i: abs(n[i]))
    a, b = [(1, 2), (0, 2), (0, 1)][ax]
    return p[a] / scale, -p[b] / scale if ax != 1 else p[b] / scale


def _transform(points_normals, origin, pitch=0.0):
    """Rotate about +x by pitch (radians), then translate."""
    c, s = math.cos(pitch), math.sin(pitch)
    out = []
    for p, n in points_normals:
        rp = (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)
        rn = (n[0], n[1] * c - n[2] * s, n[1] * s + n[2] * c)
        out.append(((rp[0] + origin[0], rp[1] + origin[1], rp[2] + origin[2]), rn))
    return out


def rounded_box(m, texture, center, half, exponent=0.22, segments=28, pitch=0.0, uv_scale=0.3):
    """Superellipsoid: a box whose edges are smoothly rounded (lower exponent = sharper)."""
    a, b, c = half
    sp = lambda w, e: math.copysign(abs(w) ** e, w)
    rows = []
    for i in range(segments + 1):
        u = -math.pi / 2 + math.pi * i / segments
        row = []
        for j in range(segments * 2 + 1):
            v = -math.pi + 2 * math.pi * j / (segments * 2)
            x = a * sp(math.cos(u), exponent) * sp(math.cos(v), exponent)
            y = b * sp(math.sin(u), exponent)
            z = c * sp(math.cos(u), exponent) * sp(math.sin(v), exponent)
            k = 2 / exponent - 1
            n = _norm((sp(x / a, k) / a, sp(y / b, k) / b, sp(z / c, k) / c))
            row.append(((x, y, z), n))
        rows.append(_transform(row, center, pitch))
    for i in range(segments):
        for j in range(segments * 2):
            corners = [rows[i][j], rows[i + 1][j], rows[i + 1][j + 1], rows[i][j + 1]]
            verts = [(*p, *n, *_box_uv(p, n, uv_scale)) for p, n in corners]
            m.quad(texture, *verts)


def cylinder(m, texture, base, radius, height, segments=32, caps=True):
    x0, y0, z0 = base
    ring = [(math.cos(2 * math.pi * k / segments), math.sin(2 * math.pi * k / segments)) for k in range(segments + 1)]
    for k in range(segments):
        (c0, s0), (c1, s1) = ring[k], ring[k + 1]
        u0, u1 = k / segments * 2 * math.pi * radius / 0.2, (k + 1) / segments * 2 * math.pi * radius / 0.2
        m.quad(texture,
               (x0 + radius * c0, y0, z0 + radius * s0, c0, 0, s0, u0, 0),
               (x0 + radius * c0, y0 + height, z0 + radius * s0, c0, 0, s0, u0, height / 0.2),
               (x0 + radius * c1, y0 + height, z0 + radius * s1, c1, 0, s1, u1, height / 0.2),
               (x0 + radius * c1, y0, z0 + radius * s1, c1, 0, s1, u1, 0))
        if caps:
            for y, ny in ((y0 + height, 1), (y0, -1)):
                m.tri(texture, (x0, y, z0, 0, ny, 0, 0, 0),
                      (x0 + radius * c0, y, z0 + radius * s0, 0, ny, 0, c0, s0),
                      (x0 + radius * c1, y, z0 + radius * s1, 0, ny, 0, c1, s1))


def box(m, texture, lo, hi, uv_scale=0.4):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    faces = [((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
             ((-1, 0, 0), [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)]),
             ((0, 1, 0), [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]),
             ((0, -1, 0), [(x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)]),
             ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
             ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)])]
    for n, ps in faces:
        m.quad(texture, *[(*p, *n, *_box_uv(p, n, uv_scale)) for p in ps])


def seat(center_x, center_z, floor_y):
    """Fire-control officer's swivel seat facing +z."""
    m = mesh()
    cylinder(m, FRAME, (center_x, floor_y, center_z), 0.23, 0.025)
    for k in range(5):  # five-star base spokes
        a = 2 * math.pi * k / 5
        dx, dz = math.cos(a), math.sin(a)
        rounded_box(m, FRAME, (center_x + dx * 0.16, floor_y + 0.04, center_z + dz * 0.16),
                    (0.03, 0.02, 0.03), exponent=0.5, segments=10)
    cylinder(m, FRAME, (center_x, floor_y + 0.025, center_z), 0.045, 0.40)
    cylinder(m, '@color:2a2c2e', (center_x, floor_y + 0.30, center_z), 0.055, 0.12)
    rounded_box(m, FRAME, (center_x, floor_y + 0.44, center_z), (0.23, 0.02, 0.22), exponent=0.3, segments=16)
    rounded_box(m, FABRIC, (center_x, floor_y + 0.51, center_z + 0.01), (0.25, 0.055, 0.245))
    back = (center_x, floor_y + 0.86, center_z - 0.25)
    rounded_box(m, FABRIC, back, (0.24, 0.30, 0.055), pitch=-0.20)
    rounded_box(m, FRAME, (back[0], back[1] - 0.02, back[2] - 0.05), (0.22, 0.27, 0.015), exponent=0.3, segments=16, pitch=-0.20)
    rounded_box(m, FABRIC, (center_x, floor_y + 1.22, center_z - 0.33), (0.15, 0.075, 0.05), pitch=-0.20)
    for side in (-1, 1):
        x = center_x + side * 0.29
        rounded_box(m, FABRIC, (x, floor_y + 0.71, center_z - 0.02), (0.035, 0.028, 0.17), exponent=0.35)
        rounded_box(m, FRAME, (x, floor_y + 0.60, center_z - 0.12), (0.012, 0.09, 0.02), exponent=0.4, segments=12)
    return m


def _shell_profile(half_width=1.62, floor_y=0.30, spring_y=1.25, top_y=3.00, arc_segments=36):
    """Left floor edge -> left wall -> crown -> right wall -> right floor edge, as (z, y)."""
    right = [(half_width - 0.10, floor_y), (half_width - 0.03, floor_y + 0.22), (half_width, floor_y + 0.55), (half_width, spring_y)]
    rise = top_y - spring_y
    for k in range(1, arc_segments + 1):
        a = math.pi / 2 * k / arc_segments
        right.append((half_width * math.cos(a), spring_y + rise * math.sin(a)))
    # Left floor edge up to the crown, then back down the right side.
    left = [(-z, y) for z, y in right]
    return left + list(reversed(right))[1:]


def fuselage(x0, x1, half_width=1.62, floor_y=0.30, top_y=3.00):
    m = mesh()
    profile = _shell_profile(half_width, floor_y, top_y=top_y)
    center = (0.0, floor_y + 1.3)
    seg_n = []
    for (za, ya), (zb, yb) in zip(profile, profile[1:]):
        n = _norm((0.0, -(zb - za), (yb - ya)))  # perpendicular in (y, z); pick the inward one below
        mid = ((za + zb) / 2, (ya + yb) / 2)
        if (center[1] - mid[1]) * n[1] + (center[0] - mid[0]) * n[2] < 0:
            n = (0.0, -n[1], -n[2])
        seg_n.append(n)
    vert_n = [seg_n[0]] + [_norm(tuple(a + b for a, b in zip(seg_n[i - 1], seg_n[i]))) for i in range(1, len(seg_n))] + [seg_n[-1]]
    arc = [0.0]
    for (za, ya), (zb, yb) in zip(profile, profile[1:]):
        arc.append(arc[-1] + math.hypot(zb - za, yb - ya))
    tile = 1.2
    step = 0.5
    xs = [x0 + (x1 - x0) * k / max(1, round((x1 - x0) / step)) for k in range(round((x1 - x0) / step) + 1)]
    for xa, xb in zip(xs, xs[1:]):
        for i in range(len(profile) - 1):
            (za, ya), (zb, yb) = profile[i], profile[i + 1]
            na, nb = vert_n[i], vert_n[i + 1]
            m.quad(INSULATION,
                   (xa, ya, za, *na, arc[i] / tile, xa / tile), (xb, ya, za, *na, arc[i] / tile, xb / tile),
                   (xb, yb, zb, *nb, arc[i + 1] / tile, xb / tile), (xa, yb, zb, *nb, arc[i + 1] / tile, xa / tile))
    # Floor and its seat/cargo tie-down rails.
    fz = profile[-1][0]
    m.quad(FLOOR, (x0, floor_y, -fz, 0, 1, 0, x0 / 1.8, -fz / 1.8), (x0, floor_y, fz, 0, 1, 0, x0 / 1.8, fz / 1.8),
           (x1, floor_y, fz, 0, 1, 0, x1 / 1.8, fz / 1.8), (x1, floor_y, -fz, 0, 1, 0, x1 / 1.8, -fz / 1.8))
    for z in (-1.15, -0.40, 0.40, 1.15):
        box(m, RAIL, (x0, floor_y, z - 0.025), (x1, floor_y + 0.012, z + 0.025))
    # Frames (every 20 in) and longitudinal stringers over the walls and crown.
    depth, width = 0.075, 0.045
    def inset(i, d):
        z, y = profile[i]; n = vert_n[i]
        return (y + n[1] * d, z + n[2] * d)
    x = x0 + 0.25
    while x < x1:
        for i in range(len(profile) - 1):
            (ya, za), (yb, zb) = inset(i, 0.003), inset(i + 1, 0.003)
            (yc, zc), (yd, zd) = inset(i, depth), inset(i + 1, depth)
            n = seg_n[i]
            for xf, nx in ((x - width / 2, -1), (x + width / 2, 1)):
                m.quad(FRAME, (xf, ya, za, nx, 0, 0, za / 0.3, ya / 0.3), (xf, yb, zb, nx, 0, 0, zb / 0.3, yb / 0.3),
                       (xf, yd, zd, nx, 0, 0, zd / 0.3, yd / 0.3), (xf, yc, zc, nx, 0, 0, zc / 0.3, yc / 0.3))
            m.quad(FRAME, (x - width / 2, yc, zc, *n, x / 0.3, arc[i] / 0.3), (x + width / 2, yc, zc, *n, x / 0.3 + 0.15, arc[i] / 0.3),
                   (x + width / 2, yd, zd, *n, x / 0.3 + 0.15, arc[i + 1] / 0.3), (x - width / 2, yd, zd, *n, x / 0.3, arc[i + 1] / 0.3))
        x += 0.508
    for i in range(4, len(profile) - 4, 6):
        (ya, za), (yb, zb) = inset(i, 0.003), inset(i, 0.035)
        t = _norm((0.0, profile[i + 1][1] - profile[i - 1][1], profile[i + 1][0] - profile[i - 1][0]))
        h = 0.015
        a = (ya - t[1] * h, za - t[2] * h); b = (ya + t[1] * h, za + t[2] * h)
        c = (yb + t[1] * h, zb + t[2] * h); d = (yb - t[1] * h, zb - t[2] * h)
        n = vert_n[i]
        for (p, q), nn in (((a, d), (0, -t[1], -t[2])), ((d, c), n), ((c, b), (0, t[1], t[2]))):
            m.quad(FRAME, (x0, p[0], p[1], *nn, x0 / 0.3, 0), (x1, p[0], p[1], *nn, x1 / 0.3, 0),
                   (x1, q[0], q[1], *nn, x1 / 0.3, 0.1), (x0, q[0], q[1], *nn, x0 / 0.3, 0.1))
    # Overhead light strips under the crown.
    for z in (-0.55, 0.55):
        y = min(py for pz, py in profile if abs(pz) <= abs(z) + 0.08 and py > floor_y + 1.5) - 0.06
        box(m, '@color:202326', (x0 + 0.4, y - 0.02, z - 0.08), (x1 - 0.4, y + 0.04, z + 0.08))
        box(m, LIGHT, (x0 + 0.5, y - 0.03, z - 0.05), (x1 - 0.5, y - 0.019, z + 0.05))
    # End bulkheads closing the compartment.
    for xe, nx in ((x0, 1), (x1, -1)):
        hub = (xe, floor_y + 1.3, 0.0)
        for (za, ya), (zb, yb) in zip(profile, profile[1:]):
            m.tri(INSULATION, (xe, *hub[1:], nx, 0, 0, 0, hub[1] / tile), (xe, ya, za, nx, 0, 0, za / tile, ya / tile),
                  (xe, yb, zb, nx, 0, 0, zb / tile, yb / tile))
        m.tri(INSULATION, (xe, *hub[1:], nx, 0, 0, 0, hub[1] / tile), (xe, profile[-1][1], profile[-1][0], nx, 0, 0, 0, 0),
              (xe, profile[0][1], profile[0][0], nx, 0, 0, 0, 0))
    return m
