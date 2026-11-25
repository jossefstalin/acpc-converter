bl_info = {'name':'Air Conflicts Model Workshop','author':'OpenAI / user-directed format research','version':(0,1,0),'blender':(3,6,0),'location':'File > Import/Export; View3D > Sidebar > Air Conflicts','description':'View BMF/MSH, keep two UV sets, export static MSH and vertex-animated BMF','category':'Import-Export'}
import bpy, os, re, math, struct, hashlib
from pathlib import Path
from bpy.props import StringProperty, BoolProperty, IntProperty, EnumProperty
from bpy_extras.io_utils import ImportHelper, ExportHelper
from mathutils import Matrix, Vector
from . import ac_formats as ac

C = Matrix(((1,0,0),(0,0,1),(0,1,0)))

def geometry_signature(me):
    h=hashlib.sha256()
    for v in me.vertices:h.update(struct.pack('<3f',*v.co))
    for p in me.polygons:h.update(struct.pack('<I',len(p.vertices)));h.update(struct.pack('<'+'I'*len(p.vertices),*p.vertices))
    return h.hexdigest()


def material(name, props, folder):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    nodes=mat.node_tree.nodes;links=mat.node_tree.links;bs=nodes.get('Principled BSDF')
    bs.inputs['Roughness'].default_value=.65
    for key in ['DIFFUSE','BUMP','SPECULAR','LIGHT']:
        ref=props.get(key)
        if not ref:continue
        p=Path(folder)/Path(ref.replace('\\','/')).name
        mat['ac_'+key.lower()]=ref
        if not p.exists():continue
        image=bpy.data.images.load(str(p),check_existing=True)
        tex=nodes.new('ShaderNodeTexImage');tex.image=image;tex.label=key;tex.name=key
        tex.location=(-600, {'DIFFUSE':280,'BUMP':-20,'SPECULAR':-300,'LIGHT':-550}[key]);tex.extension='REPEAT'
        if key=='DIFFUSE':
            links.new(tex.outputs['Color'],bs.inputs['Base Color'])
            # Legacy cockpit DDS alpha is not assumed to be opacity. Preserve it in the image.
            if 'glass' in name.lower():
                links.new(tex.outputs['Alpha'],bs.inputs['Alpha'])
                if hasattr(mat,'blend_method'):mat.blend_method='HASHED'
        elif key=='BUMP':
            image.colorspace_settings.name='Non-Color';normal=nodes.new('ShaderNodeNormalMap');normal.location=(-250,-20);links.new(tex.outputs['Color'],normal.inputs['Color']);links.new(normal.outputs['Normal'],bs.inputs['Normal'])
        elif key=='SPECULAR':
            image.colorspace_settings.name='Non-Color'
            socket=bs.inputs.get('Specular IOR Level') or bs.inputs.get('Specular')
            if socket:links.new(tex.outputs['Color'],socket)
        elif key=='LIGHT':
            uv=nodes.new('ShaderNodeUVMap');uv.uv_map='Lightmap';uv.location=(-850,-550);links.new(uv.outputs['UV'],tex.inputs['Vector'])
            # Lightmap is retained on UV1. It is not an emissive texture.
    return mat


def mesh_object(name,lod,mats,collection):
    positions=[C@Vector(p) for p in lod.arrays['position']]
    faces=[];windings=[]
    for i in range(0,len(lod.indices),3):
        ids=lod.indices[i:i+3];a=ac.face_alignment(lod,ids);windings.append(1 if a>0 else (-1 if a<0 else 0))
        faces.append([ids[0],ids[2],ids[1]] if a>0 else ids)
    me=bpy.data.meshes.new(name);me.from_pydata(positions,[],faces);me.update()
    ob=bpy.data.objects.new(name,me);collection.objects.link(ob)
    wind=me.attributes.new(name='AC_SourceWinding',type='INT',domain='FACE')
    for i,w in enumerate(windings):wind.data[i].value=w
    for m in mats:me.materials.append(m)
    for key,uvname in [('uv0','UVMap'),('uv1','Lightmap')]:
        if key in lod.arrays:
            layer=me.uv_layers.new(name=uvname)
            for loop in me.loops:
                u,v=lod.arrays[key][loop.vertex_index];layer.data[loop.index].uv=(u,1-v)
    for start,count,mi,_ in lod.groups:
        for i in range(start//3,(start+count)//3):me.polygons[i].material_index=mi
    if 'normal' in lod.arrays:
        ns=[C@Vector(lod.arrays['normal'][lo.vertex_index]) for lo in me.loops]
        for p in me.polygons:p.use_smooth=True
        if hasattr(me,'use_auto_smooth'):me.use_auto_smooth=True
        me.normals_split_custom_set(ns)
        exact=me.attributes.new(name='AC_SourceNormal',type='FLOAT_VECTOR',domain='CORNER')
        for i,n in enumerate(ns):exact.data[i].vector=n
        me['ac_geometry_signature']=geometry_signature(me)
    ob['ac_source_mask']=lod.mask
    return ob


def import_msh(path,collection):
    model=ac.read_msh(path);maps=ac.parse_materials(Path(path).with_suffix('.mtl'))
    # Converted cockpit_acpc.msh has a matching .mtl. Original cockpit.msh too.
    mats=[material(n,maps.get(n,{}),Path(path).parent) for n in model.materials];out=[]
    c4=C.to_4x4()
    for obj in model.objects:
        for level,lod in enumerate(obj.lods):
            ob=mesh_object(obj.name+(f'_LOD{level}' if level else ''),lod,mats,collection)
            native=Matrix([struct.unpack_from('<4f',obj.header,140+i*16) for i in range(4)]).transposed()
            ob.matrix_world=c4@native@c4;ob.hide_render=level>0;ob.hide_set(level>0);out.append(ob)
            ob['ac_source_format']='MSH'
    return out


def import_bmf(path,collection,texture=None,frames=True):
    model=ac.read_bmf(path);fr=model.frames[0];p=[];n=[];uv=[];idx=[];src=[];cache={}
    for fi,face in enumerate(model.faces):
        for j in range(3):
            key=(face[j],face[j+3],fr.normals[fi*3+j])
            if key not in cache:
                cache[key]=len(p);src.append(face[j]);p.append(fr.positions[face[j]]);uv.append(model.uv[face[j+3]]);n.append(fr.normals[fi*3+j])
            idx.append(cache[key])
    lod=ac.Lod(idx,{'position':p,'normal':n,'uv0':uv},[(0,len(idx),0,())],0x10a)
    props={'DIFFUSE':texture or '1.dds'}
    if Path(path).stem.startswith('cockpit'):
        props={'DIFFUSE':'cockpitlight.dds' if Path(path).stem=='cockpitlight' else 'cockpit.dds'}
    mat=material(Path(path).stem,props,Path(path).parent);ob=mesh_object(Path(path).stem,lod,[mat],collection)
    ob['ac_source_file']=str(path);ob['ac_bmf_frames']=len(model.frames)
    if frames and len(model.frames)>1:
        ob.shape_key_add(name='Basis')
        for i,frame in enumerate(model.frames):
            key=ob.shape_key_add(name=f'Frame_{i:03d}')
            for k,v in enumerate(src):key.data[k].co=C@Vector(frame.positions[v])
            for t,value in [(i,1.),(i-1,0.),(i+1,0.)]:key.value=value;key.keyframe_insert('value',frame=t)
        ob.data.shape_keys.use_relative=True
        action=ob.data.shape_keys.animation_data.action
        if action and hasattr(action,'fcurves'):
            for fc in action.fcurves:
                for kp in fc.keyframe_points:kp.interpolation='LINEAR'
        bpy.context.scene.frame_start=0;bpy.context.scene.frame_end=len(model.frames)-1;bpy.context.scene.frame_set(0)
    return [ob]


def import_ini(path,collection,frames):
    path=Path(path);entries=ac.ini_lines(path);folder=path.parent;out=[]
    texture=next((v for k,v in entries if k=='TEXTURE'),'1.dds')
    tags=['MODEL1','DIRECT1A','DIRECT1B','DIRECT2A','DIRECT2B','DIRECT3','GLASS','WHEELS','PILOT','COCKPITLOW','COCKPITLOW2']
    for k,ref in entries:
        if k not in tags or not ref.lower().endswith('.bmf'):continue
        p=folder/Path(ref).name
        if not p.exists():continue
        obs=import_bmf(p,collection,texture,frames)
        for ob in obs:ob['ac_part']=k
        out+=obs
    lines=path.read_text(errors='replace').splitlines()
    screws=[]
    for i,line in enumerate(lines):
        if line.strip().upper().startswith('NUMSCREWPOINTS '):
            count=int(line.split()[1]);screws=[tuple(map(float,lines[i+1+j].split())) for j in range(count)];break
    for k,ref in entries:
        if k not in ('SCREWHI','SCREWCON') or not ref.lower().endswith('.bmf'):continue
        p=folder/Path(ref).name
        if not p.exists():continue
        for j,off in enumerate(screws):
            obs=import_bmf(p,collection,texture,False)
            for ob in obs:ob.location=C@Vector(off);ob.name=f'{k}_{j}';ob['ac_part']=k
            out+=obs
    for tower in ac.ini_towers(path):
        for k,ref in tower['parts']:
            p=folder/Path(ref).name
            if not p.exists():continue
            obs=import_bmf(p,collection,texture,False)
            for ob in obs:ob.location=C@Vector(tower['offset']);ob.name=f'TOWER{tower["id"]}_{k}';ob['ac_part']=k
            out+=obs
    return out


def gather_selected(context):
    obs=[o for o in context.selected_objects if o.type=='MESH']
    ac.require(bool(obs),'Select at least one mesh')
    return sorted(obs,key=lambda o:o.name)


def snapshot(ob, depsgraph):
    evaluated=ob.evaluated_get(depsgraph);me=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=depsgraph)
    try:
        me.calc_loop_triangles()
        if hasattr(me,'calc_normals_split'):me.calc_normals_split()
        ac.require(me.uv_layers.active is not None,ob.name+': missing UV map')
        p=[];n=[];u=[];u2=[];mats=[];indices=[];lookup={};uv=me.uv_layers[0];light=me.uv_layers.get('Lightmap') or (me.uv_layers[1] if len(me.uv_layers)>1 else None)
        unchanged=geometry_signature(me)==ob.data.get('ac_geometry_signature')
        source_normals=me.attributes.get('AC_SourceNormal') if unchanged else None
        source_wind=me.attributes.get('AC_SourceWinding') if unchanged and ob.get('ac_source_format')=='MSH' else None
        windings=[]
        mat=ob.matrix_world;nm=mat.to_3x3().inverted().transposed();extra_reflection=mat.to_3x3().determinant()<0
        for tri in me.loop_triangles:
            loops=list(tri.loops)
            if extra_reflection:loops=[loops[0],loops[2],loops[1]]
            for li in loops:
                loop=me.loops[li];pos=tuple(C@(mat@me.vertices[loop.vertex_index].co))
                source_no=source_normals.data[li].vector if source_normals else loop.normal
                no=tuple(C@(nm@source_no)) if source_normals else tuple((C@(nm@source_no)).normalized())
                a=tuple(uv.data[li].uv);b=tuple(light.data[li].uv) if light else (.5,.5)
                row=(pos,no,(a[0],1-a[1]),(b[0],1-b[1]),tri.material_index)
                if row not in lookup:
                    lookup[row]=len(p);p.append(pos);n.append(no);u.append(row[2]);u2.append(row[3])
                indices.append(lookup[row])
            mats.append(tri.material_index)
            windings.append(source_wind.data[tri.polygon_index].value if source_wind else 1)
        return p,n,u,u2,indices,mats,windings
    finally:evaluated.to_mesh_clear()


def create_lod(ob,context,msh_mode=False):
    p,n,u,u2,idx,mids,windings=snapshot(ob,context.evaluated_depsgraph_get())
    if msh_mode:
        for fi,w in enumerate(windings):
            if w>0:idx[fi*3+1],idx[fi*3+2]=idx[fi*3+2],idx[fi*3+1]
    t=[(0.,0.,0.)]*len(p);bt=list(t)
    for i in range(0,len(idx),3):
        js=idx[i:i+3];tri=[p[j] for j in js];us=[u[j] for j in js]
        for j in js:t[j],bt[j]=ac.basis_for_triangle(tri,us,n[j])
    # Stable grouping by material, without changing the render vertex channels.
    groups=[];ordered=[]
    for m in sorted(set(mids)):
        start=len(ordered)
        for fi,mi in enumerate(mids):
            if mi==m:ordered.extend(idx[fi*3:fi*3+3])
        groups.append((start,len(ordered)-start,m,()))
    return ac.Lod(ordered,{'position':p,'normal':n,'tangent':t,'bitangent':bt,'uv0':u,'uv1':u2},groups)


def build_msh(context,obs):
    names=[];objects=[]
    for ob in obs:
        lod=create_lod(ob,context,msh_mode=True);slots=list(ob.data.materials)
        if not slots:slots=[None]
        remap={}
        for i,m in enumerate(slots):
            name=re.sub(r'[^A-Za-z0-9_-]','_',m.name if m else 'default')
            if name not in names:names.append(name)
            remap[i]=names.index(name)
        lod.groups=[(s,n,remap[m],pal) for s,n,m,pal in lod.groups]
        result=ac.make_msh(lod,names,re.sub(r'[^A-Za-z0-9_-]','_',ob.name));objects.extend(result.objects)
    return ac.Msh(names,objects)


def find_color_image(mat):
    if mat is None or not mat.use_nodes:return None
    nodes=mat.node_tree.nodes
    exact=next((n.image for n in nodes if n.type=='TEX_IMAGE' and n.image and (n.name=='DIFFUSE' or n.label=='DIFFUSE')),None)
    if exact:return exact
    bs=next((n for n in nodes if n.type=='BSDF_PRINCIPLED'),None)
    todo=[ln.from_node for ln in bs.inputs['Base Color'].links] if bs else [];seen=set()
    while todo:
        node=todo.pop()
        if node in seen:continue
        seen.add(node)
        if node.type=='TEX_IMAGE' and node.image:return node.image
        todo.extend(link.from_node for socket in node.inputs for link in socket.links)
    return None


def image_to_dds(image,path):
    import numpy as np
    w,h=image.size;ac.require(w>0 and h>0,'Empty texture image')
    pix=np.empty(w*h*4,dtype=np.float32);image.pixels.foreach_get(pix)
    pix=pix.reshape(h,w,4)[::-1,:,:].copy();levels=[]
    while True:
        levels.append(np.clip(pix*255+.5,0,255).astype(np.uint8).tobytes())
        ph,pw=pix.shape[:2]
        if ph==1 and pw==1:break
        nh,nw=max(1,ph//2),max(1,pw//2)
        # Box mip filter; dimensions may be non-powers of two.
        ys=np.minimum(np.arange(nh)*2,ph-1);xs=np.minimum(np.arange(nw)*2,pw-1)
        pix=(pix[ys[:,None],xs]+pix[np.minimum(ys+1,ph-1)[:,None],xs]+pix[ys[:,None],np.minimum(xs+1,pw-1)]+pix[np.minimum(ys+1,ph-1)[:,None],np.minimum(xs+1,pw-1)])*.25
    head=bytearray(128);head[:4]=b'DDS '
    struct.pack_into('<7I',head,4,124,0x100f|(0x20000 if len(levels)>1 else 0),h,w,w*4,0,len(levels) if len(levels)>1 else 0)
    struct.pack_into('<8I',head,76,32,0x41,0,32,0x000000ff,0x0000ff00,0x00ff0000,0xff000000)
    struct.pack_into('<I',head,108,0x1000|(0x400008 if len(levels)>1 else 0))
    data=bytes(head)+b''.join(levels);ac.dds_info(data);Path(path).write_bytes(data)


class AC_OT_atlas(bpy.types.Operator,ExportHelper):
    bl_idname='object.air_conflicts_atlas';bl_label='Make color atlas + DDS';bl_options={'UNDO'};filename_ext='.dds'
    filter_glob:StringProperty(default='*.dds',options={'HIDDEN'})
    resolution:EnumProperty(name='Atlas resolution',items=[('1024','1024',''),('2048','2048',''),('4096','4096','')],default='2048')
    def execute(self,context):
        try:
            import numpy as np
            obs=gather_selected(context);ac.require(len(obs)==1,'Select one joined mesh for the color atlas');ob=obs[0];me=ob.data
            ac.require(me.uv_layers.active is not None,'Mesh needs UVs');layer=me.uv_layers.active;used=sorted({p.material_index for p in me.polygons})
            ac.require(len(used)<=64,'Atlas supports up to 64 material regions')
            res=int(self.resolution);grid=math.ceil(math.sqrt(len(used)));tile=res//grid;pad=4;inner=tile-pad*2
            atlas=np.ones((res,res,4),dtype=np.float32);atlas[:,:,:3]=.1;mapping={}
            for i,mi in enumerate(used):
                loops=[li for p in me.polygons if p.material_index==mi for li in p.loop_indices]
                coords=np.array([layer.data[li].uv[:] for li in loops]);low=np.floor(coords.min(axis=0));high=np.maximum(low+1,np.ceil(coords.max(axis=0)))
                span=high-low;cx=(i%grid)*tile;cy=(i//grid)*tile
                mat=me.materials[mi] if mi<len(me.materials) else None;image=find_color_image(mat)
                if image:
                    iw,ih=image.size;buf=np.empty(iw*ih*4,dtype=np.float32);image.pixels.foreach_get(buf);buf=buf.reshape(ih,iw,4)
                    us=low[0]+(np.arange(tile)-pad+.5)/inner*span[0];vs=low[1]+(np.arange(tile)-pad+.5)/inner*span[1]
                    xs=(np.floor(us*iw).astype(int)%iw);ys=(np.floor(vs*ih).astype(int)%ih)
                    atlas[cy:cy+tile,cx:cx+tile]=buf[ys[:,None],xs]
                else:
                    color=tuple(mat.diffuse_color) if mat else (.8,.8,.8,1.)
                    atlas[cy:cy+tile,cx:cx+tile]=color
                for li in loops:
                    uv=layer.data[li].uv;uv.x=(cx+pad+(uv.x-low[0])/span[0]*inner)/res;uv.y=(cy+pad+(uv.y-low[1])/span[1]*inner)/res
            image=bpy.data.images.new(ob.name+'_atlas',width=res,height=res,alpha=True);image.pixels.foreach_set(atlas.ravel());image.update();image_to_dds(image,self.filepath)
            loaded=bpy.data.images.load(self.filepath,check_existing=False);new=material(ob.name+'_atlas',{'DIFFUSE':str(Path(self.filepath).name)},Path(self.filepath).parent)
            me.materials.clear();me.materials.append(new)
            for p in me.polygons:p.material_index=0
            self.report({'INFO'},'Color atlas and DDS created. Base color only; UV1 is retained.');return {'FINISHED'}
        except Exception as e:self.report({'ERROR'},str(e));return {'CANCELLED'}


def write_game_mtl(path,model,obs,target):
    props={};base=Path(path).parent
    for ob in obs:
        for mat in ob.data.materials:
            if not mat:continue
            name=re.sub(r'[^A-Za-z0-9_-]','_',mat.name);mp={}
            for key,default in [('DIFFUSE',None),('SPECULAR','planes/black.dds'),('BUMP','planes/blankbump.dds'),('LIGHT',f'planes/{target}/_ac_white.dds')]:
                ref=mat.get('ac_'+key.lower(),default)
                if key=='DIFFUSE' and mat.use_nodes:
                    image=find_color_image(mat)
                    if image:
                        p=Path(bpy.path.abspath(image.filepath))
                        name_dds=(re.sub(r'[^A-Za-z0-9_.-]','_',p.stem or image.name)+'.dds')
                        if p.is_file() and p.suffix.lower()=='.dds':data=ac.normalize_dds(p.read_bytes())
                        else:
                            temp=base/'_ac_image_temp.dds';image_to_dds(image,temp);data=temp.read_bytes();temp.unlink()
                        if (base/name_dds).exists():ac.require((base/name_dds).read_bytes()==data,'Texture filename collision: '+name_dds)
                        else:(base/name_dds).write_bytes(data)
                        ref=f'planes/{target}/{name_dds}'
                elif key in ('BUMP','SPECULAR','LIGHT'):
                    tex=next((node for node in mat.node_tree.nodes if node.type=='TEX_IMAGE' and node.image and node.name==key),None) if mat.use_nodes else None
                    if tex:
                        image=tex.image;p=Path(bpy.path.abspath(image.filepath));name_dds=re.sub(r'[^A-Za-z0-9_.-]','_',p.stem or image.name)+'.dds'
                        if p.is_file() and p.suffix.lower()=='.dds':data=ac.normalize_dds(p.read_bytes())
                        else:
                            temp=base/'_ac_image_temp.dds';image_to_dds(image,temp);data=temp.read_bytes();temp.unlink()
                        if (base/name_dds).exists():ac.require((base/name_dds).read_bytes()==data,'Texture filename collision: '+name_dds)
                        else:(base/name_dds).write_bytes(data)
                        ref=f'planes/{target}/{name_dds}'
                if ref:mp[key]=ref
            ac.require(mp.get('DIFFUSE'),f'{mat.name}: no diffuse texture');props[name]=mp
    for name in model.materials:ac.require(name in props,'Assign a material with a DDS diffuse texture')
    lines=[]
    for name,mp in props.items():
        lines+=['$MATERIAL '+name,'===========================']+['$'+key+' '+value for key,value in mp.items()]
        lines+=['$DIFFUSEMAT 1 1 1 1','$SPECULARMAT 1 1 1 1','$AMBIENTMAT 1 1 1 1','$SPECULARPOWER 18','']
    Path(path).write_text('\n'.join(lines),encoding='ascii')
    # DXT1 4x4 opaque white, one mip. Used when the source OBJ has no baked lightmap.
    header=bytearray(128);header[:4]=b'DDS ';struct.pack_into('<7I',header,4,124,0x81007,4,4,8,0,0);struct.pack_into('<2I4s',header,76,32,4,b'DXT1');struct.pack_into('<I',header,108,0x1000)
    (base/'_ac_white.dds').write_bytes(bytes(header)+struct.pack('<HHI',65535,65535,0))


class AC_OT_import(bpy.types.Operator,ImportHelper):
    bl_idname='import_scene.air_conflicts';bl_label='Air Conflicts BMF / MSH / aircraft INI';bl_options={'UNDO'}
    filter_glob:StringProperty(default='*.bmf;*.msh;*.nmf;*.ini',options={'HIDDEN'})
    import_frames:BoolProperty(name='Import BMF animation frames',default=True)
    def execute(self,context):
        try:
            path=Path(self.filepath);col=bpy.data.collections.new('AC_'+path.stem);context.scene.collection.children.link(col)
            if path.suffix.lower()=='.bmf':out=import_bmf(path,col,frames=self.import_frames)
            elif path.suffix.lower()=='.ini':out=import_ini(path,col,self.import_frames)
            else:out=import_msh(path,col)
            bpy.ops.object.select_all(action='DESELECT')
            for ob in out:ob.select_set(True)
            if out:context.view_layer.objects.active=out[0]
            self.report({'INFO'},f'Imported {len(out)} objects');return {'FINISHED'}
        except Exception as e:self.report({'ERROR'},str(e));return {'CANCELLED'}


class AC_OT_export_msh(bpy.types.Operator,ExportHelper):
    bl_idname='export_scene.air_conflicts_msh';bl_label='Air Conflicts cockpit MSH';filename_ext='.msh'
    filter_glob:StringProperty(default='*.msh',options={'HIDDEN'})
    target:StringProperty(name='Game plane folder',default='my_plane')
    def execute(self,context):
        try:
            ac.require(re.fullmatch('[A-Za-z0-9_-]+',self.target) is not None,'Use ASCII letters/numbers for plane folder')
            obs=gather_selected(context);model=build_msh(context,obs);data=model.to_bytes();ac.read_msh(data)
            path=Path(self.filepath);path.parent.mkdir(parents=True,exist_ok=True)
            write_game_mtl(path.with_suffix('.mtl'),model,obs,self.target)
            path.write_bytes(data);self.report({'INFO'},'MSH + game MTL exported; set COCKPIT and COCKPITMATERIAL in Appearance');return {'FINISHED'}
        except Exception as e:self.report({'ERROR'},str(e));return {'CANCELLED'}


class AC_OT_export_bmf(bpy.types.Operator,ExportHelper):
    bl_idname='export_scene.air_conflicts_bmf';bl_label='Air Conflicts body / animated part BMF';filename_ext='.bmf'
    filter_glob:StringProperty(default='*.bmf',options={'HIDDEN'})
    animation:BoolProperty(name='Sample scene frame range',default=False)
    write_mif:BoolProperty(name='Write uniform damage ID MIF',default=False)
    damage_id:IntProperty(name='Damage group ID',default=0,min=0,max=255)
    def execute(self,context):
        old=context.scene.frame_current
        try:
            obs=gather_selected(context);ac.require(len(obs)==1,'BMF exports one object at a time; join static hull parts if needed')
            ob=obs[0];ac.require(len(ob.data.materials)<=1,'BMF has one external texture: bake a color atlas and use one material first')
            frames=range(context.scene.frame_start,context.scene.frame_end+1) if self.animation else [old]
            result=None;topology=None
            for frame in frames:
                context.scene.frame_set(frame);lod=create_lod(ob,context)
                # Deindex render corners: permits moving vertices while keeping frame topology invariant.
                p=[lod.arrays['position'][v] for v in lod.indices];n=[lod.arrays['normal'][v] for v in lod.indices];uv=[lod.arrays['uv0'][v] for v in lod.indices]
                seq=list(range(len(p)));flat=ac.Lod(seq,{'position':p,'normal':n,'uv0':uv},[(0,len(seq),0,())],0x10a)
                model=ac.lod_to_bmf(flat)
                if result is None:result=model;topology=uv
                else:
                    ac.require(len(model.faces)==len(result.faces) and uv==topology,'Animated topology or UVs changed: use stable topology modifiers')
                    result.frames.extend(model.frames)
            data=result.to_bytes();ac.read_bmf(data);Path(self.filepath).write_bytes(data)
            if self.write_mif:Path(self.filepath).with_suffix('.mif').write_bytes(struct.pack('<I',self.damage_id)*len(result.faces))
            self.report({'INFO'},f'BMF exported: {len(result.frames)} frame(s); reuse a matching aircraft INI template');return {'FINISHED'}
        except Exception as e:self.report({'ERROR'},str(e));return {'CANCELLED'}
        finally:context.scene.frame_set(old)


class AC_PT_panel(bpy.types.Panel):
    bl_label='Air Conflicts Workshop 0.1';bl_idname='AC_PT_workshop';bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='Air Conflicts'
    def draw(self,context):
        layout=self.layout;layout.operator(AC_OT_import.bl_idname,text='Open model / aircraft INI')
        layout.separator();layout.label(text='OBJ: File > Import > Wavefront OBJ')
        layout.operator(AC_OT_atlas.bl_idname,text='Build color atlas + DDS')
        layout.operator(AC_OT_export_msh.bl_idname,text='Export cockpit MSH + MTL')
        layout.operator(AC_OT_export_bmf.bl_idname,text='Export body / moving part BMF')
        layout.separator();layout.label(text='UVMap = color; Lightmap = UV1')
        layout.label(text='BMF: one texture atlas per object')
        ob=context.active_object
        if ob and ob.type=='MESH':
            layout.label(text=f'Vertices: {len(ob.data.vertices):,}')
            layout.label(text=f'BMF source frames: {ob.get("ac_bmf_frames",1)}')


classes=[AC_OT_import,AC_OT_atlas,AC_OT_export_msh,AC_OT_export_bmf,AC_PT_panel]
def import_menu(self,context):self.layout.operator(AC_OT_import.bl_idname,text='Air Conflicts (.bmf/.msh/.ini)')
def export_menu(self,context):
    self.layout.operator(AC_OT_export_msh.bl_idname,text='Air Conflicts cockpit (.msh)')
    self.layout.operator(AC_OT_export_bmf.bl_idname,text='Air Conflicts body / animation (.bmf)')
def register():
    for c in classes:bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(import_menu);bpy.types.TOPBAR_MT_file_export.append(export_menu)
def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(export_menu);bpy.types.TOPBAR_MT_file_import.remove(import_menu)
    for c in reversed(classes):bpy.utils.unregister_class(c)
if __name__=='__main__':register()
