#!/usr/bin/env python3
"""Print skin-relevant shader and texture-set properties from Skyrim NIFs."""

from __future__ import annotations

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


FIELDS = (
    "shader_type",
    "shader_flags_1",
    "shader_flags_2",
    "uv_offset",
    "uv_scale",
    "emissive_color",
    "emissive_multiple",
    "texture_clamp_mode",
    "alpha",
    "refraction_strength",
    "glossiness",
    "smoothness",
    "specular_color",
    "specular_strength",
    "lighting_effect_1",
    "lighting_effect_2",
    "subsurface_rolloff",
    "rimlight_power",
    "grayscale_to_palette_scale",
    "fresnel_power",
    "wetness_spec_scale",
    "wetness_spec_power",
    "wetness_min_var",
    "wetness_env_map_scale",
    "wetness_fresnel_power",
    "wetness_metalness",
)


def text(value) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def useful(value) -> str:
    try:
        return f"{int(value)} ({value})"
    except (TypeError, ValueError):
        return text(value)


for filename in sys.argv[1:]:
    path = Path(filename)
    with path.open("rb") as stream:
        data = NifFormat.NifFile.from_stream(stream)

    print(path)
    shapes = [
        block
        for root in data.roots
        for block in root.tree(unique=True)
        if isinstance(block, NifClasses.BSTriShape)
    ]
    for shape in shapes:
        prop = getattr(shape, "shader_property", None)
        print(f"  shape={text(shape.name)!r}")
        if prop is None:
            print("    shader_property=None")
            continue
        print(f"    shader_property={type(prop).__name__}")
        for field in FIELDS:
            if hasattr(prop, field):
                print(f"    {field}={useful(getattr(prop, field))}")
        texture_set = getattr(prop, "texture_set", None)
        if texture_set is not None:
            textures = getattr(texture_set, "textures", ())
            for index, texture in enumerate(textures):
                print(f"    texture[{index}]={text(texture)!r}")
