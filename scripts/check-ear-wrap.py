"""Stage current groom and capture ear-rim coverage in the actual app."""
from pathlib import Path
import subprocess,shutil,json,hashlib
R=Path(__file__).resolve().parents[1];O=R/'artifacts/hair-ear-wrap'
names=['metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json','manifest.json']
for b in ('jolt-only','vs2022'):
 for c in ('Release','Debug'):
  d=R/f'build/{b}/bin/{c}/Assets/Moca'
  for n in names:shutil.copy2(R/'assets/moca'/n,d/n)
records=[]
cases=[('Release',v,['--frames','1','--paused']) for v in ('audit-left','audit-right','audit-low-left','audit-low-right','audit-back','audit-front','audit-top')]
cases+=[('Release','audit-low-right',['--steps','120','--capture-every','60','--reset-check']),('Debug','audit-back',['--steps','30','--capture-every','30','--reset-check'])]
for c,v,args in cases:
 name=v+('-motion' if '--steps' in args else '')
 out=O/c/name;out.mkdir(parents=True,exist_ok=True)
 exe=R/f'build/jolt-only/bin/{c}/HairLabDX12.exe'
 command=[str(exe),'--view',v,*args,'--capture-dir',str(out),'--log',str(out/'app.log')]
 if v.startswith('audit-'):command+=['--hide-body']
 result=subprocess.run(command,cwd=R.parent,timeout=120);assert result.returncode==0,(c,v,result.returncode)
 states=[json.loads(p.read_text()) for p in out.glob('step-*.json')]
 for s in states:
  assert s['finite'] and s['maxRootAttachmentErrorMeters']<1e-5
  assert s['displayRegionFilter']==-1
 records.append({'configuration':c,'view':v,'command':command,'exit':result.returncode,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
 print(c,name,'PASS',flush=True)
(O/'runs.json').write_text(json.dumps({'runs':records,'inputs':{n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in names}},indent=2))
