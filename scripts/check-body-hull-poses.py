"""Capture-only skinned skin versus the active Jolt convex planes."""
import sys,json
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parents[1];O=R/'artifacts/body-hulls';meta=json.loads((R/'assets/moca/body-colliders.json').read_text())
ids=sorted({i for h in meta['hulls'] for i in h['sourceSkinVertices']});reports=[]
folder=R/sys.argv[sys.argv.index('--folder')+1] if '--folder' in sys.argv else O/('final-poses' if '--final' in sys.argv else 'pose-audit')
for path in sorted(folder.glob('step-*.json')):
 s=json.loads(path.read_text());hair=np.array(s['hairWorldColumns']).reshape(4,4).T
 world=np.array(s['scalpVertices'])[ids]@hair[:3,:3].T+hair[:3,3]
 values=[];bindingError=[]
 for h in range(3,len(s['hulls'] if isinstance(s['hulls'],list) else s['hullJointIds'])):
  t=np.array(s['hullWorldColumns'][h]).reshape(4,4).T;local=(world-t[:3,3])@t[:3,:3]
  planes=np.array(s['hullPlanesLocal'][h]);values.append(np.max(local@planes[:,:3].T+planes[:,3],axis=1))
  j=s['hullJointIds'][h];bindingError.append(float(np.linalg.norm(t[:3,3]-np.array(s['posedJointPositionsWorld'][j]))))
 excess=np.min(values,axis=0)
 lookup={v:k for k,v in enumerate(ids)}
 perPart={h['name']:{'maxMeters':float(max(0,max(excess[lookup[i]] for i in h['sourceSkinVertices']))),'outside1mm':int(sum(excess[lookup[i]]>.001 for i in h['sourceSkinVertices']))} for h in meta['hulls'] if h['sourceSkinVertices']}
 reports.append({'step':s['step'],'skinPoints':len(ids),'maxPositiveHalfspaceExcessMeters':float(max(0,excess.max())),'p99PositiveHalfspaceExcessMeters':float(max(0,np.quantile(excess,.99))),'skinPointsOutside1mm':int((excess>.001).sum()),'maxHullBoneOriginErrorMeters':max(bindingError),'qualification':'Skin only, union of active body convex hulls; maximum face-plane excess is not exact Euclidean surface distance. Only selected actual body skin is scored; clothing is excluded.'})
 reports[-1]['perPart']=perPart
 reports[-1]['worstSkinVertices']=[{'vertex':ids[int(i)],'excessMeters':float(excess[i])} for i in np.argsort(excess)[-8:][::-1]]
 print(s['step'],{n:v for n,v in perPart.items() if v['maxMeters']>.001},flush=True)
(R/sys.argv[sys.argv.index('--output')+1] if '--output' in sys.argv else O/('final-pose-fit.json' if '--final' in sys.argv else 'pose-fit.json')).write_text(json.dumps(reports,indent=2))
