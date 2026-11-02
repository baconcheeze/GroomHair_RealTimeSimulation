"""Bounded Release collision profiling; no physics/collision settings changed."""
import subprocess,json,hashlib,time
from pathlib import Path
R=Path(__file__).resolve().parents[1];O=R/'artifacts/collision-profile/measured';O.mkdir(parents=True,exist_ok=True)
exe=R/'build/jolt-only/bin/Release/HairLabDX12.exe';inputs={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in ('metahuman.hair','body-colliders.bin','naruto-run.bin')}
records=[]
# Interleave instrumented and CPU-clock-only runs to expose timestamp overhead.
for i,gpu in enumerate((True,False,True,False,True)):
 d=O/(str(i+1)+('-gpu' if gpu else '-cpu'))
 d.mkdir(exist_ok=True)
 cmd=[str(exe),'--walk-test','--steps','240','--profile-csv',str(d/'profile.csv'),'--log',str(d/'app.log')]
 if not gpu:cmd+=['--profile-cpu-only']
 t=time.perf_counter();p=subprocess.run(cmd,cwd=R.parent,timeout=180);wall=time.perf_counter()-t
 record={'run':i+1,'gpuTimestamps':gpu,'exit':p.returncode,'wallSeconds':wall,'command':cmd};records.append(record)
 (O/'runs.json').write_text(json.dumps({'runs':records,'inputHashes':inputs,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2))
 assert p.returncode==0,record
 assert inputs=={n:hashlib.sha256((R/'assets/moca'/n).read_bytes()).hexdigest() for n in inputs}
 print(i+1,'GPU' if gpu else 'CPU only',round(wall,3),'s PASS',flush=True)
