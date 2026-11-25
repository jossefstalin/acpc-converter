import unittest,struct,tempfile,copy,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import ac_formats as a
class Formats(unittest.TestCase):
 def mesh(self):
  return a.Lod([0,2,1],{'position':[(0.,0.,0.),(1.,0.,0.),(0.,1.,0.)],'normal':[(0.,0.,1.)]*3,'uv0':[(0.,0.),(1.,0.),(0.,1.)],'uv1':[(.1,.2),(.3,.4),(.5,.6)],'tangent':[(1.,0.,0.)]*3,'bitangent':[(0.,1.,0.)]*3},[(0,3,0,())])
 def test_bmf_roundtrip(self):
  b=a.lod_to_bmf(self.mesh());raw=b.to_bytes();self.assertEqual(a.read_bmf(raw).to_bytes(),raw)
 def test_animated_bmf(self):
  b=a.lod_to_bmf(self.mesh());f=copy.deepcopy(b.frames[0]);f.positions=[(x,y,z+1) for x,y,z in f.positions];b.frames.append(f);self.assertEqual(len(a.read_bmf(b.to_bytes()).frames),2)
 def test_dual_uv_conversion(self):
  b=a.lod_to_bmf(self.mesh());light=copy.deepcopy(b);light.uv=self.mesh().arrays['uv1'];m=a.bmf_to_msh(b,light);raw=m.to_bytes();out=a.read_msh(raw).objects[0].lods[0];self.assertEqual(len(out.indices),3)
  self.assertGreater(a.face_alignment(out,out.indices),0)
  for j,i in enumerate(out.indices):
   p=out.arrays['position'][i];original=b.frames[0].positions.index(p);self.assertEqual(out.arrays['uv0'][i],b.uv[original]);self.assertTrue(all(abs(x-y)<1e-6 for x,y in zip(out.arrays['uv1'][i],light.uv[original])))
 def test_msh_roundtrip(self):
  m=a.make_msh(self.mesh(),['test']);raw=m.to_bytes();self.assertEqual(a.read_msh(raw).to_bytes(),raw)
 def test_bmf_rejects_truncation(self):
  with self.assertRaises(ValueError):a.read_bmf(a.lod_to_bmf(self.mesh()).to_bytes()[:-1])
 def test_bad_light_topology_rejected(self):
  b=a.lod_to_bmf(self.mesh());l=copy.deepcopy(b);l.faces=[(1,0,2,0,1,2)]
  with self.assertRaises(ValueError):a.bmf_to_msh(b,l)
 def test_ini_camera_untouched(self):
  text='[Appearance]\nCOCKPIT old.dds\nCOCKPIT old.bmf\nCOCKPITLIGTH old.bmf\nMODEL1 original.bmf\n[Camera]\nCOCKPIT 0 1 2\n'
  out=a.patch_cockpit_ini(text,'planes/a/cockpit.msh','planes/a/cockpit.mtl');self.assertIn('[Camera]\nCOCKPIT 0 1 2',out);self.assertIn('MODEL1 original.bmf',out);self.assertNotIn('old.',out)
 def test_dds_normalization_preserves_payload(self):
  h=bytearray(128);h[:4]=b'DDS ';struct.pack_into('<I',h,4,124);struct.pack_into('<2I',h,12,4,4);struct.pack_into('<2I4s',h,76,32,4,b'DXT1');raw=bytes(h)+struct.pack('<HHI',65535,0,0);fixed=a.normalize_dds(raw);self.assertEqual(fixed[128:],raw[128:]);self.assertEqual(a.dds_info(fixed)[:3],(4,4,1))
if __name__=='__main__':unittest.main()
