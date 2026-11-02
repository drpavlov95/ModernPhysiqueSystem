import sys

import bpy
from mathutils import Vector


source_path, variant_path = sys.argv[sys.argv.index("--") + 1 :]


def load_mesh(path):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=path)
    return next(obj for obj in bpy.context.scene.objects if obj.type == "MESH").data


source = load_mesh(source_path)
source_vertices = [vertex.co.copy() for vertex in source.vertices]
source_faces = [tuple(polygon.vertices) for polygon in source.polygons]

variant = load_mesh(variant_path)
variant_vertices = [vertex.co.copy() for vertex in variant.vertices]
variant_faces = [tuple(polygon.vertices) for polygon in variant.polygons]

if len(source_vertices) != len(variant_vertices) or source_faces != variant_faces:
    raise RuntimeError("Vertex count or polygon order changed")

flipped = []
degenerate = []
minimum_dot = 1.0
for face in source_faces:
    a, b, c = face[:3]
    source_normal = (source_vertices[b] - source_vertices[a]).cross(source_vertices[c] - source_vertices[a])
    variant_normal = (variant_vertices[b] - variant_vertices[a]).cross(variant_vertices[c] - variant_vertices[a])
    if variant_normal.length_squared < 1e-12:
        degenerate.append(face)
        continue
    dot = source_normal.normalized().dot(variant_normal.normalized())
    minimum_dot = min(minimum_dot, dot)
    if dot <= 0.0:
        flipped.append(face)

print("VERTEX_COUNT=%d" % len(source_vertices))
print("POLYGON_COUNT=%d" % len(source_faces))
print("FLIPPED_FACES=%d" % len(flipped))
print("DEGENERATE_FACES=%d" % len(degenerate))
print("MIN_NORMAL_DOT=%.6f" % minimum_dot)
print("FLIPPED_VERTICES=%s" % ";".join(",".join(str(index) for index in face) for face in flipped))
print("DEGENERATE_VERTICES=%s" % ";".join(",".join(str(index) for index in face) for face in degenerate))
