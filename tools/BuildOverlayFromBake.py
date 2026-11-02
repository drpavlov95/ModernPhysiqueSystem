"""Turn a high-poly normal bake into a Mu Dynamic NormalMap overlay.

The bake is kept as authored; the work is deciding *where* it applies. Alpha
is built from the band-passed magnitude of the bake's deviation from flat, so
the overlay only speaks up over real surface detail - ribs, creases,
cellulite - and stays transparent everywhere else, which is how the shipped
Details overlays behave.

Usage:
    py -3 tools/BuildOverlayFromBake.py --bake thin_normals.png \
        --out overlay_thin.png

Passing --mesh additionally re-encodes the bake into the object-space msn
convention Mu's overlay slot samples. That is a change of format the shipped
overlays do not make, so it stays opt-in and unproven in game.
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from objuv import area_weighted_normals, load_obj, rasterize, uv_tangents

Image.MAX_IMAGE_PIXELS = None


def decode_tangent(path):
    """Read a baked tangent-space normal map; alpha marks UV coverage."""
    rgba = np.asarray(Image.open(path).convert("RGBA"))
    coverage = rgba[..., 3] > 0
    tn = rgba[..., :3].astype(np.float64) / 255.0 * 2.0 - 1.0
    length = np.linalg.norm(tn, axis=-1, keepdims=True)
    length[length == 0] = 1.0
    tn /= length
    # Outside the islands the bake stores either flat blue or black; neither is
    # a normal, so pin it flat and let the coverage mask gate everything.
    tn[~coverage] = (0.0, 0.0, 1.0)
    return tn, coverage, rgba[..., :3]


def _masked_blur(xy, m, sigma):
    """Normalized convolution - without dividing by the blurred mask the empty
    background bleeds into every island edge."""
    if sigma <= 0:
        return xy
    num = np.stack([gaussian_filter(xy[..., c], sigma, mode="nearest") for c in range(2)], -1)
    den = gaussian_filter(m, sigma, mode="nearest")
    return num / np.maximum(den, 1e-6)[..., None]


def bandpass(tn, coverage, sigma_fine, sigma_coarse):
    """Keep only the feature scale that belongs in the overlay.

    The bake carries artifacts at both ends of the spectrum. Below the fine
    cutoff sit 8-bit quantization steps and cage ringing; above the coarse one
    sits the overall form, which Mu already regenerates from the live mesh and
    which would break the frozen object-space approximation once the body
    morphs. The anatomy - ribs, creases, cellulite - lives in between.
    """
    m = coverage.astype(np.float64)
    xy = tn[..., :2] * m[..., None]
    fine = _masked_blur(xy, m, sigma_fine)
    coarse = _masked_blur(xy, m, sigma_coarse)
    return (fine - coarse) * m[..., None]


def suppress_artifacts(detail, coverage, percentile):
    """Clamp bake outliers (ray misses at armpits, groin, knees) by magnitude."""
    mag = np.linalg.norm(detail, axis=-1)
    if not coverage.any():
        return detail, mag
    cap = np.percentile(mag[coverage], percentile)
    if cap <= 0:
        return detail, mag
    scale = np.minimum(1.0, cap / np.maximum(mag, 1e-9))
    return detail * scale[..., None], np.minimum(mag, cap)


def build_alpha(mag, coverage, floor, knee, edge_fade_px, max_alpha):
    """Alpha decides where the overlay overrides Mu, so gate it on real detail."""
    span = max(knee - floor, 1e-6)
    t = np.clip((mag - floor) / span, 0.0, 1.0)
    t = t * t * (3.0 - 2.0 * t)  # smoothstep
    if edge_fade_px > 0:
        # Island borders carry seam error and partial-coverage TBN, so fade in.
        edge = distance_transform_edt(coverage)
        fade = np.clip(edge / edge_fade_px, 0.0, 1.0)
        t = t * fade * fade * (3.0 - 2.0 * fade)
    return np.clip(t * coverage * max_alpha, 0.0, 1.0)


def to_object_space(detail_xy, frame):
    """Apply the detail through the mesh TBN, mirroring Mu's own shader."""
    n, t, b = frame["n"], frame["t"], frame["b"]
    # Gram-Schmidt and handedness exactly as UpdateNormalMap.hlsl does it.
    ft = t - n * np.sum(n * t, axis=-1, keepdims=True)
    ft /= np.maximum(np.linalg.norm(ft, axis=-1, keepdims=True), 1e-9)
    handed = np.where(np.sum(np.cross(n, t) * b, axis=-1) < 0.0, -1.0, 1.0)
    fb = np.cross(n, ft) * handed[..., None]
    fb /= np.maximum(np.linalg.norm(fb, axis=-1, keepdims=True), 1e-9)

    dz = np.sqrt(np.maximum(0.0, 1.0 - np.sum(detail_xy * detail_xy, axis=-1)))
    obj = detail_xy[..., 0:1] * ft + detail_xy[..., 1:2] * fb + dz[..., None] * n
    obj /= np.maximum(np.linalg.norm(obj, axis=-1, keepdims=True), 1e-9)
    return obj


def encode_msn(obj):
    """(n * 0.5 + 0.5).xzy - the encoding Mu's shader writes and reads back."""
    enc = obj * 0.5 + 0.5
    return np.stack([enc[..., 0], enc[..., 2], enc[..., 1]], -1)


def dump_stages(directory, stem, tn, detail, mag, rgb, alpha):
    out = Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    size = 1000

    def save(name, arr):
        img = Image.fromarray(np.clip(arr * 255.0 + 0.5, 0, 255).astype(np.uint8))
        img.resize((size, size), Image.LANCZOS).save(out / f"{stem}_{name}.png")

    save("1_bake", tn * 0.5 + 0.5)
    boosted = np.concatenate([detail * 6.0, np.zeros(detail.shape[:2] + (1,))], -1)
    save("2_detalhe_x6", boosted * 0.5 + 0.5)
    save("3_magnitude", np.repeat((mag / max(mag.max(), 1e-9))[..., None], 3, -1))
    save("4_overlay_rgb", rgb)
    save("5_alpha", np.repeat(alpha[..., None], 3, -1))
    composite = rgb * alpha[..., None] + 0.5 * (1.0 - alpha[..., None])
    save("6_composto", composite)
    print(f"previews -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bake", required=True, help="baked tangent-space normal PNG")
    ap.add_argument("--mesh", help="optional: OBJ of the displayed shape. Without it the "
                                   "baked RGB is passed through untouched and only alpha is "
                                   "authored, which is what the existing Details overlays do")
    ap.add_argument("--out", required=True, help="overlay PNG to write")
    ap.add_argument("--sigma-fine", type=float, default=0.0,
                    help="optional lower band edge in pixels; raise it only to fight "
                         "quantization noise, at the cost of the finest detail")
    ap.add_argument("--sigma-coarse", type=float, default=40.0,
                    help="upper band edge in pixels; above this sits form Mu regenerates itself")
    ap.add_argument("--rgb", choices=("full", "detail"), default="full",
                    help="'full' keeps the whole converted normal in RGB, matching the "
                         "existing Details overlays; 'detail' keeps only the band-passed "
                         "residual, which stays valid further from the authored shape")
    ap.add_argument("--clamp-percentile", type=float, default=99.5)
    ap.add_argument("--alpha-floor", type=float, default=0.030)
    ap.add_argument("--alpha-knee", type=float, default=0.140)
    ap.add_argument("--edge-fade", type=float, default=24.0)
    ap.add_argument("--max-alpha", type=float, default=0.50,
                    help="alpha cap; Mu's author suggests around 0.5")
    ap.add_argument("--size", type=int, default=0,
                    help="output size; 0 keeps the bake resolution, which is what the "
                         "shipped 4096 Details overlays use")
    ap.add_argument("--dump-stages", metavar="DIR",
                    help="write per-stage previews for visual review")
    args = ap.parse_args()

    tn, coverage, raw_rgb = decode_tangent(args.bake)
    res = tn.shape[0]
    print(f"bake {args.bake}: {tn.shape[:2]} coverage={coverage.mean():.2%}")

    detail = bandpass(tn, coverage, args.sigma_fine, args.sigma_coarse)
    detail, mag = suppress_artifacts(detail, coverage, args.clamp_percentile)
    inside = mag[coverage]
    print(f"detalhe |xy|: mean={inside.mean():.4f} p50={np.median(inside):.4f} "
          f"p95={np.percentile(inside, 95):.4f} max={inside.max():.4f}")

    if args.mesh:
        V, VT, VN, F = load_obj(args.mesh)
        N = VN if len(VN) == len(V) else area_weighted_normals(V, F)
        N = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
        T = uv_tangents(V, VT, F, N)
        B = np.cross(N, T)
        print(f"mesh {args.mesh}: V={len(V)} tris={len(F)} - rasterizando {res}x{res}...")
        frame = rasterize(VT, F, {"n": N, "t": T, "b": B}, res, v_flip=True)
        for key in ("n", "t", "b"):
            frame[key] /= np.maximum(np.linalg.norm(frame[key], axis=-1, keepdims=True), 1e-9)
        print(f"cobertura da malha={frame['mask'].mean():.2%}")
        valid = coverage & frame["mask"]
        carried = tn[..., :2] if args.rgb == "full" else detail
        rgb = encode_msn(to_object_space(carried, frame))
        # Outside the islands the TBN is undefined. Alpha is zero there anyway,
        # but BC7 and the mip chain both bleed neighbours, so park it on the
        # same cyan the existing Details overlays use.
        rgb[~valid] = (0.0, 1.0, 1.0)
    else:
        # Pure image path: keep the baked RGB exactly as authored and let alpha
        # alone decide where Mu blends it, the way the shipped overlays do.
        valid = coverage
        rgb = raw_rgb.astype(np.float64) / 255.0

    alpha = build_alpha(mag, valid, args.alpha_floor, args.alpha_knee,
                        args.edge_fade, args.max_alpha)

    rgba = np.concatenate([rgb, alpha[..., None]], -1)
    rgba = np.clip(rgba * 255.0 + 0.5, 0, 255).astype(np.uint8)
    if args.size and args.size != res:
        # Pillow premultiplies alpha when resampling RGBA, which would wipe the
        # normal out of every transparent texel and bleed zeros across the
        # island edges. Resample colour and coverage as separate images.
        wh = (args.size, args.size)
        colour = Image.fromarray(rgba[..., :3], "RGB").resize(wh, Image.LANCZOS)
        cover = Image.fromarray(rgba[..., 3], "L").resize(wh, Image.LANCZOS)
        rgba = np.dstack([np.asarray(colour), np.asarray(cover)])
    image = Image.fromarray(rgba, "RGBA")
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    image.save(args.out)
    live = alpha[valid]
    print(f"alpha: cobertura>0={np.mean(alpha > 0):.2%} mean={live.mean():.3f} "
          f"max={live.max():.3f} -> {args.out}")

    if args.dump_stages:
        dump_stages(args.dump_stages, Path(args.out).stem, tn, detail, mag, rgb, alpha)


if __name__ == "__main__":
    main()
