"""CLI bridge for the MaxScript adapter. No Autodesk or Blender modules needed."""
import sys,struct,re,argparse,xml.etree.ElementTree as ET
from pathlib import Path
import ac_formats as ac

def mxstr(text):return '"'+str(text).replace('\\','\\\\').replace('"','\\"').replace('\n','\\n').replace('\r','')+'"'
def point(v):return '['+','.join(format(x,'.9g') for x in v)+']'
def to_max(src,out):
    src=Path(src)
    if src.suffix.lower()=='.bmf':
        b=ac.read_bmf(src);m=ac.bmf_to_msh(b,None,'body');maps={'body':{'DIFFUSE':str(src.parent/('cockpit.dds' if src.stem.startswith('cockpit') else '1.dds'))}}
    else:m=ac.read_msh(src);maps=ac.parse_materials(src.with_suffix('.mtl'))
    code=['-- Generated locally by Air Conflicts Workshop.','(']
    for ob in m.objects:
        lo=ob.lods[0];p=lo.arrays['position'];faces=[];normals=[]
        for i in range(0,len(lo.indices),3):
            ids=lo.indices[i:i+3]
            if ac.face_alignment(lo,ids)>0:ids=[ids[0],ids[2],ids[1]]
            faces.append(tuple(v+1 for v in ids));normals.extend([(lo.arrays.get('normal') or [(0,1,0)]*len(p))[j] for j in ids])
        code+=['local acVerts=#('+','.join(point((x,z,y)) for x,y,z in p)+')','local acFaces=#('+','.join(point(f) for f in faces)+')',f'local acMesh=mesh name:{mxstr(ob.name)} vertices:acVerts faces:acFaces','local acMat=multiMaterial numsubs:'+str(len(m.materials))]
        for i,name in enumerate(m.materials):
            mp=maps.get(name,{});ref=mp.get('DIFFUSE');path=src.parent/Path(ref.replace('\\','/')).name if ref else None
            code+=['acMat['+str(i+1)+']=standardMaterial name:'+mxstr(name)]
            if path and path.exists():code+=['acMat['+str(i+1)+'].diffuseMap=bitmapTexture filename:'+mxstr(path.resolve()),'showTextureMap acMat['+str(i+1)+'] true']
        code+=['acMesh.material=acMat']
        for ch,key in [(1,'uv0'),(2,'uv1')]:
            if key not in lo.arrays:continue
            code += [f'meshop.setMapSupport acMesh {ch} true',f'meshop.setNumMapVerts acMesh {ch} {len(p)}',f'meshop.setNumMapFaces acMesh {ch} {len(faces)}', 'local acUV=#('+','.join(point((u,1-v,0)) for u,v in lo.arrays[key])+')',f'for i=1 to acUV.count do meshop.setMapVert acMesh {ch} i acUV[i]',f'for i=1 to acFaces.count do meshop.setMapFace acMesh {ch} i acFaces[i]']
        for start,count,mi,_ in lo.groups:code+=[f'for i={start//3+1} to {(start+count)//3} do setFaceMatID acMesh i {mi+1}']
        code+=['update acMesh','local acNorms=#('+','.join(point((x,z,y)) for x,y,z in normals)+')','try (','  local en=Edit_Normals()','  addModifier acMesh en','  en.MakeExplicit selection:#{1..(en.GetNumNormals node:acMesh)} node:acMesh','  for f=1 to acFaces.count do for c=1 to 3 do (','    local ni=en.GetNormalID f c node:acMesh','    en.SetNormal ni acNorms[(f-1)*3+c] node:acMesh','    en.SetNormalExplicit ni explicit:true node:acMesh','  )',') catch (format "Normal modifier warning: %\\n" (getCurrentException()))','selectMore acMesh']
    code+=['max zoomext sel all',')'];Path(out).write_text('\n'.join(code),encoding='utf-8-sig')

def from_max(src,out,kind,target):
    root=ET.parse(src).getroot();materials=[];maps=[]
    for item in root.findall('material'):
        materials.append(re.sub('[^A-Za-z0-9_-]','_',item.get('name','material')));maps.append(item.get('diffuse',''))
    ac.require(materials,'Assign a material with a diffuse texture')
    arrays={k:[] for k in ['position','normal','uv0','uv1','tangent','bitangent']};faces=[];mids=[]
    for face in root.findall('face'):
        ids=[];corners=[]
        for c in face.findall('corner'):
            def vals(k,n):
                v=tuple(map(float,c.get(k).split()));ac.require(len(v)==n,'Invalid Max exchange vector');return v
            p=vals('p',3);n=vals('n',3);u=vals('uv0',2);l=vals('uv1',2)
            corners.append(((p[0],p[2],p[1]),(n[0],n[2],n[1]),(u[0],1-u[1]),(l[0],1-l[1])))
        ac.require(len(corners)==3,'Max exchange requires triangles')
        for p,n,u,l in corners:
            ids.append(len(arrays['position']));arrays['position'].append(p);arrays['normal'].append(n);arrays['uv0'].append(u);arrays['uv1'].append(l)
            t,bt=ac.basis_for_triangle([c[0] for c in corners],[c[2] for c in corners],n);arrays['tangent'].append(t);arrays['bitangent'].append(bt)
        faces.append(ids);mids.append(int(face.get('material','0')))
    indices=[];groups=[]
    for mi in sorted(set(mids)):
        start=len(indices)
        for face,material in zip(faces,mids):
            if mi==material:indices+=([face[0],face[2],face[1]] if kind=='msh' else face)
        groups.append((start,len(indices)-start,mi,()))
    lo=ac.Lod(indices,arrays,groups)
    if kind=='bmf':
        ac.require(len(set(mids))==1,'BMF needs one diffuse atlas/material; use Blender atlas or bake in Max first')
        Path(out).write_bytes(ac.lod_to_bmf(lo).to_bytes())
    else:
        model=ac.make_msh(lo,materials,'max_mesh');data=model.to_bytes();ac.read_msh(data);Path(out).write_bytes(data)
        lines=[]
        for name,texture in zip(materials,maps):
            ac.require(Path(texture).suffix.lower()=='.dds' and Path(texture).is_file(),'Max export needs a DDS diffuse texture: '+texture)
            name_dds=Path(texture).name;(Path(out).parent/name_dds).write_bytes(ac.normalize_dds(Path(texture).read_bytes()))
            lines+=['$MATERIAL '+name,'===========================',f'$DIFFUSE planes/{target}/{name_dds}','$SPECULAR planes/black.dds','$BUMP planes/blankbump.dds','$LIGHT planes/'+target+'/_ac_white.dds','$DIFFUSEMAT 1 1 1 1','$SPECULARMAT 1 1 1 1','$AMBIENTMAT 1 1 1 1','$SPECULARPOWER 18','']
        head=bytearray(128);head[:4]=b'DDS ';struct.pack_into('<7I',head,4,124,0x81007,4,4,8,0,0);struct.pack_into('<2I4s',head,76,32,4,b'DXT1');struct.pack_into('<I',head,108,0x1000)
        (Path(out).parent/'_ac_white.dds').write_bytes(bytes(head)+struct.pack('<HHI',65535,65535,0));Path(out).with_suffix('.mtl').write_text('\n'.join(lines),encoding='ascii')

def main():
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else sys.argv[1:]
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['to-max','from-max']);p.add_argument('input');p.add_argument('output');p.add_argument('--kind',choices=['bmf','msh'],default='msh');p.add_argument('--target',default='my_plane');ns=p.parse_args(args)
    if ns.mode=='to-max':to_max(ns.input,ns.output)
    else:from_max(ns.input,ns.output,ns.kind,ns.target)
if __name__=='__main__':main()
