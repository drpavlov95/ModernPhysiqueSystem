import json
import os
import sys

import bpy
from mathutils import Vector


args = sys.argv[sys.argv.index("--") + 1 :]
nif_path, obj_path, report_path = args

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.preferences.addon_enable(module="io_scene_niftools")
bpy.ops.import_scene.nif(
    filepath=nif_path,
    process="GEOMETRY_ONLY",
    animation=False,
    use_custom_normals=False,
    use_embedded_texture=False,
)

mesh_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
if not mesh_objects:
    raise RuntimeError(f"No mesh objects imported from {nif_path}")

body = max(mesh_objects, key=lambda obj: len(obj.data.vertices))
world_vertices = [body.matrix_world @ vertex.co for vertex in body.data.vertices]
minimum = Vector(tuple(min(vertex[i] for vertex in world_vertices) for i in range(3)))
maximum = Vector(tuple(max(vertex[i] for vertex in world_vertices) for i in range(3)))

os.makedirs(os.path.dirname(obj_path), exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.wm.obj_export(
    filepath=obj_path,
    export_selected_objects=True,
    export_materials=False,
    export_uv=True,
    export_normals=True,
    apply_modifiers=False,
)

report = {
    "source": nif_path,
    "selected": body.name,
    "vertices": len(body.data.vertices),
    "polygons": len(body.data.polygons),
    "loops": len(body.data.loops),
    "uv_layers": len(body.data.uv_layers),
    "bounds_min": list(minimum),
    "bounds_max": list(maximum),
    "objects": [
        {
            "name": obj.name,
            "vertices": len(obj.data.vertices),
            "polygons": len(obj.data.polygons),
            "uv_layers": len(obj.data.uv_layers),
        }
        for obj in sorted(mesh_objects, key=lambda item: len(item.data.vertices), reverse=True)
    ],
}

with open(report_path, "w", encoding="utf-8") as report_file:
    json.dump(report, report_file, indent=2)

print("MPP_BODY_REPORT=" + json.dumps(report))
