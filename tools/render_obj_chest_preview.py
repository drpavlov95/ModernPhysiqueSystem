import sys
from math import pi

import bpy
from mathutils import Vector


obj_path = sys.argv[sys.argv.index("--") + 1]
output_path = sys.argv[sys.argv.index("--") + 2]

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.obj_import(filepath=obj_path)
mesh_object = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")

material = bpy.data.materials.new("Chest preview")
material.diffuse_color = (0.6, 0.42, 0.32, 1.0)
mesh_object.data.materials.clear()
mesh_object.data.materials.append(material)

target = Vector((0.0, 0.0, 5.70))
camera_location = Vector((0.0, 0.0, 12.0))

def look_at(obj, point):
    obj.rotation_euler = (point - obj.location).to_track_quat("-Z", "Y").to_euler()

bpy.ops.object.camera_add(location=camera_location)
camera = bpy.context.object
camera.data.type = "ORTHO"
camera.data.ortho_scale = 3.15
look_at(camera, target)
bpy.context.scene.camera = camera

for location, energy, size in [((-2.8, -1.5, 7.4), 1000, 3.0), ((2.8, -2.2, 5.0), 500, 3.0)]:
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.size = size
    look_at(light, target)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x = 800
scene.render.resolution_y = 800
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = output_path
scene.world.color = (0.025, 0.025, 0.025)
bpy.ops.render.render(write_still=True)
