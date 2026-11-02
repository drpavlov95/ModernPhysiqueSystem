import sys

import bpy
from math import pi
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view


obj_path = sys.argv[sys.argv.index("--") + 1]
output_path = sys.argv[sys.argv.index("--") + 2]

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=obj_path)
mesh_object = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
mesh = mesh_object.data

material = bpy.data.materials.new("Preview skin")
material.diffuse_color = (0.62, 0.42, 0.32, 1.0)
mesh_object.data.materials.clear()
mesh_object.data.materials.append(material)

world_vertices = [mesh_object.matrix_world @ vertex.co for vertex in mesh.vertices]
minimum = Vector((min(v.x for v in world_vertices), min(v.y for v in world_vertices), min(v.z for v in world_vertices)))
maximum = Vector((max(v.x for v in world_vertices), max(v.y for v in world_vertices), max(v.z for v in world_vertices)))
center = (minimum + maximum) / 2
print("BOUNDS_MIN=%.3f,%.3f,%.3f" % tuple(minimum))
print("BOUNDS_MAX=%.3f,%.3f,%.3f" % tuple(maximum))

def look_at(obj, target):
    obj.rotation_euler = (target - obj.location).to_track_quat("-Z", "Y").to_euler()

bpy.ops.object.camera_add(location=(center.x, center.y, maximum.z + (maximum.z - minimum.z) * 1.5))
camera = bpy.context.object
camera.data.type = "ORTHO"
camera.data.ortho_scale = (maximum.y - minimum.y) * 1.10
look_at(camera, center)
camera.rotation_euler.rotate_axis("Z", pi)
bpy.context.scene.camera = camera

projected = [world_to_camera_view(bpy.context.scene, camera, vertex) for vertex in world_vertices]
print("CAMERA=%.3f,%.3f,%.3f" % tuple(camera.location))
print("TARGET=%.3f,%.3f,%.3f" % tuple(center))
print("PROJECTED_X=%.3f,%.3f" % (min(point.x for point in projected), max(point.x for point in projected)))
print("PROJECTED_Y=%.3f,%.3f" % (min(point.y for point in projected), max(point.y for point in projected)))
print("PROJECTED_Z=%.3f,%.3f" % (min(point.z for point in projected), max(point.z for point in projected)))

bpy.ops.object.light_add(type="AREA", location=(center.x - 4.0, center.y + 1.0, maximum.z + 6.0))
key = bpy.context.object
key.data.energy = 900
key.data.shape = "DISK"
key.data.size = 5
look_at(key, center)

bpy.ops.object.light_add(type="AREA", location=(center.x + 4.0, center.y - 2.0, maximum.z + 3.0))
fill = bpy.context.object
fill.data.energy = 500
fill.data.size = 4
look_at(fill, center)

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.studio_light = "rim.sl"
scene.display.shading.color_type = "MATERIAL"
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "WORLD"
scene.display.shading.cavity_ridge_factor = 1.5
scene.display.shading.cavity_valley_factor = 1.5
scene.render.resolution_x = 700
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = output_path
scene.render.film_transparent = False
scene.display.shading.background_type = "WORLD"
scene.display.shading.background_color = (0.035, 0.035, 0.035)
bpy.ops.wm.save_as_mainfile(filepath=output_path + ".blend")
bpy.ops.render.render(write_still=True)
