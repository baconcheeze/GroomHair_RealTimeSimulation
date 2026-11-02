"""Explicit capture-only point/envelope diagnostics; not continuous collision proof."""
import json
from pathlib import Path

R=Path(__file__).resolve().parents[1]
meta=json.loads((R/'assets/moca/metahuman.json').read_text())
ears=meta['earFit']['geometry']
results=[]
for path in sorted((R/'artifacts/metahuman-ear-fit/final').glob('*/**/step-*.json')):
    state=json.loads(path.read_text())
    if state['hairstyle']!='metahuman':continue
    stats={k:{'points':0,'insidePoints':0,'maxDepthMetres':0.0} for k in ('roots','free')}
    for index,*p in state['earRegionRenderBindSamples']:
        for ear in ears:
            lo,hi=ear['bounds']
            if not all(lo[k]-.003<=p[k]<=hi[k]+.003 for k in range(3)):continue
            distance=max(sum(plane[k]*p[k] for k in range(3))-plane[3] for plane in ear['planes'])
            s=stats['roots' if index%32==0 else 'free'];s['points']+=1
            if distance<0:s['insidePoints']+=1;s['maxDepthMetres']=max(s['maxDepthMetres'],-distance)
    results.append({'capture':str(path.relative_to(R)),'step':state['step'],**stats})
out=R/'artifacts/metahuman-ear-fit/clearance.json'
out.write_text(json.dumps({'qualification':'Saved render vertices versus bind-ear convex envelopes. Roots reported separately, not discarded. No segment-interior or arbitrary-pose guarantee.','results':results},indent=2))
print(out)
print('Worst free point envelope depth (m):',max((s['free']['maxDepthMetres'] for s in results),default=0))
