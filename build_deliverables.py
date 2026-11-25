"""Reproduce exports and cockpit overlays from the attached resources."""
from pathlib import Path
import sys,shutil,json,hashlib,copy
import ac_formats as ac

ROOT=Path(__file__).resolve().parent.parent
SOURCES={
 'BF109_from_ki45':(ROOT/'analysis/ki45/ki45','ki45'),
 'DB3':(ROOT/'analysis/db3/db3','db3'),
 'FW190':(ROOT/'analysis/fw190','fw190'),
 'Avenger_Pacific_reference':(ROOT/'analysis/avenger','avenger'),
 'Avenger_SecretWars_supplied':(ROOT/'analysis/avenger_sw','avenger'),
}


def textures(src,out):
    folder=out/'textures';folder.mkdir(parents=True,exist_ok=True)
    for p in src.glob('*.dds'):
        shutil.copy2(p,folder/p.name)
        ac.png_from_dds(p,folder/(p.stem+'.png'))
        if p.name=='cockpit.dds':
            from PIL import Image
            Image.open(p).convert('RGB').save(folder/'cockpit_rgb.png')


def texture_ref(out,ref):
    name=Path(ref.replace('\\','/')).name
    if (out/'textures'/name).exists():
        if name=='cockpit.dds' and (out/'textures/cockpit_rgb.png').exists():return 'textures/cockpit_rgb.png'
        png=Path(name).with_suffix('.png').name
        return 'textures/'+(png if (out/'textures'/png).exists() else name)
    return None


def bmf_lod(b,frame=0):
    f=b.frames[frame];a={'position':[],'normal':[],'uv0':[]};idx=[];cache={}
    for fi,face in enumerate(b.faces):
        for j in range(3):
            row=(f.positions[face[j]],f.normals[fi*3+j],b.uv[face[j+3]])
            if row not in cache:
                cache[row]=len(cache)
                for key,x in zip(a,row):a[key].append(x)
            idx.append(cache[row])
    return ac.Lod(idx,a,[(0,len(idx),'body',())],0x10a)


def export_one(label,src,out):
    out.mkdir(parents=True,exist_ok=True);textures(src,out);report={'source_label':label,'outputs':[],'missing_external_assets':[]}
    ini=src/'1.ini';ent=ac.ini_lines(ini);bodytexture=next(v for k,v in ent if k=='TEXTURE')
    mp={'body':{'DIFFUSE':texture_ref(out,bodytexture),'SPECULAR':texture_ref(out,'specular.dds'),'BUMP':texture_ref(out,'bump.dds')}}
    def obj(name,parts,mats):
        ac.write_obj(out/(name+'.obj'),parts,mats)
        report['outputs'].append({'name':name+'.obj','triangles':sum(len(p[1].indices)//3 for p in parts),'objects':[p[0] for p in parts]})
    if (src/'cockpit.msh').exists():
        msh=ac.read_msh(src/'cockpit.msh');maps=ac.parse_materials(src/'cockpit.mtl');mapped={}
        for mat in msh.materials:
            mapped[mat]={k:texture_ref(out,ref) for k,ref in maps.get(mat,{}).items() if k in ('DIFFUSE','BUMP','SPECULAR','LIGHT')}
            for k,v in maps.get(mat,{}).items():
                if k in ('DIFFUSE','LIGHT') and not texture_ref(out,v):report['missing_external_assets'].append(v)
        parts=ac.obj_parts_msh(msh);obj(label+'_1p_supplied_MSH',parts,mapped)
        lp=[];lm={}
        for name,lod,off in parts:
            arr=dict(lod.arrays);arr['uv0']=arr.get('uv1',arr['uv0']);lp.append((name,ac.Lod(lod.indices,arr,lod.groups,lod.mask),off))
        for m in mapped:lm[m]={'DIFFUSE':mapped[m].get('LIGHT')}
        obj(label+'_1p_light_UV',lp,lm)
        report['msh']=ac.inspect_msh(msh)
    if (src/'cockpit.bmf').exists():
        b=ac.read_bmf(src/'cockpit.bmf');l=ac.read_bmf(src/'cockpitlight.bmf')
        rebuilt=ac.bmf_to_msh(b,l,'legacy');parts=ac.obj_parts_msh(rebuilt)
        obj(label+'_1p_legacy_BMF',parts,{'legacy':{'DIFFUSE':texture_ref(out,'cockpit.dds'),'BUMP':texture_ref(out,'cockpitbump.dds'),'LIGHT':texture_ref(out,'cockpitlight.dds')}})
        report['legacy_cockpit']={'faces':len(b.faces),'position_bounds':b.frames[0].meta,'uv_light_topology_equal':all(x[:3]==y[:3] for x,y in zip(b.faces,l.faces))}
    bodyref=next(v for k,v in ent if k=='MODEL1');body=src/Path(bodyref).name
    obj(label+'_3p_body_LOD0',[(body.stem,bmf_lod(ac.read_bmf(body)),(0,0,0))],mp)
    for tag in ['MODEL2','MODEL3']:
        refs=[v for k,v in ent if k==tag]
        if refs and (src/Path(refs[0]).name).exists():
            p=src/Path(refs[0]).name;obj(label+'_3p_'+tag,[(p.stem,bmf_lod(ac.read_bmf(p)),(0,0,0))],mp)
    parts=[];used=set()
    for k,ref in ent:
        if k not in ('MODEL1','DIRECT1A','DIRECT1B','DIRECT2A','DIRECT2B','DIRECT3','GLASS','WHEELS','PILOT','COCKPITLOW','COCKPITLOW2') or not ref.lower().endswith('.bmf'):continue
        p=src/Path(ref).name
        if not p.exists():
            if Path(ref).name!='blank.bmf':report['missing_external_assets'].append(ref)
            continue
        if (k,p.name) in used:continue
        used.add((k,p.name));parts.append((k+'_'+p.stem,bmf_lod(ac.read_bmf(p)),(0,0,0)))
    lines=ini.read_text().splitlines();screws=[]
    for i,line in enumerate(lines):
        if line.strip().upper().startswith('NUMSCREWPOINTS '):screws=[tuple(map(float,lines[i+1+j].split())) for j in range(int(line.split()[1]))];break
    for k,ref in ent:
        if k not in ('SCREWHI','SCREWCON') or not ref.lower().endswith('.bmf') or not (src/Path(ref).name).exists():continue
        for j,off in enumerate(screws):parts.append((k+str(j),bmf_lod(ac.read_bmf(src/Path(ref).name)),off))
    for tower in ac.ini_towers(ini):
        for k,ref in tower['parts']:
            p=src/Path(ref).name
            if p.exists():parts.append((f'TOWER{tower["id"]}_{k}',bmf_lod(ac.read_bmf(p)),tower['offset']))
    obj(label+'_3p_assembled_frame0',parts,mp)
    report['assembly_note']='Frame 0 of configured geometry; tower animation, weapon effects and flight behavior are not represented by OBJ. Control surfaces are not recentered.'
    (out/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


def main():
    exports=ROOT/'deliverables/OBJ_Models';patches=ROOT/'deliverables/Workshop/Cockpit_Overlays';reports=[]
    for label,(src,target) in SOURCES.items():
        reports.append(export_one(label,src,exports/label))
        if label!='Avenger_Pacific_reference':
            for mode in ['msh','bmf']:
                if mode=='bmf' and not (src/'cockpit.bmf').exists():continue
                dest=patches/(label+'_'+mode.upper());ac.port_cockpit(src,dest,target,mode)
    (exports/'all_exports.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
    print(json.dumps([{'model':r['source_label'],'exports':len(r['outputs'])} for r in reports],indent=2))


if __name__=='__main__':main()
