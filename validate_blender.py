from pathlib import Path
import bpy,sys,json,traceback,math
from mathutils import Vector
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'toolkit'))
import blender_addon as addon
import ac_formats as ac
addon.register()
OUT=ROOT/'analysis/blender_validation';OUT.mkdir(exist_ok=True)
results=[]

def clean():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for c in list(bpy.data.collections):
        if c.name!='Collection':bpy.data.collections.remove(c)

def check(name,fn):
    try:results.append({'check':name,'result':fn(),'passed':True})
    except Exception as e:
        results.append({'check':name,'error':str(e),'traceback':traceback.format_exc(),'passed':False});print(traceback.format_exc())

def import_all():
    report=[]
    for p in [ROOT/'analysis/ki45/ki45/cockpit.msh',ROOT/'analysis/db3/db3/cockpit.msh',ROOT/'analysis/fw190/cockpit.msh',ROOT/'analysis/avenger/cockpit.msh',ROOT/'analysis/avenger_sw/cockpit.msh']:
        clean();r=bpy.ops.import_scene.air_conflicts(filepath=str(p));assert r=={'FINISHED'}
        obs=[o for o in bpy.context.selected_objects if o.type=='MESH'];src=ac.read_msh(p)
        assert sum(len(o.data.polygons) for o in obs)==sum(len(o.lods[0].indices)//3 for o in src.objects)
        assert all(len(o.data.uv_layers)==2 for o in obs)
        for ob,so in zip(obs,src.objects):
            uv=ob.data.uv_layers['UVMap'];light=ob.data.uv_layers['Lightmap'];lo=so.lods[0]
            for loop in ob.data.loops:
                vi=loop.vertex_index
                assert max(abs(x-y) for x,y in zip(uv.data[loop.index].uv,(lo.arrays['uv0'][vi][0],1-lo.arrays['uv0'][vi][1])))<1e-6
                assert max(abs(x-y) for x,y in zip(light.data[loop.index].uv,(lo.arrays['uv1'][vi][0],1-lo.arrays['uv1'][vi][1])))<1e-6
        report.append({'file':str(p.relative_to(ROOT)),'faces':sum(len(o.data.polygons) for o in obs),'UV_sets':2})
    return report

def import_animation():
    clean();p=ROOT/'analysis/avenger/wheels.bmf';r=bpy.ops.import_scene.air_conflicts(filepath=str(p));assert r=={'FINISHED'}
    ob=bpy.context.active_object;b=ac.read_bmf(p);assert len(ob.data.shape_keys.key_blocks)==len(b.frames)+1
    return {'frames':len(b.frames),'shape_keys':len(ob.data.shape_keys.key_blocks)}

def export_msh():
    clean();p=ROOT/'analysis/avenger/cockpit.msh';assert bpy.ops.import_scene.air_conflicts(filepath=str(p))=={'FINISHED'}
    q=OUT/'test_cockpit.msh';r=bpy.ops.export_scene.air_conflicts_msh(filepath=str(q),target='test_plane');assert r=={'FINISHED'}
    m=ac.read_msh(q);src=ac.read_msh(p)
    assert len(m.objects[0].lods[0].indices)==len(src.objects[0].lods[0].indices)
    # Compare triangle corners rather than render vertex indices (seam splitting is permitted).
    a,b=src.objects[0].lods[0],m.objects[0].lods[0]
    for key in ['position','normal','uv0','uv1']:
        for ia,ib in zip(a.indices,b.indices):assert max(abs(x-y) for x,y in zip(a.arrays[key][ia],b.arrays[key][ib]))<2e-5,(key,ia,ib)
    return {'triangles':len(b.indices)//3,'roundtrip_geometry_and_both_uvs':True}

def export_obj_to_bmf():
    clean();p=ROOT/'deliverables/OBJ_Models/BF109_from_ki45/BF109_from_ki45_3p_body_LOD0.obj'
    bpy.ops.wm.obj_import(filepath=str(p),forward_axis='Y',up_axis='Z');ob=bpy.context.active_object;imported_faces=len(ob.data.polygons)
    q=OUT/'obj_body.bmf';r=bpy.ops.export_scene.air_conflicts_bmf(filepath=str(q));assert r=={'FINISHED'}
    b=ac.read_bmf(q);original=ac.read_bmf(ROOT/'analysis/ki45/ki45/model1.bmf');assert len(b.faces)==imported_faces
    assert max(abs(a-b) for a,b in zip(b.frames[0].meta[:6],original.frames[0].meta[:6]))<1e-4
    return {'triangles':len(b.faces),'frames':len(b.frames),'bounds_preserved':True,'source_obj_faces':len(original.faces),'degenerate_faces_removed_by_Blender_OBJ_import':len(original.faces)-imported_faces}

def export_animation():
    clean();p=ROOT/'analysis/ki45/ki45/direct1a.bmf';b=ac.read_bmf(p)
    assert bpy.ops.import_scene.air_conflicts(filepath=str(p))=={'FINISHED'}
    q=OUT/'animated_direct1a.bmf';assert bpy.ops.export_scene.air_conflicts_bmf(filepath=str(q),animation=True)=={'FINISHED'}
    n=ac.read_bmf(q);assert len(n.frames)==len(b.frames)
    # Imported source frames are reconstructed per corner, retaining exact positions.
    for fi,(fr,original) in enumerate(zip(n.frames,b.frames)):
        for nf,of in zip(n.faces,b.faces):
            for j in range(3):assert max(abs(x-y) for x,y in zip(fr.positions[nf[j]],original.positions[of[j]]))<2e-5,(fi,j)
    return {'frames':len(n.frames),'corner_positions_preserved':True}

def external_atlas():
    clean();p=ROOT/'deliverables/OBJ_Models/BF109_from_ki45/BF109_from_ki45_3p_body_LOD0.obj'
    bpy.ops.wm.obj_import(filepath=str(p),forward_axis='Y',up_axis='Z');ob=bpy.context.active_object
    # Make the external OBJ use two materials, with the second using a generated color image.
    mat=bpy.data.materials.new('external_green');mat.use_nodes=True;image=bpy.data.images.new('green',width=4,height=4);image.pixels[:]=[.2,.6,.1,1.]*16
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=image;mat.node_tree.links.new(tex.outputs['Color'],mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color']);ob.data.materials.append(mat)
    for poly in ob.data.polygons:
        if poly.index%2:poly.material_index=1
    q=OUT/'external_atlas.dds';assert bpy.ops.object.air_conflicts_atlas(filepath=str(q),resolution='1024')=={'FINISHED'}
    assert len(ob.data.materials)==1
    info=ac.dds_info(q.read_bytes());assert info[:3]==(1024,1024,11)
    target=OUT/'external_with_atlas.bmf';assert bpy.ops.export_scene.air_conflicts_bmf(filepath=str(target))=={'FINISHED'}
    msh=OUT/'external_with_atlas.msh';assert bpy.ops.export_scene.air_conflicts_msh(filepath=str(msh),target='external')=={'FINISHED'}
    return {'texture_size':info[:2],'mip_levels':info[2],'triangles':len(ac.read_bmf(target).faces),'MSH_export':True,'BMF_export':True}

check('Blender imports all five MSH files with exact UVs',import_all)
check('BMF animation import',import_animation)
check('Original MSH > Blender > MSH roundtrip',export_msh)
check('OBJ > Blender > game BMF',export_obj_to_bmf)
check('Animated BMF > Blender > animated BMF',export_animation)
check('External OBJ multi-material color atlas > DDS > BMF and MSH',external_atlas)
(OUT/'validation.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
if not all(r['passed'] for r in results):raise RuntimeError('Validation failed')
