"""Offline repair of fitted ponytail rest-curve kinks, with unchanged roots/tips.
Run with Blender Python. Outputs a candidate; --apply stages only after checks.
"""
import sys,struct,json,hashlib,shutil,math
from pathlib import Path
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'scripts'))
from groom_surface import ScalpSurface
A=R/'assets/moca';O=R/'artifacts/guide-twist/repair';O.mkdir(parents=True,exist_ok=True)
# Freeze the immediate pre-repair input, not the previously approved checkpoint.
B=O/'before';B.mkdir(exist_ok=True)
for n in ('metahuman.hair','metahuman.json','manifest.json','metahuman.materials'):
 if not (B/n).exists():shutil.copy2(A/n,B/n)
blob=(B/'metahuman.hair').read_bytes();p=np.frombuffer(blob,dtype='<f4',offset=128).reshape(-1,32,3).copy();old=p.copy()
base=np.frombuffer((R/'authoring/side-style/metahuman.hair').read_bytes(),dtype='<f4',offset=128).reshape(-1,32,3)
m=np.frombuffer((B/'metahuman.materials').read_bytes(),dtype='<u4',offset=4)
def angles(q):
 e=np.diff(q.astype(np.float64),axis=0);e/=np.linalg.norm(e,axis=1)[:,None]
 return np.degrees(np.arccos(np.clip(np.sum(e[:-1]*e[1:],axis=1),-1,1)))
changed=[]
for i in np.flatnonzero(np.isin(m,[2,6])):
 a=angles(p[i]);bad=np.flatnonzero(a[:10]>75)
 if not len(bad):continue
 # Restore the coherent source curve with a smoothly varying attachment offset.
 # Keep the exact current follicle and all samples from endpoint onwards.
 end=12
 q=base[i,:end+1].astype(np.float64).copy()
 arc=np.r_[0,np.cumsum(np.linalg.norm(np.diff(q,axis=0),axis=1))];t=arc/arc[-1];s=t*t*(3-2*t)
 q+=(1-s[:,None])*(old[i,0]-q[0])+s[:,None]*(old[i,end]-q[-1])
 p[i,1:end]=q[1:end]
 changed.append(int(i))
# Remove the few remaining original near-root hooks too. This is offline
# geometric fairing, not a simulation constraint or runtime length projection.
faired=[]
for i in np.flatnonzero(np.isin(m,[2,6])):
 touched=False
 for iteration in range(80):
  a=angles(p[i]);j=int(np.argmax(a[:11]))+1
  if a[j-1]<=70:break
  p[i,j]=.5*p[i,j]+.25*(p[i,j-1]+p[i,j+1]);touched=True
 if touched:faired.append(int(i))
surface=ScalpSurface(A/'character.bin')
bvh=BVHTree.FromPolygons(surface.v,surface.head_triangles_all,all_triangles=True)
def crossings(points):
 hits=[]
 for i in np.flatnonzero(np.isin(m,[2,6])):
  for j in range(31):
   a=Vector(points[i,j]);edge=Vector(points[i,j+1])-a;l=edge.length
   if l<.00008:continue
   d=edge/l;hit=bvh.ray_cast(a+d*.00003,d,l-.00006)[0]
   if hit is not None:hits.append([int(i),j+1,list(hit)])
 return hits
beforeHits=crossings(old);afterHits=crossings(p)
ids=np.flatnonzero(np.isin(m,[2,6]));beforeAngles=np.array([angles(old[i]).max() for i in ids]);afterAngles=np.array([angles(p[i]).max() for i in ids])
assert np.array_equal(old[:,0],p[:,0]) and np.array_equal(old[:,12:],p[:,12:])
assert np.array_equal(old[~np.isin(m,[2,6])],p[~np.isin(m,[2,6])])
report={'changed':sorted(set(changed+faired)),'faired':faired,'beforeOver90':int((beforeAngles>90).sum()),'afterOver90':int((afterAngles>90).sum()),'beforeMaxAngle':float(beforeAngles.max()),'afterMaxAngle':float(afterAngles.max()),'beforeCrossings':beforeHits,'afterCrossings':afterHits,'rootsAndVertices12OnwardUnchanged':True,'nonPonytailUnchanged':True}
(O/'candidate.hair').write_bytes(blob[:128]+p.astype('<f4').tobytes());(O/'candidate.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ('changed','faired','beforeCrossings','afterCrossings')},indent=2));print('changed',len(report['changed']),'crossings',len(beforeHits),'->',len(afterHits),flush=True)
if '--apply' in sys.argv:
 assert report['afterOver90']==0 and len(afterHits)<=len(beforeHits)
 assert np.isfinite(p).all() and np.linalg.norm(np.diff(p,axis=1),axis=2).min()>1.e-7
 assert (A/'metahuman.hair').read_bytes() in (blob,(O/'candidate.hair').read_bytes()), 'Groom changed since this repair input was frozen; do not overwrite later edits.'
 shutil.copy2(O/'candidate.hair',A/'metahuman.hair')
 meta=json.loads((B/'metahuman.json').read_text())
 meta['name']='Photo3 fit with smooth ponytail stems and fuller side bangs'
 meta['profile']='photo3-sidebang-v15-ponytail-stem-repair'
 meta['ponytailStemRepair']={
  'inputSha256':hashlib.sha256(blob).hexdigest(),
  'baseSource':'authoring/side-style/metahuman.hair',
  'baseSha256':hashlib.sha256((R/'authoring/side-style/metahuman.hair').read_bytes()).hexdigest(),
  'method':'Restore source stem with smooth attachment-offset blend; fair remaining near-root hooks. Only points 1..11 of affected ponytail curves change. Roots, points 12..31, other regions and material settings unchanged.',
  'changedStrands':len(report['changed']),'beforeOver90':report['beforeOver90'],'afterOver90':0,
  'maxAdjacentTurnDegrees':report['afterMaxAngle'],'bindHeadMeshCrossings':len(afterHits)}
 meta.setdefault('stagedSha256',{})['metahuman.hair']=hashlib.sha256((A/'metahuman.hair').read_bytes()).hexdigest()
 (A/'metahuman.json').write_text(json.dumps(meta,indent=2))
 manifest=json.loads((B/'manifest.json').read_text(encoding='utf-8'))
 for name in ('metahuman.hair','metahuman.json'):
  manifest['files'][name]=hashlib.sha256((A/name).read_bytes()).hexdigest()
 (A/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
 print('Staged repaired groom; physics/material/source assets unchanged.',flush=True)
