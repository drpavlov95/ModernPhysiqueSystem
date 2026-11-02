import sys

import bpy
from mathutils import Vector


obj_path = sys.argv[sys.argv.index("--") + 1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=obj_path)
mesh_object = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
mesh = mesh_object.data

world_vertices = [mesh_object.matrix_world @ vertex.co for vertex in mesh.vertices]
minimum = Vector((min(v.x for v in world_vertices), min(v.y for v in world_vertices), min(v.z for v in world_vertices)))
maximum = Vector((max(v.x for v in world_vertices), max(v.y for v in world_vertices), max(v.z for v in world_vertices)))

print(f"OBJECT={mesh_object.name}")
print(f"VERTICES={len(mesh.vertices)}")
print(f"POLYGONS={len(mesh.polygons)}")
print(f"LOOPS={len(mesh.loops)}")
print(f"UV_LAYERS={len(mesh.uv_layers)}")
print(f"BOUNDS_MIN={minimum.x:.6f},{minimum.y:.6f},{minimum.z:.6f}")
print(f"BOUNDS_MAX={maximum.x:.6f},{maximum.y:.6f},{maximum.z:.6f}")
print(f"MEAN_NORMAL_Y={sum(vertex.normal.y for vertex in mesh.vertices) / len(mesh.vertices):.6f}")
print(f"OBJECT_SCALE={mesh_object.scale.x:.6f},{mesh_object.scale.y:.6f},{mesh_object.scale.z:.6f}")
print(f"OBJECT_LOCATION={mesh_object.location.x:.6f},{mesh_object.location.y:.6f},{mesh_object.location.z:.6f}")
