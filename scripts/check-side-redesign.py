"""Stage current groom and capture ear-rim coverage in the actual app."""
from pathlib import Path
import subprocess,shutil,json,hashlib,sys
R=Path(__file__).resolve().parents[1]
label=sys.argv[sys.argv.index('--label')+1] if '--label' in sys.argv else 'photo3-accepted'
assert label.replace('-','').isalnum()
O=R/'artifacts/hair-side-redesign'/label
names=['metahuman.hair','metahuman.materials','metahuman.ears','metahuman.skull','metahuman.json','manifest.json']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
input_hashes={n:sha(R/'assets/moca'/n) for n in names}
shader_hash=sha(R/'shaders/JoltDisplay.hlsl')
for b in ('jolt-only','vs2022'):
 for c in ('Release','Debug'):
  d=R/f'build/{b}/bin/{c}/Assets/Moca'
  assert sha(d.parent.parent/'shaders/JoltDisplay.hlsl')==shader_hash,'Stale runtime display shader'
  for n in names:shutil.copy2(R/'assets/moca'/n,d/n)
records=[]
cases=[('Release',v,['--frames','1','--paused']) for v in ('audit-left','audit-right','audit-low-left','audit-low-right','audit-back','audit-front','audit-top')]
cases+=[('Release','audit-low-right',['--steps','120','--capture-every','60','--reset-check']),('Debug','audit-back',['--steps','30','--capture-every','30','--reset-check'])]
if '--preview' in sys.argv:
 cases=[('Release',v,['--frames','1','--paused']) for v in ('audit-front','audit-left','audit-right','audit-low-right')]
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
assert input_hashes=={n:sha(R/'assets/moca'/n) for n in names},'Inputs changed during run; do not qualify this batch.'
assert shader_hash==sha(R/'shaders/JoltDisplay.hlsl'),'Display shader changed during run'
(O/'runs.json').write_text(json.dumps({'runs':records,'inputs':input_hashes,'displayShaderSha256':shader_hash},indent=2))
