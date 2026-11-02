import sys
from math import exp

import bpy
from mathutils import Vector


arguments = sys.argv[sys.argv.index("--") + 1 :]
source_path, output_path = arguments

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=source_path)
mesh_object = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
mesh = mesh_object.data

# The input uses Z as body height and Y as front/back.  The narrow, symmetric
# bands create anatomical rib relief without changing topology or vertex order.
mesh.update(calc_edges=True)
rib_heights = (6.55, 6.16, 5.79, 5.45, 5.12, 4.80)
changed = 0
maximum_displacement = 0.0

for vertex in mesh.vertices:
    position = vertex.co
    x, y, z = position.x, position.y, position.z

    front_weight = exp(-((y - 0.64) / 0.32) ** 2)
    side_weight = exp(-((abs(x) - 0.78) / 0.58) ** 2)
    torso_weight = exp(-((abs(x) - 0.78) / 1.05) ** 4)
    sternum_fade = 1.0 - exp(-(abs(x) / 0.18) ** 2)
    lower_chest_fade = 1.0 / (1.0 + exp((4.58 - z) / 0.12))
    upper_chest_fade = 1.0 / (1.0 + exp((z - 6.82) / 0.12))

    band = 0.0
    for index, height in enumerate(rib_heights):
        width = 0.105 + index * 0.005
        band += exp(-((z - height) / width) ** 2)

    displacement = 0.028 * band * front_weight * torso_weight * sternum_fade * lower_chest_fade * upper_chest_fade
    displacement += 0.010 * band * front_weight * side_weight * lower_chest_fade * upper_chest_fade
    if displacement <= 0.00002:
        continue

    normal = vertex.normal.normalized()
    # Keep the sculpt on the front-facing torso.  The normal direction avoids
    # making a world-axis assumption and preserves the mesh's local form.
    direction = max(normal.y, 0.0)
    actual = displacement * direction
    vertex.co += normal * actual
    changed += 1
    maximum_displacement = max(maximum_displacement, actual)

bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode="OBJECT")

bpy.ops.wm.obj_export(
    filepath=output_path,
    export_materials=True,
    export_uv=True,
    export_normals=True,
    export_triangulated_mesh=False,
)

print("VERTICES=%d" % len(mesh.vertices))
print("POLYGONS=%d" % len(mesh.polygons))
print("CHANGED_VERTICES=%d" % changed)
print("MAX_DISPLACEMENT=%.6f" % maximum_displacement)
