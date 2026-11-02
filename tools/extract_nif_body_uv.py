import json
import os
import sys

import numpy as np


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


def vector3(value):
    return (float(value.x), float(value.y), float(value.z))


def decode_name(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


nif_path, shape_name, output_path = sys.argv[1:4]
with open(nif_path, "rb") as stream:
    data = NifFormat.NifFile.from_stream(stream)

shapes = [
    block
    for root in data.roots
    for block in root.tree(unique=True)
    if isinstance(block, NifClasses.BSTriShape)
]
matches = [shape for shape in shapes if decode_name(shape.name) == shape_name]
if len(matches) != 1:
    available = [decode_name(shape.name) for shape in shapes]
    raise RuntimeError(f"Expected one shape named {shape_name!r}; found {available}")

shape = matches[0]
vertex_data = shape.get_vertex_data()
vertices = np.asarray([vector3(vertex.vertex) for vertex in vertex_data], dtype=np.float32)
uvs = np.asarray([(float(vertex.uv.u), float(vertex.uv.v)) for vertex in vertex_data], dtype=np.float32)
normals = np.asarray([vector3(vertex.normal) for vertex in vertex_data], dtype=np.float32)
tangents = np.asarray([vector3(vertex.tangent) for vertex in vertex_data], dtype=np.float32)
bitangents = np.asarray(
    [
        (float(vertex.bitangent_x), float(vertex.bitangent_y), float(vertex.bitangent_z))
        for vertex in vertex_data
    ],
    dtype=np.float32,
)
triangles = np.asarray(
    [(int(triangle.v_1), int(triangle.v_2), int(triangle.v_3)) for triangle in shape.get_triangles()],
    dtype=np.int32,
)

os.makedirs(os.path.dirname(output_path), exist_ok=True)
np.savez_compressed(
    output_path,
    vertices=vertices,
    uvs=uvs,
    normals=normals,
    tangents=tangents,
    bitangents=bitangents,
    triangles=triangles,
)

report = {
    "source": nif_path,
    "shape": shape_name,
    "vertices": int(len(vertices)),
    "triangles": int(len(triangles)),
    "bounds_min": vertices.min(axis=0).tolist(),
    "bounds_max": vertices.max(axis=0).tolist(),
    "uv_min": uvs.min(axis=0).tolist(),
    "uv_max": uvs.max(axis=0).tolist(),
    "normal_lengths": [float(np.linalg.norm(normals, axis=1).min()), float(np.linalg.norm(normals, axis=1).max())],
    "tangent_lengths": [float(np.linalg.norm(tangents, axis=1).min()), float(np.linalg.norm(tangents, axis=1).max())],
    "bitangent_lengths": [float(np.linalg.norm(bitangents, axis=1).min()), float(np.linalg.norm(bitangents, axis=1).max())],
}
print(json.dumps(report, indent=2))
