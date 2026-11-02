import sys
from math import exp

import bpy


source_path, output_path = sys.argv[sys.argv.index("--") + 1 :]

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=source_path)
mesh = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH").data
mesh.update(calc_edges=True)

rib_heights = (6.55, 6.16, 5.79, 5.45, 5.12, 4.80)
positions = {}

for vertex in mesh.vertices:
    x, y, z = vertex.co
    front_weight = exp(-((y - 0.64) / 0.32) ** 2)
    side_weight = exp(-((abs(x) - 0.78) / 0.58) ** 2)
    torso_weight = exp(-((abs(x) - 0.78) / 1.05) ** 4)
    sternum_fade = 1.0 - exp(-(abs(x) / 0.18) ** 2)
    lower_chest_fade = 1.0 / (1.0 + exp((4.58 - z) / 0.12))
    upper_chest_fade = 1.0 / (1.0 + exp((z - 6.82) / 0.12))

    band = sum(exp(-((z - height) / (0.105 + index * 0.005)) ** 2) for index, height in enumerate(rib_heights))
    amount = (0.014 * band * front_weight * torso_weight * sternum_fade + 0.005 * band * front_weight * side_weight)
    amount *= lower_chest_fade * upper_chest_fade * max(vertex.normal.y, 0.0)
    if amount >= 0.0001:
        positions[vertex.index] = vertex.co + vertex.normal.normalized() * amount

with open(source_path, "r", newline="") as source_file:
    source_lines = source_file.readlines()

vertex_index = 0
with open(output_path, "w", newline="") as output_file:
    for line in source_lines:
        if line.startswith("v "):
            position = positions.get(vertex_index)
            if position is None:
                output_file.write(line)
            else:
                suffix = "\r\n" if line.endswith("\r\n") else "\n"
                output_file.write("v %.6f %.6f %.6f%s" % (position.x, position.y, position.z, suffix))
            vertex_index += 1
        else:
            output_file.write(line)

print("VERTICES=%d" % len(mesh.vertices))
print("POLYGONS=%d" % len(mesh.polygons))
print("CHANGED_VERTICES=%d" % len(positions))
print("MAX_DISPLACEMENT=%.6f" % max((positions[index] - mesh.vertices[index].co).length for index in positions))
