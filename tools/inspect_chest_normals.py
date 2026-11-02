import sys

import bpy


obj_path = sys.argv[sys.argv.index("--") + 1]
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=obj_path)
mesh_object = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
mesh = mesh_object.data
print("OBJECT_BOUNDS")
print("X=%.4f,%.4f" % (min(v.co.x for v in mesh.vertices), max(v.co.x for v in mesh.vertices)))
print("Y=%.4f,%.4f" % (min(v.co.y for v in mesh.vertices), max(v.co.y for v in mesh.vertices)))
print("Z=%.4f,%.4f" % (min(v.co.z for v in mesh.vertices), max(v.co.z for v in mesh.vertices)))

selected = [vertex for vertex in mesh.vertices if 4.1 <= vertex.co.z <= 6.4 and abs(vertex.co.x) < 1.8]
print("CHEST_VERTICES=%d" % len(selected))
print("CHEST_Y_MIN=%.4f" % min(vertex.co.y for vertex in selected))
print("CHEST_Y_MAX=%.4f" % max(vertex.co.y for vertex in selected))
print("CHEST_NORMAL_Y_MIN=%.4f" % min(vertex.normal.y for vertex in selected))
print("CHEST_NORMAL_Y_MAX=%.4f" % max(vertex.normal.y for vertex in selected))
for vertex in sorted(selected, key=lambda item: item.co.y, reverse=True)[:20]:
    print("V=%d POS=%.4f,%.4f,%.4f NORMAL=%.4f,%.4f,%.4f" % (vertex.index, vertex.co.x, vertex.co.y, vertex.co.z, vertex.normal.x, vertex.normal.y, vertex.normal.z))
