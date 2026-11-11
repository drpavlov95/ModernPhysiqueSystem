#!/usr/bin/env python3
"""Report the spatial strength of BodySlide OSD morphs on one NIF shape."""

from __future__ import annotations

import argparse
import math
import os
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "migrate_legacy"))
sys.path.insert(
    0,
    os.path.join(
        os.environ["APPDATA"],
        "Blender Foundation",
        "Blender",
        "4.4",
        "scripts",
        "addons",
        "io_scene_niftools",
        "dependencies",
    ),
)

import nifgen.formats.nif as NifFormat  # noqa: E402
from nifgen.formats.nif import classes as NifClasses  # noqa: E402

from osd_parser import read_osd  # noqa: E402


def text(value: object) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("nif", type=Path)
    parser.add_argument("shape")
    parser.add_argument("morph", nargs="+")
    parser.add_argument("--osd", type=Path)
    args = parser.parse_args()

    with args.nif.open("rb") as stream:
        nif = NifFormat.NifFile.from_stream(stream)

    shape = next(
        block
        for root in nif.roots
        for block in root.tree(unique=True)
        if isinstance(block, NifClasses.BSTriShape) and text(block.name) == args.shape
    )
    vertices = [
        (float(vertex.vertex.x), float(vertex.vertex.y), float(vertex.vertex.z))
        for vertex in shape.get_vertex_data()
    ]
    entries = {entry.name: entry for entry in read_osd(args.osd or args.nif.with_suffix(".osd"))}

    print(f"shape={args.shape} vertices={len(vertices)}")
    print(
        "bounds="
        + " ".join(
            f"{axis}[{min(vertex[index] for vertex in vertices):.3f},"
            f"{max(vertex[index] for vertex in vertices):.3f}]"
            for index, axis in enumerate("xyz")
        )
    )
    print("bands use absolute X; on this T-pose they approximate torso/upper-arm/elbow/forearm/wrist")

    bands = (
        ("0-12", 0.0, 12.0),
        ("12-20", 12.0, 20.0),
        ("20-26", 20.0, 26.0),
        ("26-32", 26.0, 32.0),
        ("32+", 32.0, math.inf),
    )

    for name in args.morph:
        entry = entries.get(name)
        if entry is None:
            print(f"\n{name}: MISSING")
            continue

        samples = []
        for vertex_index, delta in entry.deltas.items():
            if vertex_index >= len(vertices):
                continue
            magnitude = math.sqrt(sum(component * component for component in delta))
            samples.append((vertices[vertex_index], magnitude))

        magnitudes = [magnitude for _, magnitude in samples]
        total_magnitude = sum(magnitudes)
        weighted_x = (
            sum(abs(vertex[0]) * magnitude for vertex, magnitude in samples) / total_magnitude
            if total_magnitude
            else 0.0
        )
        weighted_z = (
            sum(vertex[2] * magnitude for vertex, magnitude in samples) / total_magnitude
            if total_magnitude
            else 0.0
        )
        print(
            f"\n{name}: count={len(samples)} "
            f"mean={sum(magnitudes) / max(1, len(magnitudes)):.6f} "
            f"p50={percentile(magnitudes, 0.5):.6f} "
            f"p90={percentile(magnitudes, 0.9):.6f} "
            f"max={max(magnitudes, default=0.0):.6f} "
            f"weightedAbsX={weighted_x:.3f} weightedZ={weighted_z:.3f}"
        )
        for label, low, high in bands:
            selected = [
                magnitude
                for vertex, magnitude in samples
                if low <= abs(vertex[0]) < high
            ]
            print(
                f"  absX {label:>5}: count={len(selected):5d} "
                f"mean={sum(selected) / max(1, len(selected)):.6f} "
                f"p90={percentile(selected, 0.9):.6f} "
                f"max={max(selected, default=0.0):.6f}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
