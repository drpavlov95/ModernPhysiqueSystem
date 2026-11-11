#!/usr/bin/env python3
"""Measure diffuse color discontinuities at wrist and ankle mesh borders."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

from align_skin_seam_normals import Mesh, matched_pairs


def incident_centroids(mesh: Mesh) -> dict[int, np.ndarray]:
    result: dict[int, list[np.ndarray]] = defaultdict(list)
    for tri in mesh.triangles:
        centroid = mesh.uvs[tri].mean(axis=0)
        for index in tri:
            result[int(index)].append(centroid)
    return {index: np.mean(values, axis=0) for index, values in result.items()}


def inward_uv(mesh: Mesh, centroids: dict[int, np.ndarray], index: int, size, pixels=12):
    uv = mesh.uvs[index].copy()
    direction = centroids[index] - uv
    pixel_direction = direction * np.asarray([size[0], size[1]])
    length = np.linalg.norm(pixel_direction)
    if length > 1.0e-9:
        uv += direction * (pixels / length)
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


def stats(deltas: np.ndarray):
    return {
        "count": int(len(deltas)),
        "mean_rgb": np.mean(deltas, axis=0).round(3).tolist(),
        "median_rgb": np.median(deltas, axis=0).round(3).tolist(),
        "mean_abs_rgb": np.mean(np.abs(deltas), axis=0).round(3).tolist(),
        "p95_abs_rgb": np.percentile(np.abs(deltas), 95, axis=0).round(3).tolist(),
        "mean_abs_total": float(np.mean(np.abs(deltas))),
    }


parser = argparse.ArgumentParser()
parser.add_argument("--body", type=Path, required=True)
parser.add_argument("--hands", type=Path, required=True)
parser.add_argument("--feet", type=Path, required=True)
parser.add_argument("--body-texture", type=Path, required=True)
parser.add_argument("--hands-texture", type=Path, required=True)
parser.add_argument("--pixels", type=float, default=12.0)
args = parser.parse_args()

body = Mesh(args.body, "Softbody")
hands = Mesh(args.hands, "Hands")
feet = Mesh(args.feet, "Feet")
body_image = np.asarray(Image.open(args.body_texture).convert("RGB"), dtype=np.float64)
hands_image = np.asarray(Image.open(args.hands_texture).convert("RGB"), dtype=np.float64)
centers = {
    "body": incident_centroids(body),
    "hands": incident_centroids(hands),
    "feet": incident_centroids(feet),
}

wrist_delta = []
for body_index, hand_index in matched_pairs(body.vertices, hands.vertices, 0.001):
    body_uv = inward_uv(body, centers["body"], body_index, body_image.shape[1::-1], args.pixels)
    hand_uv = inward_uv(hands, centers["hands"], hand_index, hands_image.shape[1::-1], args.pixels)
    wrist_delta.append(sample(body_image, body_uv) - sample(hands_image, hand_uv))

ankle_delta = []
for body_index, foot_index in matched_pairs(body.vertices, feet.vertices, 0.001):
    body_uv = inward_uv(body, centers["body"], body_index, body_image.shape[1::-1], args.pixels)
    foot_uv = inward_uv(feet, centers["feet"], foot_index, body_image.shape[1::-1], args.pixels)
    ankle_delta.append(sample(body_image, body_uv) - sample(body_image, foot_uv))

print(json.dumps({
    "pixel_depth": args.pixels,
    "wrist_body_minus_hands": stats(np.asarray(wrist_delta)),
    "ankle_leg_minus_foot": stats(np.asarray(ankle_delta)),
}, indent=2))
