import json
import os
import sys

import numpy as np
from PIL import Image


def normalize(vectors):
    lengths = np.linalg.norm(vectors, axis=1, keepdims=True)
    return vectors / np.maximum(lengths, 1e-8)


def build_basis(vertices, uvs, triangles):
    normals = np.zeros_like(vertices)
    tangents = np.zeros_like(vertices)
    bitangents = np.zeros_like(vertices)

    p0 = vertices[triangles[:, 0]]
    p1 = vertices[triangles[:, 1]]
    p2 = vertices[triangles[:, 2]]
    uv0 = uvs[triangles[:, 0]]
    uv1 = uvs[triangles[:, 1]]
    uv2 = uvs[triangles[:, 2]]
    edge1 = p1 - p0
    edge2 = p2 - p0
    duv1 = uv1 - uv0
    duv2 = uv2 - uv0

    face_normals = np.cross(edge1, edge2)
    denominator = duv1[:, 0] * duv2[:, 1] - duv1[:, 1] * duv2[:, 0]
    valid = np.abs(denominator) > 1e-10
    reciprocal = np.zeros_like(denominator)
    reciprocal[valid] = 1.0 / denominator[valid]
    face_tangents = (edge1 * duv2[:, 1, None] - edge2 * duv1[:, 1, None]) * reciprocal[:, None]
    face_bitangents = (-edge1 * duv2[:, 0, None] + edge2 * duv1[:, 0, None]) * reciprocal[:, None]

    for corner in range(3):
        np.add.at(normals, triangles[:, corner], face_normals)
        np.add.at(tangents, triangles[:, corner], face_tangents)
        np.add.at(bitangents, triangles[:, corner], face_bitangents)

    normals = normalize(normals)
    tangents = normalize(tangents - normals * np.sum(normals * tangents, axis=1, keepdims=True))
    handedness = np.sign(np.sum(np.cross(normals, tangents) * bitangents, axis=1, keepdims=True))
    handedness[handedness == 0] = 1
    bitangents = normalize(np.cross(normals, tangents)) * handedness
    return normals, tangents, bitangents


def rasterize(uvs, triangles, attributes, size):
    height = width = size
    channels = attributes.shape[1]
    result = np.zeros((height, width, channels), dtype=np.float32)
    coverage = np.zeros((height, width), dtype=np.uint8)
    overlaps = 0

    pixel_uvs = np.column_stack((uvs[:, 0] * (width - 1), uvs[:, 1] * (height - 1)))
    for triangle in triangles:
        points = pixel_uvs[triangle]
        min_x = max(0, int(np.floor(points[:, 0].min())))
        max_x = min(width - 1, int(np.ceil(points[:, 0].max())))
        min_y = max(0, int(np.floor(points[:, 1].min())))
        max_y = min(height - 1, int(np.ceil(points[:, 1].max())))
        if min_x > max_x or min_y > max_y:
            continue

        x, y = np.meshgrid(
            np.arange(min_x, max_x + 1, dtype=np.float32) + 0.5,
            np.arange(min_y, max_y + 1, dtype=np.float32) + 0.5,
        )
        p0, p1, p2 = points
        denominator = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1])
        if abs(denominator) < 1e-8:
            continue
        w0 = ((p1[1] - p2[1]) * (x - p2[0]) + (p2[0] - p1[0]) * (y - p2[1])) / denominator
        w1 = ((p2[1] - p0[1]) * (x - p2[0]) + (p0[0] - p2[0]) * (y - p2[1])) / denominator
        w2 = 1.0 - w0 - w1
        inside = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
        if not np.any(inside):
            continue

        area = result[min_y : max_y + 1, min_x : max_x + 1]
        area_coverage = coverage[min_y : max_y + 1, min_x : max_x + 1]
        values = (
            w0[..., None] * attributes[triangle[0]]
            + w1[..., None] * attributes[triangle[1]]
            + w2[..., None] * attributes[triangle[2]]
        )
        overlaps += int(np.count_nonzero(inside & (area_coverage != 0)))
        area[inside] = values[inside]
        area_coverage[inside] = 255

    return result, coverage, overlaps


def save_rgb(path, values, coverage):
    image = np.clip(values * 255.0, 0, 255).astype(np.uint8)
    image[coverage == 0] = 0
    Image.fromarray(image, "RGB").save(path)


npz_path, output_dir, size_text = sys.argv[1:4]
size = int(size_text)
sex = os.path.basename(npz_path).split("_")[0]
data = np.load(npz_path)
vertices = data["vertices"]
uvs = data["uvs"]
triangles = data["triangles"]
normals, tangents, bitangents = build_basis(vertices, uvs, triangles)

minimum = vertices.min(axis=0)
maximum = vertices.max(axis=0)
position_colors = (vertices - minimum) / np.maximum(maximum - minimum, 1e-8)
basis_colors = normals * 0.5 + 0.5
attributes = np.concatenate((vertices, normals, tangents, bitangents, position_colors, basis_colors), axis=1)
raster, coverage, overlaps = rasterize(uvs, triangles, attributes, size)

os.makedirs(output_dir, exist_ok=True)
prefix = os.path.join(output_dir, sex)
np.savez_compressed(
    prefix + "_uv_fields.npz",
    position=raster[:, :, 0:3],
    normal=raster[:, :, 3:6],
    tangent=raster[:, :, 6:9],
    bitangent=raster[:, :, 9:12],
    coverage=coverage,
)
save_rgb(prefix + "_uv_position.png", raster[:, :, 12:15], coverage)
save_rgb(prefix + "_uv_normal.png", raster[:, :, 15:18], coverage)
Image.fromarray(coverage, "L").save(prefix + "_uv_coverage.png")

with open(prefix + "_body.obj", "w", encoding="ascii", newline="\n") as obj:
    obj.write(f"o {sex}_mpp_body\n")
    for vertex in vertices:
        obj.write(f"v {vertex[0]:.8f} {vertex[1]:.8f} {vertex[2]:.8f}\n")
    for uv in uvs:
        obj.write(f"vt {uv[0]:.8f} {1.0 - uv[1]:.8f}\n")
    for normal in normals:
        obj.write(f"vn {normal[0]:.8f} {normal[1]:.8f} {normal[2]:.8f}\n")
    for triangle in triangles:
        indices = triangle + 1
        obj.write("f " + " ".join(f"{index}/{index}/{index}" for index in indices) + "\n")

print(
    json.dumps(
        {
            "sex": sex,
            "size": size,
            "covered_pixels": int(np.count_nonzero(coverage)),
            "overlapping_writes": overlaps,
            "normal_length": [float(np.linalg.norm(normals, axis=1).min()), float(np.linalg.norm(normals, axis=1).max())],
        },
        indent=2,
    )
)
