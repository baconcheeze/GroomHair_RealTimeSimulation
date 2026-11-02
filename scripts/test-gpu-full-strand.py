"""Offline launcher: the application executes all physical work and captures."""
import subprocess,json,time,hashlib,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
exe=root/'build/gpu-demo/bin/Release/HairLabDX12.exe'
out=root/'artifacts/gpu-full-strand/20261031T090000Z'
results=[]
scenarios=[('gravity3',3,16,False),('kick3',3,48,True),('kick32',32,16,True),('long32',32,60,True),('kick128',128,16,True)]
for name,count,steps,kick in scenarios:
 for backend in ['cpu','gpu']:
  target=out/backend/name
  args=[str(exe),'--solver-mode','reference','--reference-backend',backend,'--reference-guides',str(count),'--reference-steps',str(steps),'--reference-every','4','--reference-reset-check','--reference-capture-dir',str(target),'--log',str(target/'app.log')]
  if kick:args+=['--reference-kick','0,0.05,0']
  if count==3:args+=['--reference-tip']
  t=time.perf_counter();p=subprocess.Popen(args,cwd=root)
  print('START',name,backend,p.pid,flush=True)
  try:code=p.wait(timeout=180)
  except subprocess.TimeoutExpired:
   print('INCOMPLETE timeout; process left for inspection',p.pid,flush=True);code=None
  results.append(dict(name=name,backend=backend,count=count,steps=steps,code=code,seconds=time.perf_counter()-t,args=args))
  (out/'motion-runs.json').write_text(json.dumps(results,indent=2))
  print('END',name,backend,code,results[-1]['seconds'],flush=True)
  if code!=0:sys.exit(1)
