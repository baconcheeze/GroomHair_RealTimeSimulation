"""Aggregate full-update GPU work before calculating median/P95. No dependencies."""
from pathlib import Path
import csv,json,math,statistics,sys,collections
root=Path(sys.argv[1] if len(sys.argv)>1 else 'artifacts/gpu-stages/measured')
def stats(values):
 a=sorted(values);return {'samples':len(a),'medianMs':statistics.median(a),'p95Ms':a[math.ceil(.95*len(a))-1],'minMs':a[0],'maxMs':a[-1]}
categories={'FrameTransform':['ApplyDeltaTransform'],'ScalpRootSkin':['SkinVertices','SkinRoots'],'BodySkinWire':['BodySkin'],'BodyBitmask':['BodyBitmask'],'HeadHullSearch':['CollisionSearch'],'BodyCapsuleNarrow':['BodyNarrow'],'GridBuild':['GridClear','GridAccumulate','GridNormalize'],'InitialIntegrate':['Integrate'],'RootUpdate':['UpdateRoots'],'StrandSolve':['UpdateStrands'],'VelocityCollisionGridIntegrate':['UpdateVelocityIntegrate','UpdateVelocity'],'RenderReconstruction':['RenderReconstruction']}
result={}
for style in ['long','metahuman']:
 compute=collections.defaultdict(list);kernels=collections.defaultdict(list);draw=collections.defaultdict(list);counts=collections.defaultdict(set);perRun=[]
 for folder in sorted(root.glob(style+'-*')):
  if not (folder/'profile.csv').exists():continue
  rows={int(r['step']):r for r in csv.DictReader((folder/'profile.csv').open()) if 60<=int(r['step'])<240}
  byStep=collections.defaultdict(lambda:collections.defaultdict(float));calls=collections.defaultdict(collections.Counter)
  for r in csv.DictReader((folder/'profile.csv.dispatch.csv').open()):
   step=int(r['step'])
   if step in rows:byStep[step][r['kernel']]+=float(r['gpu_ms']);calls[step][r['kernel']]+=1
  draws=collections.defaultdict(dict)
  for r in csv.DictReader((folder/'profile.csv.graphics.csv').open()):
   step=int(r['step'])
   if step in rows:
    assert r['stage'] not in draws[step], 'Repeated graphics step; declare a different aggregation policy'
    draws[step][r['stage']]=float(r['gpu_ms'])
  assert len(rows)==180 and len(draws)==180
  for step,r in rows.items():
   for key,group in categories.items():compute[key].append(sum(byStep[step].get(k,0) for k in group))
   for k,v in byStep[step].items():kernels[k].append(v);counts[k].add(calls[step][k])
   compute['ComputeTotal'].append(float(r['gpu_total_ms']));compute['ComputeDispatchSum'].append(float(r['gpu_dispatch_sum_ms']))
   compute['ComputeUnattributed'].append(float(r['gpu_total_ms'])-float(r['gpu_dispatch_sum_ms']))
   compute['CPUUpdateIncludingWait'].append(float(r['cpu_step_ms']))
   for k,v in draws[step].items():draw[k].append(v)
   draw['GraphicsUnattributed'].append(draws[step]['GraphicsTotal']-sum(v for k,v in draws[step].items() if k!='GraphicsTotal'))
  perRun.append({'run':folder.name,'computeTotal':stats([float(r['gpu_total_ms']) for r in rows.values()]),'graphicsTotal':stats([d['GraphicsTotal'] for d in draws.values()])})
 result[style]={'compute':{k:stats(v) for k,v in compute.items()},'kernelsPerUpdate':{k:stats(v) for k,v in kernels.items()},'callsPerUpdate':{k:sorted(v) for k,v in counts.items()},'graphics':{k:stats(v) for k,v in draw.items()},'runs':perRun}
(root/'summary.json').write_text(json.dumps(result,indent=2))
with (root/'summary.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['style','queue','stage','samples','median_ms','p95_ms'])
 for style,data in result.items():
  for section in ['compute','graphics']:
   for stage,s in data[section].items():w.writerow([style,section,stage,s['samples'],s['medianMs'],s['p95Ms']])
print(json.dumps({style:{q:{k:round(v['medianMs'],5) for k,v in d[q].items()} for q in ['compute','graphics']} for style,d in result.items()},indent=2))
