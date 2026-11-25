"""Air Conflicts BMF / B3DMH10 MSH. Python 3.10+, no third-party dependencies.

Native coordinates and D3D texture coordinates are retained internally. The
Blender adapter changes (x,y,z) to (x,z,y) and UV (u,v) to (u,1-v).
"""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
import math, struct, re, json, shutil, argparse, zlib, binascii


def require(test, message):
    if not test:
        raise ValueError(message)


class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def take(self, n):
        require(0 <= n <= len(self.data)-self.pos, f'Truncated file at {self.pos:#x}, need {n}')
        out = self.data[self.pos:self.pos+n]; self.pos += n; return out
    def unpack(self, fmt): return struct.unpack('<'+fmt, self.take(struct.calcsize('<'+fmt)))
    def u32(self): return self.unpack('I')[0]
    def rows(self, fmt, n):
        require(0 <= n <= 10000000, 'Unreasonable array length')
        return list(struct.iter_unpack('<'+fmt, self.take(struct.calcsize('<'+fmt)*n)))


def pack_rows(fmt, rows): return b''.join(struct.pack('<'+fmt, *row) for row in rows)
def name64(s):
    b = s.encode('ascii', 'replace'); require(len(b)<64, 'Name must be <64 ASCII bytes')
    return b.ljust(64, b'\0')
def norm(v, fallback=(0.,1.,0.)):
    q = math.sqrt(sum(x*x for x in v))
    return tuple(x/q for x in v) if q>1e-12 and math.isfinite(q) else fallback
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def face_alignment(lod, ids):
    p=[lod.arrays['position'][i] for i in ids]
    n=lod.arrays.get('normal')
    avg=tuple(sum(n[i][j] for i in ids) for j in range(3)) if n else (0.,0.,0.)
    return dot(cross(sub(p[1],p[0]),sub(p[2],p[0])),avg)
def bounds(p): return tuple(min(v[i] for v in p) for i in range(3))+tuple(max(v[i] for v in p) for i in range(3))
def finite(rows): return all(math.isfinite(x) for row in rows for x in row)
IDENTITY = (1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)


@dataclass
class BmfFrame:
    meta: tuple
    positions: list
    normals: list  # 3 corners per face, not indexed by position
    tangents: list
    planes: list


@dataclass
class Bmf:
    faces: list  # (position0,1,2, uv0,1,2)
    uv: list
    frames: list
    def to_bytes(self):
        n = len(self.faces); nv = len(self.frames[0].positions)
        out = struct.pack('<4I',len(self.frames),n,nv,len(self.uv))
        out += pack_rows('6I', self.faces)+pack_rows('2f',self.uv)
        for f in self.frames:
            require(len(f.positions)==nv and len(f.normals)==n*3 and len(f.tangents)==n*3 and len(f.planes)==n, 'BMF topology changed across frames')
            out += struct.pack('<7f',*f.meta)+pack_rows('3f',f.positions)+pack_rows('3f',f.normals)+pack_rows('3f',f.tangents)+pack_rows('4f',f.planes)
        return out


def read_bmf(path_or_bytes):
    b = path_or_bytes if isinstance(path_or_bytes,bytes) else Path(path_or_bytes).read_bytes()
    r=Reader(b); nf,nt,nv,nu=r.unpack('4I')
    require(0<nf<=10000 and 0<nt<=5000000 and nv>0 and nu>0, 'Invalid BMF header')
    expected=16+24*nt+8*nu+nf*(28+12*nv+88*nt)
    require(expected==len(b), f'Unsupported BMF layout: expected {expected}, got {len(b)}')
    faces=r.rows('6I',nt);uv=r.rows('2f',nu);frames=[]
    require(all(max(f[:3])<nv and max(f[3:])<nu for f in faces),'BMF index out of range')
    for _ in range(nf):
        frames.append(BmfFrame(r.unpack('7f'),r.rows('3f',nv),r.rows('3f',nt*3),r.rows('3f',nt*3),r.rows('4f',nt)))
    require(finite(uv) and all(finite(f.positions) and finite(f.normals) for f in frames),'Non-finite BMF position/normal/UV')
    return Bmf(faces,uv,frames)


@dataclass
class Lod:
    indices: list
    arrays: dict
    groups: list  # start index, number of indices, material index, bone palette tuple
    mask: int = 0x33a
    reserved: int = 0
    trailing: int = 0
    def to_bytes(self):
        nv=len(self.arrays['position']); require(nv<=65536,'MSH uses 16-bit indices: split mesh below 65537 render vertices')
        require(len(self.indices)%3==0 and (not self.indices or max(self.indices)<nv),'Invalid MSH indices')
        payload=struct.pack('<6I',nv,len(self.indices),len(self.groups),self.reserved,self.mask,self.trailing)
        payload+=struct.pack('<'+'H'*len(self.indices),*self.indices)
        for key,bit,fmt in ARRAY_LAYOUT:
            if bit==0 or self.mask&bit:
                require(key in self.arrays and len(self.arrays[key])==nv, 'Missing vertex channel: '+key)
                payload+=pack_rows(fmt,self.arrays[key])
        for start,count,material,palette in self.groups:
            payload+=struct.pack('<IIhh',start,count,material,len(palette))
            if palette: payload+=struct.pack('<'+'H'*len(palette),*palette)
        return struct.pack('<I',len(payload)+12)+payload


ARRAY_LAYOUT=[('position',0,'3f'),('normal',8,'3f'),('tangent',16,'3f'),('bitangent',32,'3f'),
              ('uv0',0x100,'2f'),('uv1',0x200,'2f'),('uv2',0x400,'2f'),('uv3',0x800,'2f'),
              ('weights',0x20000,'4H'),('bone_ids',0x10000,'I')]


@dataclass
class MeshObject:
    header: bytes
    lods: list
    @property
    def name(self): return self.header[8:72].split(b'\0')[0].decode('ascii','replace')
    def to_bytes(self):
        require(len(self.header)==228 and struct.unpack_from('<I',self.header)[0]==0,'Only mesh objects can be written')
        body=struct.pack('<I',len(self.lods))+b''.join(x.to_bytes() for x in self.lods)
        h=bytearray(self.header);struct.pack_into('<I',h,4,len(h)+len(body)+4)
        return bytes(h)+body


@dataclass
class Msh:
    materials: list
    objects: list
    def to_bytes(self):
        body=b''.join(name64(m) for m in self.materials)+b''.join(o.to_bytes() for o in self.objects)
        return b'B3DMH\x0010'+struct.pack('<3I',len(self.materials),len(self.objects),20+len(body))+body


def read_msh(path_or_bytes):
    b=path_or_bytes if isinstance(path_or_bytes,bytes) else Path(path_or_bytes).read_bytes()
    r=Reader(b);require(r.take(8)==b'B3DMH\x0010','Only B3DMH10 MSH is supported')
    nm,no,size=r.unpack('3I');require(size==len(b) and nm<=4096 and no<=10000,'Invalid MSH header')
    mats=[r.take(64).split(b'\0')[0].decode('ascii','replace') for _ in range(nm)];objects=[]
    for _ in range(no):
        header=r.take(228);typ=struct.unpack_from('<I',header)[0]
        require(typ==0,f'MSH object type {typ} is not a static mesh; refusing to discard hierarchy')
        nl=r.u32();require(0<nl<=100,'Invalid MSH LOD count');lods=[]
        for _ in range(nl):
            chunk,nv,ni,ng,res,mask,extra=r.unpack('7I')
            require(mask & ~(0x33a)==0 and mask&2,'Unsupported MSH vertex channels: '+hex(mask))
            require(nv<=65536 and ni%3==0 and ni<=30000000 and ng<=4096,'Invalid MSH counts')
            idx=list(r.unpack('H'*ni)); require(not idx or max(idx)<nv,'MSH index out of range')
            arrays={}
            for key,bit,fmt in ARRAY_LAYOUT:
                if bit==0 or mask&bit: arrays[key]=r.rows(fmt,nv)
            groups=[]
            for _ in range(ng):
                start,count,mat,np=r.unpack('IIhh'); require(np>=0 and start+count<=ni and start%3==0 and count%3==0 and 0<=mat<nm,'Invalid MSH material range')
                groups.append((start,count,mat,r.unpack('H'*np)))
            require(finite(arrays['position']) and finite(arrays.get('normal',[])) and finite(arrays.get('uv0',[])),'Non-finite MSH geometry/UV')
            lods.append(Lod(idx,arrays,groups,mask,res,extra))
        objects.append(MeshObject(header,lods))
    require(r.pos==len(b), f'Unsupported MSH trailer: {len(b)-r.pos} bytes')
    return Msh(mats,objects)


def make_msh(lod, material_names, object_name='cockpit'):
    p=lod.arrays['position'];box=bounds(p)
    # type, in-memory size hint, name, parent id, local/world matrices, bounds
    h=struct.pack('<2I',0,0)+name64(object_name)+struct.pack('<I',65535)+struct.pack('<32f',*(IDENTITY+IDENTITY))+struct.pack('<6f',*box)
    require(len(h)==228,'Internal object header error')
    return Msh(material_names,[MeshObject(h,[lod])])


def basis_for_triangle(p,uv,normal):
    e1,e2=sub(p[1],p[0]),sub(p[2],p[0]);d1,d2=sub(uv[1],uv[0]),sub(uv[2],uv[0]);det=d1[0]*d2[1]-d1[1]*d2[0]
    if abs(det)>1e-12:
        t=tuple((e1[i]*d2[1]-e2[i]*d1[1])/det for i in range(3))
        bit=tuple((e2[i]*d1[0]-e1[i]*d2[0])/det for i in range(3))
    else:
        t=cross((0.,0.,1.) if abs(normal[2])<.9 else (0.,1.,0.),normal);bit=cross(normal,t)
    t=norm(sub(t,tuple(dot(t,normal)*x for x in normal)),(1.,0.,0.))
    return t,norm(bit,(0.,0.,1.))


def bmf_to_msh(model, light=None, material='cockpit', frame=0):
    f=model.frames[frame]
    if light is not None:
        require(len(light.faces)==len(model.faces),'Light model face count differs')
        require(all(a[:3]==b[:3] for a,b in zip(model.faces,light.faces)),'Light model topology differs')
        require(light.frames[0].positions==f.positions,'Light model coordinates differ')
    arrays={k:[] for k in ['position','normal','tangent','bitangent','uv0','uv1']};idx=[];cache={}
    for fi,face in enumerate(model.faces):
        tri=[f.positions[v] for v in face[:3]];uvs=[model.uv[u] for u in face[3:]]
        for j in range(3):
            n=f.normals[fi*3+j];t,bt=basis_for_triangle(tri,uvs,n)
            uv2=light.uv[light.faces[fi][j+3]] if light is not None else (.5,.5)
            row=(tri[j],n,t,bt,uvs[j],uv2)
            if row not in cache:
                cache[row]=len(cache)
                for key,value in zip(arrays,row): arrays[key].append(value)
            idx.append(cache[row])
    # Pacific Avenger MSH uses normals aligned to the native triangle cross product.
    lo=Lod(idx,arrays,[(0,len(idx),0,())])
    for i in range(0,len(idx),3):
        if face_alignment(lo,idx[i:i+3])<0:idx[i+1],idx[i+2]=idx[i+2],idx[i+1]
    return make_msh(lo,[material])


def lod_to_bmf(lod):
    p=lod.arrays['position'];uv=lod.arrays.get('uv0',[(0.,0.)]*len(p));n=lod.arrays.get('normal')
    faces=[];normals=[];tangents=[];planes=[]
    for i in range(0,len(lod.indices),3):
        ids=tuple(lod.indices[i:i+3])
        if face_alignment(lod,ids)>0:ids=(ids[0],ids[2],ids[1])
        tri=[p[j] for j in ids];us=[uv[j] for j in ids]
        gn=norm(tuple(-x for x in cross(sub(tri[1],tri[0]),sub(tri[2],tri[0]))))
        faces.append(ids+ids)
        for j in ids:
            nr=n[j] if n is not None else gn; normals.append(nr); tangents.append(basis_for_triangle(tri,us,nr)[0])
        planes.append(gn+(-dot(gn,tri[0]),))
    box=bounds(p);center=tuple((box[i]+box[i+3])/2 for i in range(3));radius=max(math.sqrt(dot(sub(v,center),sub(v,center))) for v in p)
    return Bmf(faces,list(uv),[BmfFrame(box+(radius,),p,normals,tangents,planes)])


def parse_materials(path):
    result={};cur=None
    if not Path(path).exists(): return result
    for line in Path(path).read_text(errors='replace').splitlines():
        words=line.strip().split(None,1)
        if len(words)!=2:continue
        key,val=words
        if key=='$MATERIAL':cur=result.setdefault(val,{})
        elif cur is not None and key.startswith('$'):cur[key[1:]]=val
    return result


def ini_lines(path,section='Appearance'):
    active=False;out=[]
    for line in Path(path).read_text(errors='replace').splitlines():
        s=line.strip()
        if s.startswith('['):active=s.casefold()=='['+section.casefold()+']';continue
        if active:
            words=s.split(None,1)
            if len(words)==2:out.append((words[0].upper(),words[1]))
    return out


def ini_towers(path):
    towers=[];active=False;current=None
    for line in Path(path).read_text(errors='replace').splitlines():
        s=line.strip()
        if s.startswith('['):active=s.lower()=='[towers]';continue
        if not active:continue
        words=s.split()
        if not words:continue
        if words[0].upper()=='TOWER' and len(words)==2:current={'id':int(words[1]),'offset':(0.,0.,0.),'parts':[]};towers.append(current)
        elif current is not None and words[0].upper()=='POINT' and len(words)==4:current['offset']=tuple(map(float,words[1:]))
        elif current is not None and words[0].upper() in ('CANNONMODEL','TOWERMODEL','GLASSMODEL') and words[-1].lower().endswith('.bmf'):current['parts'].append((words[0].upper(),words[-1]))
    return towers


def write_obj(path, parts, material_maps, axes='blender'):
    """parts: (name, Lod, offset). Always CCW OBJ with normals matching faces."""
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);vo=0
    def xyz(v):return (v[0],v[2],v[1]) if axes=='blender' else v
    with path.open('w',encoding='utf-8',newline='\n') as out:
        out.write('# Air Conflicts export; D3D UV converted to OBJ UV; no decimation\n')
        out.write('mtllib '+path.with_suffix('.mtl').name+'\n')
        for name,lod,offset in parts:
            out.write('o '+name.replace(' ','_')+'\n');a=lod.arrays;nv=len(a['position'])
            for p in a['position']:out.write('v '+' '.join(format(x,'.9g') for x in xyz(tuple(x+y for x,y in zip(p,offset))))+'\n')
            for u,v in a.get('uv0',[(0.,0.)]*nv):out.write(f'vt {u:.9g} {1-v:.9g}\n')
            for n in a.get('normal',[(0.,1.,0.)]*nv):out.write('vn '+' '.join(format(x,'.9g') for x in xyz(n))+'\n')
            for start,count,mat,_ in lod.groups:
                out.write('usemtl '+mat+'\n')
                for i in range(start,start+count,3):
                    ids=lod.indices[i:i+3]
                    alignment=face_alignment(lod,ids)
                    if (axes=='native' and alignment<0) or (axes=='blender' and alignment>0):ids=[ids[0],ids[2],ids[1]]
                    out.write('f '+' '.join(f'{v+vo+1}/{v+vo+1}/{v+vo+1}' for v in ids)+'\n')
            vo+=nv
    lines=['# Portable relative paths. Game shader is not reproduced by OBJ.']
    for name,mp in material_maps.items():
        lines.extend(['newmtl '+name,'Ka 0.15 0.15 0.15','Kd 1 1 1','Ks 0.12 0.12 0.12','Ns 18','d '+str(mp.get('opacity',1)),'illum 2'])
        for key,tag in [('DIFFUSE','map_Kd'),('SPECULAR','map_Ks'),('BUMP','norm')]:
            if mp.get(key):lines.append(tag+' '+mp[key])
        if mp.get('LIGHT'):lines.append('# LIGHT_UV1 '+mp['LIGHT'])
    path.with_suffix('.mtl').write_text('\n'.join(lines)+'\n',encoding='utf-8')


def obj_parts_msh(msh):
    out=[]
    for ob in msh.objects:
        # Original source matrices are retained in MSH. These aircraft have identity matrices.
        matrix=struct.unpack_from('<16f',ob.header,140)
        require(all(abs(x-y)<1e-5 for x,y in zip(matrix,IDENTITY)),'OBJ export of non-identity world matrix requires Blender import')
        lo=ob.lods[0];groups=[(s,n,msh.materials[m],pal) for s,n,m,pal in lo.groups]
        out.append((ob.name,Lod(lo.indices,lo.arrays,groups,lo.mask),(0.,0.,0.)))
    return out


def dds_info(data):
    require(data[:4]==b'DDS ' and len(data)>=128,'Not a legacy DDS texture')
    h,w=struct.unpack_from('<2I',data,12);mips=max(1,struct.unpack_from('<I',data,28)[0]);cc=data[84:88]
    raw=struct.unpack_from('<I',data,88)[0]==32 and bool(struct.unpack_from('<I',data,80)[0]&0x40)
    require(cc in (b'DXT1',b'DXT3',b'DXT5') or raw,'Only DXT1/3/5 or 32-bit RGB(A) DDS supported')
    bs=8 if cc==b'DXT1' else 16
    sizes=[max(1,w>>i)*max(1,h>>i)*4 if raw else max(1,(max(1,w>>i)+3)//4)*max(1,(max(1,h>>i)+3)//4)*bs for i in range(mips)]
    require(128+sum(sizes)==len(data),'DDS mip payload size mismatch')
    return w,h,mips,cc,sizes


def normalize_dds(data):
    w,h,mips,cc,sizes=dds_info(data);b=bytearray(data)
    raw=struct.unpack_from('<I',data,88)[0]==32 and bool(struct.unpack_from('<I',data,80)[0]&0x40)
    struct.pack_into('<I',b,8,0x1007|(0x8 if raw else 0x80000)|(0x20000 if mips>1 else 0))
    struct.pack_into('<I',b,20,w*4 if raw else sizes[0]);struct.pack_into('<I',b,28,mips if mips>1 else 0)
    struct.pack_into('<I',b,108,0x1000|(0x400008 if mips>1 else 0))
    return bytes(b)


def png_from_dds(source,dest):
    # Pillow is optional; Blender reads the untouched DDS copies without Pillow.
    try:
        from PIL import Image
        Image.open(source).save(dest);return True
    except ImportError:return False


def patch_cockpit_ini(text, mesh_path, material_path):
    lines=text.splitlines(keepends=True);in_app=False;found=set();out=[]
    replace={'COCKPIT':mesh_path,'COCKPITMATERIAL':material_path}
    for line in lines:
        words=line.strip().split(None,1)
        if line.strip().startswith('['):
            if in_app:
                for k in replace.keys()-found:out.append(k+' '+replace[k]+'\n')
            in_app=line.strip().casefold()=='[appearance]'
        key=words[0].upper() if words else ''
        if in_app and key in replace:
            if key not in found:out.append(key+' '+replace[key]+'\n');found.add(key)
            continue
        if in_app and key in ('COCKPITLIGTH','COCKPITLIGHT','COCKPITBUMP'):continue
        out.append(line)
    if in_app:
        for k in replace.keys()-found:out.append(k+' '+replace[k]+'\n')
    require(found or '[Appearance]' in text,'No Appearance section')
    return ''.join(out)


def port_cockpit(source, dest, target, mode='bmf', offset=(0.,0.,0.), diffuse='cockpit.dds'):
    """Create an overlay outside the source/game. Preserve original BMF animations."""
    source,dest=Path(source),Path(dest);require(source.resolve()!=dest.resolve(),'Output must be a separate folder')
    require(re.fullmatch(r'[A-Za-z0-9_-]+',target) is not None,'Target folder must be an ASCII slug')
    output=dest/'media'/'planes'/target;require(not output.exists(),'Output exists; choose a new folder to avoid overwriting')
    if mode=='bmf':
        model=read_bmf(source/'cockpit.bmf');light=read_bmf(source/'cockpitlight.bmf')
        msh=bmf_to_msh(model,light,'cockpit_legacy')
        for ob in msh.objects:
            lo=ob.lods[0];lo.arrays['position']=[tuple(x+y for x,y in zip(p,offset)) for p in lo.arrays['position']]
            h=bytearray(ob.header);struct.pack_into('<6f',h,204,*bounds(lo.arrays['position']));ob.header=bytes(h)
        mats={'cockpit_legacy':{'DIFFUSE':f'planes/{target}/{diffuse}','SPECULAR':'planes/black.dds','BUMP':f'planes/{target}/cockpitbump.dds' if (source/'cockpitbump.dds').exists() else 'planes/blankbump.dds','LIGHT':f'planes/{target}/cockpitlight.dds'}}
    else:
        msh=read_msh(source/'cockpit.msh');mats=parse_materials(source/'cockpit.mtl')
        require(all(m in mats for m in msh.materials),'MSH material names are missing from MTL')
        for mp in mats.values():
            for key in ('DIFFUSE','SPECULAR','BUMP','LIGHT'):
                if key in mp:
                    base=Path(mp[key].replace('\\','/')).name
                    if (source/base).is_file():mp[key]=f'planes/{target}/{base}'
                    else:require(mp[key] in ('planes/black.dds','planes/blankbump.dds'), 'Unresolved material texture: '+mp[key])
        if tuple(offset)!=(0.,0.,0.):
            for ob in msh.objects:
                for lo in ob.lods:lo.arrays['position']=[tuple(x+y for x,y in zip(p,offset)) for p in lo.arrays['position']]
                h=bytearray(ob.header);struct.pack_into('<6f',h,204,*bounds(ob.lods[0].arrays['position']));ob.header=bytes(h)
    required={Path(mp[k]).name for mp in mats.values() for k in ('DIFFUSE','BUMP','LIGHT','SPECULAR') if k in mp and mp[k].startswith(f'planes/{target}/')}
    for name in required:
        require((source/name).is_file(),'Missing texture: '+name);dds_info((source/name).read_bytes())
    output.mkdir(parents=True)
    if mode=='msh' and offset==(0.,0.,0.):
        (output/'cockpit_acpc.msh').write_bytes((source/'cockpit.msh').read_bytes())
    else:(output/'cockpit_acpc.msh').write_bytes(msh.to_bytes())
    for name in required:(output/name).write_bytes(normalize_dds((source/name).read_bytes()))
    lines=[]
    for name,mp in mats.items():
        lines += ['$MATERIAL '+name,'===========================']
        lines += ['$'+k+' '+mp[k] for k in ('DIFFUSE','SPECULAR','BUMP','LIGHT') if k in mp]
        lines += ['$DIFFUSEMAT 1 1 1 1','$SPECULARMAT 1 1 1 1','$AMBIENTMAT 1 1 1 1','$SPECULARPOWER 18','']
    (output/'cockpit_acpc.mtl').write_text('\n'.join(lines),encoding='ascii')
    # Only INIs already referring to the destination aircraft are auto-patched.
    patched=[];skipped=[]
    for p in sorted(source.glob('*.ini')):
        refs=re.findall(r'planes/([^/\s]+)/',p.read_text(errors='replace'))
        if target not in refs:skipped.append(p.name);continue
        text=patch_cockpit_ini(p.read_text(),f'planes/{target}/cockpit_acpc.msh',f'planes/{target}/cockpit_acpc.mtl')
        (output/p.name).write_text(text,encoding='ascii',errors='replace');patched.append(p.name)
    report={'mode':mode,'target':target,'input':source.name,'offset':offset,'patched_ini':patched,'skipped_foreign_ini':skipped,
            'mesh_validation':inspect_msh(read_msh(output/'cockpit_acpc.msh')),'game_tested':False,
            'note':'Overlay for an already installed aircraft. Existing body models, sounds, physics and global blank textures must be present.'}
    (dest/'conversion_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


def inspect_msh(m):
    return {'signature':'B3DMH10','materials':m.materials,'objects':[{'name':o.name,'lods':[{'vertices':len(l.arrays['position']),'triangles':len(l.indices)//3,'channels':list(l.arrays),'nonfinite_tangent_components':sum(not math.isfinite(x) for k in ('tangent','bitangent') for v in l.arrays.get(k,[]) for x in v)} for l in o.lods]} for o in m.objects]}


def main(argv=None):
    ap=argparse.ArgumentParser(description='Air Conflicts asset parser and cockpit converter')
    subp=ap.add_subparsers(dest='cmd',required=True)
    a=subp.add_parser('inspect');a.add_argument('file')
    a=subp.add_parser('port');a.add_argument('source');a.add_argument('output');a.add_argument('--target',required=True);a.add_argument('--mode',choices=['bmf','msh'],default='bmf');a.add_argument('--diffuse',default='cockpit.dds');a.add_argument('--offset',nargs=3,type=float,default=(0.,0.,0.))
    ns=ap.parse_args(argv)
    if ns.cmd=='inspect':
        p=Path(ns.file)
        if p.suffix.lower() in ('.msh','.nmf'):info=inspect_msh(read_msh(p))
        else:
            b=read_bmf(p);info={'frames':len(b.frames),'positions':len(b.frames[0].positions),'triangles':len(b.faces),'uvs':len(b.uv),'bounds':b.frames[0].meta}
    else:info=port_cockpit(ns.source,ns.output,ns.target,ns.mode,tuple(ns.offset),ns.diffuse)
    print(json.dumps(info,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
