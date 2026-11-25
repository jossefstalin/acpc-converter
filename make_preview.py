from pathlib import Path
import sys,math,bpy,os
from mathutils import Vector
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'toolkit'))
import blender_addon as addon
PRE=ROOT/'deliverables/OBJ_Models/Previews';PRE.mkdir(exist_ok=True)
def reset():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for c in list(bpy.data.collections):
        if c.name!='Collection':bpy.data.collections.remove(c)
def setup(obs,cockpit=False):
    scene=bpy.context.scene
    pts=[o.matrix_world@Vector(c) for o in obs for c in o.bound_box if o.type=='MESH']
    low=Vector([min(p[i] for p in pts) for i in range(3)]);high=Vector([max(p[i] for p in pts) for i in range(3)]);center=(low+high)/2;extent=max(high-low)
    camdata=bpy.data.cameras.new('Preview camera');cam=bpy.data.objects.new('Preview camera',camdata);scene.collection.objects.link(cam);scene.camera=cam
    direction=Vector((.9,-1.25,.75)) if not cockpit else Vector((.8,-1.5,.8))
    cam.location=center+direction*extent
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();camdata.type='ORTHO';camdata.ortho_scale=extent*1.3
    world=bpy.data.worlds.new('Studio');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.22,.24,.28,1);world.node_tree.nodes['Background'].inputs[1].default_value=.8;scene.world=world
    for pos,power,size in [((.5,-.8,1.8),1500,1.),((-1.,.5,1.),1100,1.)]:
        ld=bpy.data.lights.new('Softbox','AREA');lo=bpy.data.objects.new('Softbox',ld);scene.collection.objects.link(lo);lo.location=center+Vector(pos)*extent;lo.rotation_euler=(center-lo.location).to_track_quat('-Z','Y').to_euler();ld.energy=power*extent**2/15;ld.shape='DISK';ld.size=extent*size
    scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=8;scene.cycles.use_denoising=False
    scene.render.resolution_x=840;scene.render.resolution_y=590;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
    scene.view_settings.view_transform='Standard';scene.view_settings.look='Medium High Contrast' if 'Medium High Contrast' in [x.name for x in []] else 'None'
    scene.render.film_transparent=False
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=extent*1.8;area.spaces.active.region_3d.view_location=center;area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion();area.spaces.active.shading.type='MATERIAL'
    return cam

renders=[
 ('BF109_body',ROOT/'analysis/ki45/ki45/1.ini',False),
 ('BF109_legacy_cockpit',ROOT/'analysis/ki45/ki45/cockpit.bmf',True),
 ('BF109_supplied_MSH',ROOT/'analysis/ki45/ki45/cockpit.msh',True),
 ('DB3_legacy_cockpit',ROOT/'analysis/db3/db3/cockpit.bmf',True),
 ('FW190_body',ROOT/'analysis/fw190/1.ini',False),
 ('Pacific_Avenger_cockpit',ROOT/'analysis/avenger/cockpit.msh',True),
]
for name,path,cp in renders:
    if os.environ.get('AC_PREVIEW_ONLY') and name not in os.environ['AC_PREVIEW_ONLY'].split(','):continue
    reset();col=bpy.data.collections.new(name);bpy.context.scene.collection.children.link(col)
    if path.suffix=='.ini':obs=addon.import_ini(path,col,False)
    elif path.suffix=='.bmf':obs=addon.import_bmf(path,col,frames=False)
    else:obs=addon.import_msh(path,col)
    setup(obs,cp);bpy.context.scene.render.filepath=str(PRE/(name+'.png'));bpy.ops.render.render(write_still=True)
    if name=='BF109_body':
        # Openable viewer example with embedded textures; works without the add-on too.
        for image in bpy.data.images:
            if image.source=='FILE' and not image.packed_file:
                try:image.pack()
                except:pass
        bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'deliverables/OBJ_Models/BF109_Viewer_Demo.blend'),compress=True)
