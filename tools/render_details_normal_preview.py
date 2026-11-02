import os
import sys

import bpy
from mathutils import Vector


obj_path, normal_path, output_prefix = sys.argv[sys.argv.index("--") + 1 :]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=obj_path, forward_axis="Y", up_axis="Z")
body = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
for polygon in body.data.polygons:
    polygon.use_smooth = True

material = bpy.data.materials.new("Skin preview")
material.use_nodes = True
principled = material.node_tree.nodes.get("Principled BSDF")
principled.inputs["Base Color"].default_value = (0.68, 0.38, 0.25, 1.0)
principled.inputs["Roughness"].default_value = 0.62
principled.inputs["Metallic"].default_value = 0.0

texture = material.node_tree.nodes.new("ShaderNodeTexImage")
texture.image = bpy.data.images.load(normal_path)
texture.image.colorspace_settings.name = "Non-Color"
normal_map = material.node_tree.nodes.new("ShaderNodeNormalMap")
normal_map.space = "OBJECT"
normal_map.inputs["Strength"].default_value = 1.0
material.node_tree.links.new(texture.outputs["Color"], normal_map.inputs["Color"])
material.node_tree.links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])
body.data.materials.clear()
body.data.materials.append(material)


def point_at(obj, target):
    obj.rotation_euler = (target - obj.location).to_track_quat("-Z", "Y").to_euler()


target = Vector((0.0, 0.0, 79.0))
bpy.ops.object.camera_add(location=(0.0, 165.0, 80.0))
camera = bpy.context.object
camera.data.type = "ORTHO"
camera.data.ortho_scale = 78.0
camera.data.clip_end = 500.0
point_at(camera, target)
bpy.context.scene.camera = camera

for location, energy, size in [
    ((-48.0, 85.0, 125.0), 9000.0, 42.0),
    ((52.0, 45.0, 78.0), 6500.0, 36.0),
    ((0.0, -55.0, 105.0), 5000.0, 38.0),
]:
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.size = size
    point_at(light, target)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x = 700
scene.render.resolution_y = 920
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.film_transparent = False
if scene.world is None:
    scene.world = bpy.data.worlds.new("Preview world")
scene.world.use_nodes = True
background = scene.world.node_tree.nodes.get("Background")
background.inputs["Color"].default_value = (0.055, 0.055, 0.055, 1.0)
background.inputs["Strength"].default_value = 0.45
scene.view_settings.look = "AgX - Medium High Contrast"
scene.view_settings.exposure = 0.7

os.makedirs(os.path.dirname(output_prefix), exist_ok=True)
for label, y in (("front", 165.0), ("back", -165.0)):
    camera.location = (0.0, y, 80.0)
    point_at(camera, target)
    scene.render.filepath = output_prefix + f"_{label}.png"
    bpy.ops.render.render(write_still=True)
