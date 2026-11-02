"""Parse an OBJ and rasterize per-vertex attributes into UV (texture) space."""
import numpy as np

def load_obj(path):
    V, VT, VN, F = [], [], [], []
    with open(path, 'r', errors='ignore') as fh:
        for ln in fh:
            if ln.startswith('v '):
                p = ln.split(); V.append((float(p[1]), float(p[2]), float(p[3])))
            elif ln.startswith('vt '):
                p = ln.split(); VT.append((float(p[1]), float(p[2])))
            elif ln.startswith('vn '):
                p = ln.split(); VN.append((float(p[1]), float(p[2]), float(p[3])))
            elif ln.startswith('f '):
                corners = []
                for tok in ln.split()[1:]:
                    parts = (tok.split('/') + ['', ''])[:3]
                    vi = int(parts[0]) - 1
                    ti = int(parts[1]) - 1 if parts[1] else -1
                    ni = int(parts[2]) - 1 if parts[2] else -1
                    corners.append((vi, ti, ni))
                for i in range(1, len(corners) - 1):
                    F.append((corners[0], corners[i], corners[i + 1]))
    return (np.asarray(V, np.float64), np.asarray(VT, np.float64),
            np.asarray(VN, np.float64), np.asarray(F, np.int64))


def area_weighted_normals(V, F):
    tri = F[:, :, 0]
    fn = np.cross(V[tri[:, 1]] - V[tri[:, 0]], V[tri[:, 2]] - V[tri[:, 0]])
    N = np.zeros_like(V)
    for c in range(3):
        np.add.at(N, tri[:, c], fn)
    ln = np.linalg.norm(N, axis=1, keepdims=True); ln[ln == 0] = 1
    return N / ln


def uv_tangents(V, VT, F, N):
    """Per-vertex tangent from UV derivatives (Lengyel's method)."""
    tri, uvi = F[:, :, 0], F[:, :, 1]
    p0, p1, p2 = V[tri[:, 0]], V[tri[:, 1]], V[tri[:, 2]]
    w0, w1, w2 = VT[uvi[:, 0]], VT[uvi[:, 1]], VT[uvi[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    du1, dv1 = (w1 - w0)[:, 0], (w1 - w0)[:, 1]
    du2, dv2 = (w2 - w0)[:, 0], (w2 - w0)[:, 1]
    det = du1 * dv2 - du2 * dv1
    r = np.where(np.abs(det) < 1e-12, 0.0, 1.0 / np.where(det == 0, 1, det))
    sdir = (e1 * dv2[:, None] - e2 * dv1[:, None]) * r[:, None]
    T = np.zeros_like(V)
    for c in range(3):
        np.add.at(T, tri[:, c], sdir)
    # Gram-Schmidt against the vertex normal
    T = T - N * np.sum(N * T, axis=1, keepdims=True)
    ln = np.linalg.norm(T, axis=1, keepdims=True); ln[ln == 0] = 1
    return T / ln


def rasterize(VT, F, vert_attrs, size, v_flip=True):
    """Rasterize triangles in UV space, interpolating per-vertex attributes.

    vert_attrs: dict name -> (V,K) array indexed by the *vertex* index.
    Returns dict name -> (size,size,K) plus 'mask' (size,size) bool.
    """
    uv = VT.copy()
    if v_flip:
        uv[:, 1] = 1.0 - uv[:, 1]
    px = uv * np.array([size, size], np.float64)

    out = {k: np.zeros((size, size, v.shape[1]), np.float64) for k, v in vert_attrs.items()}
    mask = np.zeros((size, size), bool)

    tri, uvi = F[:, :, 0], F[:, :, 1]
    a, b, c = px[uvi[:, 0]], px[uvi[:, 1]], px[uvi[:, 2]]
    area = (c[:, 0] - a[:, 0]) * (b[:, 1] - a[:, 1]) - (c[:, 1] - a[:, 1]) * (b[:, 0] - a[:, 0])

    for t in range(len(F)):
        if abs(area[t]) < 1e-12:
            continue
        p0, p1, p2 = a[t], b[t], c[t]
        x0 = max(int(np.floor(min(p0[0], p1[0], p2[0]))), 0)
        x1 = min(int(np.ceil(max(p0[0], p1[0], p2[0]))) + 1, size)
        y0 = max(int(np.floor(min(p0[1], p1[1], p2[1]))), 0)
        y1 = min(int(np.ceil(max(p0[1], p1[1], p2[1]))) + 1, size)
        if x1 <= x0 or y1 <= y0:
            continue
        ys, xs = np.mgrid[y0:y1, x0:x1]
        pxs = xs + 0.5; pys = ys + 0.5
        inv = 1.0 / area[t]
        # EdgeFunction(a, b, p) = (p.x-a.x)*(b.y-a.y) - (p.y-a.y)*(b.x-a.x),
        # matching Mu's shader so the weights stay consistent with `area`.
        w0 = ((pxs - p1[0]) * (p2[1] - p1[1]) - (pys - p1[1]) * (p2[0] - p1[0])) * inv
        w1 = ((pxs - p2[0]) * (p0[1] - p2[1]) - (pys - p2[1]) * (p0[0] - p2[0])) * inv
        w2 = 1.0 - w0 - w1
        inside = (w0 >= -1e-9) & (w1 >= -1e-9) & (w2 >= -1e-9)
        if not inside.any():
            continue
        yy, xx = ys[inside], xs[inside]
        b0, b1, b2 = w0[inside], w1[inside], w2[inside]
        i0, i1, i2 = tri[t]
        for k, va in vert_attrs.items():
            out[k][yy, xx] = (va[i0] * b0[:, None] + va[i1] * b1[:, None] + va[i2] * b2[:, None])
        mask[yy, xx] = True
    out['mask'] = mask
    return out
