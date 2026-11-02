import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage


SIZE = 2048


def smoothstep(value, edge0, edge1):
    t = np.clip((value - edge0) / max(edge1 - edge0, 1e-8), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def window(value, low, high, feather):
    return smoothstep(value, low - feather, low + feather) * (
        1.0 - smoothstep(value, high - feather, high + feather)
    )


def normalize(vectors):
    lengths = np.linalg.norm(vectors, axis=2, keepdims=True)
    return vectors / np.maximum(lengths, 1e-8)


def load_gray(path):
    image = Image.open(path).convert("L")
    return np.asarray(image, dtype=np.float32) / 255.0


def sample_image(image, x, y):
    coordinates = np.stack(
        (
            np.clip(y, 0.0, 1.0) * (image.shape[0] - 1),
            np.clip(x, 0.0, 1.0) * (image.shape[1] - 1),
        )
    )
    return ndimage.map_coordinates(image, coordinates, order=1, mode="nearest")


def object_normal_from_rgb(rgb):
    encoded = rgb / 127.5 - 1.0
    return normalize(np.stack((encoded[:, :, 0], encoded[:, :, 2], encoded[:, :, 1]), axis=2))


def rgb_from_object_normal(normal):
    encoded = np.stack((normal[:, :, 0], normal[:, :, 2], normal[:, :, 1]), axis=2)
    return np.clip(np.rint((encoded * 0.5 + 0.5) * 255.0), 0, 255).astype(np.uint8)


def standard_rgb_from_object_normal(normal):
    return np.clip(np.rint((normal * 0.5 + 0.5) * 255.0), 0, 255).astype(np.uint8)


def crop_non_background(image, threshold):
    mask = image > threshold
    y, x = np.nonzero(mask)
    if not len(x):
        raise RuntimeError("Generated component contains no relief")
    pad = 8
    return image[
        max(0, y.min() - pad) : min(image.shape[0], y.max() + pad + 1),
        max(0, x.min() - pad) : min(image.shape[1], x.max() + pad + 1),
    ]


def build_thin_height(sex, position, normal, rib_component):
    x = position[:, :, 0]
    y = position[:, :, 1]
    z = position[:, :, 2]
    front = smoothstep(y, 0.2, 3.0) * smoothstep(normal[:, :, 1], -0.05, 0.55)

    if sex == "female":
        z_low, z_high = 84.5, 102.8
        half_width = 12.8
        breast_suppression = 1.0 - smoothstep(y, 5.5, 7.0) * smoothstep(z, 91.0, 95.0)
        amplitude = 0.78
    else:
        z_low, z_high = 84.0, 103.5
        half_width = 13.4
        breast_suppression = 1.0
        amplitude = 1.0

    torso = window(x, -half_width, half_width, 1.4) * window(z, z_low, z_high, 1.6)
    brush_x = x / (2.0 * half_width) + 0.5
    brush_y = 1.0 - (z - z_low) / (z_high - z_low)
    ribs = sample_image(rib_component, brush_x, brush_y)
    ribs = np.clip((ribs - 0.025) / 0.90, 0.0, 1.0)
    height = ribs * torso * front * breast_suppression * amplitude

    # A small clavicle and sternum contribution makes the endpoint look bony
    # without turning the entire torso into broad shadow bands.
    clavicle = np.exp(-((z - 104.0) / 1.15) ** 2) * np.exp(-((np.abs(x) - 5.4) / 4.4) ** 2)
    clavicle *= window(x, -12.0, 12.0, 1.2) * front
    sternum = np.exp(-(x / 0.75) ** 2) * window(z, 88.0, 104.5, 1.4) * front
    height += 0.18 * clavicle + 0.11 * sternum
    return ndimage.gaussian_filter(height, sigma=0.72)


def sample_heavy_component(component, position, offset_x, offset_y, scale_x, scale_z):
    x = position[:, :, 0]
    z = position[:, :, 2]
    u = np.mod((x + offset_x) / scale_x, 2.0)
    u = 1.0 - np.abs(u - 1.0)
    v = np.mod((z + offset_y) / scale_z, 2.0)
    v = 1.0 - np.abs(v - 1.0)
    return sample_image(component, u, v)


def sample_cluster(component, position, bounds, crop):
    x0, x1, z0, z1 = bounds
    u0, u1, v0, v1 = crop
    x = position[:, :, 0]
    z = position[:, :, 2]
    local_u = (x - x0) / (x1 - x0)
    local_v = 1.0 - (z - z0) / (z1 - z0)
    sample_u = u0 + np.clip(local_u, 0.0, 1.0) * (u1 - u0)
    sample_v = v0 + np.clip(local_v, 0.0, 1.0) * (v1 - v0)
    return sample_image(component, sample_u, sample_v) - 0.5


def build_heavy_height(sex, position, normal, cellulite_component, stretch_component):
    x = position[:, :, 0]
    y = position[:, :, 1]
    z = position[:, :, 2]
    abs_x = np.abs(x)

    front = smoothstep(y, 0.2, 2.5) * smoothstep(normal[:, :, 1], -0.15, 0.45)
    back = smoothstep(-y, 0.4, 3.0) * smoothstep(-normal[:, :, 1], -0.15, 0.45)
    sides = smoothstep(abs_x, 6.0, 10.0) * window(abs_x, 5.0, 15.5, 1.5)

    lower_abdomen = front * window(abs_x, 0.0, 12.0, 1.4) * window(z, 66.0, 86.0, 2.0)
    hips = sides * window(z, 61.0, 84.0, 2.5)
    glutes = back * window(abs_x, 0.0, 14.0, 1.5) * window(z, 58.0, 78.5, 2.5)
    upper_thighs = window(abs_x, 3.0, 14.5, 1.4) * window(z, 43.0, 67.0, 2.5)
    soft_tissue = np.clip(lower_abdomen + hips + glutes + upper_thighs, 0.0, 1.0)

    low = ndimage.gaussian_filter(cellulite_component, sigma=7.0)
    very_low = ndimage.gaussian_filter(cellulite_component, sigma=35.0)
    cellulite_band = low - very_low
    cellulite = sample_heavy_component(cellulite_band, position, 17.0, 41.0, 31.0, 37.0)
    cellulite -= float(np.median(cellulite_band))
    cellulite_scale = 1.0 if sex == "female" else 0.62
    height = cellulite * soft_tissue * cellulite_scale * 5.8

    # Use one non-branching generated cluster on each side. The geometry mask
    # handles placement; the source brush only supplies organic line profiles.
    left = sample_cluster(stretch_component, position, (-14.0, -3.0, 64.0, 84.0), (0.02, 0.48, 0.50, 0.98))
    right = sample_cluster(stretch_component, position, (3.0, 14.0, 64.0, 84.0), (0.52, 0.98, 0.50, 0.98))
    left_mask = window(x, -14.0, -3.0, 1.1) * window(z, 64.0, 84.0, 1.8) * np.clip(front + sides, 0.0, 1.0)
    right_mask = window(x, 3.0, 14.0, 1.1) * window(z, 64.0, 84.0, 1.8) * np.clip(front + sides, 0.0, 1.0)
    striae_scale = 0.85 if sex == "female" else 0.58
    height += striae_scale * (left * left_mask + right * right_mask) * 2.2
    return ndimage.gaussian_filter(height, sigma=0.45), soft_tissue, left_mask + right_mask


def perturb_normal(base, tangent, bitangent, height, strength):
    dv, du = np.gradient(height)
    slope_u = du * strength
    slope_v = dv * strength
    perturbed = base - tangent * slope_u[:, :, None] - bitangent * slope_v[:, :, None]
    return normalize(perturbed), np.sqrt(slope_u * slope_u + slope_v * slope_v)


def render_regions(path, coverage, thin_mask, soft_mask, striae_mask):
    preview = np.zeros((SIZE, SIZE, 3), dtype=np.uint8)
    preview[:, :, 0] = np.clip(thin_mask * 255.0, 0, 255).astype(np.uint8)
    preview[:, :, 1] = np.clip(soft_mask * 210.0, 0, 255).astype(np.uint8)
    preview[:, :, 2] = np.clip(striae_mask * 255.0, 0, 255).astype(np.uint8)
    preview[coverage == 0] = 0
    Image.fromarray(preview, "RGB").resize((1024, 1024), Image.Resampling.LANCZOS).save(path)


def build_profile(sex, profile, current_path, fields, components, output_dir):
    image = Image.open(current_path).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    current = np.asarray(image, dtype=np.uint8)
    current_rgb = current[:, :, :3].astype(np.float32)
    current_alpha = current[:, :, 3].astype(np.float32) / 255.0

    coverage = fields["coverage"] > 0
    normal = fields["normal"].astype(np.float32)
    tangent = fields["tangent"].astype(np.float32)
    bitangent = fields["bitangent"].astype(np.float32)
    position = fields["position"].astype(np.float32)
    edge_distance = ndimage.distance_transform_edt(coverage)
    edge_fade = smoothstep(edge_distance, 5.0, 28.0)

    existing_normal = object_normal_from_rgb(current_rgb)
    existing_valid = (current_alpha > 1.0 / 255.0)[:, :, None]
    base_normal = normalize(np.where(existing_valid, existing_normal, normal))

    current_final = normalize(normal * (1.0 - current_alpha[:, :, None]) + existing_normal * current_alpha[:, :, None])
    current_preview_rgb = standard_rgb_from_object_normal(current_final)
    current_preview_rgb[~coverage] = 0
    Image.fromarray(current_preview_rgb, "RGB").save(
        os.path.join(output_dir, f"{sex}_{profile}_100_current_preview_normal.png")
    )

    output_normal = base_normal
    output_alpha = current_alpha.copy()
    thin_mask = np.zeros_like(current_alpha)
    soft_mask = np.zeros_like(current_alpha)
    striae_mask = np.zeros_like(current_alpha)
    slope = np.zeros_like(current_alpha)

    if profile == "thin":
        height = build_thin_height(sex, position, normal, components["ribs"])
        thin_mask = smoothstep(height, 0.015, 0.32) * edge_fade * coverage
        strength = 9.5 if sex == "male" else 8.0
        output_normal, slope = perturb_normal(base_normal, tangent, bitangent, height, strength)
        feature_alpha = thin_mask * (0.22 + 0.78 * smoothstep(slope, 0.006, 0.11))
        feature_alpha *= 0.43 if sex == "male" else 0.38
        output_alpha = np.maximum(output_alpha, feature_alpha)
    elif profile == "heavy":
        height, soft_mask, striae_mask = build_heavy_height(
            sex, position, normal, components["cellulite"], components["stretch"]
        )
        soft_mask = np.clip(soft_mask, 0.0, 1.0) * edge_fade * coverage
        striae_mask = np.clip(striae_mask, 0.0, 1.0) * edge_fade * coverage
        strength = 13.0 if sex == "female" else 10.0
        # H100 contains useful folds but also broad endpoint shading that Mu
        # already reconstructs from geometry. Keep only its local variation in
        # the intended soft-tissue regions to avoid the old dirty appearance.
        blurred = np.dstack(
            [ndimage.gaussian_filter(existing_normal[:, :, channel], sigma=7.0) for channel in range(3)]
        )
        blurred = normalize(blurred)
        local_change = 1.0 - np.clip(np.sum(existing_normal * blurred, axis=2), -1.0, 1.0)
        legacy_detail = smoothstep(local_change, 0.00018, 0.0045)
        legacy_alpha = current_alpha * soft_mask * (0.18 + 0.82 * legacy_detail) * 0.62
        base_normal = normalize(
            normal * (1.0 - legacy_alpha[:, :, None]) + existing_normal * legacy_alpha[:, :, None]
        )
        output_normal, slope = perturb_normal(base_normal, tangent, bitangent, height, strength)
        feature_alpha = soft_mask * smoothstep(slope, 0.0025, 0.055)
        feature_alpha *= 0.31 if sex == "female" else 0.25
        striae_alpha = striae_mask * smoothstep(slope, 0.003, 0.06)
        striae_alpha *= 0.34 if sex == "female" else 0.27
        output_alpha = np.maximum(legacy_alpha, np.maximum(feature_alpha, striae_alpha))
    else:
        # Keep the authored muscular shape. Only remove alpha outside the body
        # and fade it before UV borders so a full-island outline cannot return.
        output_normal = existing_normal

    output_alpha *= edge_fade * coverage
    output_rgba = np.dstack((rgb_from_object_normal(output_normal), np.rint(output_alpha * 255.0).astype(np.uint8)))
    output_rgba[~coverage] = 0
    output_path = os.path.join(output_dir, f"{sex}_{profile}_100.png")
    Image.fromarray(output_rgba, "RGBA").save(output_path)

    final_normal = normalize(normal * (1.0 - output_alpha[:, :, None]) + output_normal * output_alpha[:, :, None])
    preview_rgb = standard_rgb_from_object_normal(final_normal)
    preview_rgb[~coverage] = 0
    Image.fromarray(preview_rgb, "RGB").save(os.path.join(output_dir, f"{sex}_{profile}_100_preview_normal.png"))

    return {
        "file": output_path,
        "alpha_nonzero": int(np.count_nonzero(output_rgba[:, :, 3])),
        "alpha_mean_nonzero": float(output_rgba[:, :, 3][output_rgba[:, :, 3] > 0].mean()),
        "alpha_max": int(output_rgba[:, :, 3].max()),
        "new_slope_p99": float(np.percentile(slope[coverage], 99.0)),
        "thin_pixels": int(np.count_nonzero(thin_mask > 0.05)),
        "soft_pixels": int(np.count_nonzero(soft_mask > 0.05)),
        "striae_pixels": int(np.count_nonzero(striae_mask > 0.05)),
    }, (thin_mask, soft_mask, striae_mask)


current_dir, fields_dir, generated_dir, output_dir = sys.argv[1:5]
os.makedirs(output_dir, exist_ok=True)
rib_raw = load_gray(os.path.join(generated_dir, "rib_relief_component.png"))
rib_component = crop_non_background(rib_raw, 0.035)
cellulite_component = load_gray(os.path.join(generated_dir, "heavy_relief_component.png"))
stretch_component = load_gray(os.path.join(generated_dir, "stretchmark_relief_component.png"))
components = {"ribs": rib_component, "cellulite": cellulite_component, "stretch": stretch_component}

report = []
for sex in ("female", "male"):
    fields = np.load(os.path.join(fields_dir, f"{sex}_uv_fields.npz"))
    region_layers = []
    for profile in ("thin", "heavy", "muscular"):
        entry, regions = build_profile(
            sex,
            profile,
            os.path.join(current_dir, f"{sex}_{profile}_100.png"),
            fields,
            components,
            output_dir,
        )
        report.append(entry)
        region_layers.append(regions)
    thin_mask = region_layers[0][0]
    soft_mask = region_layers[1][1]
    striae_mask = region_layers[1][2]
    render_regions(
        os.path.join(output_dir, f"{sex}_anatomical_regions.png"),
        fields["coverage"],
        thin_mask,
        soft_mask,
        striae_mask,
    )

with open(os.path.join(output_dir, "report.json"), "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, indent=2)
print(json.dumps(report, indent=2))
