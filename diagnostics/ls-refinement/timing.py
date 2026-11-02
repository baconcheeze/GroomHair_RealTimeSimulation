import pathlib,json,sys,statistics,math
from core import Host,run
from frozen import fl,intsizes
sys.path.insert(0,str(pathlib.Path(__file__).parent.parent/'qr-audit'));import audit
source=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);host=Host(pathlib.Path(sys.argv[3]).resolve());data={}
try:
 for name in ['primary__original_initial','synthetic-2-pins0__original_initial','synthetic-2-pins1__original_initial']:
  p=audit.decode(source/'frozen'/(name+'.json'));n=p['n'];a=audit.chunks(fl(p['A']),n);samples=[]
  for k in range(11):
   curve,stats=run(host,n,intsizes(p),fl(p['M']),fl(p['c']),fl(p['D']),a,fl(p['b']))
   if k>=2:samples.append(stats)
  metrics={}
  for key in ['mechanismWallMs','factorMs','initialSolveMs','residualSetupMs','residualMs','triangularCorrectionMs','initRoundtripMs','correctionIPCroundtripMs']:
   values=sorted(s[key] for s in samples);metrics[key]={'median':statistics.median(values),'P95':values[math.ceil(.95*len(values))-1]}
  data[name]={'warmup':2,'samples':9,'n':n,'measured':'CPU wall; persistent processes; input/oracle parsing and output writing excluded; IPC included separately','metrics':metrics,'rawSamples':samples}
finally:host.close()
out.write_text(json.dumps(data,indent=2))
