#!/usr/bin/env python3
"""Print BSTriShape and packed vertex layout details for Skyrim NIF files."""

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


def name(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


for filename in sys.argv[1:]:
    path = Path(filename)
    with path.open("rb") as stream:
        data = NifFormat.NifFile.from_stream(stream)

    shapes = [
        block
        for root in data.roots
        for block in root.tree(unique=True)
        if isinstance(block, NifClasses.BSTriShape)
    ]
    print(path)
    for shape in shapes:
        vertices = shape.get_vertex_data()
        print(
            f"  {name(shape.name)!r}: vertices={len(vertices)} "
            f"shape_offset={getattr(shape, 'io_start', None)} "
            f"shape_size={getattr(shape, 'io_size', None)} "
            f"vertex_size={int(shape.vertex_desc.vertex_data_size)}"
        )
        if vertices:
            vertex = vertices[0]
            print(
                f"    vertex type={type(vertex).__name__} "
                f"offset={getattr(vertex, 'io_start', None)} "
                f"size={getattr(vertex, 'io_size', None)}"
            )
            for field in ("vertex", "uv", "normal", "tangent"):
                value = getattr(vertex, field)
                print(
                    f"    {field}: type={type(value).__name__} "
                    f"offset={getattr(value, 'io_start', None)} "
                    f"size={getattr(value, 'io_size', None)} value={value}"
                )
            print(
                "    bitangent: "
                f"{float(vertex.bitangent_x):.6f}, "
                f"{float(vertex.bitangent_y):.6f}, "
                f"{float(vertex.bitangent_z):.6f}"
            )
