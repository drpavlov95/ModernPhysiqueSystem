#!/usr/bin/env python3
"""Set Emissive Multiple on Skyrim SKIN_TINT shapes without touching geometry."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


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


SKIN_TINT = 5


def decode(value) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--value", type=float, default=1.0)
    args = parser.parse_args()

    with args.input.open("rb") as stream:
        data = NifFormat.NifFile.from_stream(stream)

    changed = []
    for root in data.roots:
        for block in root.tree(unique=True):
            if not isinstance(block, NifClasses.BSTriShape):
                continue
            shader = getattr(block, "shader_property", None)
            if shader is None or int(shader.shader_type) != SKIN_TINT:
                continue
            old = float(shader.emissive_multiple)
            shader.emissive_multiple = args.value
            changed.append((decode(block.name), old, args.value))

    if not changed:
        raise RuntimeError(f"{args.input}: no SKIN_TINT BSTriShape found")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        NifFormat.NifFile.to_stream(data, stream)

    for name, old, new in changed:
        print(f"{args.input}: {name}: emissive_multiple {old:g} -> {new:g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
