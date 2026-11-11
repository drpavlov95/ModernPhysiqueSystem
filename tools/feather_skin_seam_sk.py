#!/usr/bin/env python3
"""Feather the BnP skin-response (_sk) map across COTR wrist/ankle seams."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

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
    return (image[y0, x0] * (1 - fx) * (1 - fy) + image[y0, x1] * fx * (1 - fy) + image[y1, x0] * (1 - fx) * fy + image[y1, x1] * fx * fy)


def seam_correction(source_mesh, target_mesh, source_image, target_image, pairs, pixels):
    source_centers = incident_centroids(source_mesh)
    target_centers = incident_centroids(target_mesh)
    deltas = []
    points = []
    for source_index, target_index in pairs:
        source_uv = inward_uv(
            source_mesh, source_centers, source_index, source_image.shape[1::-1], pixels
        )
        target_uv = inward_uv(
            target_mesh, target_centers, target_index, target_image.shape[1::-1], pixels
        )
        deltas.append(sample(source_image, source_uv) - sample(target_image, target_uv))
        points.append(target_uv)
    return np.median(np.asarray(deltas), axis=0), points


def apply(image: Image.Image, points, delta, blur_radius):
    rgba = image.convert("RGBA")
    width, height = rgba.size
    mask = Image.new("L", rgba.size)
    draw = ImageDraw.Draw(mask)
    radius = 10
    for uv in points:
        x, y = float(uv[0]) * (width - 1), float(uv[1]) * (height - 1)
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(blur_radius))
    mask_array = np.asarray(mask, dtype=np.float32)
    if mask_array.max() <= 0:
        raise RuntimeError("empty seam mask")
    mask_array = np.rint(mask_array * (255.0 / mask_array.max())).astype(np.uint8)
    mask = Image.fromarray(mask_array, "L")
    array = np.asarray(rgba, dtype=np.int16).copy()
    array[:, :, :3] = np.clip(array[:, :, :3] + np.rint(delta).astype(np.int16), 0, 255)
    adjusted = Image.fromarray(array.astype(np.uint8), "RGBA")
    return Image.composite(adjusted, rgba, mask)


parser = argparse.ArgumentParser()
parser.add_argument("--body", type=Path, required=True)
parser.add_argument("--hands", type=Path, required=True)
parser.add_argument("--feet", type=Path, required=True)
parser.add_argument("--body-sk", type=Path, required=True)
parser.add_argument("--hands-sk", type=Path, required=True)
parser.add_argument("--out-body", type=Path, required=True)
parser.add_argument("--out-hands", type=Path, required=True)
args = parser.parse_args()

body = Mesh(args.body, "Softbody")
hands = Mesh(args.hands, "Hands")
feet = Mesh(args.feet, "Feet")
body_rgba = Image.open(args.body_sk).convert("RGBA")
hands_rgba = Image.open(args.hands_sk).convert("RGBA")
body_rgb = np.asarray(body_rgba.convert("RGB"), dtype=np.float64)
hands_rgb = np.asarray(hands_rgba.convert("RGB"), dtype=np.float64)

wrist_delta, wrist_points = seam_correction(
    body, hands, body_rgb, hands_rgb, matched_pairs(body.vertices, hands.vertices, 0.001), 8
)
ankle_delta, ankle_points = seam_correction(
    body, feet, body_rgb, body_rgb, matched_pairs(body.vertices, feet.vertices, 0.001), 8
)

args.out_body.parent.mkdir(parents=True, exist_ok=True)
args.out_hands.parent.mkdir(parents=True, exist_ok=True)
apply(body_rgba, ankle_points, ankle_delta, 96).save(args.out_body)
apply(hands_rgba, wrist_points, wrist_delta, 96).save(args.out_hands)
print("wrist delta", np.rint(wrist_delta).astype(int).tolist())
print("ankle delta", np.rint(ankle_delta).astype(int).tolist())
