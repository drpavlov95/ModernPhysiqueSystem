#!/usr/bin/env python3
"""Compare Advanced Skin RFAOS channels at the COtR body/head neck seam."""

from __future__ import annotations

import argparse
import os
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

from align_skin_seam_normals import Mesh, matched_pairs, read_head_tri


NIFTOOLS = os.path.join(
    os.environ["APPDATA"],
    "Blender Foundation",
    "Blender",
    "4.4",
    "scripts",
    "addons",
    "io_scene_niftools",
    "dependencies",
)
sys.path.insert(0, NIFTOOLS)

import nifgen.formats.nif as NifFormat  # noqa: E402
from nifgen.formats.nif import classes as NifClasses  # noqa: E402


CHANNELS = ("roughness_r", "fuzz_g", "ao_b", "specular_a")


def text(value) -> str:
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else str(value)


def head_uvs(path: Path, shape_name: str) -> np.ndarray:
    with path.open("rb") as stream:
        data = NifFormat.NifFile.from_stream(stream)
    matches = [
        block
        for root in data.roots
        for block in root.tree(unique=True)
        if isinstance(block, NifClasses.BSTriShape) and text(block.name) == shape_name
    ]
    if len(matches) != 1:
        raise RuntimeError(f"{path}: expected one {shape_name!r} BSTriShape, got {len(matches)}")
    return np.asarray(
        [[float(vertex.uv.u), float(vertex.uv.v)] for vertex in matches[0].get_vertex_data()],
        dtype=np.float64,
    )


def centroids(uvs: np.ndarray, triangles: np.ndarray) -> dict[int, np.ndarray]:
    result: dict[int, list[np.ndarray]] = defaultdict(list)
    for tri in triangles:
        point = uvs[tri].mean(axis=0)
        for index in tri:
            result[int(index)].append(point)
    return {index: np.asarray(points).mean(axis=0) for index, points in result.items()}


def inward(uv: np.ndarray, centroid: np.ndarray, size: tuple[int, int], pixels: float) -> np.ndarray:
    direction = centroid - uv
    scaled = direction * np.asarray(size)
    length = float(np.linalg.norm(scaled))
    if length > 1.0e-12:
        return uv + direction * (pixels / length)
    return uv


def sample(image: np.ndarray, uv: np.ndarray) -> np.ndarray:
    height, width = image.shape[:2]
    x = float(np.clip(uv[0], 0.0, 1.0) * (width - 1))
    y = float(np.clip(uv[1], 0.0, 1.0) * (height - 1))
    x0, y0 = int(np.floor(x)), int(np.floor(y))
    x1, y1 = min(x0 + 1, width - 1), min(y0 + 1, height - 1)
    fx, fy = x - x0, y - y0
    return (
        image[y0, x0] * (1 - fx) * (1 - fy)
        + image[y0, x1] * fx * (1 - fy)
        + image[y1, x0] * (1 - fx) * fy
        + image[y1, x1] * fx * fy
    )


def vector_stats(values: np.ndarray) -> dict[str, list[float]]:
    return {
        "mean": np.mean(values, axis=0).round(3).tolist(),
        "median": np.median(values, axis=0).round(3).tolist(),
        "p05": np.percentile(values, 5, axis=0).round(3).tolist(),
        "p95": np.percentile(values, 95, axis=0).round(3).tolist(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body", type=Path, required=True)
    parser.add_argument("--head-nif", type=Path, required=True)
    parser.add_argument("--head-tri", type=Path, required=True)
    parser.add_argument("--body-rfaos", type=Path, required=True)
    parser.add_argument("--head-rfaos", type=Path, required=True)
    parser.add_argument("--pixels", type=float, default=12.0)
    parser.add_argument("--tolerance", type=float, default=0.001)
    args = parser.parse_args()

    body = Mesh(args.body, "Softbody")
    head_vertices, head_triangles = read_head_tri(args.head_tri)
    head_uv = head_uvs(args.head_nif, "FemaleHead")
    if len(head_vertices) != len(head_uv):
        raise RuntimeError(
            f"head TRI/NIF vertex-count mismatch: {len(head_vertices)} != {len(head_uv)}"
        )

    body_image = np.asarray(Image.open(args.body_rfaos).convert("RGBA"), dtype=np.float64)
    head_image = np.asarray(Image.open(args.head_rfaos).convert("RGBA"), dtype=np.float64)
    body_centers = centroids(body.uvs, body.triangles)
    head_centers = centroids(head_uv, head_triangles)
    pairs = matched_pairs(body.vertices, head_vertices, args.tolerance)
    if not pairs:
        raise RuntimeError("no neck vertices matched")

    body_samples, head_samples = [], []
    for body_index, head_index in pairs:
        body_uv = inward(body.uvs[body_index], body_centers[body_index], body_image.shape[1::-1], args.pixels)
        head_sample_uv = inward(head_uv[head_index], head_centers[head_index], head_image.shape[1::-1], args.pixels)
        body_samples.append(sample(body_image, body_uv))
        head_samples.append(sample(head_image, head_sample_uv))
    body_array, head_array = np.asarray(body_samples), np.asarray(head_samples)
    delta = body_array - head_array

    print("channels:", ", ".join(CHANNELS))
    print("pairs:", len(pairs), "pixel_depth:", args.pixels)
    print("body:", vector_stats(body_array))
    print("head:", vector_stats(head_array))
    print("body_minus_head:", vector_stats(delta))
    print("mean_abs_delta:", np.mean(np.abs(delta), axis=0).round(3).tolist())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
